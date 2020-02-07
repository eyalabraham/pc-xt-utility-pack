/*
 *  host.c
 *
 *      Utility for performing DNS lookups similar to the Linux/UNIX command.
 *      Name server is specified on command line or
 *      as environment variable DNS.
 *
 *      Usage: host [-V | -h] [-R <retry>] [-s <name-server>] [-t <type>] {name}
 *
 */
#define     __STDC_WANT_LIB_EXT1__  1               // safe library function calls

#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <dos.h>

#include    "ip/types.h"
#include    "ip/stack.h"
#include    "ip/dnsresolve.h"

/* -----------------------------------------
   Definitions
----------------------------------------- */
#define     VERSION         "v1.0"
#define     USAGE           "Usage: host [-V | -h] [-R <retry>] [-s <name-server>] [-t <type>] {name}"
#define     HELP            USAGE                                                           \
                            "\n"                                                            \
                            "-V     Version\n"                                              \
                            "-h     Help\n"                                                 \
                            "-s     Override default name server\n"                         \
                            "-R     Retry count of DNS UDP query (default=1)\n"             \
                            "-t     Query type: A=address, MX=mail exchange,\n"             \
                            "                   CNAME=canonical name\n"                     \
                            "                   default='A'\n"                              \
                            "{name} Name or IPv4 to resolve"

#define     NAME_LIST_LEN   10
#define     RETRY_INTERVAL  5       // Seconds

/* -----------------------------------------
   Types and data structures
----------------------------------------- */


/* -----------------------------------------
   Globals
----------------------------------------- */
char                host_name[MAX_HOST_NAME_LEN] = {0};
char                ip[16] = {0};
ip4_addr_t          name_server = 0;

int                 type;
char                name_type[][20] = {{"has address"},
                                       {"name server?"},
                                       {"is an alias for"},
                                       {"authority?"},
                                       {"domain name pointer"},
                                       {"mail is handled by"},
                                       {"has text"},
                                       {"?"}};

struct hostent_t        host_entity[NAME_LIST_LEN];
struct dns_resolution_t host_info;

/*------------------------------------------------
 * main()
 *
 */
int main(int argc, char* argv[])
{
    int             dos_result = 0;
    int             query_retry = 1;
    int             dns_on_argsv = 0;
    type_t          query_type = T_A;

    int             i;
    dns_result_t    dns_query_result;

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
            printf("host.exe %s %s %s\n", VERSION, __DATE__, __TIME__);
            return 0;
        }
        else if ( strcmp(argv[i], "-h") == 0 )
        {
            printf("%s\n", HELP);
            return 0;
        }
        else if ( strcmp(argv[i], "-s") == 0 )
        {
            i++;
            strcpy_s(ip, sizeof(ip), argv[i]);
            if ( stack_ip4addr_aton(ip, &name_server) )
            {
                dns_on_argsv = 1;
            }
        }
        else if ( strcmp(argv[i], "-R") == 0 )
        {
            i++;
            query_retry = atoi(argv[i]);
            if ( query_retry < 1 )
                query_retry = 1;
        }
        else if ( strcmp(argv[i], "-t") == 0 )
        {
            i++;
            if ( strcmp(argv[i], "A") == 0 )
            {
                query_type = T_A;
            }
            else if ( strcmp(argv[i], "MX") == 0 )
            {
                query_type = T_MX;
            }
            else if ( strcmp(argv[i], "CNAME") == 0 )
            {
                query_type = T_CNAME;
            }
        }
        else
        {
            strcpy_s(host_name, sizeof(host_name), argv[i]);
        }
    }

    // Prepare parameters for DNS query
    if ( !dns_on_argsv )
    {
        if ( !stack_ip4addr_getenv("DNS", &name_server) )
        {
            printf("No DNS server\n");
            return -1;
        }
    }

    for ( i = 0; i <= query_retry; i++ )
    {
        memset(host_entity, 0, sizeof(host_entity));
        host_info.h_list_len = NAME_LIST_LEN;
        host_info.h_error = DNS_NOT_SET;
        host_info.h_info_list = host_entity;

        dns_query_result = dnsresolve_gethostbynameEx(host_name, query_type, name_server, &host_info);

        if ( dns_query_result == DNS_OK ||
             dns_query_result == DNS_LIST_TRUNC ||
             dns_query_result == DNS_NO_RESULTS ||
             i == query_retry )
        {
            break;
        }

        sleep(RETRY_INTERVAL);
        printf("Retrying (%d of %d)\n", (i + 1), query_retry);
    }

    // Print query results and/or errors
    if ( dns_query_result == DNS_OK || dns_query_result == DNS_LIST_TRUNC )
    {
        if ( dns_on_argsv )
        {
            stack_ip4addr_ntoa(name_server, ip, sizeof(ip));
            printf("Using domain server: %s\n", ip);
        }

        if ( host_info.h_list_len )
        {
            printf("%s", (dns_query_result == DNS_LIST_TRUNC) ? "Truncated name list\n" : "");
            for ( i = 0; i < host_info.h_list_len; i++)
            {
                switch ( host_entity[i].h_type )
                {
                    case T_A:     type = 0;
                        break;
                    case T_NS:    type = 1;
                        break;
                    case T_CNAME: type = 2;
                        break;
                    case T_SOA:   type = 3;
                        break;
                    case T_PTR:   type = 4;
                        break;
                    case T_MX:    type = 5;
                        break;
                    case T_TXT:   type = 6;
                        break;
                    default:      type = 7;
                }
                printf("%s %s %s\n", host_entity[i].h_names, &name_type[type][0], host_entity[i].h_aliases);
            }
        }
        else
        {
            printf("No records found.\n");
        }
    }
    else if ( dns_query_result == DNS_STACK_ERR )
    {
        printf("Name resolution failed, IP stack UDP error %u\n", host_info.h_error);
        dos_result = -1;
    }
    else if ( dns_query_result == DNS_TIME_OUT )
    {
        printf("DNS server time-out, IP stack error %u\n", host_info.h_error);
        dos_result = -1;
    }
    else if ( dns_query_result == DNS_NO_RESULTS )
    {
        printf("%s has no record type %d (RC %d)\n", host_name, query_type, host_info.h_error);
        dos_result = -1;
    }
    else
    {
        printf("Name resolution type not supported (probably SOA)\n");
        dos_result = -1;
    }

    return dos_result;
}
