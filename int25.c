/**************************************************
 *   int25.c
 *
 *      INT 25 test for diskette and fixed disk
 *
 */

#include    <stdio.h>
#include    <string.h>
#include    <stdint.h>
#include    <stdlib.h>
#include    <dos.h>
#include    <i86.h>

/* -----------------------------------------
   definitions
----------------------------------------- */
#define     USAGE       "Usage: int25 -d <drive-number> [-i | -h]\n" \
                        "       drive-number: 0=A, 1=B, 2=c, 3=D"

/* boot sector and BPB
 * https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#BPB
 */
typedef struct
    {
        uint16_t  bytes_per_sector;         // DOS 2.0 BPB
        uint8_t   sectors_per_cluster;
        uint16_t  reserved_sectors;
        uint8_t   fat_count;
        uint16_t  root_directory_entries;
        uint16_t  total_sectors;
        uint8_t   media_descriptor;
        uint16_t  sectors_per_fat;
        uint16_t  sectors_per_track;        // DOS 3.31 BPB
        uint16_t  heads;
        uint32_t  hidden_sectors;
        uint32_t  total_logical_sectors;
    } bpb_t;

/* INT 25 return status:

AH = status code if CF set:
    01  bad command
    02  bad address mark
    03  write protect
    04  sector not found
    08  DMA failure
    10  data error (bad CRC)
    20  controller failed
    40  seek failed
    80  attachment failed to respond

AL = BIOS error code if CF set
    00  write protect error
    01  unknown unit
    02  drive not ready
    03  unknown command
    04  data error (bad CRC)
    05  bad request structure length
    06  seek error
    07  unknown media type
    08  sector not found
    0A  write fault
    0B  read fault
    0C  general failure
*/

/* -----------------------------------------
   Prototypes
----------------------------------------- */
void (__interrupt __far *original_int13)();

/* -----------------------------------------
   Globals
----------------------------------------- */
union  REGS     int13_regs;
struct SREGS    int13_segment_regs;

volatile int13_invoked = 0;

/**************************************************
 *  int13_intercept()
 *
 *      This interrupt routing intercepts INT 13 vector
 *      displays the calling parameters and then
 *      either calls the original vector or responds
 *      to the INT 13 caller (see compile time directives)
 *
 */
void __interrupt __far int13_intercept()
{
    /* INT 13 intercept work
     */
    int13_invoked++;

    /* call original handler
     */
    _chain_intr(original_int13);
}

/**************************************************
 *  main()
 */
int main(int argc, char* argv[])
{
    int             intercept_int13 = 0;
    int             drive = -1;

    int             i, drives;
    union  REGS     regs;
    struct SREGS    segment_regs;
    uint8_t         sector[512];
    bpb_t           bpb;
    char            oem_name[8] = {0};

    uint8_t __far   *drive_count;

    printf("int25 %s %s\n", __DATE__, __TIME__);

    /* parse command line variables
     */
    if ( argc < 3 )
    {
        printf("%s\n", USAGE);
        return 1;
    }

    for ( i = 1; i < argc; i++ )
    {
        if ( strcmp(argv[i], "-d") == 0 )
        {
            i++;
            drive = (int) strtol(argv[i],NULL,10);
        }
        else if ( strcmp(argv[i], "-i") == 0 )
        {
            intercept_int13 = 1;
        }
        else if ( strcmp(argv[i], "-h") == 0 )
        {
            printf("%s\n", USAGE);
            return 1;
        }
    }

    if ( drive < 0 || drive > 3 )
    {
        printf("%s\n", USAGE);
        return 1;
    }

    if ( intercept_int13 )
    {
        printf("Swapping INT 13 vectors\n");
        original_int13 = _dos_getvect(0x13);
        _dos_setvect(0x13, int13_intercept);
    }

    drive_count = (uint8_t __far *)MK_FP(0x40, 0x75);
    drives = (int)(*drive_count);
    printf("BIOS hard drive count %d\n", drives);

    printf("INT 25 read boot record of active partition on drive %d\n", drive);

    memset(sector, 0, sizeof(sector));
    regs.h.al = drive;  // drive A=0, B=1, C=2, D=3, ...
    regs.w.cx = 1;
    regs.w.dx = 0;
    regs.w.bx = FP_OFF(&sector[0]);
    segment_regs.ds = FP_SEG(&sector[0]);
    int86x(0x25, &regs, &regs, &segment_regs);

    if ( regs.w.cflag & 1 )
    {
        printf(" call failed with status AH=0x%02x, BIOS error code 0x%02x\n", regs.h.ah, regs.h.al);
    }
    else
    {
        if ( sector[510] != 0x55 || sector[511] != 0xaa )
        {
            printf(" boot partition signature not 0x55aa or not formatted\n");
        }
        else
        {
            strncpy(oem_name, &sector[3], 8);
            oem_name[8] = '\0';
            printf(" oem name '%s'\n", oem_name);

            memcpy(&bpb, &sector[11], sizeof(bpb_t));
            printf(" bytes per sector %d\n", bpb.bytes_per_sector);
            printf(" sectors per cluster %d\n", bpb.sectors_per_cluster);
            printf(" reserved sectors %d\n", bpb.reserved_sectors);
            printf(" FAT tables %d\n", bpb.fat_count);
            printf(" root directory entries %d\n", bpb.root_directory_entries);
            printf(" total sectors %u\n", bpb.total_sectors);
            printf(" media_descriptor 0x%02x\n", bpb.media_descriptor);
            printf(" sectors per FAT %d\n", bpb.sectors_per_fat);
            printf(" sectors per track %d\n", bpb.sectors_per_track);
            printf(" heads %d\n", bpb.heads);
            printf(" hidden sectors %lu\n", bpb.hidden_sectors);
            printf(" total logical sectors %lu\n", bpb.total_logical_sectors);
        }
    }

    if ( intercept_int13 )
    {
        printf("INT 13 was invoked %d times\n", int13_invoked);
        _dos_setvect(0x13, original_int13);
    }

    return 0;
}
