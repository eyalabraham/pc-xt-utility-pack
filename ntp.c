/*
 *  ntp.c
 *
 *      Network Time retrieval and PC-XT clock update
 *
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
#include    "ip/udp.h"
#include    "ip/error.h"
#include    "ip/types.h"

#include    "ip/slip.h"     // TODO for slip_close(), remove once this is in a stack_close() call

/* -----------------------------------------
   definitions
----------------------------------------- */
#define     VERSION                 "v1.0"
#define     USAGE                   "Usage: ntp [-u | -h | -V]"
#define     HELP                    USAGE                           \
                                    "\n"                            \
                                    "-V     Version information\n"  \
                                    "-u     Update system clock\n"  \
                                    "-h     Help\n"

// NTP
#define     NTP_STATE_REQUEST       1               // send a request
#define     NTP_STATE_WAIT_RESP     2               // wait for a response
#define     NTP_STATE_COMPLETE      3               // response received and processed

#define     NTP_PORT                123             // NTP port number
#define     NTP_MINPOLL             4               // minimum poll exponent (16 s)
#define     NTP_MAXPOLL             17              // maximum poll exponent (36 h)
#define     NTP_MAXDISP             16              // maximum dispersion (16 s)
#define     NTP_MINDISP             .005            // minimum dispersion increment (s)
#define     NTP_MAXDIST             1               // distance threshold (1 s)
#define     NTP_MAXSTRAT            16              // maximum stratum number
#define     NTP_REQUEST_INTERVAL    5000            // mSec between repeat requests
#define     NTP_RETRY_COUNT         3               // max retries

#define     NTP_LI_NONE             0x00            // NTP Leap Second indicator (b7..b6)
#define     NTP_LI_ADD_SEC          0x40
#define     NTP_LI_SUB_SEC          0x80
#define     NTP_LI_UNKNOWN          0xc0

#define     NTP_VERSION4            0x20            // NTP version number (b5..b3)
#define     NTP_VERSION3            0x18

#define     NTP_MODE_RES            0x00            // NTP mode (b2..b0)
#define     NTP_MODE_SYMACT         0x01
#define     NTP_MODE_SYMPASV        0x02
#define     NTP_MODE_CLIENT         0x03
#define     NTP_MODE_SERVER         0x04
#define     NTP_MODE_BCAST          0x05
#define     NTP_MODE_CTRL           0x06
#define     NTP_MODE_PRIV           0x07

#define     DIFF_SEC_1900_1970      (2208988800UL)  // number of seconds between 1900 and 1970 (MSB=1)
#define     DIFF_SEC_1970_2036      (2085978496UL)  // number of seconds between 1970 and Feb 7, 2036 (6:28:16 UTC) (MSB=0)

#define     MY_PORT                 (30000+NTP_PORT)

/* -----------------------------------------
   types and data structures
----------------------------------------- */
// NTP
// derived from: https://tools.ietf.org/html/rfc5905
struct ntp_short_t
{
    uint16_t    seconds;
    uint16_t    fraction;
};

struct ntp_timestamp_t
{
    uint32_t    seconds;
    uint32_t    fraction;
};

struct ntp_date_t
{
    uint32_t    eraNumber;
    uint32_t    eraOffset;
    uint32_t    eraFraction1;
    uint32_t    eraFraction2;
    uint32_t    eraFraction3;
};

struct ntp_t
{
    uint8_t                 flagsMode;
    uint8_t                 stratum;
    uint8_t                 poll;
    int8_t                  precision;
    struct ntp_short_t      rootDelay;
    struct ntp_short_t      rootDispersion;
    uint8_t                 referenceID[4];
    struct ntp_timestamp_t  refTimestamp;
    struct ntp_timestamp_t  orgTimestamp;   // T1
    struct ntp_timestamp_t  recTimestamp;   // T2
    struct ntp_timestamp_t  xmtTimestamp;   // T3 (T4 = dstTimestamp is calculated upon packet arrival)
};

