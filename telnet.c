/*
 *  telnet.c
 *
 *      TELNET utility
 *
 *  based on: https://l3net.wordpress.com/2012/12/09/a-simple-telnet-client/
 *
 *  SimpleTelnet: a simple telnet client suitable for embedded systems
 *  Copyright (C) 2013  netblue30@yahoo.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define     __STDC_WANT_LIB_EXT1__  1               // safe library function calls

#include    <stdlib.h>
#include    <stdio.h>
#include    <string.h>
#include    <assert.h>
#include    <conio.h>
#include    <dos.h>
#include    <time.h>
#include    <signal.h>

#include    "ip/netif.h"
#include    "ip/stack.h"
#include    "ip/tcp.h"
#include    "ip/error.h"
#include    "ip/types.h"

#include    "ip/slip.h"     // TODO for slip_close(), remove once this is in a stack_close() call

/* -----------------------------------------
   Definitions
----------------------------------------- */
#define     SE                  240
#define     NOP                 241
#define     DM                  242
#define     BRK                 243
#define     IP                  244
#define     AO                  245
#define     AYT                 246
#define     EC                  247
#define     EL                  248
#define     GA                  249
#define     SB                  250
#define     WILL                251
#define     WONT                252
#define     DO                  253
#define     DONT                254
#define     IAC                 255

#define     CMD_ECHO            1
#define     CMD_SUP_GOAHEAD     3
#define     CMD_WINDOW_SIZE     31

#define     TELNET_PORT         23
#define     MY_PORT             (30000+TELNET_PORT)
#define     BUFLEN              1536

#define     TELNET_IDLE         0
#define     TELNET_DATA_AVAIL   1
#define     TELNET_REMOTE_CLOSE 2
#define     TELNET_REMOTE_RESET 3
#define     TELNET_LOCAL_CLOSE  4
#define     TELNET_CON_ABORT    5

/* -----------------------------------------
   Static prototypes
----------------------------------------- */
static void notify_callback(pcbid_t, tcp_event_t);
static void ctrl_break(int);
static void negotiate(pcbid_t, uint8_t*, int);

/* -----------------------------------------
   Types and data structures
----------------------------------------- */

/* -----------------------------------------
   Globals
----------------------------------------- */
pcbid_t         telnet_client = -1;
int             telnet_state = TELNET_IDLE;
uint8_t         buf[BUFLEN];
uint8_t         telnet_opt[3];
char            ip[17];

/*------------------------------------------------
 * main()
 *
 *
 */
