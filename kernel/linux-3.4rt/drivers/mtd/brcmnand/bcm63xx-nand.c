#if defined(CONFIG_BCM_KF_MTD_BCMNAND)
/*
 *
 *  drivers/mtd/brcmnand/bcm7xxx-nand.c
 *
    <:copyright-BRCM:2011:DUAL/GPL:standard
    
       Copyright (c) 2011 Broadcom Corporation
       All Rights Reserved
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License, version 2, as published by
    the Free Software Foundation (the "GPL").
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    
    A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php, or by
    writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
    Boston, MA 02111-1307, USA.
    
    :> 


    File: bcm7xxx-nand.c

    Description: 
    This is a device driver for the Broadcom NAND flash for bcm97xxx boards.
when    who what
-----   --- ----
051011  tht codings derived from OneNand generic.c implementation.

 * THIS DRIVER WAS PORTED FROM THE 2.6.18-7.2 KERNEL RELEASE
 */
 
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <bcm_map_part.h>
#include <board.h>
#include "brcmnand_priv.h"
#include <linux/slab.h> 
#include <flash_api.h>

#define PRINTK(...)
//#define PRINTK printk

#define DRIVER_NAME     "brcmnand"
#define DRIVER_INFO     "Broadcom NAND controller"

extern bool kerSysIsRootfsSet(void);

static int __devinit brcmnanddrv_probe(struct platform_device *pdev);
static int __devexit brcmnanddrv_remove(struct platform_device *pdev);

static struct mtd_partition bcm63XX_nand_parts[] = 
{
    {name: "rootfs",        offset: 0, size: 0},
#ifndef _SC_BUILD_
    /*disable them*/
    {name: "rootfs_update", offset: 0, size: 0},
    {name: "data",          offset: 0, size: 0},
#endif
    {name: "bootloader",         offset: 0, size: 0},

#ifdef _SC_BUILD_
/* First 13 MTD still follow D6200, other MTD are for D7000 new add
*/
    {name: "language_DEU",      offset: 0, size: 0},//2
    {name: "scnvram",       offset: 0, size: 0},//3
    {name: "factory",       offset: 0, size: 0},//4
    {name: "upgrade",       offset: 0, size: 0},//5
    {name: "flag",          offset: 0, size: 0},//6
    {name: "pcbasn",        offset: 0, size: 0},//7
    {name: "xxx",           offset: 0, size: 0},//8
    {name: "language_ITA",      offset: 0, size: 0},//9
    {name: "language_RUS",      offset: 0, size: 0},//10
    {name: "language_CHS",      offset: 0, size: 0},//11
    {name: "language_ENU",      offset: 0, size: 0},//12
    {name: "language_dev",      offset: 0, size: 0},//13
    {name: "language_NLD",      offset: 0, size: 0},//14
    {name: "language_KOR",      offset: 0, size: 0},//15
    {name: "language_FRA",      offset: 0, size: 0},//16
    {name: "language_SVE",      offset: 0, size: 0},//17
    {name: "language_ESP",      offset: 0, size: 0},//18
    {name: "language_CSY",      offset: 0, size: 0},//19
    {name: "language_TUR",      offset: 0, size: 0},//20       
    {name: "data", offset: 0, size: 0}, //21
    {name: "part-map", offset: 0, size: 0}, //22
    {name: "rootfs2", offset: 0, size: 0}, //23             
#else 
    {name: "image",         offset: 0, size: 0},
    {name: "image_update",  offset: 0, size: 0},
    {name: NULL,            offset: 0, size: 0},
    {name: NULL,            offset: 0, size: 0},
#endif
    {name: NULL,            offset: 0, size: 0}
};

static struct platform_driver brcmnand_platform_driver =
{
    .probe      = brcmnanddrv_probe,
    .remove     = __devexit_p(brcmnanddrv_remove),
    .driver     =
     {
        .name   = DRIVER_NAME,
     },
};

static struct resource brcmnand_resources[] =
{
    [0] = {
            .name   = DRIVER_NAME,
            .start  = BPHYSADDR(BCHP_NAND_REG_START),
            .end    = BPHYSADDR(BCHP_NAND_REG_END) + 3,
            .flags  = IORESOURCE_MEM,
          },
};

static struct brcmnand_info
{
    struct mtd_info mtd;
    struct brcmnand_chip brcmnand;
    int nr_parts;
    struct mtd_partition* parts;
} *gNandInfo[NUM_NAND_CS];

int gNandCS[NAND_MAX_CS];
/* Number of NAND chips, only applicable to v1.0+ NAND controller */
int gNumNand = 0;
int gClearBBT = 0;
char gClearCET = 0;
uint32_t gNandTiming1[NAND_MAX_CS], gNandTiming2[NAND_MAX_CS];
uint32_t gAccControl[NAND_MAX_CS], gNandConfig[NAND_MAX_CS];

static unsigned long t1[NAND_MAX_CS] = {0};
static int nt1 = 0;
static unsigned long t2[NAND_MAX_CS] = {0};
static int nt2 = 0;
static unsigned long acc[NAND_MAX_CS] = {0};
static int nacc = 0;
static unsigned long nandcfg[NAND_MAX_CS] = {0};
static int ncfg = 0;
static void* gPageBuffer = NULL;

#ifndef _SC_BUILD_
static int __devinit 
is_split_partition (struct mtd_info* mtd, unsigned long offset, unsigned long size, unsigned long *split_offset)
{
    uint8_t buf[0x100];
    size_t retlen;
    int split_found = 0;

    /* Search RootFS partion for split marker.
     * Marker is located in the last 0x100 bytes of the last BootFS Erase Block
     * If marker is found, we have separate Boot and Root Partitions.
     */
    for (*split_offset = offset + mtd->erasesize; *split_offset <= offset + size; *split_offset += mtd->erasesize)
    {
        if (mtd->_block_isbad(mtd, *split_offset - mtd->erasesize)) {
            continue;
        }
        mtd->_read(mtd, *split_offset - 0x100, 0x100, &retlen, buf);

        if (!strncmp (BCM_BCMFS_TAG, buf, strlen (BCM_BCMFS_TAG))) {
            if (!strncmp (BCM_BCMFS_TYPE_UBIFS, &buf[strlen (BCM_BCMFS_TAG)], strlen (BCM_BCMFS_TYPE_UBIFS)))
            {
                printk("***** Found UBIFS Marker at 0x%08lx\n", *split_offset - 0x100); 
                split_found = 1;
                break;
            }
        }
    }

    return split_found;
}
#endif

