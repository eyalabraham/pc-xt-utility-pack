/*
 *
 * tftp.c
 *
 *  A client for the Trivial file Transfer Protocol,
 *  which can be used to transfer files to and from remote machines.
 *  This tftp client has no interactive mode, it is limited to command line only.
 *  Default block size 512 bytes, default time out 5 sec with no retry.
 *
 *  tftp [-V | -h ] [-m <mode>] -g | -p  <file> <host>
 *
 *  -V          version info
 *  -h          help
 *  -g          "get" command
 *  -p          "put" command
 *  <file>      file name
 *  <host>      remote host IPv4 address
 *
 *  resources:
 *      UBUNTU:         https://help.ubuntu.com/community/TFTP
 *      TFTP:           http://tcpip.marcolavoie.ca/tftp.html
 *      TFTP RFC:       https://tools.ietf.org/html/rfc1350
 *      Opt extensions: https://tools.ietf.org/html/rfc2347
 *      Block size:     https://tools.ietf.org/html/rfc2348
 *      Timeout:        https://tools.ietf.org/html/rfc2349
 *                      https://tools.ietf.org/html/rfc1123#page-44
 *
 *  TODO:
 *      Handle Ctrl-C and Ctrl-break
 *
 */
#define     __STDC_WANT_LIB_EXT1__  1               // safe library function calls

#include    <stdlib.h>
#include    <stdio.h>
#include    <string.h>
#include    <assert.h>
#include    <errno.h>
//#include    <signal.h>
#include    <unistd.h>

#include    "ip/netif.h"
#include    "ip/stack.h"
#include    "ip/udp.h"
#include    "ip/error.h"
#include    "ip/types.h"

#include    "ip/slip.h"     // TODO for slip_close(), remove once this is in a stack_close() call

/* -----------------------------------------
   definitions
----------------------------------------- */
#define     VERSION                 "v1.0"
#define     USAGE                   "Usage: tftp [-V | -h ] -g | -p  <file> <host>"
#define     HELP                    USAGE                                   \
                                    "\n"                                    \
                                    "-V     version info\n"                 \
                                    "-h     help\n"                         \
                                    "-g     'get' command\n"                \
                                    "-p     'put' command\n"                \
                                    "<file> file name to send or receive\n" \
                                    "<host> remote host IPv4 address\n"

#define     TFTP_OP_NONE            0
#define     TFTP_OP_RRQ             1
#define     TFTP_OP_WRQ             2
#define     TFTP_OP_DATA            3
#define     TFTP_OP_ACK             4
#define     TFTP_OP_ERR             5
#define     TFTP_OP_OACK            6

#define     TFTP_DATA               512     // Bytes
#define     TFTP_DEF_TIMEOUT        5000    // Simple timeout in mili-seconds
#define     TFTP_DEF_PACKET_SIZE    (TFTP_DATA + sizeof(uint16_t) + sizeof(uint16_t))

#define     TFTP_STATE_SEND_REQ     0       // TFTP client states
#define     TFTP_STATE_WAIT         1

#define     TFTP_MODE_BIN           1
#define     TFTP_MODE_ASCII         2

#define     TFTP_PORT               69      // TFTP port number
#define     MY_PORT                 (30000+TFTP_PORT)

/* -----------------------------------------
   Types and data structures
----------------------------------------- */
typedef struct
{
    uint16_t    opcode;
    union ptr_t
    {
        char        options;
        uint8_t     data;
        uint16_t    block_id;
        uint16_t    err_code;
    } ptr;
    uint8_t     payload;
} tftp_t;

typedef enum
{
    NOT_DEFINED = 0,
    FILE_NOT_FOUND = 1,
    ACCESS_VIOLATION = 2,
    DISK_FULL = 3,
    ILLIGAL_OPERATION = 4,
    UNKNOWN_ID = 5,
    FILE_EXISTS = 6,
    NO_SUCH_USER = 7,
    TERMINATED_UNACCEPTABLE_OPTION = 8,
} tftp_err_t;

/* -----------------------------------------
   Globals
----------------------------------------- */
struct udp_pcb_t   *tftp;
ip4_addr_t          tftp_server_address;
uint16_t            tftp_server_port = TFTP_PORT;
uint16_t            byte_count = 0;

tftp_t             *tftp_payload;
uint8_t             tftp_tx_data[TFTP_DEF_PACKET_SIZE];
uint8_t             tftp_rx_data[TFTP_DEF_PACKET_SIZE];
uint8_t             file_read_buff[TFTP_DATA];