int main(int argc , char *argv[])
{
    struct net_interface_t *netif;
    struct tcp_conn_state_t telnet_connection_state;
    ip4_addr_t              telnet_server_address,
                            local_host = IP4_ADDR(10,0,0,19),
                            gateway = IP4_ADDR(10,0,0,1),
                            network_mask = IP4_ADDR(255,255,255,0),
                            temp;

    int                     port, linkState, len, rv, i, dos_result = 0;
    ip4_err_t               result;

    /* parse command line variables
     */
    if (argc < 2 || argc > 3)
    {
        printf("Usage: telnet address [port]\n");
        return -1;
    }

    if ( stack_ip4addr_aton(argv[1], &telnet_server_address) == 0 )
    {
        printf("Server address must be in IPv4 format 0.0.0.0\n");
        return -1;
    }

    port = 23;
    if (argc == 3)
        port = atoi(argv[2]);

    /* Get setup from DOS environment
     */
    if( stack_ip4addr_getenv("LOCALHOST", &temp) )
    {
        local_host = temp;
    }
    else
    {
        stack_ip4addr_ntoa(local_host, ip, sizeof(ip));
        printf("missing or invalid LOCALHOST address. using %s\n", ip);
    }

    if( stack_ip4addr_getenv("NETMASK", &temp) )
    {
        network_mask = temp;
    }
    else
    {
        stack_ip4addr_ntoa(network_mask, ip, sizeof(ip));
        printf("missing or invalid NETMASK. using %s\n", ip);
    }

    if( stack_ip4addr_getenv("GATEWAY", &temp) )
    {
        gateway = temp;
    }
    else
    {
        stack_ip4addr_ntoa(gateway, ip, sizeof(ip));
        printf("missing or invalid GATEWAY. using %s\n", ip);
    }

    /* initialize IP stack
     * TODO: Get stack parameters from environment
     */
    stack_init();                                                   // initialize IP stack
    assert(stack_set_route(network_mask, gateway, 0) == ERR_OK);    // setup default route
    netif = stack_get_ethif(0);                                     // get pointer to interface 0
    assert(netif);

    assert(interface_slip_init(netif) == ERR_OK);                   // initialize interface and link HW
    interface_set_addr(netif, local_host,                           // setup static IP addressing
                              network_mask,
                              gateway);

    /* initialize telnet TCP client
     */
    tcp_init();                                                                 // initialize TCP
    telnet_client = tcp_new();                                                  // get a TCP
    assert(telnet_client >= 0);                                                 // make sure it is valid
    assert(tcp_bind(telnet_client, local_host, MY_PORT) == ERR_OK);             // bind
    assert(tcp_notify(telnet_client, notify_callback) == ERR_OK);               // notify on remote connection close

    result = tcp_connect(telnet_client, telnet_server_address, port);           // try to connect
    if ( result != ERR_OK )
    {
        printf("connect failed. Error\n");
        return -1;
    }

    stack_ip4addr_ntoa(telnet_server_address, ip, sizeof(ip));
    printf("trying %s...\n", ip);

    /* setup Ctrl-Break / Ctrl-C signal call back
     */
    //signal(SIGBREAK, ctrl_break);
    signal(SIGINT, ctrl_break);

    /* main loop
     *
     */
    while ( 1 )
    {
        /* periodically poll link state and if a change occurred from the last
         * test propagate the notification
         */
        if ( interface_link_state(netif) != linkState )
        {
            linkState = interface_link_state(netif);
            printf("link state change, now = '%s'\n", linkState ? "up" : "down");
        }

        /* periodically poll for received frames,
         * drop or feed them up the stack for processing
         */
        interface_input(netif);

        /* cyclic timer update and check
         */
        stack_timers();

        /* Process received data or termination notifications.
         * In IDLE state, simply monitor the state of the connection and
         * close the client if the connection is closed (FREE).
         * This is a work around to allow graceful connection closure
         * after a locally initiated close.
         */
        if ( telnet_state == TELNET_IDLE )
        {
            if ( tcp_util_conn_state(telnet_client, &telnet_connection_state) &&
                 telnet_connection_state.state == FREE )
            {
                break;
            }
        }
        /* Process TCP packet for command options or
         * text data to output on the local terminal
         */
        else if ( telnet_state == TELNET_DATA_AVAIL )
        {
            rv = tcp_recv(telnet_client, buf, BUFLEN);
            if ( rv < 0 )
            {
                dos_result = -1;
                break;
            }
            else if ( rv > 0 )
            {
                i = 0;
                while ( i < rv )
                {
                    if ( buf[i] == IAC )
                    {
                        telnet_opt[0] = buf[i++];
                        telnet_opt[1] = buf[i++];   // get 2 more bytes
                        telnet_opt[2] = buf[i++];
                        negotiate(telnet_client, telnet_opt, sizeof(telnet_opt));
                    }
                    else
                    {
                        putch(buf[i]);
                        i++;
                    }
                }
            }
            else
            {
/*              TODO
                printf("Connection closed by the remote end\n\r");
                return 0;
*/
            }

            telnet_state = TELNET_IDLE;
        }
        /* A RESET notification will be issues by the stack when the remote
         * server is not accepting connections, and an ABORT when a connection
         * attempt times out. In either case exit the client.
         */
        else if ( telnet_state == TELNET_REMOTE_RESET || telnet_state == TELNET_CON_ABORT )
        {
            break;
        }
        /* Initiate a connection close if the local user or server
         * issued a close request. Idle the client to allow the TCP
         * handler to gracefully close the connection.
         */
        else if ( telnet_state == TELNET_REMOTE_CLOSE || telnet_state == TELNET_LOCAL_CLOSE )
        {
            if ( (result = tcp_close(telnet_client)) != ERR_OK )
            {
                printf("\ntcp_close() returned %d\n", result);
                dos_result = -1;
            }

            telnet_state = TELNET_IDLE;
        }
        /* *** debug assertion telnet_state > max. states ***
         */
        else
        {
            tcp_close(telnet_client);
            dos_result = -1;
            printf("\n*** Bug check ***\n");
            break;
        }

        /* collect data from keyboard and transmit
         * TODO the RFC calls for transmitting <CR><LF> but things seem to work well
         * with only a <CR>
         */
        if ( kbhit() )
        {
            buf[0] = getch();
            if ( tcp_send(telnet_client, buf, 1, 0) < 0 )
            {
                dos_result = -1;
                break;
            }
        }
    }

    slip_close();

    printf("\nConnection closed.\n");

    return dos_result;
}