#ifdef _SC_BUILD_
#define SC_MTD_NUMBER   24
#define SC_MTD_BOOTFLG  6
struct _sc_mmap_info {
    __u32   index;
    __u32   offset;
    __u32   size;
};
#define FLASH_BLOCK_SIZE    (128 * 1024)
#define FLASH_PAGE_SIZE     (2048)

static void __devinit
sc_set_running_rootfs(struct brcmnand_info *nandinfo)
{
    struct mtd_info *mtd = &nandinfo->mtd;
    size_t retlen;
    loff_t oft = 0x0;
    size_t mtdsize;
    __u8 buf[16] = {0};

    oft = bcm63XX_nand_parts[SC_MTD_BOOTFLG].offset;
    mtdsize = bcm63XX_nand_parts[SC_MTD_BOOTFLG].size;
    while(mtd->_block_isbad(mtd, oft)) {
        oft += FLASH_BLOCK_SIZE;
        if (oft >= mtdsize) {
            break;
        }
    }

    if(mtd->_read(mtd, oft, sizeof(buf), &retlen, buf) < 0) {
        return;
    }

    
    if (buf[0] == 2) 
    {
        kerSysSetBootParm("root=", "mtd:rootfs2");
        printk("boot kernel from kernel_rootfs 2.\n");
    }
    else 
    {
        kerSysSetBootParm("root=", "mtd:rootfs");
        printk("boot kernel from kernel_rootfs 1.\n");
    }

    return;
}
static void __devinit
sc_setup_mtd_partitions(struct brcmnand_info *nandinfo)
{
    struct mtd_info *mtd = &nandinfo->mtd;
    size_t retlen;
    static __u8 buf[FLASH_PAGE_SIZE];
    __u8 mmap_flag_found = 0;
    struct _sc_mmap_info *sc_mtd_info = (struct _sc_mmap_info *)buf;
    loff_t oft = FLASH_BLOCK_SIZE * (10+1);

    while(1) 
    {
        while(mtd->_block_isbad(mtd, oft)) 
        {
            oft += FLASH_BLOCK_SIZE;
            if(unlikely(oft >= mtd->size)) 
            {
                return;
            }
        }
        if(mtd->_read(mtd, oft, sizeof(buf), &retlen, buf) < 0) 
        {
            return;
        }

        if(memcmp(&buf, "SCFLMAPOK", 9) == 0) 
        {
            mmap_flag_found = 1;
            oft += FLASH_PAGE_SIZE; /* data saved in next page */
            if(mtd->_read(mtd, oft, sizeof(buf), &retlen, buf) < 0) 
            {
                return;
            }
            break;
        }
        oft += FLASH_BLOCK_SIZE;
    }

    nandinfo->nr_parts = SC_MTD_NUMBER + 1; /* 1 for dummy MTD(bootloader) */
    nandinfo->parts = bcm63XX_nand_parts;
    
    /* Now Sercomm partition MTD will not same with bootloader ,so 
     * so we will create it follow mapping.
    0x000000200000-0x000002600000 : "rootfs"
    0x000000000000-0x000000020000 : "bootloader"
    0x000002900000-0x000002a00000 : "language_DEU"
    0x000003700000-0x000003b00000 : "scnvram"
    0x000003b00000-0x000003c00000 : "factory"
    0x000000200000-0x000003300000 : "upgrade"
    0x000003300000-0x000003500000 : "flag"
    0x000003500000-0x000003600000 : "pcbasn"
    0x000003200000-0x000003300000 : "xxx"
    0x000002800000-0x000002900000 : "language_ITA"
    0x000002e00000-0x000002f00000 : "language_RUS"
    0x000002700000-0x000002800000 : "language_CHS"
    0x000002600000-0x000002700000 : "language_ENU"
    0x000003600000-0x000003700000 : "language_dev"
    0x000002a00000-0x000002b00000 : "language_NLD"
    0x000002b00000-0x000002c00000 : "language_KOR"
    0x000002c00000-0x000002d00000 : "language_FRA"
    0x000002d00000-0x000002e00000 : "language_SVE"
    0x000002f00000-0x000003000000 : "language_ESP"
    0x000003000000-0x000003100000 : "language_CSY"
    0x000003100000-0x000003200000 : "language_TUR"
    0x000003c00000-0x000003e00000 : "data"
    0x000000160000-0x000000200000 : "part-map"
    0x000004000000-0x000006400000 : "rootfs2"
     */
    /* D7000 bootloader MTD partition table
    00     bootloader       0x00000000      0x00160000      0x00000000      0x00160000
    01     part_map         0x00160000      0x000a0000      0x00160000      0x000a0000
    02     rootfs           0x00200000      0x02400000      0x00200000      0x02420000
    03     language_ENU     0x02600000      0x00100000      0x02620000      0x00100000
    04     language_CHS     0x02700000      0x00100000      0x02720000      0x00100000
    05     language_ITA     0x02800000      0x00100000      0x02820000      0x00100000
    06     language_DEU     0x02900000      0x00100000      0x02920000      0x00100000
    07     language_NLD     0x02a00000      0x00100000      0x02a20000      0x00100000
    08     language_KOR     0x02b00000      0x00100000      0x02b20000      0x00100000
    09     language_FRA     0x02c00000      0x00100000      0x02c20000      0x00100000
    10     language_SVE     0x02d00000      0x00100000      0x02d20000      0x00100000
    11     language_RUS     0x02e00000      0x00100000      0x02e20000      0x00100000
    12     language_ESP     0x02f00000      0x00100000      0x02f20000      0x00100000
    13     language_CSY     0x03000000      0x00100000      0x03020000      0x00100000
    14     language_TUR     0x03100000      0x00100000      0x03120000      0x00100000
    15     xxx              0x03200000      0x00100000      0x03220000      0x00100000
    16     boot_flag        0x03300000      0x00200000      0x03320000      0x00200000
    17     pcbasn           0x03500000      0x00100000      0x03520000      0x00100000
    18     language_dev     0x03600000      0x00100000      0x03620000      0x00100000
    19     sc_nvram         0x03700000      0x00400000      0x03720000      0x00400000
    20     factory_data     0x03b00000      0x00100000      0x03b20000      0x00100000
    21     data             0x03c00000      0x00200000      0x03c20000      0x00200000
    22     reserved1        0x03e00000      0x00200000      0x03e20000      0x00200000
    23     rootfs2          0x04000000      0x02400000      0x04020000      0x02400000
    24     reserved2        0x06400000      0x00200000      0x06420000      0x00220000
    25     reserved3        0x06600000      0x00200000      0x06640000      0x00200000
    26     reserved4        0x06800000      0x00200000      0x06840000  
    */
    
    {
        /* RootFs */
        bcm63XX_nand_parts[0].offset = sc_mtd_info[2].offset;
        bcm63XX_nand_parts[0].size = sc_mtd_info[2].size ;
        bcm63XX_nand_parts[0].ecclayout = mtd->ecclayout;

        /* Boot and NVRAM data */
        bcm63XX_nand_parts[1].offset = sc_mtd_info[0].offset;
        bcm63XX_nand_parts[1].size = sc_mtd_info[0].size ;
        bcm63XX_nand_parts[1].ecclayout = mtd->ecclayout;
        
        /*languages DEU*/
        bcm63XX_nand_parts[2].offset = sc_mtd_info[6].offset;
        bcm63XX_nand_parts[2].size = sc_mtd_info[6].size ;
        bcm63XX_nand_parts[2].ecclayout = mtd->ecclayout;

        /* nvram , tm*/
        bcm63XX_nand_parts[3].offset = sc_mtd_info[19].offset;
        bcm63XX_nand_parts[3].size = sc_mtd_info[19].size ;
        bcm63XX_nand_parts[3].ecclayout = mtd->ecclayout;

        /* pot , dpf */
        bcm63XX_nand_parts[4].offset = sc_mtd_info[20].offset;
        bcm63XX_nand_parts[4].size = sc_mtd_info[20].size ;
        bcm63XX_nand_parts[4].ecclayout = mtd->ecclayout;

        /* upgrade area */
        bcm63XX_nand_parts[5].offset = sc_mtd_info[2].offset;
        bcm63XX_nand_parts[5].size = (sc_mtd_info[15].offset - sc_mtd_info[2].offset) ;
        bcm63XX_nand_parts[5].ecclayout = mtd->ecclayout;
        
        /* upgrade flag */
        bcm63XX_nand_parts[6].offset = sc_mtd_info[16].offset;
        bcm63XX_nand_parts[6].size = sc_mtd_info[16].size ;
        bcm63XX_nand_parts[6].ecclayout = mtd->ecclayout;
        /* PCBA SN */
        bcm63XX_nand_parts[7].offset = sc_mtd_info[17].offset;
        bcm63XX_nand_parts[7].size = sc_mtd_info[17].size ;
        bcm63XX_nand_parts[7].ecclayout = mtd->ecclayout;
        /* XXX: other language */
        bcm63XX_nand_parts[8].offset = sc_mtd_info[15].offset;
        bcm63XX_nand_parts[8].size = sc_mtd_info[15].size ;
        bcm63XX_nand_parts[8].ecclayout = mtd->ecclayout;
        /* PTB: ITA language */
        bcm63XX_nand_parts[9].offset = sc_mtd_info[5].offset;
        bcm63XX_nand_parts[9].size = sc_mtd_info[5].size ;
        bcm63XX_nand_parts[9].ecclayout = mtd->ecclayout;
        /* RUS: RUS language */
        bcm63XX_nand_parts[10].offset = sc_mtd_info[11].offset;
        bcm63XX_nand_parts[10].size = sc_mtd_info[11].size ;
        bcm63XX_nand_parts[10].ecclayout = mtd->ecclayout;
        /* CHS: CHS language */
        bcm63XX_nand_parts[11].offset = sc_mtd_info[4].offset;
        bcm63XX_nand_parts[11].size = sc_mtd_info[4].size ;
        bcm63XX_nand_parts[11].ecclayout = mtd->ecclayout;
        /* ENU: ENU language */
        bcm63XX_nand_parts[12].offset = sc_mtd_info[3].offset;
        bcm63XX_nand_parts[12].size = sc_mtd_info[3].size ;
        bcm63XX_nand_parts[12].ecclayout = mtd->ecclayout;
        /* DEV: Language Device*/
        bcm63XX_nand_parts[13].offset = sc_mtd_info[18].offset;
        bcm63XX_nand_parts[13].size = sc_mtd_info[18].size ;
        bcm63XX_nand_parts[13].ecclayout = mtd->ecclayout;
        /* NLD: NLD language */
        bcm63XX_nand_parts[14].offset = sc_mtd_info[7].offset;
        bcm63XX_nand_parts[14].size = sc_mtd_info[7].size ;
        bcm63XX_nand_parts[14].ecclayout = mtd->ecclayout;        
        /* KOR: KOR language */
        bcm63XX_nand_parts[15].offset = sc_mtd_info[8].offset;
        bcm63XX_nand_parts[15].size = sc_mtd_info[8].size ;
        bcm63XX_nand_parts[15].ecclayout = mtd->ecclayout;   
        /* FRA: FRA language */
        bcm63XX_nand_parts[16].offset = sc_mtd_info[9].offset;
        bcm63XX_nand_parts[16].size = sc_mtd_info[9].size ;
        bcm63XX_nand_parts[16].ecclayout = mtd->ecclayout; 
        /* SVE: SVE language */
        bcm63XX_nand_parts[17].offset = sc_mtd_info[10].offset;
        bcm63XX_nand_parts[17].size = sc_mtd_info[10].size ;
        bcm63XX_nand_parts[17].ecclayout = mtd->ecclayout;
        /* ESP: ESP language */
        bcm63XX_nand_parts[18].offset = sc_mtd_info[12].offset;
        bcm63XX_nand_parts[18].size = sc_mtd_info[12].size ;
        bcm63XX_nand_parts[18].ecclayout = mtd->ecclayout;
        /* CSY: CSY language */
        bcm63XX_nand_parts[19].offset = sc_mtd_info[13].offset;
        bcm63XX_nand_parts[19].size = sc_mtd_info[13].size ;
        bcm63XX_nand_parts[19].ecclayout = mtd->ecclayout;
        /* TUR: TUR language */
        bcm63XX_nand_parts[20].offset = sc_mtd_info[14].offset;
        bcm63XX_nand_parts[20].size = sc_mtd_info[14].size ;
        bcm63XX_nand_parts[20].ecclayout = mtd->ecclayout;
        /* DATA */                                  
        bcm63XX_nand_parts[21].offset = sc_mtd_info[21].offset;
        bcm63XX_nand_parts[21].size = sc_mtd_info[21].size ;
        bcm63XX_nand_parts[21].ecclayout = mtd->ecclayout;
        /* PART-MAP */                                  
        bcm63XX_nand_parts[22].offset = sc_mtd_info[1].offset;
        bcm63XX_nand_parts[22].size = sc_mtd_info[1].size ;
        bcm63XX_nand_parts[22].ecclayout = mtd->ecclayout;

        /* rootfs2 */                                  
        bcm63XX_nand_parts[23].offset = sc_mtd_info[23].offset;
        bcm63XX_nand_parts[23].size = sc_mtd_info[23].size ;
        bcm63XX_nand_parts[23].ecclayout = mtd->ecclayout; 
    }

    sc_set_running_rootfs(nandinfo);

    return;
}
#endif

