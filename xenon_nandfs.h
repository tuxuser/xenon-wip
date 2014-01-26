#ifndef _XENON_NANDFS_H
#define _XENON_NANDFS_H

#define MMC_ANCHOR_BLOCKS 		2
#define MMC_ANCHOR_HASH_LEN		0x14
#define MMC_ANCHOR_VERSION_POS	0x18
#define MMC_ANCHOR_MOBI_START	0x1C
#define MMC_ANCHOR_MOBI_SIZE	0x8

#define MAX_MOBILE				9
#define MOBILE_BASE				0x30
#define MOBILE_FSROOT			0x30
#define BB_MOBILE_FSROOT		0x2C

#define FSROOT_SIZE				0x2000

#define MOBILE_PB			32				// pages counting towards FsPageCount
#define MOBILE_MULTI		1				// small block multiplier for (MOBILE_PB-FsPageCount)
#define BB_MOBILE_PB		(MOBILE_PB*2)	// pages counting towards FsPageCount
#define BB_MOBILE_MULTI		4				// small block multiplier for (BB_MOBILE_PB-FsPageCount)

typedef struct _METADATA_SMALLBLOCK{
	unsigned char BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	unsigned char FsUnused0 : 4;
	unsigned char BlockID0 : 4; 
	unsigned char FsSequence0; // oddly these aren't reversed
	unsigned char FsSequence1;
	unsigned char FsSequence2;
	unsigned char BadBlock;
	unsigned char FsSequence3;
	unsigned char FsSize1; // (((FsSize0<<8)&0xFF)+(FsSize1&0xFF)) = cert size
	unsigned char FsSize0;
	unsigned char FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	unsigned char FsUnused1[2];
	unsigned char ECC3 : 2;
	unsigned char FsBlockType : 6;
	unsigned char ECC2; // 26 bit ECD
	unsigned char ECC1;
	unsigned char ECC0;
} SMALLBLOCK, *PSMALLBLOCK;

typedef struct _METADATA_BIGONSMALL{
	unsigned char FsSequence0;
	unsigned char BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	unsigned char FsUnused0 : 4;
	unsigned char BlockID0 : 4; 
	unsigned char FsSequence1;
	unsigned char FsSequence2;
	unsigned char BadBlock;
	unsigned char FsSequence3;
	unsigned char FsSize1; // (((FsSize0<<8)&0xFF)+(FsSize1&0xFF)) = cert size
	unsigned char FsSize0;
	unsigned char FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	unsigned char FsUnused1[2];
	unsigned char ECC3 : 2;
	unsigned char FsBlockType : 6;
	unsigned char ECC2; // 26 bit ECD
	unsigned char ECC1;
	unsigned char ECC0;
} BIGONSMALL, *PBIGONSMALL;

typedef struct _METADATA_BIGBLOCK{
	unsigned char BadBlock;
	unsigned char BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	unsigned char FsUnused0 : 4;
	unsigned char BlockID0 : 4;
	unsigned char FsSequence2; // oddly, compared to before these are reversed...?
	unsigned char FsSequence1;
	unsigned char FsSequence0;
	unsigned char FsUnused1;
	unsigned char FsSize1; // FS: 06 (system reserve block number) else ((FsSize0<<16)+(FsSize1<<8)) = cert size
	unsigned char FsSize0; // FS: 20 (size of flash filesys in smallblocks >>5)
	unsigned char FsPageCount; // FS: 04 (system config reserve) free pages left in block (multiples of 4 pages, ie if 3f then 3f*4 pages are free after)
	unsigned char FsUnused2[0x2];
	unsigned char ECC3 : 2;
	unsigned char FsBlockType : 6; // FS: 2a bitmap: 2c (both use FS: vals for size), mobiles
	unsigned char ECC2; // 26 bit ECD
	unsigned char ECC1;
	unsigned char ECC0;
} BIGBLOCK, *PBIGBLOCK;

typedef struct _METADATA{
	union{
		SMALLBLOCK sm;
		BIGONSMALL bos;
		BIGBLOCK bg;
	};
} METADATA, *PMETADATA;

typedef struct _PAGEDATA{
	unsigned char user[512];
	METADATA meta;
} PAGEDATA, *PPAGEDATA;

typedef struct _FS_TIME_STAMP{
	unsigned int DoubleSeconds : 5;
	unsigned int Minute : 6;
	unsigned int Hour : 5;
	unsigned int Day : 5;
	unsigned int Month : 4;
	unsigned int Year : 7;
} FS_TIME_STAMP;

typedef struct _FS_ENT{
	char fileName[22];
	unsigned short startCluster; //u8 startCluster[2];
	unsigned int clusterSz; //u8 clusterSz[4];
	unsigned int typeTime;
} FS_ENT, *PFS_ENT;

typedef struct _DUMPDATA{
	int fsroot_blk[16];
	int lba_map[0x1000];
	int mobile_blocks[MAX_MOBILE];
	int mobile_size[MAX_MOBILE];
	int mobile_ver[MAX_MOBILE];
	FS_ENT* fs_ent;
} DUMPDATA, *PDUMPDATA;

void xenon_nandfs_calcecc(unsigned int *data, unsigned char* edc);
int xenon_nandfs_get_lba(METADATA* meta);
int xenon_nandfs_get_blocktype(METADATA* meta);
int xenon_nandfs_get_badblock_mark(METADATA* meta);
int xenon_nandfs_get_fssize(METADATA* meta);
int xenon_nandfs_get_fsfreepages(METADATA* meta);
int xenon_nandfs_get_fssequence(METADATA* meta);
unsigned int xenon_nandfs_check_mmc_anchor_sha(unsigned char* buf);
unsigned int xenon_nandfs_get_mmc_anchor_ver(unsigned char* buf);
unsigned int xenon_nandfs_get_mmc_mobileblock(unsigned char* buf, int mobile_num);
unsigned int xenon_nandfs_get_mmc_mobilesize(unsigned char* buf, int mobile_num);
int xenon_nandfs_check_ecc(PAGEDATA* pdata);
int xenon_nandfs_find_mobile(METADATA* metadata, int mobi);
int xenon_nandfs_init(void);
int xenon_nandfs_init_one(void);

#endif