int                 tftp_client_state = TFTP_STATE_SEND_REQ;

char               *tftp_error_text[] = {"Not defined, see error text",         // 0
                                         "File not found",                      // 1
                                         "Access violation",                    // 2
                                         "Disk full or allocation exceeded",    // 3
                                         "Illegal TFTP operation",              // 4
                                         "Unknown transfer ID",                 // 5
                                         "File already exists",                 // 6
                                         "No such user",                        // 7
                                         "Unacceptable option negotiation"      // 8
                                        };

/* -----------------------------------------
   Function prototypes
----------------------------------------- */
void      tftp_response(struct pbuf_t* const, const ip4_addr_t, const uint16_t);
ip4_err_t tftp_send_req(ip4_addr_t, uint16_t, uint16_t, char *);
ip4_err_t tftp_send_ack(ip4_addr_t, uint16_t, uint16_t);
ip4_err_t tftp_send_data(ip4_addr_t, uint16_t, uint16_t, uint8_t *, int);
ip4_err_t tftp_send_error(ip4_addr_t, uint16_t, tftp_err_t);
void      tftp_get_filename(char *, char *, int);

/*------------------------------------------------
 * main()
 *
 *
 */
int main(int argc, char* argv[])
{
    int                     c;
    char                   *xfr_mode, *host_ip;

    char                   *file_spec;              // Full file specifier including drive and path
    char                    file_name[16] = {0};    // File name and extension (8.3 DOS format)

    FILE                   *pfile;

    struct net_interface_t *netif;
    int                     linkState;
    ip4_err_t               result;

    uint32_t                send_time;
    uint16_t                op_code;
    uint16_t                block_number = 0;

    ip4_addr_t              gateway = 0;
    ip4_addr_t              net_mask = 0;
    ip4_addr_t              local_host = 0;

    int                     dos_exit = 0;
    int                     done = 0;
    int                     action = TFTP_OP_NONE;
    int                     mode = TFTP_MODE_BIN;

    /* parse command line variables
     */
    if ( argc < 2 )
    {
        printf("%s\n", USAGE);
        return -1;
    }

    while ( ( c = getopt(argc, argv, ":Vhp:g:m:")) != -1 )
    {
        switch ( c )
        {
            case 'V':
                printf("tftp.exe %s %s %s\n", VERSION, __DATE__, __TIME__);
                return 0;

            case 'h':
                printf("%s\n", HELP);
                return 0;

            case 'p':
                // Set write to server ('put')
                file_spec = optarg;
                action = TFTP_OP_WRQ;
                break;

            case 'g':
                // Set read from server ('get')
                file_spec = optarg;
                tftp_get_filename(file_spec, file_name, sizeof(file_name));
                action = TFTP_OP_RRQ;
                break;

            case 'm':
                // Get transfer mode
                xfr_mode = optarg;
                if ( strcmp(xfr_mode, "netascii") == 0 )
                {
                    mode = TFTP_MODE_ASCII;
                }
                else if ( strcmp(xfr_mode, "octet") == 0 )
                {
                    mode = TFTP_MODE_BIN;
                }
                else
                {
                    printf( "'-m' option with bad mode parameter\n");
                    return 1;
                }
                break;

            case ':':
                if ( optopt == 'm')
                    printf( "'-%c' without mode parameter\n", optopt);
                else if ( optopt == 'p' || optopt == 'g' )
                    printf( "'-%c' without file name\n", optopt);
                return 1;

            case '?':
                printf("%s\n", USAGE);
                return 1;
        }
    }

    if ( action == TFTP_OP_NONE )
    {
        printf( "'-p' or '-g' with file name is required\n");
        return 1;
    }

    if ( optind < argc )
    {
        host_ip = argv[optind];
        if ( !stack_ip4addr_aton(host_ip, &tftp_server_address) )
        {
            printf( "Host IP address is not in IPv4 format\n");
            return 1;
        }
    }
    else
    {
        printf( "Host IP address is required\n");
        return 1;
    }

/*
    printf("file-and-path=%s\n", file_spec);
    printf("file=%s\n", file_name);
    printf("host=%s\n", host_ip);
    printf("action=%d\n", action);
    printf("mode=%d\n", mode);

    return 0;
*/

    /* Prepare file for IO
     */
    if ( action == TFTP_OP_RRQ )
    {
        pfile = fopen(file_spec, "wb");
    }
    else
    {
        pfile = fopen(file_spec, "rb");
    }

    if ( pfile == NULL )
    {
        printf("File open error %d\n", errno);
        return 1;
    }

    /* Initialize IP stack
     */
    if ( !stack_ip4addr_getenv("GATEWAY", &gateway) ||
         !stack_ip4addr_getenv("NETMASK", &net_mask) ||
         !stack_ip4addr_getenv("LOCALHOST", &local_host) )
    {
        printf("Missing IP stack environment variable(s)\n");
        return 1;
    }

    stack_init();
    assert(stack_set_route(net_mask,
                           gateway,
                           0) == ERR_OK);
    netif = stack_get_ethif(0);
    assert(netif);

    assert(interface_slip_init(netif) == ERR_OK);
    interface_set_addr(netif, local_host,
                              net_mask,
                              gateway);

    /* Test link state and send gratuitous ARP (not relevant for SLIP)
     */
    linkState = interface_link_state(netif);

    /* Prepare UDP protocol and initialize for TFTP
     */
    udp_init();
    tftp = udp_new();
    assert(tftp);
    assert(udp_bind(tftp, local_host, MY_PORT) == ERR_OK);
    assert(udp_recv(tftp, tftp_response) == ERR_OK);

    /* Main TFTP loop
     */
    while ( !done && linkState )
    {
        /* Periodically poll link state and if a change occurred from the last
         * test propagate the notification
         */
        if ( interface_link_state(netif) != linkState )
        {
            linkState = interface_link_state(netif);
            printf("Link state change, now = '%s'\n", linkState ? "up" : "down");
        }

        /* Periodically poll for received frames
         */
        interface_input(netif);

        /* Cyclic timer update and check
         */
        stack_timers();

        switch ( tftp_client_state )
        {
            /* Initiate connection by sending a read or write request to the server
             */
            case TFTP_STATE_SEND_REQ:

                result = tftp_send_req(tftp_server_address, tftp_server_port, action, file_name);

                send_time = stack_time();

                if ( result == ERR_OK ||
                     result == ERR_ARP_QUEUE )
                {
                    /* Wait for TFTP server response
                     */
                    tftp_client_state = TFTP_STATE_WAIT;
                }
                else if ( result == ERR_ARP_NONE )
                {
                    /* TFTP server address could not be resolved or was not found.
                     * Exit with error.
                     * Will not happen with SLIP.
                     */
                    printf("Cannot resolve TFTP server address\n");
                    dos_exit = 1;
                    done = 1;
                }
                else
                {
                    printf("Error code %d\n", result);
                    dos_exit = 1;
                    done = 1;
                }

                break;

            /* Wait for response packet: DATA, ACK, ERR, or OACK
             * and monitor for a timeout.
             * Use first response to extract server port number for next transmission.
             */
            case TFTP_STATE_WAIT:
                tftp_payload = (tftp_t *) &tftp_rx_data[0];
                op_code = stack_ntoh(tftp_payload->opcode);

                /* No response yet, process time-outs
                 */
                if ( op_code == TFTP_OP_NONE )
                {
                    if ( (stack_time() - send_time) > TFTP_DEF_TIMEOUT )
                    {
                        printf("No response from TFTP server\n");
                        dos_exit = 1;
                        done = 1;
                    }
                }

                /* Received data packet for read request or previous a read ACK,
                 * store the data and send an ACK
                 */
                else if ( op_code == TFTP_OP_DATA &&
                          action == TFTP_OP_RRQ )
                {
                    block_number++;

                    if ( stack_ntoh(tftp_payload->ptr.block_id) != block_number )
                    {
                        tftp_send_error(tftp_server_address, tftp_server_port, UNKNOWN_ID);
                        printf("Bad block ID (expected %d, received %d)\n", block_number, stack_ntoh(tftp_payload->ptr.block_id));
                        dos_exit = 1;
                        done = 1;
                    }
                    else if ( fwrite(tftp_rx_data + 2 * sizeof(uint16_t), sizeof(uint8_t), byte_count, pfile) != byte_count )
                    {
                        tftp_send_error(tftp_server_address, tftp_server_port, DISK_FULL);
                        printf("Output file write error\n");
                        dos_exit = 1;
                        done = 1;
                    }
                    else
                    {
                        tftp_send_ack(tftp_server_address, tftp_server_port, block_number);
                        send_time = stack_time();
                    }

                    // Complete the exchange if partial block was received
                    if ( byte_count < TFTP_DATA )
                    {
                        block_number--;
                        printf("Receive complete (%lu bytes)\n", ((uint32_t) block_number * TFTP_DATA + byte_count));
                        dos_exit = 0;
                        done = 1;
                    }
                }

                /* Received an ACK for data sent or a write request,
                 * send next data block.
                 */
                else if ( op_code == TFTP_OP_ACK &&
                          action == TFTP_OP_WRQ )
                {
                    if ( stack_ntoh(tftp_payload->ptr.block_id) != block_number )
                    {
                        tftp_send_error(tftp_server_address, tftp_server_port, UNKNOWN_ID);
                        printf("Bad block ID (expected %u, received %u)\n", block_number, stack_ntoh(tftp_payload->ptr.block_id));
                        dos_exit = 1;
                        done = 1;
                    }
                    else
                    {
                        byte_count = fread(file_read_buff, sizeof(uint8_t), TFTP_DATA, pfile);

                        if (  ferror(pfile) )
                        {
                            tftp_send_error(tftp_server_address, tftp_server_port, ACCESS_VIOLATION);
                            printf("File read error\n");
                            dos_exit = 1;
                            done = 1;
                        }
                        else
                        {
                            block_number++;
                            tftp_send_data(tftp_server_address, tftp_server_port,
                                           block_number, file_read_buff, byte_count);
                            send_time = stack_time();

                            // Complete the exchange if partial block was read from the file (don't wait for ACK)
                            if ( byte_count < TFTP_DATA )
                            {
                                block_number--;
                                printf("Send complete (%lu bytes)\n", ((uint32_t) block_number * TFTP_DATA + byte_count));
                                dos_exit = 0;
                                done = 1;
                            }
                        }
                    }
                }

                /* Received and error condition from the server
                 */
                else if ( op_code == TFTP_OP_ERR )
                {
                    printf("Server error: %s\n", tftp_error_text[stack_ntoh(tftp_payload->ptr.err_code)]);
                    dos_exit = 1;
                    done = 1;
                }

                /* Send and error to the server and abort, including for:
                 *  TFTP_OP_RRQ, TFTP_OP_WRQ, TFTP_OP_OACK
                 */
                else
                {
                    tftp_send_error(tftp_server_address, tftp_server_port, ILLIGAL_OPERATION);
                    printf("Unexpected response code %u\n", op_code);
                    dos_exit = 1;
                    done = 1;
                }

                break;

            default:
                printf("*** Bug check (tftp_client_state=%d) ***\n", tftp_client_state);
                dos_exit = 1;
                done = 1;
        }

    } /* End of main loop */

    fclose(pfile);

    slip_close();

    return dos_exit;
}

