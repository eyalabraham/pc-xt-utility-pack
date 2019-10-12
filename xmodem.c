/**************************************************
 *   xmodem.c
 *
 *      Xmodem upload and download utility
 *
 *      usage: xmodem <-r|-s> [-b baud] -f filename
 *             -s: send to host
 *             -r: receive from host
 *             -b: {optional} 0=110, 1=150, 2=300 , 3=600, 4=1200, 5=2400, 6=4800, 7=9600
 *             -f: file name to send or create/overwrite upon receive
 *
 *      resources:
 *              code based on: https://www.menie.org/georges/embedded/
 *                             http://web.mit.edu/6.115/www/amulet/xmodem.htm
 */

/*
 * Copyright 2001-2019 Georges Menie (www.menie.org)
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include    <stdlib.h>
#include    <stdio.h>
#include    <errno.h>
#include    <string.h>
#include    <stdint.h>
#include    <conio.h>
#include    <dos.h>
#include    <i86.h>
#include    "crc16.h"

/* Xmodem signaling byte values
 */
#define     SOH             0x01
#define     STX             0x02
#define     EOT             0x04
#define     ETB             0x17
#define     ACK             0x06
#define     NAK             0x15
#define     CAN             0x18
#define     CTRLZ           0x1A

/* Xmodem transmit states
 */
#define     XMODEM_TX_SYN   0           // sync state, send 'C'
#define     XMODEM_TX_TXCRC 1           // transmit packets with CRC
#define     XMODEM_TX_TXCS  2           // transmit packets with checksum

/* general definitions
 */
#define     DLY_1S          10          // value to yield a 1sec delay
#define     RCV_RETRY       10
#define     SND_RETRY       10
#define     MAXRETRANS      10
#define     ERR_CODES       5

#define     TX_PACKET       128         // Xmodem transmit default packet *** size: 128 or 1024 ***
#define     XMODEM_SND      1
#define     XMODEM_RCV      2
#define     USAGE           "usage: xmodem <-s|-r> [-b baud] -f filename\n"             \
                            "       -s: send to host\n"                                 \
                            "       -r: receive from host\n"                            \
                            "       -b: {default=4} 0=110, 1=150, 2=300, 3=600,\n"      \
                            "                       4=1200, 5=2400, 6=4800, 7=9600\n"   \
                            "       -f: file to send or create/overwrite upon receive\n"

typedef enum SendFlag
{
    XMODEM_128 = 0,     // 128-byte data packet
    XMODEM_CLOSE = 1,   // close the session, no more data
    XMODEM_ABORT = 2    // abort
} send_flag_t;

/**************************************************
 *  function prototypes
 */
void  flushinput(void);
int   inbyte(uint8_t);      // multiple of 100msec timeout
void  outbyte(uint8_t);
void  xmodem_abort(void);
void  xmodem_nak(void);
int   xmodem_rx(uint8_t*);
int   xmodem_tx(uint8_t*, send_flag_t);

/**************************************************
 *  globals
 */
FILE           *pfile;
union  REGS     regs;
struct SREGS    segment_regs;
uint8_t        *ptimeout;
int             com_base;

uint8_t         buff[1024];     /* 1024 for XModem 1k */
char           *errors[ERR_CODES] = {"no data, terminating.",           \
                                     "done.",                           \
                                     "time out, terminating.",          \
                                     "remote cancel, terminating.",     \
                                     "transmit error, terminating" };

/**************************************************
 *  main()
 *
 *   Exit with 0 on success, or negative exit code if error
 */
