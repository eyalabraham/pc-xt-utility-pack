/*
 *  ping.c
 *
 *      Network PING for PC-XT
 *
 */

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
#include    "ip/icmp.h"
#include    "ip/error.h"
#include    "ip/types.h"

#include    "ip/slip.h"     // TODO for slip_close(), remove once this is in a stack_close() call

/* -----------------------------------------
   definitions
----------------------------------------- */
#define     VERSION                 "v1.0"
#define     USAGE                   "ping  [-V] [-c count] [-i interval] destination_ip_address\n"

// PING
#define     PING_INTERVAL           1000            // interval increments in mSec
#define     MAX_PING_INTERVAL       30              // maximum of 30sec between PINGs
#define     WAIT_FOR_PING_RESPONSE  5000            // in mSec
#define     TEXT_PAYLOAD_LEN        30
#define     PING_TEXT               "ping from px-xt 8088\0"

/* -----------------------------------------
   types and data structures
----------------------------------------- */
// PING
struct ping_payload_t
{
    uint32_t    time;
    char        payload[TEXT_PAYLOAD_LEN];
} pingPayload;

/* -----------------------------------------
   globals
----------------------------------------- */
uint16_t    rxSeq;
char        ip[17];
int         ping_count = -1;    // '-1' ping forever, '0' stop ping, >0 remaining PING count
int         dos_exit = 0;
volatile int done = 0;

/*------------------------------------------------
 * ping_input()
 *
 *  Ping response handler.
 *  Register as a call-back function
 *
 * param:  pointer response pbuf
 * return: none
 *
 */
void ping_input(struct pbuf_t* const p)
{
    struct ip_header_t *ip_in;
    struct icmp_t      *icmp_in;
    uint32_t            pingTime;

    pingTime = stack_time();

    ip_in = (struct ip_header_t*) &(p->pbuf[FRAME_HDR_LEN]);                // get pointers to data sections in packet
    icmp_in = (struct icmp_t*) &(p->pbuf[FRAME_HDR_LEN + IP_HDR_LEN]);

    stack_ip4addr_ntoa(ip_in->srcIp, ip, sizeof(ip));                       // print out response in Ping format
    pingTime -= ((struct ping_payload_t*) &(icmp_in->payloadStart))->time;
    printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%lu ms\n",
            stack_ntoh(ip_in->length),
            ip,
            stack_ntoh(icmp_in->seq),
            ip_in->ttl,
            pingTime);

    if ( ping_count > 0 )
        ping_count--;

    dos_exit = 0;

    rxSeq = stack_ntoh(icmp_in->seq);                                       // signal a valid response
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
    done = 1;
}

/*------------------------------------------------
 * main()
 *
 *
 */
