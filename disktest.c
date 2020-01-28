/**************************************************
 *   disktest.c
 *
 *      Fixed disk interrupt and parameter check
 *
 */


#include    <stdio.h>
#include    <string.h>
#include    <stdint.h>
#include    <dos.h>
#include    <i86.h>

/* Partition table structure
 */
typedef struct 
    {
        uint8_t   status;
        uint8_t   first_head;
        uint8_t   first_sector;
        uint8_t   first_cylinder;
        uint8_t   type;
        uint8_t   last_head;
        uint8_t   last_sector;
        uint8_t   last_cylinder;
        uint32_t  first_lba;
        uint32_t  num_sectors;
    } partition_t;

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

/* drive parameter block
 * http://www.ctyme.com/intr/rb-2724.htm
 */
typedef struct
    {
        uint8_t     drive_id;
        uint8_t     unit_num;
        uint16_t    bytes_per_sector;
        uint8_t     max_sector_in_cluster;
        uint8_t     shift_count;
        uint16_t    sectors_before_fat;
        uint8_t     fat_copies;
        uint16_t    root_directory_entries;
        uint16_t    user_data_sector;
        uint16_t    max_cluster_num;
        uint8_t     sectors_per_fat;
        uint16_t    first_dir_sector;
        void __far *dev_header;
        uint8_t     media_id;
        uint8_t     disk_access;
        void __far *next_dpb;
        uint16_t    free_space_cluster_num;
        uint16_t    free_cluster_count;
    } dpb_t;

/* Hard drive parameter table,
 * referenced by INT 41h and INT 46H vectors
 * http://www.techhelpmanual.com/53-hard_disk_parameter_table.html
 */
typedef struct
    {
        uint16_t    wMaxCyls;           // maximum number of cylinders
        uint8_t     bMaxHds;            // maximum number of heads
        uint16_t    wRWCyl;             // starting reduced-write current cylinder
        uint16_t    wWPCyl;             // starting write pre-compensation cylinder
        uint8_t     bECCLen;            // maximum ECC data burst length
        uint8_t     rOptFlags;          // drive step options:
        uint8_t     bTimeOutStd;        // standard timeout value
        uint8_t     bTimeOutFmt;        // timeout value for format drive
        uint8_t     bTimeOutChk;        // timeout value for check drive
        uint16_t    wLandingZone;
        uint8_t     bSectorsPerTrack;
        uint8_t     bReserved;
    } hdpt_t;

/**************************************************
 *  main()
 */