int main(int argc, char *argv[])
{

    int         i, function;
    int         exit_code = 0, baud = 4;
    char       *file_spec = 0;

    printf("xmodem %s %s\n", __DATE__, __TIME__);

    /* parse command line parameters
     */
    if ( argc < 4 )
    {
        printf("%s", USAGE);
        return -1;
    }
    else
    {
        for (i = 1; i < argc; i++)
        {
            if ( strcmp(argv[i], "-s") == 0 )
            {
                function = XMODEM_SND;
            }
            else if ( strcmp(argv[i], "-r") == 0 )
            {
                function = XMODEM_RCV;
            }
            else if ( strcmp(argv[i], "-b") == 0 )
            {
                i++;
                baud = atoi(argv[i]);
                if ( baud < 0 || baud > 7 )
                {
                    printf("Baud rate out of range [0..7]\n");
                    printf("%s", USAGE);
                    return -1;
                }
            }
            else if ( strcmp(argv[i], "-f") == 0 )
            {
                i++;
                if ( i < argc )
                {
                    file_spec = argv[i];
                }
                else
                {
                    printf("Missing file name\n");
                    printf("%s", USAGE);
                    return -1;
                }
            }
            else
            {
                printf("%s", USAGE);
                return -1;
            }
        }
    }

    //printf("function %d, baud %d, file %s\n", function, baud, file_spec);

    /* setting up COM1 port baud rate
     */
    com_base = (int) *((uint16_t *)MK_FP(0x40, 0));
    ptimeout = MK_FP(0x40, 0x7c);
    *ptimeout = 0;

    regs.h.ah = 0;
    regs.h.al = ((uint8_t)baud << 5) | 0x03;
    regs.w.dx = 0;

    int86x(0x14, &regs, &regs, &segment_regs);

    /* xmodem send and receive functions
     */
    if ( function == XMODEM_RCV )
    {
        pfile = fopen(file_spec, "wb");
        if ( pfile )
        {
            printf("start Xmodem send on remote\n");

            while ( (i = xmodem_rx(buff)) > 0 )
            {
                if ( fwrite(buff, sizeof(uint8_t), i, pfile) != i )
                {
                    printf("output file write error\n");
                    abort();
                    exit_code = -1;
                    break;
                }
            }
            fclose(pfile);

            i = abs(i);                     // safe: 'i' will always be 0 or negative here
            if ( i > 3 )
                printf("xmodem_rx() unknown error %d, terminating.\n", -i);
            else
                printf("%s\n", errors[abs(i)]);
        }
        else
        {
            printf("file open error %d\n", errno);
            exit_code = -1;
        }
    }
    else if ( function == XMODEM_SND )
    {
        pfile = fopen(file_spec, "rb");
        if ( pfile )
        {
            printf("start Xmodem receive on remote\n");

            while ( !feof(pfile) && !ferror(pfile) && i > 0 )
            {
                memset(buff, CTRLZ, sizeof(buff));
                if ( fread(buff, sizeof(uint8_t), TX_PACKET, pfile) > 0 )
                    i = xmodem_tx(buff, XMODEM_128);
            }

            fclose(pfile);

            if ( ferror(pfile) || i < 0 )
                xmodem_tx(buff, XMODEM_ABORT);
            else
                i = xmodem_tx(buff, XMODEM_CLOSE);

            if ( i < 0 )
            {
                i = abs(i);                     // safe: 'i' will always be 0 or negative here
                if ( i > 4 )
                    printf("xmodem_tx() unknown error %d, terminating.\n", -i);
                else
                    printf("%s\n", errors[abs(i)]);
            }
        }
        else
        {
            printf("file open error %d\n", errno);
            exit_code = -1;
        }
    }

    /* reset COM1 timeout
     */
    *ptimeout = 0;

    printf("exiting\n");

    return exit_code;
}

/**************************************************
 *  flushinput()
 *
 *   Discard any pending input bytes
 *
 *   param:  none
 *   return: none
 */
void flushinput(void)
{
    while ( inbyte(DLY_1S*3) >= 0 );
}

/**************************************************
 *  inbyte()
 *
 *   Input a byte from the serial com port and allow
 *   for timeout in multiples of 100msec timeout
 *
 *   param:  timeout value in multiples of 100msec
 *   return: >=0 byte read from com port, -1 timeout error
 */
int inbyte(uint8_t timeout)
{
    *ptimeout = timeout;

    regs.h.ah = 2;
    regs.h.al = 0;
    regs.w.dx = 0;

    int86x(0x14, &regs, &regs, &segment_regs);

    /* check only for timeout status
     * all other transmission errors will be detected with a bad CRC
     */
    if ( regs.h.ah & 0x80 )
        return -1;

    return (int)regs.h.al;
}

/**************************************************
 *  outbyte()
 *
 *   Output a byte to the serial com port
 *
 *   param:  byte to send
 *   return: none (ignoring errors)
 */
void outbyte(uint8_t c)
{
    regs.h.ah = 1;
    regs.h.al = c;
    regs.w.dx = 0;

    int86x(0x14, &regs, &regs, &segment_regs);
}

/**************************************************
 *  xmodem_abort()
 *
 *   Abort and data Xmodem exchange
 *
 *   param:  none
 *   return: none
 */
void xmodem_abort(void)
{
    flushinput();
    outbyte(CAN);
    outbyte(CAN);
    outbyte(CAN);
}

