#ifndef _XENON_NANDFS_H
#define _XENON_NANDFS_H

#define MMC_ANCHOR_BLOCKS 		2
#define MMC_ANCHOR_HASH_LEN		0x14
#define MMC_ANCHOR_VERSION_POS	0x18
#define MMC_ANCHOR_MOBI_START	0x1C
#define MMC_ANCHOR_MOBI_SIZE	0x8

#define MOBILE_BASE				0x30
#define MOBILE_FSROOT			0x30

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
	int badblocks[100];
	int fsentry[10];
	int fsentry_v;
	int rootblock[0x1000];
	FS_ENT fs_ent;
} DUMPDATA, *PDUMPDATA;

#endif