static void __devinit 
brcmnanddrv_setup_mtd_partitions(struct brcmnand_info* nandinfo)
{
    int boot_from_nand;

    if (flash_get_flash_type() == FLASH_IFC_NAND)
        boot_from_nand = 1;
    else
        boot_from_nand = 0;

    if( boot_from_nand == 0 )
    {
        nandinfo->nr_parts = 1;
        nandinfo->parts = bcm63XX_nand_parts;

        bcm63XX_nand_parts[0].name = "data";
        bcm63XX_nand_parts[0].offset = 0;
        if( device_size(&(nandinfo->mtd)) < NAND_BBT_THRESHOLD_KB )
        {
            bcm63XX_nand_parts[0].size =
                device_size(&(nandinfo->mtd)) - (NAND_BBT_SMALL_SIZE_KB*1024);
        }
        else
        {
            bcm63XX_nand_parts[0].size =
                device_size(&(nandinfo->mtd)) - (NAND_BBT_BIG_SIZE_KB*1024);
        }
        bcm63XX_nand_parts[0].ecclayout = nandinfo->mtd.ecclayout;

        PRINTK("Part[0] name=%s, size=%llx, ofs=%llx\n", bcm63XX_nand_parts[0].name,
            bcm63XX_nand_parts[0].size, bcm63XX_nand_parts[0].offset);
    }
    else
    {
        static NVRAM_DATA nvram;
        struct mtd_info* mtd = &nandinfo->mtd;
        unsigned long rootfs_ofs;
        int rootfs, rootfs_update;
#ifndef _SC_BUILD_
        unsigned long  split_offset;
#endif

        kerSysBlParmsGetInt(NAND_RFS_OFS_NAME, (int *) &rootfs_ofs);
        kerSysNvRamGet((char *)&nvram, sizeof(nvram), 0);
        nandinfo->nr_parts = 6;
        nandinfo->parts = bcm63XX_nand_parts;

        /* Root FS.  The CFE RAM boot loader saved the rootfs offset that the
         * Linux image was loaded from.
         */
#ifndef _SC_BUILD_
        PRINTK("rootfs_ofs=0x%8.8lx, part1ofs=0x%8.8lx, part2ofs=0x%8.8lx\n",
	       rootfs_ofs, (unsigned long)nvram.ulNandPartOfsKb[NP_ROOTFS_1],
	       (unsigned long)nvram.ulNandPartOfsKb[NP_ROOTFS_2]);
        if( rootfs_ofs == nvram.ulNandPartOfsKb[NP_ROOTFS_1] )
        {
            rootfs = NP_ROOTFS_1;
            rootfs_update = NP_ROOTFS_2;
        }
        else
        {
            if( rootfs_ofs == nvram.ulNandPartOfsKb[NP_ROOTFS_2] )
            {
                rootfs = NP_ROOTFS_2;
                rootfs_update = NP_ROOTFS_1;
            }
            else
            {
                /* Backward compatibility with old cferam. */
                extern unsigned char _text;
                unsigned long rootfs_ofs = *(unsigned long *) (&_text - 4);

                if( rootfs_ofs == nvram.ulNandPartOfsKb[NP_ROOTFS_1] )
                {
                    rootfs = NP_ROOTFS_1;
                    rootfs_update = NP_ROOTFS_2;
                }
                else
                {
                    rootfs = NP_ROOTFS_2;
                    rootfs_update = NP_ROOTFS_1;
                }
            }
        }
#else
        rootfs = NP_ROOTFS_2;
        rootfs_update = NP_ROOTFS_1; //cferam
#endif

#ifndef _SC_BUILD_
        /* RootFS partition */
        bcm63XX_nand_parts[0].offset = nvram.ulNandPartOfsKb[rootfs]*1024;
        bcm63XX_nand_parts[0].size = nvram.ulNandPartSizeKb[rootfs]*1024;
        bcm63XX_nand_parts[0].ecclayout = mtd->ecclayout;

        /* This partition is used for flashing images */
        bcm63XX_nand_parts[4].offset = bcm63XX_nand_parts[0].offset;
        bcm63XX_nand_parts[4].size = bcm63XX_nand_parts[0].size;
        bcm63XX_nand_parts[4].ecclayout = mtd->ecclayout;

        if (is_split_partition (mtd, bcm63XX_nand_parts[0].offset, bcm63XX_nand_parts[0].size, &split_offset))
        {
            /* RootFS partition */
            bcm63XX_nand_parts[0].offset = split_offset;
            bcm63XX_nand_parts[0].size -= (split_offset - nvram.ulNandPartOfsKb[rootfs]*1024);

            /* BootFS partition */
            bcm63XX_nand_parts[nandinfo->nr_parts].name = "bootfs";
            bcm63XX_nand_parts[nandinfo->nr_parts].offset = nvram.ulNandPartOfsKb[rootfs]*1024;
            bcm63XX_nand_parts[nandinfo->nr_parts].size = split_offset - nvram.ulNandPartOfsKb[rootfs]*1024;
            bcm63XX_nand_parts[nandinfo->nr_parts].ecclayout = mtd->ecclayout;
            nandinfo->nr_parts++;

            if (kerSysIsRootfsSet() == false) {
                kerSysSetBootParm("ubi.mtd", "0");
                kerSysSetBootParm("root=", "ubi:rootfs_ubifs");
                kerSysSetBootParm("rootfstype=", "ubifs");
            }
        }
        else {
            if (kerSysIsRootfsSet() == false) {
                kerSysSetBootParm("root=", "mtd:rootfs");
                kerSysSetBootParm("rootfstype=", "jffs2");
            }
        }

        /* RootFS_update partition */
        bcm63XX_nand_parts[1].offset = nvram.ulNandPartOfsKb[rootfs_update]*1024;
        bcm63XX_nand_parts[1].size = nvram.ulNandPartSizeKb[rootfs_update]*1024;
        bcm63XX_nand_parts[1].ecclayout = mtd->ecclayout;

        /* This partition is used for flashing images */
        bcm63XX_nand_parts[5].offset = bcm63XX_nand_parts[1].offset;
        bcm63XX_nand_parts[5].size = bcm63XX_nand_parts[1].size;
        bcm63XX_nand_parts[5].ecclayout = mtd->ecclayout;

        if (is_split_partition (mtd, bcm63XX_nand_parts[1].offset, bcm63XX_nand_parts[1].size, &split_offset))
        {
            /* rootfs_update partition */
            bcm63XX_nand_parts[1].offset = split_offset;
            bcm63XX_nand_parts[1].size -= (split_offset - nvram.ulNandPartOfsKb[rootfs_update]*1024);

            /* bootfs_update partition */
            bcm63XX_nand_parts[nandinfo->nr_parts].name = "bootfs_update";
            bcm63XX_nand_parts[nandinfo->nr_parts].offset = nvram.ulNandPartOfsKb[rootfs_update]*1024;
            bcm63XX_nand_parts[nandinfo->nr_parts].size = split_offset - nvram.ulNandPartOfsKb[rootfs_update]*1024;
            bcm63XX_nand_parts[nandinfo->nr_parts].ecclayout = mtd->ecclayout;
            nandinfo->nr_parts++;
        }

        /* Data (psi, scratch pad) */
        bcm63XX_nand_parts[2].offset = nvram.ulNandPartOfsKb[NP_DATA] * 1024;
        bcm63XX_nand_parts[2].size = nvram.ulNandPartSizeKb[NP_DATA] * 1024;
        bcm63XX_nand_parts[2].ecclayout = mtd->ecclayout;

        /* Boot and NVRAM data */
        bcm63XX_nand_parts[3].offset = nvram.ulNandPartOfsKb[NP_BOOT] * 1024;
        bcm63XX_nand_parts[3].size = nvram.ulNandPartSizeKb[NP_BOOT] * 1024;
        bcm63XX_nand_parts[3].ecclayout = mtd->ecclayout;
#else
/* Now here are D7000 mtd flash mapping define*/
#define BOOTLOADER_OFFSET         0
#define BOOTLOADER_SIZE           0x000020000   //128K, ROM only
#define ROOTFS_OFFSET       0x00200000   //2M
#define ROOTFS_SIZE         0x02400000
#define LANG_OFFSET         (ROOTFS_OFFSET + ROOTFS_SIZE)
#define LANG_SIZE           0x00D00000   //13M
/* 
LANG_SIZE = language_DEU_SIZE + language_PTB_SIZE + language_RUS_SIZE
				+ language_CHS_SIZE + language_ENU_SIZE 
*/
#define LANG_ENU_OFFSET         (ROOTFS_OFFSET + ROOTFS_SIZE)
#define LANG_ENU_SIZE           0x00100000   // 1M
#define LANG_CHS_OFFSET         (LANG_ENU_OFFSET + LANG_ENU_SIZE)
#define LANG_CHS_SIZE           0x00100000   // 1M
#define LANG_ITA_OFFSET         (LANG_CHS_OFFSET + LANG_CHS_SIZE)
#define LANG_ITA_SIZE           0x00100000   // 1M
#define LANG_DEU_OFFSET         (LANG_ITA_OFFSET + LANG_ITA_SIZE)
#define LANG_DEU_SIZE           0x00100000   // 1M
#define LANG_NLD_OFFSET         (LANG_DEU_OFFSET + LANG_DEU_SIZE)
#define LANG_NLD_SIZE           0x00100000   // 1M
#define LANG_KOR_OFFSET         (LANG_NLD_OFFSET + LANG_NLD_SIZE)
#define LANG_KOR_SIZE           0x00100000   // 1M
#define LANG_FRA_OFFSET         (LANG_KOR_OFFSET + LANG_KOR_SIZE)
#define LANG_FRA_SIZE           0x00100000   // 1M
#define LANG_SVE_OFFSET         (LANG_FRA_OFFSET + LANG_FRA_SIZE)
#define LANG_SVE_SIZE           0x00100000   // 1M
#define LANG_RUS_OFFSET         (LANG_SVE_OFFSET + LANG_SVE_SIZE)
#define LANG_RUS_SIZE           0x00100000   // 1M
#define LANG_ESP_OFFSET         (LANG_RUS_OFFSET + LANG_RUS_SIZE)
#define LANG_ESP_SIZE           0x00100000   // 1M
#define LANG_CSY_OFFSET         (LANG_ESP_OFFSET + LANG_ESP_SIZE)
#define LANG_CSY_SIZE           0x00100000   // 1M
#define LANG_TUR_OFFSET         (LANG_CSY_OFFSET + LANG_CSY_SIZE)
#define LANG_TUR_SIZE           0x00100000   // 1M
#define XXX_OFFSET              (LANG_TUR_OFFSET + LANG_TUR_SIZE)
#define XXX_SIZE                0x00100000   //1M

#define FLAG_OFFSET         (XXX_OFFSET + XXX_SIZE)   //
#define FLAG_SIZE           0x00200000   //2M
#define PCBASN_OFFSET         (FLAG_OFFSET + FLAG_SIZE)   //
#define PCBASN_SIZE           0x00100000   //1M
#define LANG_DEV_OFFSET    (PCBASN_OFFSET + PCBASN_SIZE)
#define LANG_DEV_SIZE           0x00100000   //1M
#define NVRAM_OFFSET        (LANG_DEV_OFFSET + LANG_DEV_SIZE)
#define NVRAM_SIZE          0x00400000   //4M
#define FACTORY_OFFSET      (NVRAM_OFFSET + NVRAM_SIZE)
#define FACTORY_SIZE        0x00100000   //1M
#define DATA_OFFSET         (FACTORY_OFFSET + FACTORY_SIZE)
#define DATA_SIZE           0x00200000 //2M
#define MTDMAP_OFFSET       0x00160000
#define MTDMAP_SIZE         0x000A0000
#define ROOTFS2_OFFSET      0x04000000 

#define UPGRADE_OFFSET      (ROOTFS_OFFSET)
#define UPGRADE_SIZE        ROOTFS_SIZE + LANG_SIZE  
        
        nandinfo->nr_parts = 25;
        bcm63XX_nand_parts[0].offset = ROOTFS_OFFSET;
        bcm63XX_nand_parts[0].size = ROOTFS_SIZE ;
        bcm63XX_nand_parts[0].ecclayout = mtd->ecclayout;

        /* Boot and NVRAM data */
        bcm63XX_nand_parts[1].offset = BOOTLOADER_OFFSET;
        bcm63XX_nand_parts[1].size = BOOTLOADER_SIZE;
        bcm63XX_nand_parts[1].ecclayout = mtd->ecclayout;
        
        /*languages DEU*/
        bcm63XX_nand_parts[2].offset = LANG_DEU_OFFSET;
        bcm63XX_nand_parts[2].size = LANG_DEU_SIZE;
        bcm63XX_nand_parts[2].ecclayout = mtd->ecclayout;

        /* nvram , tm*/
        bcm63XX_nand_parts[3].offset = NVRAM_OFFSET;
        bcm63XX_nand_parts[3].size = NVRAM_SIZE;
        bcm63XX_nand_parts[3].ecclayout = mtd->ecclayout;

        /* pot , dpf */
        bcm63XX_nand_parts[4].offset = FACTORY_OFFSET;
        bcm63XX_nand_parts[4].size = FACTORY_SIZE;
        bcm63XX_nand_parts[4].ecclayout = mtd->ecclayout;

        /* upgrade area */
        bcm63XX_nand_parts[5].offset = UPGRADE_OFFSET;
        bcm63XX_nand_parts[5].size = UPGRADE_SIZE;
        bcm63XX_nand_parts[5].ecclayout = mtd->ecclayout;
        /* upgrade flag */
        bcm63XX_nand_parts[6].offset = FLAG_OFFSET;
        bcm63XX_nand_parts[6].size = FLAG_SIZE;
        bcm63XX_nand_parts[6].ecclayout = mtd->ecclayout;
        /* PCBA SN */
        bcm63XX_nand_parts[7].offset = PCBASN_OFFSET;
        bcm63XX_nand_parts[7].size = PCBASN_SIZE;
        bcm63XX_nand_parts[7].ecclayout = mtd->ecclayout;
        /* XXX: other language */
        bcm63XX_nand_parts[8].offset = XXX_OFFSET;
        bcm63XX_nand_parts[8].size = XXX_SIZE;
        bcm63XX_nand_parts[8].ecclayout = mtd->ecclayout;
        /* PTB: ITA language */
        bcm63XX_nand_parts[9].offset = LANG_ITA_OFFSET;
        bcm63XX_nand_parts[9].size = LANG_ITA_SIZE;
        bcm63XX_nand_parts[9].ecclayout = mtd->ecclayout;
        /* RUS: RUS language */
        bcm63XX_nand_parts[10].offset = LANG_RUS_OFFSET;
        bcm63XX_nand_parts[10].size = LANG_RUS_SIZE;
        bcm63XX_nand_parts[10].ecclayout = mtd->ecclayout;
        /* CHS: CHS language */
        bcm63XX_nand_parts[11].offset = LANG_CHS_OFFSET;
        bcm63XX_nand_parts[11].size = LANG_CHS_SIZE;
        bcm63XX_nand_parts[11].ecclayout = mtd->ecclayout;
        /* ENU: ENU language */
        bcm63XX_nand_parts[12].offset = LANG_ENU_OFFSET;
        bcm63XX_nand_parts[12].size = LANG_ENU_SIZE;
        bcm63XX_nand_parts[12].ecclayout = mtd->ecclayout;
        /* DEV: Language Device*/
        bcm63XX_nand_parts[13].offset = LANG_DEV_OFFSET;
        bcm63XX_nand_parts[13].size = LANG_DEV_SIZE;
        bcm63XX_nand_parts[13].ecclayout = mtd->ecclayout;
        /* NLD: NLD language */
        bcm63XX_nand_parts[14].offset = LANG_NLD_OFFSET;
        bcm63XX_nand_parts[14].size = LANG_NLD_SIZE;
        bcm63XX_nand_parts[14].ecclayout = mtd->ecclayout;        
        /* KOR: KOR language */
        bcm63XX_nand_parts[15].offset = LANG_KOR_OFFSET;
        bcm63XX_nand_parts[15].size = LANG_KOR_SIZE;
        bcm63XX_nand_parts[15].ecclayout = mtd->ecclayout;   
        /* FRA: FRA language */
        bcm63XX_nand_parts[16].offset = LANG_FRA_OFFSET;
        bcm63XX_nand_parts[16].size = LANG_FRA_SIZE;
        bcm63XX_nand_parts[16].ecclayout = mtd->ecclayout; 
        /* SVE: SVE language */
        bcm63XX_nand_parts[17].offset = LANG_SVE_OFFSET;
        bcm63XX_nand_parts[17].size = LANG_SVE_SIZE;
        bcm63XX_nand_parts[17].ecclayout = mtd->ecclayout;
        /* ESP: ESP language */
        bcm63XX_nand_parts[18].offset = LANG_ESP_OFFSET;
        bcm63XX_nand_parts[18].size = LANG_ESP_SIZE;
        bcm63XX_nand_parts[18].ecclayout = mtd->ecclayout;
        /* CSY: CSY language */
        bcm63XX_nand_parts[19].offset = LANG_CSY_OFFSET;
        bcm63XX_nand_parts[19].size = LANG_CSY_SIZE;
        bcm63XX_nand_parts[19].ecclayout = mtd->ecclayout;
        /* TUR: TUR language */
        bcm63XX_nand_parts[20].offset = LANG_TUR_OFFSET;
        bcm63XX_nand_parts[20].size = LANG_TUR_SIZE;
        bcm63XX_nand_parts[20].ecclayout = mtd->ecclayout;
        /* DATA */                                  
        bcm63XX_nand_parts[21].offset = DATA_OFFSET;
        bcm63XX_nand_parts[21].size = DATA_SIZE;
        bcm63XX_nand_parts[21].ecclayout = mtd->ecclayout;
        /* MTD-MAP */                                  
        bcm63XX_nand_parts[22].offset = MTDMAP_OFFSET;
        bcm63XX_nand_parts[22].size = MTDMAP_SIZE;
        bcm63XX_nand_parts[22].ecclayout = mtd->ecclayout;

        /* rootfs2 */                                  
        bcm63XX_nand_parts[23].offset = ROOTFS2_OFFSET;
        bcm63XX_nand_parts[23].size = ROOTFS_SIZE ;
        bcm63XX_nand_parts[23].ecclayout = mtd->ecclayout;  
        
        sc_setup_mtd_partitions(nandinfo);

#endif
        PRINTK("Part[0] name=%s, size=%llx, ofs=%llx\n", bcm63XX_nand_parts[0].name,
            bcm63XX_nand_parts[0].size, bcm63XX_nand_parts[0].offset);
        PRINTK("Part[1] name=%s, size=%llx, ofs=%llx\n", bcm63XX_nand_parts[1].name,
            bcm63XX_nand_parts[1].size, bcm63XX_nand_parts[1].offset);
        PRINTK("Part[2] name=%s, size=%llx, ofs=%llx\n", bcm63XX_nand_parts[2].name,
            bcm63XX_nand_parts[2].size, bcm63XX_nand_parts[2].offset);
        PRINTK("Part[3] name=%s, size=%llx, ofs=%llx\n", bcm63XX_nand_parts[3].name,
            bcm63XX_nand_parts[3].size, bcm63XX_nand_parts[3].offset);
#ifdef _SC_BUILD_
        PRINTK("Part[4] name=%s, size=%x, ofs=%x\n", bcm63XX_nand_parts[4].name,
            bcm63XX_nand_parts[4].size, bcm63XX_nand_parts[4].offset);
        PRINTK("Part[5] name=%s, size=%x, ofs=%x\n", bcm63XX_nand_parts[5].name,
            bcm63XX_nand_parts[5].size, bcm63XX_nand_parts[5].offset);
        PRINTK("Part[6] name=%s, size=%x, ofs=%x\n", bcm63XX_nand_parts[6].name,
            bcm63XX_nand_parts[6].size, bcm63XX_nand_parts[6].offset);

#endif
    }
}


