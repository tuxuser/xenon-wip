#ifndef _XENON_NANDFS_H
#define _XENON_NANDFS_H

typedef struct _METADATA_SMALLBLOCK{
	unsigned char BlockID1; // lba/id = (((BlockID0&0xF)<<8)+(BlockID1))
	unsigned char BlockID0 : 4;
	unsigned char FsUnused0 : 4;
	//unsigned char BlockID0; 
	unsigned char FsSequence0; // oddly these aren't reversed
	unsigned char FsSequence1;
	unsigned char FsSequence2;
	unsigned char BadBlock;
	unsigned char FsSequence3;
	unsigned char FsSize1; // ((FsSize0<<8)+FsSize1) = cert size
	unsigned char FsSize0;
	unsigned char FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	unsigned char FsUnused1[0x2];
	unsigned char FsBlockType : 6;
	unsigned char ECC3 : 2;
	unsigned char ECC2; // 14 bit ECD
	unsigned char ECC1;
	unsigned char ECC0;
} SMALLBLOCK, *PSMALLBLOCK;

typedef struct _METADATA_BIGONSMALL{
	unsigned char FsSequence0;
	unsigned char BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	unsigned char BlockID0 : 4; 
	unsigned char FsUnused0 : 4;
	unsigned char FsSequence1;
	unsigned char FsSequence2;
	unsigned char BadBlock;
	unsigned char FsSequence3;
	unsigned char FsSize1; // (((FsSize0<<8)&0xFF)+(FsSize1&0xFF)) = cert size
	unsigned char FsSize0;
	unsigned char FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	unsigned char FsUnused1[2];
	unsigned char FsBlockType : 6;
	unsigned char ECC3 : 2;
	unsigned char ECC2; // 26 bit ECD
	unsigned char ECC1;
	unsigned char ECC0;
} BIGONSMALL;

typedef struct _METADATA_BIGBLOCK{
	unsigned char BadBlock;
	unsigned char BlockID1; // lba/id = (((BlockID0&0xF)<<8)+(BlockID1&0xFF))
	unsigned char BlockID0 : 4;
	unsigned char FsUnused0 : 4;
	unsigned char FsSequence2; // oddly, compared to before these are reversed...?
	unsigned char FsSequence1;
	unsigned char FsSequence0;
	unsigned char FsUnused1;
	unsigned char FsSize1; // FS: 06 ((FsSize0<<16)+(FsSize1<<8)+FsSize2) = cert size
	unsigned char FsSize0; // FS: 20
	unsigned char FsPageCount; // FS: 04 free pages left in block (multiples of 4 pages, ie if 3f then 3f*4 pages are free after)
	unsigned char FsUnused2[0x2];
	unsigned char FsBlockType : 6; // FS: 2a bitmap: 2c (both use FS: vals for size), mobiles
	unsigned char ECC3 : 2;
	unsigned char ECC2; // 14 bit ECD
	unsigned char ECC1;
	unsigned char ECC0;
} BIGBLOCK;

typedef struct _METADATA{
	union{
		SMALLBLOCK sm;
		BIGONSMALL bos;
		BIGBLOCK bg;
	};
} METADATA, *PMETADATA;

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
	int badblocks[100];
	int fsentry[10];
	int fsentry_v;
	int rootblock[0x1000];
	struct _FS_ENT fs_ent;
} DUMPDATA, *PDUMPDATA;

#endif