/* -----------------------------------------
   globals
----------------------------------------- */
struct udp_pcb_t   *ntp;
ip4_addr_t          ntp_server_address;
int                 ntp_request_state = NTP_STATE_REQUEST;
int                 dos_time_update = 0;
char                ip[16] = {0};

/*------------------------------------------------
 * ntp_send_request()
 *
 *  Send and NTP request
 *
 * param:  send a request to NTP server address
 * return: stack status
 *
 */
ip4_err_t ntp_send_request(ip4_addr_t ntp_server_address)
{
    struct ntp_t    ntpPayload;
    ip4_err_t       result;

    ntpPayload.flagsMode = NTP_LI_UNKNOWN | NTP_VERSION3 | NTP_MODE_CLIENT;
    ntpPayload.stratum = 0;
    ntpPayload.poll = 10;
    ntpPayload.precision = -6;                                      // approx 18mSec (DOS clock tick)
    ntpPayload.rootDelay.seconds = stack_ntoh(0x0001);
    ntpPayload.rootDelay.fraction = 0;
    ntpPayload.rootDispersion.seconds = stack_ntoh(0x0001);
    ntpPayload.rootDispersion.fraction = 0;
    ntpPayload.referenceID[0] = 0;
    ntpPayload.referenceID[1] = 0;
    ntpPayload.referenceID[2] = 0;
    ntpPayload.referenceID[3] = 0;
    ntpPayload.refTimestamp.seconds = 0;
    ntpPayload.refTimestamp.fraction = 0;
    ntpPayload.orgTimestamp.seconds = 0;
    ntpPayload.orgTimestamp.fraction = 0;
    ntpPayload.recTimestamp.seconds = 0;
    ntpPayload.recTimestamp.fraction = 0;
    ntpPayload.xmtTimestamp.seconds = 0;
    ntpPayload.xmtTimestamp.fraction = 0;

    result = udp_sendto(ntp,(uint8_t*) &ntpPayload, sizeof(struct ntp_t), ntp_server_address, NTP_PORT);

    return result;
}

/*------------------------------------------------
 * ntp_response()
 *
 *  Callback to receive NTP server responses and process time information
 *
 * param:  pointer to response pbuf, source IP address and source port
 * return: none
 *
 */
void ntp_response(struct pbuf_t* const p, const ip4_addr_t srcIP, const uint16_t srcPort)
{
    struct ntp_t   *ntpResponse;
    uint32_t        rx_secs, t, us;
    int             is_1900_based;

    time_t              time_of_day;
    struct dosdate_t    date;
    struct dostime_t    time;
    struct tm           tmbuf;

    ntpResponse = (struct ntp_t*) &(p->pbuf[FRAME_HDR_LEN+IP_HDR_LEN+UDP_HDR_LEN]); // crude way to get pointer the NTP response payload

    /* convert NTP time (1900-based) to unix GMT time (1970-based)
     * if MSB is 0, SNTP time is 2036-based!
     */
    rx_secs = stack_ntohl(ntpResponse->recTimestamp.seconds);
    //printf("NTP time, net order: %08lx host order: %08lx\n", ntpResponse->recTimestamp.seconds, rx_secs);
    is_1900_based = ((rx_secs & 0x80000000) != 0);
    t = is_1900_based ? (rx_secs - DIFF_SEC_1900_1970) : (rx_secs + DIFF_SEC_1970_2036);
    time_of_day = t;

    us = stack_ntohl(ntpResponse->recTimestamp.fraction) / 4295;
    //SNTP_SET_SYSTEM_TIME_US(t, us);
    /* display local time from GMT time */
    printf("NTP time: %s", ctime(&time_of_day));

    if ( dos_time_update )
    {
        _localtime(&time_of_day, &tmbuf);
        date.year = tmbuf.tm_year+1900;
        date.month = tmbuf.tm_mon+1;
        date.day = tmbuf.tm_mday;
        date.dayofweek = tmbuf.tm_wday;
        time.hour = tmbuf.tm_hour;
        time.minute = tmbuf.tm_min;
        time.second = tmbuf.tm_sec;
        time.hsecond = 0;
        _dos_setdate(&date);
        _dos_settime(&time);
        printf("System time updated\n");
    }

    ntp_request_state = NTP_STATE_COMPLETE;
}

