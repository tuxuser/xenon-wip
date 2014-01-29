#ifndef _XENON_NANDFS_H
#define _XENON_NANDFS_H

#define MMC_ANCHOR_BLOCKS 		2
#define MMC_ANCHOR_HASH_LEN		0x14
#define MMC_ANCHOR_VERSION_POS	0x1A
#define MMC_ANCHOR_MOBI_START	0x1C
#define MMC_ANCHOR_MOBI_SIZE	0x4

#define MAX_MOBILE				0xF
#define MOBILE_BASE				0x30
#define MOBILE_END				0x3F
#define MOBILE_FSROOT			0x30
#define BB_MOBILE_FSROOT		0x2C

#define FSROOT_SIZE				0x2000

#define MOBILE_PB			32				// pages counting towards FsPageCount
#define MOBILE_MULTI		1				// small block multiplier for (MOBILE_PB-FsPageCount)
#define BB_MOBILE_PB		(MOBILE_PB*2)	// pages counting towards FsPageCount
#define BB_MOBILE_MULTI		4				// small block multiplier for (BB_MOBILE_PB-FsPageCount)

#define MAX_LBA				0x1000

typedef struct _METADATA_SMALLBLOCK{
	u8 BlockID1; // lba/id = (((BlockID0&0xF)<<8)+(BlockID1))
	u8 BlockID0 : 4;
	u8 FsUnused0 : 4;
	//u8 BlockID0; 
	u8 FsSequence0; // oddly these aren't reversed
	u8 FsSequence1;
	u8 FsSequence2;
	u8 BadBlock;
	u8 FsSequence3;
	u8 FsSize1; // ((FsSize0<<8)+FsSize1) = cert size
	u8 FsSize0;
	u8 FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	u8 FsUnused1[0x2];
	u8 FsBlockType : 6;
	u8 ECC3 : 2;
	u8 ECC2; // 14 bit ECD
	u8 ECC1;
	u8 ECC0;
} SMALLBLOCK, *PSMALLBLOCK;

typedef struct _METADATA_BIGONSMALL{
	u8 FsSequence0;
	u8 BlockID1; // lba/id = (((BlockID0<<8)&0xF)+(BlockID1&0xFF))
	u8 BlockID0 : 4; 
	u8 FsUnused0 : 4;
	u8 FsSequence1;
	u8 FsSequence2;
	u8 BadBlock;
	u8 FsSequence3;
	u8 FsSize1; // (((FsSize0<<8)&0xFF)+(FsSize1&0xFF)) = cert size
	u8 FsSize0;
	u8 FsPageCount; // free pages left in block (ie: if 3 pages are used by cert then this would be 29:0x1d)
	u8 FsUnused1[2];
	u8 FsBlockType : 6;
	u8 ECC3 : 2;
	u8 ECC2; // 26 bit ECD
	u8 ECC1;
	u8 ECC0;
} BIGONSMALL, *PBIGONSMALL;

typedef struct _METADATA_BIGBLOCK{
	u8 BadBlock;
	u8 BlockID1; // lba/id = (((BlockID0&0xF)<<8)+(BlockID1&0xFF))
	u8 BlockID0 : 4;
	u8 FsUnused0 : 4;
	u8 FsSequence2; // oddly, compared to before these are reversed...?
	u8 FsSequence1;
	u8 FsSequence0;
	u8 FsUnused1;
	u8 FsSize1; // FS: 06 ((FsSize0<<16)+(FsSize1<<8)+FsSize2) = cert size
	u8 FsSize0; // FS: 20
	u8 FsPageCount; // FS: 04 free pages left in block (multiples of 4 pages, ie if 3f then 3f*4 pages are free after)
	u8 FsUnused2[0x2];
	u8 FsBlockType : 6; // FS: 2a bitmap: 2c (both use FS: vals for size), mobiles
	u8 ECC3 : 2;
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
	u8 User[512];
	METADATA Meta;
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
	char FileName[22];
	u16 StartCluster; //u8 startCluster[2];
	u32 ClusterSz; //u8 clusterSz[4];
	u32 TypeTime;
} FS_ENT, *PFS_ENT;

typedef struct _DUMPDATA{
	u16 LBAMap[MAX_LBA];
	u16 FSRootBlock;
	u16 FSRootVer;
	u8 FSRootBuf[FSROOT_SIZE];
	u16* pFSRootBufShort; // u16* pFSRootBufShort = (u16*)FSRootBuf;
	u8 FSRootFileBuf[FSROOT_SIZE];
	u32 MobileBlock[MAX_MOBILE];
	u32 MobileSize[MAX_MOBILE];
	u16 MobileVer[MAX_MOBILE];
	FS_ENT *FsEnt;
} DUMPDATA, *PDUMPDATA;

void xenon_nandfs_CalcECC(u32* data, u8* edc);
u16 xenon_nandfs_GetLBA(METADATA* meta);
u8 xenon_nandfs_GetBlockType(METADATA* meta);
u8 xenon_nandfs_GetBadblockMark(METADATA* meta);
u32 xenon_nandfs_GetFsSize(METADATA* meta);
u32 xenon_nandfs_GetFsFreepages(METADATA* meta);
u32 xenon_nandfs_GetFsSequence(METADATA* meta);
bool xenon_nandfs_CheckMMCAnchorSha(u8* buf);
u16 xenon_nandfs_GetMMCAnchorVer(u8* buf);
u16 xenon_nandfs_GetMMCMobileBlock(u8* buf, u8 mobi);
u16 xenon_nandfs_GetMMCMobileSize(u8* buf, u8 mobi);
bool xenon_nandfs_CheckECC(PAGEDATA* pdata);
int xenon_nandfs_SplitFsRootBuf(u8* userbuf);
bool xenon_nandfs_Init(void);
bool xenon_nandfs_init_one(void);

#endif
