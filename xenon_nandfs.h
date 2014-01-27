#ifndef _XENON_NANDFS_H
#define _XENON_NANDFS_H

#define MMC_ANCHOR_BLOCKS 		2
#define MMC_ANCHOR_HASH_LEN		0x14
#define MMC_ANCHOR_VERSION_POS	0x18
#define MMC_ANCHOR_MOBI_START	0x1C
#define MMC_ANCHOR_MOBI_SIZE	0x8

#define MAX_MOBILE				0xF
#define MOBILE_BASE				0x30
#define MOBILE_FSROOT			0x30
#define BB_MOBILE_FSROOT		0x2C

#define FSROOT_SIZE				0x2000

#define MOBILE_PB			32				// pages counting towards FsPageCount
#define MOBILE_MULTI		1				// small block multiplier for (MOBILE_PB-FsPageCount)
#define BB_MOBILE_PB		(MOBILE_PB*2)	// pages counting towards FsPageCount
#define BB_MOBILE_MULTI		4				// small block multiplier for (BB_MOBILE_PB-FsPageCount)

typedef struct _METADATA_SMALLBLOCK{
	u8 BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	u8 FsUnused0 : 4;
	u8 BlockID0 : 4; 
	u8 FsSequence0; // oddly these aren't reversed
	u8 FsSequence1;
	u8 FsSequence2;
	u8 BadBlock;
	u8 FsSequence3;
	u8 FsSize1; // (((FsSize0<<8)&0xFF)+(FsSize1&0xFF)) = cert size
	u8 FsSize0;
	u8 FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	u8 FsUnused1[2];
	u8 ECC3 : 2;
	u8 FsBlockType : 6;
	u8 ECC2; // 26 bit ECD
	u8 ECC1;
	u8 ECC0;
} SMALLBLOCK, *PSMALLBLOCK;

typedef struct _METADATA_BIGONSMALL{
	u8 FsSequence0;
	u8 BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	u8 FsUnused0 : 4;
	u8 BlockID0 : 4; 
	u8 FsSequence1;
	u8 FsSequence2;
	u8 BadBlock;
	u8 FsSequence3;
	u8 FsSize1; // (((FsSize0<<8)&0xFF)+(FsSize1&0xFF)) = cert size
	u8 FsSize0;
	u8 FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	u8 FsUnused1[2];
	u8 ECC3 : 2;
	u8 FsBlockType : 6;
	u8 ECC2; // 26 bit ECD
	u8 ECC1;
	u8 ECC0;
} BIGONSMALL, *PBIGONSMALL;

typedef struct _METADATA_BIGBLOCK{
	u8 BadBlock;
	u8 BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	u8 FsUnused0 : 4;
	u8 BlockID0 : 4;
	u8 FsSequence2; // oddly, compared to before these are reversed...?
	u8 FsSequence1;
	u8 FsSequence0;
	u8 FsUnused1;
	u8 FsSize1; // FS: 06 (system reserve block number) else ((FsSize0<<16)+(FsSize1<<8)) = cert size
	u8 FsSize0; // FS: 20 (size of flash filesys in smallblocks >>5)
	u8 FsPageCount; // FS: 04 (system config reserve) free pages left in block (multiples of 4 pages, ie if 3f then 3f*4 pages are free after)
	u8 FsUnused2[0x2];
	u8 ECC3 : 2;
	u8 FsBlockType : 6; // FS: 2a bitmap: 2c (both use FS: vals for size), mobiles
	u8 ECC2; // 26 bit ECD
	u8 ECC1;
	u8 ECC0;
} BIGBLOCK, *PBIGBLOCK;

typedef struct _METADATA{
	union{
		SMALLBLOCK sm;
		BIGONSMALL bos;
		BIGBLOCK bg;
	};
} METADATA, *PMETADATA;

typedef struct _PAGEDATA{
	u8 user[512];
	METADATA meta;
} PAGEDATA, *PPAGEDATA;

typedef struct _FS_TIME_STAMP{
	u32 DoubleSeconds : 5;
	u32 Minute : 6;
	u32 Hour : 5;
	u32 Day : 5;
	u32 Month : 4;
	u32 Year : 7;
} FS_TIME_STAMP;

typedef struct _FS_ENT{
	char fileName[22];
	u16 startCluster; //u8 startCluster[2];
	u32 clusterSz; //u8 clusterSz[4];
	u32 typeTime;
} FS_ENT, *PFS_ENT;

typedef struct _DUMPDATA{
	u16 lba_map[0x1000];
	u16 fsroot_block;
	u16 fsroot_v;
	u32 mobile_block[MAX_MOBILE];
	u32 mobile_size[MAX_MOBILE];
	u16 mobile_ver[MAX_MOBILE];
	FS_ENT* fs_ent;
} DUMPDATA, *PDUMPDATA;

void xenon_nandfs_calcecc(u32* data, u8* edc);
u16 xenon_nandfs_get_lba(METADATA* meta);
u32 xenon_nandfs_get_blocktype(METADATA* meta);
u8 xenon_nandfs_get_badblock_mark(METADATA* meta);
u32 xenon_nandfs_get_fssize(METADATA* meta);
u32 xenon_nandfs_get_fsfreepages(METADATA* meta);
u32 xenon_nandfs_get_fssequence(METADATA* meta);
bool xenon_nandfs_check_mmc_anchor_sha(u8* buf);
u32 xenon_nandfs_get_mmc_anchor_ver(u8* buf);
u16 xenon_nandfs_get_mmc_mobileblock(u8* buf, u8 mobi);
u16 xenon_nandfs_get_mmc_mobilesize(u8* buf, u8 mobi);
bool xenon_nandfs_check_ecc(PAGEDATA* pdata);
u32 xenon_nandfs_find_mobile(METADATA* metadata, u8 mobi);
int xenon_nandfs_parse_fsentries(u8* userbuf);
bool xenon_nandfs_init(void);
bool xenon_nandfs_init_one(void);

#endif