/*------------------------------------------------
 * main()
 *
 *
 */
int main(int argc, char* argv[])
{
    int                     done = 0, dos_exit = 0;
    int                     ntp_request_count = NTP_RETRY_COUNT;

    ip4_err_t               result;
    struct net_interface_t *netif;
    int                     linkState;
    uint32_t                lastNtpRequest;

    ip4_addr_t              gateway = 0;
    ip4_addr_t              net_mask = 0;
    ip4_addr_t              local_host = 0;

    /* parse command line variables
     */
    if ( argc == 2 )
    {
        if ( strcmp(argv[1], "-V") == 0 )
        {
            printf("host.exe %s %s %s\n", VERSION, __DATE__, __TIME__);
            return 0;
        }
        else if ( strcmp(argv[1], "-u") == 0 )
        {
            dos_time_update = 1;
        }
        else if ( strcmp(argv[1], "-h") == 0 )
        {
            printf("%s\n", HELP);
            return 0;
        }
        else
        {
            printf("%s\n", USAGE);
            return 1;
        }
    }

    if( stack_ip4addr_getenv("NTP", &ntp_server_address) )
    {
        printf("Trying NTP server at %s ...\n", stack_ip4addr_ntoa(ntp_server_address, ip, sizeof(ip)));
    }
    else
    {
        printf("Missing or invalid IPv4 NTP server address\n");
        return -1;
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

    /* test link state and send gratuitous ARP
     *
     */
    linkState = interface_link_state(netif);

    /* prepare UDP protocol and initialize for NTP
     */
    udp_init();
    ntp = udp_new();
    assert(ntp);
    assert(udp_bind(ntp, IP4_ADDR(10,0,0,19), MY_PORT) == ERR_OK);
    assert(udp_recv(ntp, ntp_response) == ERR_OK);
    lastNtpRequest = 0;

    while ( !done && linkState )
    {
        /* periodically poll link state and if a change occurred from the last
         * test propagate the notification
         */
        if ( interface_link_state(netif) != linkState )
        {
            linkState = interface_link_state(netif);
            printf("Link state change, now = '%s'\n", linkState ? "up" : "down");
        }

        /* periodically poll for received frames,
         * drop or feed them up the stack for processing
         */
        interface_input(netif);

        /* cyclic timer update and check
         */
        stack_timers();

        /* Send an NTP request
         */
        if ( ntp_request_state == NTP_STATE_REQUEST )
        {
            lastNtpRequest = stack_time();
            ntp_request_count--;

            result = ntp_send_request(ntp_server_address);

            if ( result == ERR_OK ||
                 result == ERR_ARP_QUEUE )
            {
                /* Wait for NTP response
                 */
                ntp_request_state = NTP_STATE_WAIT_RESP;
            }
            else if ( result == ERR_ARP_NONE )
            {
                /* NTP server address could not be resolved or was not found.
                 * Exit with error.
                 * Will not happen with SLIP.
                 */
                printf("Cannot resolve NTP server address\n");
                dos_exit = 1;
                done = 1;
            }
            else
            {
                printf("Error code %d\n", result);
                dos_exit = 1;
                done = 1;
            }
        }

        /* Wait for NTP response and display NTP time,
         * optionally update DOS time.
         */
        else if ( ntp_request_state == NTP_STATE_WAIT_RESP )
        {
            if ( (stack_time() - lastNtpRequest) > NTP_REQUEST_INTERVAL )
            {
                if ( ntp_request_count )
                {
                    ntp_request_state = NTP_STATE_REQUEST;
                }
                else
                {
                    printf("No response from NTP server\n", result);
                    dos_exit = 1;
                    done = 1;
                }
            }
        }

        /* NTP response was received and processed
         */
        else
        {
            dos_exit = 0;
            done = 1;
        }
    } /* main loop */

    slip_close();

    return dos_exit;
}