/**************************************************
 *  xmodem_nak()
 *
 *   Reject transmission with NAK
 *
 *   param:  none
 *   return: none
 */
void xmodem_nak(void)
{
    flushinput();
    outbyte(NAK);
}
/**************************************************
 *  xmodem_rx()
 *
 *   Receive data packets using Xmodem CRC protocol
 *   The function will be called and then block until
 *   a valid packet or an end-of-transmission  is received,
 *   or a serial timeout has occurred.
 *   The function should be called repeatedly until it either
 *   fails or signals a transmission end.
 *   The function maintains state and handled all Xmodem signaling.
 *   To use, call the function, copy its returned data and call it again.
 *
 *   param:  pointer to output buffer
 *   return: number of bytes received, or status:
 *           -1 end of transmission received
 *           -2 timeout waiting for input data, data exchange aborted
 *           -3 cancellation by remote and data exchange aborted
 */
int xmodem_rx(uint8_t *buffer)
{
    static  int         send_ack = 0, packet_number = 1;
    static  uint8_t     trychar = 'C';

    int         i, byte_count = 0;
    int         retry, c, retrans = MAXRETRANS;
    int         crc_hi, crc_lo, in_packet, not_in_packet;
    uint8_t    *p;
    uint16_t    crc;

    while (1)
    {
        /* ACK previously accepted packet
         */
        if ( send_ack )
        {
            outbyte(ACK);
            send_ack = 0;
        }

        /* packer control character parser
         */
        for( retry = 0; retry < RCV_RETRY; retry++)
        {
            if ( trychar )
            {
                outbyte(trychar);
            }

            if ( (c = inbyte(DLY_1S)) >= 0 )
            {
                switch (c)
                {
                    case SOH:
                        byte_count = 128;
                        goto start_recv;

                    case STX:
                        byte_count = 1024;
                        goto start_recv;

                    case EOT:
                    case ETB:
                        flushinput();
                        outbyte(ACK);
                        return -1;      // normal end

                    case CAN:
                        if ( (c = inbyte(DLY_1S)) == CAN )
                        {
                            flushinput();
                            outbyte(ACK);
                            return -3;  // canceled by remote
                        }
                        break;

                    default:
                        break;
                }
            }
        }

        /* fall through if there was no valid response to 'C'
         */
        if (trychar == 'C')
        {
            trychar = NAK;
            continue;
        }

        xmodem_abort();

        return -2;  // sync error

    start_recv:
        trychar = 0;

        /* next two bytes are the packet number and inverse packet number
         */
        in_packet = inbyte(DLY_1S);
        if ( in_packet < 0 )
        {
            xmodem_nak();
            continue;
        }

        not_in_packet = inbyte(DLY_1S);
        if ( not_in_packet < 0 )
        {
            xmodem_nak();
            continue;
        }

        /* collect the data bytes
         */
        p = buffer;
        for (i = 0;  i < byte_count; i++)
        {
            if ( (c = inbyte(DLY_1S)) < 0 )
            {
                xmodem_nak();
                continue;
            }
            *p++ = (uint8_t)c;
        }

        /* collect the CRC
         */
        crc_hi = inbyte(DLY_1S);
        if ( crc_hi < 0 )
        {
            xmodem_nak();
            continue;
        }

        crc_lo = inbyte(DLY_1S);
        if ( crc_lo < 0 )
        {
            xmodem_nak();
            continue;
        }

        crc = ((uint16_t)crc_hi << 8) + (uint16_t)crc_lo;

        /* check for valid packet and return,
         * or fall through to NAK
         */
        if ( (in_packet + not_in_packet) == 255 &&
            (in_packet == packet_number || in_packet == packet_number-1 ) &&
             crc == crc16_ccitt_tab(buffer, byte_count) )
        {
            if (in_packet == packet_number)
            {
                packet_number++;
                if (packet_number == 256)   // roll over packet numbers
                    packet_number = 0;

                retrans = MAXRETRANS;

                /* don't ACK a packet here.
                 * return to caller and allow it to process the received packet,
                 * send the ACK once the caller calls this function again.
                 * only flag the state that an ACK should be sent as soon as this function
                 * enters.
                 * not a problem if ACK is missing, the transmitter will time out
                 */
                // outbyte(ACK);
                send_ack = 1;

                return byte_count;
            }

            if ( retrans == 0 )
            {
                xmodem_abort();
                return -2;  // too many retry error
            }
            else
            {
                retrans--;
            }
        }

        xmodem_nak();
    }

    return 0;
}