/*------------------------------------------------
 * notify_callback()
 *
 *  callback to notify TCP connection activity
 *
 * param:  PCB ID of connection
 * return: none
 *
 */
void notify_callback(pcbid_t connection, tcp_event_t reason)
{
    ip4_addr_t  ip4addr;
    int         recvResult, i;

    ip4addr = tcp_remote_addr(connection);
    stack_ip4addr_ntoa(ip4addr, ip, sizeof(ip));

    switch ( reason )
    {
        case TCP_EVENT_CLOSE:
            printf("\nconnection closed by %s\n", ip);
            telnet_state = TELNET_REMOTE_CLOSE;         // server closed the connection, issue a close to go from CLOSE_WAIT to LAST_ACK
            break;

        case TCP_EVENT_ABORTED:
            printf("\nconnection aborted\n");
            telnet_state = TELNET_CON_ABORT;            // too many connection retries
            break;

        case TCP_EVENT_REMOTE_RST:
            printf("\nconnection reset by %s\n", ip);
            telnet_state = TELNET_REMOTE_RESET;
            break;

        case TCP_EVENT_DATA_RECV:
        case TCP_EVENT_PUSH:
            telnet_state = TELNET_DATA_AVAIL;
            break;

        default:
            printf("\nunknown event %d from %s\n", reason, ip);
            telnet_state = TELNET_LOCAL_CLOSE;
    }
}

/*------------------------------------------------
 * ctrl_break()
 *
 *  Ctrl-Break / Ctrl-C function
 *
 * param:  signal type
 * return: none
 *
 */
void ctrl_break(int sig_no)
{
    telnet_state = TELNET_LOCAL_CLOSE;
}

/*------------------------------------------------
 * negotiate()
 *
 *  TELNET negotiation phase of the connection.
 *  Will respond with WONT to any DO coming from the server except for window size and echo,
 *  and will encourage the server to DO echo and suppress go ahead.
 *  Screen size setting, advertised as 24 x 80 by the client.
 *
 * param:  Connection PCB,data buffer, and data buffer length
 * return: none
 *
 */
void negotiate(pcbid_t sock, uint8_t *buf, int len)
{
    int     i;
    uint8_t tmp1[] = {IAC, WILL, CMD_WINDOW_SIZE};
    uint8_t tmp2[] = {IAC, SB, CMD_WINDOW_SIZE, 0, 80, 0, 24, IAC, SE};

    /* Responses to server's DO commands
     */
    if ( buf[1] == DO )
    {
        if ( buf[2] == CMD_WINDOW_SIZE )
        {
            if ( tcp_send(sock, tmp1, sizeof(tmp1), 0) < 0 )
                exit(1);

            if ( tcp_send(sock, tmp2, sizeof(tmp2), 0) < 0)
                exit(1);

            return;     // return here !
        }
        else if ( buf[2] == CMD_ECHO )
            buf[1] = WILL;
        else
            buf[1] = WONT;
    }
    /* Responses to server's will requests
     */
    else if ( buf[1] == WILL )
    {
        if ( buf[2] == CMD_ECHO         ||
             buf[2] == CMD_SUP_GOAHEAD     )
            buf[1] = DO;
        else
            buf[1] = DONT;
    }
    /* Ignore any bad commands
     */
    else
    {
        //printf("\nnegotiate(): found bad command.\n");
        return;     // return here !
    }

    if ( tcp_send(sock, buf, len, 0) < 0 )
        exit(1);
}