void main(void)
{
    int             drives, drive, drive_id, i;
    union  REGS     regs;
    struct SREGS    segment_regs;
    uint16_t        temp;
    uint8_t         sector[512];
    partition_t     partitions[4];
    bpb_t           bpb;
    dpb_t __far    *pDPB = 0;
    char            oem_name[8] = {0};

    uint8_t __far  *drive_count;
    hdpt_t __far   *hd_param_table;

    printf("disktest.exe %s %s\n", __DATE__, __TIME__);

    drive_count = (uint8_t __far *)MK_FP(0x40, 0x75);
    drives = (int)(*drive_count);
    printf("BIOS drive count %d\n", drives);
    printf("===========================\n\n");

    printf("Hard drive parameter table, vector 41h\n");
    hd_param_table = (hdpt_t __far *) _dos_getvect(0x41);

    printf(" vector %p\n", hd_param_table);
    printf(" maximum number of cylinders %u\n", hd_param_table->wMaxCyls);
    printf(" maximum number of heads %u\n", hd_param_table->bMaxHds);
    printf(" starting reduced-write current cylinder %u\n", hd_param_table->wRWCyl);
    printf(" starting write pre-compensation cylinder %u\n", hd_param_table->wWPCyl);
    printf(" maximum ECC data burst length %u\n", hd_param_table->bECCLen);
    printf(" drive step options 0x%02x\n", hd_param_table->rOptFlags);
    printf(" standard timeout value %d\n", hd_param_table->bTimeOutStd);
    printf(" timeout value for format drive %u\n", hd_param_table->bTimeOutFmt);
    printf(" timeout value for check drive %u\n", hd_param_table->bTimeOutChk);
    printf(" landing zone %u\n", hd_param_table->wLandingZone);
    printf(" sectors per track %u\n", hd_param_table->bSectorsPerTrack);
    printf(" reserved 0x%02x\n\n", hd_param_table->bReserved);

    for (drive = 0; drive < drives; drive++)
    {
        drive_id = 0x80 + drive;

        printf("drive ID 0x%02x\n", drive_id);

        /* issue a GET DRIVE PARAMETERS BIOS call to
         * retrieve fixed disk parameters
         */
        printf("\ntrying INT 13,8\n");
        regs.h.ah = 8;
        regs.h.dl = drive_id;
        regs.w.di = 0;
        segment_regs.es = 0;

        int86x(0x13, &regs, &regs, &segment_regs);

        if ( regs.w.cflag & 1 )
        {
            printf(" call failed with status 0x%02x\n", regs.h.ah);
            continue;
        }
        else
        {
            printf(" drives %d\n", regs.h.dl);
            printf(" drive type %d\n", regs.h.bl);
            printf(" sectors [1..%d]\n", regs.h.cl & 0x3f);
            temp = ((uint16_t)(regs.h.cl & 0xc0) << 2) + (uint16_t)regs.h.ch;
            printf(" cylinders [0..%u]\n", temp);
            printf(" heads [0..%d]\n", regs.h.dh);
        }

        /* if the GET DRIVE PARAMETERS was successful, then reach here
         * and issue a raw read of sector 0 to retrieve and analyze boot sector
         * and partition table
         */
        memset(sector, 0, sizeof(sector));

        printf("\ntrying INT 13,2\n");
        regs.h.ah = 2;
        regs.h.al = 1;
        regs.w.cx = 0x0001;
        regs.h.dh = 0;
        regs.h.dl = drive_id;
        regs.w.bx = FP_OFF(&sector[0]);
        segment_regs.es = FP_SEG(&sector[0]);
        int86x(0x13, &regs, &regs, &segment_regs);

        if ( regs.w.cflag & 1 )
        {
            printf(" call failed with status 0x%02x\n", regs.h.ah);
            continue;
        }
        else
        {
            /* analyze boot sector and partition table
             */
            if ( sector[510] != 0x55 || sector[511] != 0xaa )
            {
                printf(" sector signature not 0x55aa\n");
                continue;
            }
            else
            {
                printf(" partition table\n");
                printf(" stat | first sector | type | last sector  | first LBA  | sectors\n");
                printf("      |  hd  cyl sec |      |  hd  cyl sec |            |\n");
                printf(" -----|--------------|------|--------------|------------|-----------\n");
                memcpy(partitions, &sector[446], sizeof(partitions));

                /* print partition table content
                  */
                for ( i = 0; i < 4; i++)
                {
                     printf(" 0x%02x | %3d %4d %3d | %4d | %3d %4d %3d | 0x%08lx | 0x%08lx\n",
                           partitions[i].status,
                           partitions[i].first_head,
                           ((uint16_t)(partitions[i].first_sector & 0xc0) << 2) + (uint16_t)partitions[i].first_cylinder,
                           partitions[i].first_sector & 0x3f,
                           partitions[i].type,
                           partitions[i].last_head,
                           ((uint16_t)(partitions[i].last_sector & 0xc0) << 2) + (uint16_t)partitions[i].last_cylinder,
                           partitions[i].last_sector & 0x3f,
                           partitions[i].first_lba,
                           partitions[i].num_sectors);
                }

                /* use partition table to access active logical partition's
                 * volume boot record and BPB
                 */
                for ( i = 0; i < 4; i++)
                {
                    if ( partitions[i].status & 0x80 )
                    {
                        printf("\nINT 13 read of boot record of active partition\n");

                        memset(sector, 0, sizeof(sector));
                        regs.h.ah = 2;
                        regs.h.al = 1;
                        regs.h.cl = partitions[i].first_sector;
                        regs.h.ch = partitions[i].first_cylinder;
                        regs.h.dh = partitions[i].first_head;
                        regs.h.dl = drive_id;
                        regs.w.bx = FP_OFF(&sector[0]);
                        segment_regs.es = FP_SEG(&sector[0]);
                        int86x(0x13, &regs, &regs, &segment_regs);

                        if ( regs.w.cflag & 1 )
                        {
                            printf(" call failed with status 0x%02x\n", regs.h.ah);
                            continue;
                        }
                        else
                        {
                            if ( sector[510] != 0x55 || sector[511] != 0xaa )
                            {
                                printf(" boot partition signature not 0x55aa or not formatted\n");
                                continue;
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

                        printf("\nINT 25 read of boot record of active partition\n");

                        memset(sector, 0, sizeof(sector));
                        regs.h.al = (drive_id & 0x7f) + 2;
                        regs.w.cx = 1;
                        regs.w.dx = 0;
                        regs.w.bx = FP_OFF(&sector[0]);
                        segment_regs.ds = FP_SEG(&sector[0]);
                        int86x(0x25, &regs, &regs, &segment_regs);

                        if ( regs.w.cflag & 1 )
                        {
                            printf(" call failed with status 0x%02x, error code 0x%02x\n", regs.h.ah, regs.h.al);
                            continue;
                        }
                        else
                        {
                            if ( sector[510] != 0x55 || sector[511] != 0xaa )
                            {
                                printf(" boot partition signature not 0x55aa or not formatted\n");
                                continue;
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
                    }
                } // read boot record of active partition
            }
        } // read fixed disk sector 0
    } // loop over fixed disks

    /* read device parameter block
     */
    printf("\nINT 21,32 read DOS DRIVE PARAMETER BLOCK\n");

    regs.h.ah = 0x32;
    regs.h.dl = 1;
    int86x(0x21, &regs, &regs, &segment_regs);

    if ( regs.h.al == 0 )
    {
        pDPB = MK_FP(segment_regs.ds, regs.w.bx);
        i = 1;

        while ( FP_OFF(pDPB) != 0xffff )
        {
            printf("\n dbp #%i\n", i);
            printf("  drive ID %d, unit %d\n", pDPB->drive_id, pDPB->unit_num);
            printf("  bytes per sector %u\n", pDPB->bytes_per_sector);
            printf("  sector number in cluster [0..%d], shift count %d\n", pDPB->max_sector_in_cluster, pDPB->shift_count);
            printf("  reserved sectors before FAT %u\n", pDPB->sectors_before_fat);
            printf("  FAT copies %d\n", pDPB->fat_copies);
            printf("  entries in root directory %u\n", pDPB->root_directory_entries);
            printf("  first sector containing user data %u\n", pDPB->user_data_sector);
            printf("  highest cluster number %u\n", pDPB->max_cluster_num);
            printf("  sectors per FAT %d\n", pDPB->sectors_per_fat);
            printf("  sector number of first directory sector %u\n", pDPB->first_dir_sector);
            printf("  media ID 0x%02x\n", pDPB->media_id);
            printf("  disk accessed '%s'\n", (pDPB->disk_access) == 0xff ? "no" : "yes");
            //printf("  cluster at which to start search for free space %u\n", pDPB->free_space_cluster_num);
            printf("  number of free clusters %u\n", pDPB->free_cluster_count);

            pDPB = pDPB->next_dpb;
            i++;
        }
    }
    else
    {
        printf(" call failed\n");
    }
}