/**************************************************
 *  xmodem_tx()
 *
 *   Transmit data packets using Xmodem CRC protocol
 *   The function will be called and then block until
 *   the packet is send or the remote aborts or times out.
 *   The function should be called repeatedly until it either
 *   fails or signals a transmission end.
 *   The function maintains state and handled all Xmodem signaling.
 *   To use, copy data into a buffer, call the function, monitor the returned
 *   value; call again or abort.
 *
 *   param:  pointer to packet buffer padded with CTRLZ
 *           flag: 0=128-byte data, 1=1024-byte data, 3=close, no more data, 4=abort
 *   return: number of bytes sent, or status:
 *           -1 abort of EOT sent
 *           -2 timeout waiting for remote response, data exchange aborted
 *           -3 cancellation by remote, data exchange aborted
 *           -4 transmission error, data exchange aborted
 */
int xmodem_tx(uint8_t *buffer, send_flag_t send_flag)
{
    static int      tx_state = XMODEM_TX_SYN;
    static uint8_t  txbuff[TX_PACKET+5];    // TX_PACKET + 3 header bytes + 2 crc
    static uint8_t  packet_number = 1;

    uint8_t     crc_hi, crc_lo, adjusted_packet_len;
    uint16_t    crc;
    int         i, c, retry;

    if ( tx_state == XMODEM_TX_SYN )
    {
        for ( retry = 0; retry < SND_RETRY; retry++)
        {
            if ( (c = inbyte(DLY_1S)) >= 0 )
            {
                switch (c)
                {
                    case 'C':
                        tx_state = XMODEM_TX_TXCRC;
                        goto start_trans;

                    case NAK:
                        tx_state = XMODEM_TX_TXCS;
                        goto start_trans;

                    case CAN:
                        if ( (c = inbyte(DLY_1S)) == CAN )
                        {
                            outbyte(ACK);
                            flushinput();
                            return -3;      // canceled by remote
                        }
                        break;

                    default:
                        break;
                }
            }
        }

        xmodem_abort();

        return -2;  // no sync
    }
    else if ( tx_state == XMODEM_TX_TXCRC || tx_state == XMODEM_TX_TXCS )
    {

    start_trans:

        memset(txbuff, 0, TX_PACKET+5);

        if ( send_flag == XMODEM_128 )
        {
            /* setup transmission buffer with header
             * and data
             */
            txbuff[0] = SOH;
            txbuff[1] = packet_number;
            txbuff[2] = ~packet_number;
            memcpy(&txbuff[3], buffer, TX_PACKET);

            if ( tx_state == XMODEM_TX_TXCRC )
            {
                crc = crc16_ccitt_tab(buffer, TX_PACKET);
                txbuff[TX_PACKET+3] = (uint8_t)((crc >> 8) & 0x00ff);
                txbuff[TX_PACKET+4] = (uint8_t)(crc & 0x00ff);
                adjusted_packet_len = TX_PACKET + 5;
            }
            else
            {
                crc = 0;
                for (i = 0; i < TX_PACKET; i++)
                {
                    crc += buffer[i];
                }
                txbuff[TX_PACKET+3] = (uint8_t)(crc & 0x00ff);
                adjusted_packet_len = TX_PACKET + 4;
            }

            /* transmit the packet and wait for ACK/NAK
             */
            for (retry = 0; retry < SND_RETRY; retry++)
            {
                for (i = 0; i < adjusted_packet_len; i++)
                {
                    outbyte(txbuff[i]);
                }

                if ( (c = inbyte(DLY_1S)) >= 0 )
                {
                    switch (c)
                    {
                        case ACK:
                            packet_number++;
                            return TX_PACKET;   // completed successful

                        case CAN:
                            if ( (c = inbyte(DLY_1S)) == CAN )
                            {
                                outbyte(ACK);
                                flushinput();
                                return -3;      // canceled by remote
                            }
                            break;

                        case NAK:
                            continue;           // try to send the same packet again

                        default:
                            return -4;
                    }
                }
            }

            flushinput();
            return -4;
        }
        else if ( send_flag == XMODEM_CLOSE )
        {
            for (retry = 0; retry < SND_RETRY; retry++)
            {
                outbyte(EOT);
                if ( (c = inbyte(DLY_1S)) == ACK )
                    return -1;
            }
            flushinput();
            return -4;
        }
        else if ( send_flag == XMODEM_ABORT )
        {
            xmodem_abort();
            return -1;
        }
    }

    return 0;
}