static int __devinit brcmnanddrv_probe(struct platform_device *pdev)
{
    static int csi = 0; // Index into dev/nandInfo array
    int cs = 0;  // Chip Select
    int err = 0;
    struct brcmnand_info* info = NULL;
    static struct brcmnand_ctrl* ctrl = (struct brcmnand_ctrl*) 0;

    if(!gPageBuffer &&
       (gPageBuffer = kmalloc(sizeof(struct nand_buffers),GFP_KERNEL)) == NULL)
    {
        err = -ENOMEM;
    }
    else
    {
        if( (ctrl = kmalloc(sizeof(struct brcmnand_ctrl), GFP_KERNEL)) != NULL)
        {
            memset(ctrl, 0, sizeof(struct brcmnand_ctrl));
            ctrl->state = FL_READY;
            init_waitqueue_head(&ctrl->wq);
            spin_lock_init(&ctrl->chip_lock);

            if((info=kmalloc(sizeof(struct brcmnand_info),GFP_KERNEL)) != NULL)
            {
                gNandInfo[csi] = info;
                memset(info, 0, sizeof(struct brcmnand_info));
                info->brcmnand.ctrl = ctrl;
                info->brcmnand.ctrl->numchips = gNumNand = 1;
                info->brcmnand.csi = csi;

                /* For now all devices share the same buffer */
                info->brcmnand.ctrl->buffers =
                    (struct nand_buffers*) gPageBuffer;

                info->brcmnand.ctrl->numchips = gNumNand; 
                info->brcmnand.chip_shift = 0; // Only 1 chip
                info->brcmnand.priv = &info->mtd;
                info->mtd.name = dev_name(&pdev->dev);
                info->mtd.priv = &info->brcmnand;
                info->mtd.owner = THIS_MODULE;

                /* Enable the following for a flash based bad block table */
                info->brcmnand.options |= NAND_BBT_USE_FLASH;

                /* Each chip now will have its own BBT (per mtd handle) */
                if (brcmnand_scan(&info->mtd, cs, gNumNand) == 0)
                {
                    PRINTK("Master size=%08llx\n", info->mtd.size); 
                    brcmnanddrv_setup_mtd_partitions(info);
                    mtd_device_register(&info->mtd, info->parts, info->nr_parts);
                    dev_set_drvdata(&pdev->dev, info);
                }
                else
                    err = -ENXIO;

            }
            else
                err = -ENOMEM;

        }
        else
            err = -ENOMEM;
    }

    if( err )
    {
        if( gPageBuffer )
        {
            kfree(gPageBuffer);
            gPageBuffer = NULL;
        }

        if( ctrl )
        {
            kfree(ctrl);
            ctrl = NULL;
        }

        if( info )
        {
            kfree(info);
            info = NULL;
        }
    }

    return( err );
}