/*------------------------------------------------
 * tftp_response()
 *
 *  Callback to receive TFTP server responses.
 *  Copy pbuf data into TFTP input buffer and update server port after first server response.
 *
 * param:  pointer to response pbuf, source IP address and source port
 * return: This function changes global variable (1) the server port 'tftp_server_port' after
 *         the first packet is received, and (2) reflects the byte count 'byte_count' of the
 *         received TFTP transfer.
 *
 */
void tftp_response(struct pbuf_t* const p, const ip4_addr_t srcIP, const uint16_t srcPort)
{
    byte_count = p->len-(FRAME_HDR_LEN+IP_HDR_LEN+UDP_HDR_LEN);

    memcpy_s(tftp_rx_data, sizeof(tftp_rx_data),
             &(p->pbuf[(FRAME_HDR_LEN+IP_HDR_LEN+UDP_HDR_LEN)]), byte_count);

    byte_count -= 2 * sizeof(uint16_t); // Adjust for opcode and block number

    if ( tftp_server_port == TFTP_PORT )
        tftp_server_port = stack_ntoh(srcPort);
}

/*------------------------------------------------
 * tftp_send_req()
 *
 *  Sent TFTP read or write request.
 *  The request code is NOT checked to be either '1'=Read or '2'=Write.
 *  Function always requests an 'octet' (binary) mode transfer, with no other options.
 *
 * param:  Server IP and port number, request type, and pointer to file name
 * return: Clears global receive data buffer, and returns stack error code
 *
 */