int main(int argc, char* argv[])
{
    int             interval = 1;
    int             a = -1, b = -1, c = -1, d = -1, conv = 0;

    int             linkState, i;
    uint16_t        ident, seq;
    ip4_addr_t      ping_addr;
    ip4_err_t       result;

    struct net_interface_t *netif;

    ip4_addr_t      gateway = 0;
    ip4_addr_t      net_mask = 0;
    ip4_addr_t      local_host = 0;

    /* parse command line variables
     */
    if ( argc == 1 )
    {
        printf("%s\n", USAGE);
        return -1;
    }

    for ( i = 1; i < argc; i++ )
    {
        if ( strcmp(argv[i], "-V") == 0 )
        {
            printf("ping.exe %s %s %s\n", VERSION, __DATE__, __TIME__);
            return 0;
        }
        else if ( strcmp(argv[i], "-c") == 0 )
        {
            /* PING count
             */
            i++;
            ping_count = atoi(argv[i]);
            if ( ping_count < 1 )
                ping_count = 1;
        }
        else if ( strcmp(argv[i], "-i") == 0 )
        {
            /* PING interval
             */
            i++;
            interval = atoi(argv[i]);
            if ( interval < 0 )
                interval = 1;
            else if ( interval > MAX_PING_INTERVAL )
                interval = MAX_PING_INTERVAL;
        }
        else
        {
            /* PING address
             */
            conv = stack_ip4addr_aton(argv[i], &ping_addr);
        }
    }

    if ( conv == 0 )
    {
        printf("PING address must be in IPv4 format 0.0.0.0\n");
        return -1;
    }

    /* Initialize IP stack and ICMP PING
     */
    if ( !stack_ip4addr_getenv("GATEWAY", &gateway) ||
         !stack_ip4addr_getenv("NETMASK", &net_mask) ||
         !stack_ip4addr_getenv("LOCALHOST", &local_host) )
    {
        printf("Missing IP stack environment variable(s)\n");
        return 1;
    }

    stack_init();                                       // initialize IP stack
    assert(stack_set_route(net_mask,
                           gateway,
                           0) == ERR_OK);               // setup default route
    netif = stack_get_ethif(0);                         // get pointer to interface 0
    assert(netif);

    assert(interface_slip_init(netif) == ERR_OK);       // initialize interface and link HW
    interface_set_addr(netif, local_host,               // setup static IP addressing
                              net_mask,
                              gateway);

    icmp_ping_init(ping_input);
    ident = 0xbeef;
    seq = 0;
    rxSeq = seq;
    interval *= PING_INTERVAL;
    strncpy(pingPayload.payload, PING_TEXT, TEXT_PAYLOAD_LEN);

    /* test link state and other info
     */
    linkState = interface_link_state(netif);
    stack_ip4addr_ntoa(ping_addr, ip, sizeof(ip));
    printf("PING %s (%s '%s')\n", ip, netif->name, linkState ? "up" : "down");

    /* setup Ctrl-Break / Ctrl-C signal call back
     */
    //signal(SIGBREAK, ctrl_break);
    signal(SIGINT, ctrl_break);

    /* main loop
     *
     */
    while ( !done && linkState )
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

        /* send a PING per set interval
         * print 'no response' if response times out.
         * a response will be handled by the ping_input() callback
         * and (rxSeq == seq) will be true.
         */
        if ( rxSeq == seq )
        {
            if ( (stack_time() - pingPayload.time) > interval )
            {
                pingPayload.time = stack_time();
                seq++;
                result = icmp_ping_output(ping_addr, ident, seq,
                                          (uint8_t* const) &pingPayload, sizeof(struct ping_payload_t));    // output an ICMP Ping packet

                switch ( result )
                {
                    case ERR_OK:                                        // keep sending ping requests
                    case ERR_ARP_QUEUE:                                 // ARP sent, packet queued (will not happen with SLIP)
                        break;

                    case ERR_ARP_NONE:                                  // a route to the ping destination was not found (will not happen with SLIP)
                        printf("cannot resolve destination address, packet dropped.\n retrying...\n");
                        break;

                    case ERR_NETIF:
                    case ERR_NO_ROUTE:
                    case ERR_MEM:
                    case ERR_DRV:
                    case ERR_TX_COLL:
                    case ERR_TX_LCOLL:
                        printf("error code %d\n", result);
                        done = 1;
                        break;

                    default:
                        printf("unexpected error code %d\n", result);
                        done = 1;
                }
            }
        }
        else if ( (stack_time() - pingPayload.time) > WAIT_FOR_PING_RESPONSE )
        {
            stack_ip4addr_ntoa(netif->ip4addr, ip, 17);
            printf("From %s icmp_seq=%d Destination Host Unreachable\n", ip, seq);
            rxSeq = seq;
            dos_exit = 1;
            if ( ping_count > 0 )
                ping_count--;
        }

        /* exit if a PING count was specified
         * otherwise run forever at set interval time.
         */
        if ( ping_count == 0 )
            done = 1;

    } /* main loop */

    slip_close();

    return dos_exit;
}