static int __devexit brcmnanddrv_remove(struct platform_device *pdev)
{
    struct brcmnand_info *info = dev_get_drvdata(&pdev->dev);

    dev_set_drvdata(&pdev->dev, NULL);

    if (info)
    {
        mtd_device_unregister(&info->mtd);

        brcmnand_release(&info->mtd);
        kfree(gPageBuffer);
        kfree(info);
    }

    return 0;
}

int get_kernel_rootfs_offset(void)
{
    return bcm63XX_nand_parts[0].offset;
}

int get_kernel_rootfs_size(void)
{
    return bcm63XX_nand_parts[0].size;
}
int get_language_size(void)
{
    return (bcm63XX_nand_parts[8].offset - bcm63XX_nand_parts[12].offset);
}

static int __init brcmnanddrv_init(void)
{
    int ret = 0;
    int csi;
    int ncsi;
    char cmd[32] = "\0";
    struct platform_device *pdev;

    if (flash_get_flash_type() != FLASH_IFC_NAND)
        return -ENODEV;

    kerSysBlParmsGetStr(NAND_COMMAND_NAME, cmd, sizeof(cmd));
    PRINTK("%s: brcmnanddrv_init - NANDCMD='%s'\n", __FUNCTION__, cmd);

    if (cmd[0])
    {
        if (strcmp(cmd, "rescan") == 0)
            gClearBBT = 1;
        else if (strcmp(cmd, "showbbt") == 0)
            gClearBBT = 2;
        else if (strcmp(cmd, "eraseall") == 0)
            gClearBBT = 8;
        else if (strcmp(cmd, "erase") == 0)
            gClearBBT = 7;
        else if (strcmp(cmd, "clearbbt") == 0)
            gClearBBT = 9;
        else if (strcmp(cmd, "showcet") == 0)
            gClearCET = 1;
        else if (strcmp(cmd, "resetcet") == 0)
            gClearCET = 2;
        else if (strcmp(cmd, "disablecet") == 0)
            gClearCET = 3;
        else
            printk(KERN_WARNING "%s: unknown command '%s'\n",
                __FUNCTION__, cmd);
    }
    
    for (csi=0; csi<NAND_MAX_CS; csi++)
    {
        gNandTiming1[csi] = 0;
        gNandTiming2[csi] = 0;
        gAccControl[csi] = 0;
        gNandConfig[csi] = 0;
    }

    if (nacc == 1)
        PRINTK("%s: nacc=%d, gAccControl[0]=%08lx, gNandConfig[0]=%08lx\n", \
            __FUNCTION__, nacc, acc[0], nandcfg[0]);

    if (nacc>1)
        PRINTK("%s: nacc=%d, gAccControl[1]=%08lx, gNandConfig[1]=%08lx\n", \
            __FUNCTION__, nacc, acc[1], nandcfg[1]);

    for (csi=0; csi<nacc; csi++)
        gAccControl[csi] = acc[csi];

    for (csi=0; csi<ncfg; csi++)
        gNandConfig[csi] = nandcfg[csi];

    ncsi = max(nt1, nt2);
    for (csi=0; csi<ncsi; csi++)
    {
        if (nt1 && csi < nt1)
            gNandTiming1[csi] = t1[csi];

        if (nt2 && csi < nt2)
            gNandTiming2[csi] = t2[csi];
        
    }

    printk (KERN_INFO DRIVER_INFO " (BrcmNand Controller)\n");
    if( (pdev = platform_device_alloc(DRIVER_NAME, 0)) != NULL )
    {
        platform_device_add(pdev);
        platform_device_put(pdev);
        ret = platform_driver_register(&brcmnand_platform_driver);
        if (ret >= 0)
            request_resource(&iomem_resource, &brcmnand_resources[0]);
        else
            printk("brcmnanddrv_init: driver_register failed, err=%d\n", ret);
    }
    else
        ret = -ENODEV;

    return ret;
}

static void __exit brcmnanddrv_exit(void)
{
    release_resource(&brcmnand_resources[0]);
    platform_driver_unregister(&brcmnand_platform_driver);
}

EXPORT_SYMBOL(get_kernel_rootfs_offset);
EXPORT_SYMBOL(get_kernel_rootfs_size);
EXPORT_SYMBOL(get_language_size);

module_init(brcmnanddrv_init);
module_exit(brcmnanddrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ton Truong <ttruong@broadcom.com>");
MODULE_DESCRIPTION("Broadcom NAND flash driver");

#endif //CONFIG_BCM_KF_MTD_BCMNAND