ip4_err_t tftp_send_req(ip4_addr_t server_ip, uint16_t server_port, uint16_t request_type, char *file_name)
{
    ip4_err_t   result;
    char       *options;
    int         options_length;

    memset(tftp_rx_data, 0, sizeof(tftp_rx_data));

    tftp_payload = (tftp_t *) &tftp_tx_data[0];

    tftp_payload->opcode = stack_hton(request_type);

    options = &(tftp_payload->ptr.options);

    strcpy_s(options, TFTP_DATA, file_name);
    options_length = strnlen_s(file_name, TFTP_DATA) + 1;

    strcpy_s(options + options_length, TFTP_DATA, "octet"); // TODO Always binary mode
    options_length += 6;                                    // TODO "octet\0"

    options_length += sizeof(uint16_t);

    result = udp_sendto(tftp, (uint8_t*) &tftp_tx_data[0], options_length,
                        server_ip, server_port);

    return result;
}

/*------------------------------------------------
 * tftp_send_ack()
 *
 *  Sent TFTP ACK message.
 *
 * param:  Server IP and port number, block ID being ACKed
 * return: Clears global receive data buffer, and returns stack error code
 *
 */
ip4_err_t tftp_send_ack(ip4_addr_t server_ip, uint16_t server_port, uint16_t block_id)
{
    ip4_err_t   result;

    memset(tftp_rx_data, 0, sizeof(tftp_rx_data));

    tftp_payload = (tftp_t *) &tftp_tx_data[0];

    tftp_payload->opcode = stack_hton(TFTP_OP_ACK);
    tftp_payload->ptr.block_id = stack_hton(block_id);

    result = udp_sendto(tftp, (uint8_t*) &tftp_tx_data[0], (2 * sizeof(uint16_t)),
                        server_ip, server_port);

    return result;
}

/*------------------------------------------------
 * tftp_send_data()
 *
 *  Sent TFTP data to server.
 *
 * param:  Server IP and port number, block ID and pointer to data buffer with its size.
 * return: Clears global receive data buffer, and returns stack error code
 *
 */
ip4_err_t tftp_send_data(ip4_addr_t server_ip, uint16_t server_port,
                         uint16_t block_id, uint8_t *buffer, int byte_count)
{
    ip4_err_t   result;

    memset(tftp_rx_data, 0, sizeof(tftp_rx_data));

    tftp_payload = (tftp_t *) &tftp_tx_data[0];

    tftp_payload->opcode = stack_hton(TFTP_OP_DATA);
    tftp_payload->ptr.block_id = stack_hton(block_id);

    memcpy_s(&(tftp_payload->payload), (sizeof(tftp_tx_data) - 2 * sizeof(uint16_t)),
             buffer, byte_count);

    result = udp_sendto(tftp, (uint8_t*) &tftp_tx_data[0], (byte_count + 2 * sizeof(uint16_t)),
                        server_ip, server_port);

    return result;
}

/*------------------------------------------------
 * tftp_send_error()
 *
 *  Sent TFTP error from client to server.
 *
 * param:  Server IP and port number, error code
 * return: Clears global receive data buffer, and returns stack error code
 *
 */
ip4_err_t tftp_send_error(ip4_addr_t server_ip, uint16_t server_port, tftp_err_t error_code)
{
    ip4_err_t   result;

    memset(tftp_rx_data, 0, sizeof(tftp_rx_data));

    tftp_payload = (tftp_t *) &tftp_tx_data[0];

    tftp_payload->opcode = stack_hton(TFTP_OP_ERR);
    tftp_payload->ptr.err_code = stack_hton(error_code);
    tftp_payload->payload = 0;  // No error text

    result = udp_sendto(tftp, (uint8_t*) &tftp_tx_data[0], (2 * sizeof(uint16_t) + 1),
                        server_ip, server_port);

    return result;
}

/*------------------------------------------------
 * tftp_get_filename()
 *
 *  Extract the file name and extension out of the full file specifier
 *  that might include drive and/or path.
 *
 * param:  Full file specifier, pointer to file name output buffer and its length
 * return: Nothing
 *
 */
void tftp_get_filename(char *file_specifier, char *file_name, int file_name_len)
{
    static char fname[16];   // DOS file name length
    static char ext[16];     // DOS file extension

    _splitpath(file_specifier, NULL, NULL, fname, ext);
    snprintf(file_name, file_name_len, "%s%s", fname, ext);
}
