/*
 *  Xenon System Flash Filesystem
 *
 *  Copyright (C) 2014 tuxuser
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * https://www.kernel.org/doc/htmldocs/mtdnand/
 * http://www.informit.com/articles/article.aspx?p=1187102&seqNum=1
 */

#include "xenon_sfc.h"
#include "xenon_nandfs.h"

#ifdef DEBUG

	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#define vmalloc malloc
	#define vfree free
	#define printk printf 
	#define KERN_INFO

#else

	#include <linux/vmalloc.h>

#endif

static xenon_nand nand = {0};
static DUMPDATA dumpdata;

#ifdef DEBUG

	unsigned char fixed_type = -1;
	FILE * pFile;
	
	static inline unsigned short __builtin_bswap16(unsigned short a)
	{
	  return (a<<8)|(a>>8);
	}
	
	int xenon_sfc_ReadBlockSeparate(unsigned char* user, unsigned char* spare, unsigned int block)
	{
		unsigned short i;
		unsigned char* buf = malloc(nand.BlockSzPhys);
		unsigned int addr = block * nand.BlockSzPhys;
		//printf("Reading block %04x from addr %08x\n", block, addr);
		fseek(pFile, addr, SEEK_SET);
		fread(buf ,nand.BlockSzPhys, 1, pFile);
		for(i=0;i<nand.PagesInBlock; i++){
			memcpy(&user[i*nand.PageSz], &buf[i*nand.PageSzPhys], nand.PageSz);
			memcpy(&spare[i*nand.MetaSz], &buf[(i*nand.PageSzPhys)+nand.PageSz], nand.MetaSz);
		}
	}
	
	void xenon_sfc_ReadMapData(unsigned char* buf, unsigned int startaddr, unsigned int total_len)
	{
		fseek(pFile, startaddr, SEEK_SET);
		fread(buf, total_len, 1, pFile);
	}
	
	void xenon_sfc_GetNandStruct(xenon_nand* xe_nand)
	{	
		xe_nand->PagesInBlock = 32;
		xe_nand->MetaSz = 0x10;
		xe_nand->PageSz = 0x200;
		xe_nand->PageSzPhys = xe_nand->PageSz + xe_nand->MetaSz;
		
		xe_nand->MetaType = META_TYPE_SM;
		xe_nand->BlockSz = 0x4000;
		xe_nand->BlockSzPhys = 0x4200;
		xe_nand->isBB = false;
		xe_nand->isBBCont = false;
		
		switch(fixed_type)
		{
			case META_TYPE_SM:
					xe_nand->SizeDump = 0x1080000;
					xe_nand->SizeData = 0x1000000;
					xe_nand->SizeSpare = 0x80000;
					xe_nand->SizeUsableFs = 0x3E0;
					break;
			case META_TYPE_BOS:
					xe_nand->MetaType = META_TYPE_BOS;
					xe_nand->isBBCont = true;
					xe_nand->SizeDump = 0x1080000;
					xe_nand->SizeData = 0x1000000;
					xe_nand->SizeSpare = 0x80000;
					xe_nand->SizeUsableFs = 0x3E0;
					break;
			case META_TYPE_BG:
					xe_nand->MetaType = META_TYPE_BG;
					xe_nand->isBBCont = true;
					xe_nand->isBB = true;
					xe_nand->SizeDump = 0x4200000;
					xe_nand->BlockSzPhys = 0x21000;
					xe_nand->SizeData = 0x4000000;
					xe_nand->SizeSpare = 0x200000;
					xe_nand->PagesInBlock = 256;
					xe_nand->BlockSz = 0x20000;
					xe_nand->SizeUsableFs = 0x1E0;
					break;
			case META_TYPE_NONE:
					xe_nand->MMC = true;
					xe_nand->MetaType = META_TYPE_NONE;
					xe_nand->BlockSz = 0x4000;
					xe_nand->BlockSzPhys = 0x20000;
					xe_nand->MetaSz = 0;
			
					xe_nand->SizeDump = 0x3000000;
					xe_nand->SizeData = 0x3000000;
					xe_nand->SizeSpare = 0;
					xe_nand->SizeUsableFs = 0xC00; // (nand.size_dump/nand.BlockSz)
				break;
		}
		xe_nand->ConfigBlock = xe_nand->SizeUsableFs - CONFIG_BLOCKS;
		xe_nand->BlocksCount = xe_nand->SizeDump / xe_nand->BlockSzPhys;
		xe_nand->PagesCount = xe_nand->BlocksCount * xe_nand->PagesInBlock;
		
#ifdef DEBUG
	printf("Enumerated NAND Information:\n");
	printf("MetaType: %i\n", xe_nand->MetaType);
	printf("MMC: %i\n", xe_nand->MMC);
	printf("isBBCont: %i\n", xe_nand->isBBCont);
	printf("isBB: %i\n", xe_nand->isBB);
	printf("BlockSz: 0x%x\n", xe_nand->BlockSz);
	printf("BlockSzPhys: 0x%x\n", xe_nand->BlockSzPhys);
	printf("PageSz: 0x%x\n", xe_nand->PageSz);
	printf("PageSzPhys: 0x%x\n", xe_nand->PageSzPhys);
	printf("PagesInBlock: %i\n", xe_nand->PagesInBlock);
	printf("SizeUsableFs: 0x%x\n", xe_nand->SizeUsableFs);
	printf("MetaSz: 0x%x\n", xe_nand->MetaSz);
	printf("SizeDump: 0x%x\n", xe_nand->SizeDump);
	printf("SizeData: 0x%x\n", xe_nand->SizeData);
	printf("SizeSpare: 0x%x\n", xe_nand->SizeSpare);
	printf("ConfigBlock: 0x%x\n", xe_nand->ConfigBlock);
	printf("BlocksCount: 0x%x\n", xe_nand->BlocksCount);
	printf("PagesCount: 0x%x\n", xe_nand->PagesCount);
	printf("\n\n\n");
#endif
	}
	
	int main(int argc, char *argv[])
	{
		if(argc != 3)
		{
			printf("Usage: %s nandtype dump_filename.bin\n", argv[0]);
			printf("Valid nandtypes:\n\n");
			printf("sm - Small Block (Xenon, Zephyr, Falcon, some Jasper 16MB)\n");
			printf("bos - Big on Small Block (some Jasper 16MB)\n");
			printf("bg - Big Block (Jasper 256/512MB\n");
			printf("mmc - eMMC NAND (Corona)\n");
			return 1;
		}
		
		if(!strcmp(argv[1],"sm"))
			fixed_type = META_TYPE_SM;
		else if(!strcmp(argv[1],"bos"))
			fixed_type = META_TYPE_BOS;
		else if(!strcmp(argv[1],"bg"))
			fixed_type = META_TYPE_BG;
		else if(!strcmp(argv[1],"mmc"))
			fixed_type = META_TYPE_NONE;
		else
		{
			printf("Unsupported meta-type: %s\n", argv[1]);
			return 2;
		}
		
		pFile = fopen(argv[2],"rb");
		if (pFile==NULL)
		{
			printf("Failed opening \'%s\'!!!\n", argv[2]);
			return 4;
		}
		
		xenon_nandfs_init_one();
		fclose (pFile);
		
		return 0;
	}

int writeToFile(char* filename, unsigned char *buf, unsigned int size)
{
	FILE* outfile;
	outfile = fopen(filename, "wb");
	if(outfile != NULL)
	{
		fwrite(buf, size, 1, outfile);
		fclose(outfile);
		return 0;
	}
	return 1;
}

int fileExists(char* filename)
{
	FILE* test;
	int ret = 0;
	test = fopen(filename, "rb");
	if(test != NULL)
	{
		ret = 1;
		fclose(test);
	}
	return ret;
}

void appendBlockToFile(char* filename, unsigned int block, unsigned int len)
{
	FILE* outfile;
	unsigned int offsetUser = block*nand.BlockSz;
	unsigned char *userbuf = (unsigned char *)vmalloc(nand.BlockSz);
	unsigned char* sparebuf = (unsigned char *)vmalloc(nand.MetaSz*nand.PagesInBlock);

	if(nand.MMC)
		xenon_sfc_ReadMapData(userbuf, offsetUser, nand.BlockSz); 
	else
		xenon_sfc_ReadBlockSeparate(userbuf, sparebuf, block);
	
	if(fileExists(filename))
		outfile = fopen(filename, "ab+");
	else
		outfile = fopen(filename, "wb");
	fwrite(userbuf, len, 1, outfile);
	fclose(outfile);
}

void xenon_nandfs_bb_test(void)
{
	unsigned short prev_lba, cur_lba;
	unsigned int page, block;
	unsigned char* userbuf = (unsigned char *)vmalloc(nand.BlockSz);
	unsigned char* sparebuf = (unsigned char *)vmalloc(nand.MetaSz*nand.PagesInBlock);
	METADATA *meta;
	
	for(block=0;block<nand.BlocksCount;block++)
	{
		xenon_sfc_ReadBlockSeparate(userbuf, sparebuf, block);
		meta = (METADATA*)sparebuf;
#if 0
		printf("Block: %x - LBA:%x\n",block, dumpdata.LBAMap[block]);
		printf("Meta:BadBlock %02x\n",meta->bg.BadBlock);
		printf("Meta:BlockID0 %02x\n",meta->bg.BlockID0);
		printf("Meta:BlockID1 %02x\n",meta->bg.BlockID1);
		printf("Meta:FsBlockType %02x\n",meta->bg.FsBlockType);
		printf("Meta:FsPageCount %02x\n",meta->bg.FsPageCount);
		printf("Meta:FsSequence0 %02x\n",meta->bg.FsSequence0);
		printf("Meta:FsSequence1 %02x\n",meta->bg.FsSequence1);
		printf("Meta:FsSequence2 %02x\n",meta->bg.FsSequence2);
		printf("Meta:FsSize0 %02x\n",meta->bg.FsSize0);
		printf("Meta:FsSize1 %02x\n",meta->bg.FsSize1);
		printf("Meta:FsUnused0 %02x\n",meta->bg.FsUnused0);
		printf("Meta:FsUnused1 %02x\n",meta->bg.FsUnused1);
		printf("Meta:FsUnused2 %02x\n",meta->bg.FsUnused2);
#endif
		for(page=0;page<nand.PagesInBlock;page++)
		{
			meta = (METADATA*)&sparebuf[page*nand.MetaSz];
			cur_lba = xenon_nandfs_GetLBA(meta);
			if(cur_lba != prev_lba)
			{
//				printk(KERN_INFO "block: 0x%03x, page: %03i, LBA: %04x\n", block, page, cur_lba);
				prev_lba = cur_lba;
			}
		}
	}
	
}
#endif

void xenon_nandfs_CalcECC(unsigned int *data, unsigned char* edc) {
	unsigned int i=0, val=0;
	unsigned int v=0;

	for (i = 0; i < 0x1066; i++)
	{
		if (!(i & 31))
			v = ~__builtin_bswap32(*data++);
		val ^= v & 1;
		v>>=1;
		if (val & 1)
			val ^= 0x6954559;
		val >>= 1;
	}

	val = ~val;

	// 26 bit ecc data
	edc[0] = ((val << 6) | (data[0x20C] & 0x3F)) & 0xFF;
	edc[1] = (val >> 2) & 0xFF;
	edc[2] = (val >> 10) & 0xFF;
	edc[3] = (val >> 18) & 0xFF;
}

unsigned short xenon_nandfs_GetLBA(METADATA* meta)
{
	unsigned short ret = 0;
	
	switch (nand.MetaType)
	{
		case META_TYPE_SM:
			ret =  (((meta->sm.BlockID0&0xF)<<8)+(meta->sm.BlockID1));
			break;
		case META_TYPE_BOS:
			ret =  (((meta->bos.BlockID0<<8)&0xF)+(meta->bos.BlockID1&0xFF));
			break;
		case META_TYPE_BG:
			ret =  (((meta->bg.BlockID0&0xF)<<8)+(meta->bg.BlockID1&0xFF));
			break;
	}
	return ret;
}

unsigned char xenon_nandfs_GetBlockType(METADATA* meta)
{
	unsigned char ret = 0;
	
	switch (nand.MetaType)
	{
		case META_TYPE_SM:
			ret =  (meta->sm.FsBlockType&0x3F);
			break;
		case META_TYPE_BOS:
			ret =  (meta->bos.FsBlockType&0x3F);
			break;
		case META_TYPE_BG:
			ret =  (meta->bg.FsBlockType&0x3F);
			break;
	}
	return ret;
}

unsigned char xenon_nandfs_GetBadBlockMark(METADATA* meta)
{
	unsigned char ret = 0;
	
	switch (nand.MetaType)
	{
		case META_TYPE_SM:
			ret =  meta->sm.BadBlock;
			break;
		case META_TYPE_BOS:
			ret =  meta->bos.BadBlock;
			break;
		case META_TYPE_BG:
			ret =  meta->bg.BadBlock;
			break;
	}
	return ret;
}

unsigned int xenon_nandfs_GetFsSize(METADATA* meta)
{
	unsigned int ret = 0;
	
	switch (nand.MetaType)
	{
		case META_TYPE_SM:
			ret = ((meta->sm.FsSize0<<8)+meta->sm.FsSize1);
			break;
		case META_TYPE_BOS:
			ret = (((meta->bos.FsSize0<<8)&0xFF)+(meta->bos.FsSize1&0xFF));
			break;
		case META_TYPE_BG:
			ret = (((meta->bg.FsSize0&0xFF)<<8)+(meta->bg.FsSize1&0xFF));
			break;
	}
	return ret;
}

unsigned int xenon_nandfs_GetFsFreepages(METADATA* meta)
{
	unsigned int ret = 0;
	
	switch (nand.MetaType)
	{
		case META_TYPE_SM:
			ret =  meta->sm.FsPageCount;
			break;
		case META_TYPE_BOS:
			ret =  meta->bos.FsPageCount;
			break;
		case META_TYPE_BG:
			ret =  (meta->bg.FsPageCount * 4);
			break;
	}
	return ret;
}

unsigned int xenon_nandfs_GetFsSequence(METADATA* meta)
{
	unsigned int ret = 0;
	
	switch (nand.MetaType)
	{
		case META_TYPE_SM:
			ret =  (meta->sm.FsSequence0+(meta->sm.FsSequence1<<8)+(meta->sm.FsSequence2<<16));
			break;
		case META_TYPE_BOS:
			ret =  (meta->bos.FsSequence0+(meta->bos.FsSequence1<<8)+(meta->bos.FsSequence2<<16));
			break;
		case META_TYPE_BG:
			ret =  (meta->bg.FsSequence0+(meta->bg.FsSequence1<<8)+(meta->bg.FsSequence2<<16));
			break;
	}
	return ret;
}

bool xenon_nandfs_CheckMMCAnchorSha(unsigned char* buf)
{
	//unsigned char* data = buf;
	//CryptSha(&data[MMC_ANCHOR_HASH_LEN], (0x200-MMC_ANCHOR_HASH_LEN), NULL, 0, NULL, 0, sha, MMC_ANCHOR_HASH_LEN);
	return 0;
}

unsigned short xenon_nandfs_GetMMCAnchorVer(unsigned char* buf)
{
	unsigned char* data = buf;
	unsigned short tmp = (data[MMC_ANCHOR_VERSION_POS]<<8|data[MMC_ANCHOR_VERSION_POS+1]);

	return tmp;
}

unsigned short xenon_nandfs_GetMMCMobileBlock(unsigned char* buf, unsigned char mobi)
{
	unsigned char* data = buf;
	unsigned char mob = mobi - MOBILE_BASE;
	unsigned char offset = MMC_ANCHOR_MOBI_START+(mob*MMC_ANCHOR_MOBI_SIZE);
	unsigned short tmp = data[offset]<<8|data[offset+1];
	
	return tmp;
}

unsigned short xenon_nandfs_GetMMCMobileSize(unsigned char* buf, unsigned char mobi)
{
	unsigned char* data = buf;
	unsigned char mob = mobi - MOBILE_BASE;
	unsigned char offset = MMC_ANCHOR_MOBI_START+(mob*MMC_ANCHOR_MOBI_SIZE)+0x2;
	unsigned short tmp = data[offset]<<8|data[offset+1];
	
	return __builtin_bswap16(tmp);
}

bool xenon_nandfs_CheckECC(PAGEDATA* pdata)
{
	unsigned char ecd[4];
	xenon_nandfs_CalcECC((unsigned int*)pdata->User, ecd);
	if ((ecd[0] == pdata->Meta.sm.ECC0) && (ecd[1] == pdata->Meta.sm.ECC1) && (ecd[2] == pdata->Meta.sm.ECC2) && (ecd[3] == pdata->Meta.sm.ECC3))
		return 0;
	return 1;
}

unsigned int xenon_nandfs_ExtractFsEntry(void)
{
	unsigned int i, k;
	unsigned int fsBlock, realBlock;
	unsigned int fsFileSize;
//	FS_TIME_STAMP timeSt;
	
	dumpdata.pFSRootBufShort = (unsigned short*)dumpdata.FSRootBuf;
	
	for(i=0; i<256; i++)
	{
		dumpdata.FsEnt = (FS_ENT*)&dumpdata.FSRootFileBuf[i*sizeof(FS_ENT)];
		if(dumpdata.FsEnt->FileName[0] != 0)
		{
			printk(KERN_INFO "file: %s ", dumpdata.FsEnt->FileName);
			for(k=0; k< (22-strlen(dumpdata.FsEnt->FileName)); k++)
			{
				printk(KERN_INFO " ");
			}
			fsBlock = __builtin_bswap16(dumpdata.FsEnt->StartCluster);
			fsFileSize = __builtin_bswap32(dumpdata.FsEnt->ClusterSz);
			printk(KERN_INFO "start: %04x size: %08x stamp: %08x\n", fsBlock, fsFileSize, (unsigned int)__builtin_bswap32(dumpdata.FsEnt->TypeTime));

			// extract the file
			if(dumpdata.FsEnt->FileName[0] != 0x5) // file is erased but still in the record
			{
				realBlock = fsBlock;
				while(fsFileSize > nand.BlockSz)
				{
#ifdef DEBUG
					printk(KERN_INFO "%04x:%04x, ", fsBlock, dumpdata.LBAMap[fsBlock]);
#endif
#ifdef WRITE_OUT
					appendBlockToFile(dumpdata.FsEnt->FileName, realBlock, nand.BlockSz);
#endif
					fsFileSize = fsFileSize-nand.BlockSz;
					fsBlock = __builtin_bswap16(dumpdata.pFSRootBufShort[(fsBlock)]); // gets next block
					realBlock = dumpdata.LBAMap[fsBlock];
					realBlock = fsBlock;
				}
				if((fsFileSize > 0)&&(fsBlock<0x1FFE))
				{
#ifdef DEBUG
					printk(KERN_INFO "%04x:%04x, ", fsBlock, dumpdata.LBAMap[fsBlock]);
#endif
#ifdef WRITE_OUT
					appendBlockToFile(dumpdata.FsEnt->FileName, realBlock, fsFileSize);
#endif
				}
				else
					printk(KERN_INFO "** Couldn't write file tail! %04x:%04x, ", fsBlock, dumpdata.LBAMap[fsBlock]);
			}
			else
				printk(KERN_INFO "   erased still has entry???");		
			printk(KERN_INFO "\n\n");
		}
		dumpdata.FsEnt++;
	}
	return 0;
}

int xenon_nandfs_SplitFsRootBuf(unsigned char* userbuf)
{
	unsigned int i, j, root_off, file_off, ttl_off;
	unsigned char* data = userbuf;
	
	root_off = 0;
	file_off = 0;
	ttl_off = 0;
	for(i=0; i<16; i++) // copy alternating 512 bytes into each buf
	{
		for(j=0; j<nand.PageSz; j++)
		{
			dumpdata.FSRootBuf[root_off+j] = data[ttl_off+j];
			dumpdata.FSRootFileBuf[file_off+j] = data[ttl_off+j+512];
		}
		root_off += nand.PageSz;
		file_off += nand.PageSz;
		ttl_off  += (nand.PageSz*2);
	}

#ifdef FSROOT_WRITE_OUT
	writeToFile("fsrootbuf.bin", dumpdata.FSRootBuf, FSROOT_SIZE);
	writeToFile("fsrootfilebuf.bin", dumpdata.FSRootFileBuf, FSROOT_SIZE);
#endif

	xenon_nandfs_ExtractFsEntry();
	return 0;
}

bool xenon_nandfs_init(void)
{
	unsigned char mobi;
	unsigned int i, j, mmc_anchor_blk, prev_mobi_ver, prev_fsroot_ver, tmp_ver, lba, blk, size = 0, page_each;
	bool ret = false;
	unsigned char anchor_num = 0;
	char mobileName[] = {"MobileA"};
	METADATA* meta;

	if(nand.MMC)
	{
		unsigned char* blockbuf = (unsigned char *)vmalloc(nand.BlockSz * 2);
		unsigned char* fsrootbuf = (unsigned char *)vmalloc(nand.BlockSz);
		mmc_anchor_blk = nand.ConfigBlock - MMC_ANCHOR_BLOCKS;
		prev_mobi_ver = 0;
		
		for(blk=0;blk<nand.BlocksCount;blk++) // Create LBA map
			dumpdata.LBAMap[blk] = blk; // Hail to the phison, just this one time
		
		xenon_sfc_ReadMapData(blockbuf, (mmc_anchor_blk * nand.BlockSz), (nand.BlockSz * 2));
		
		for(i=0; i < MMC_ANCHOR_BLOCKS; i++)
		{
			tmp_ver = xenon_nandfs_GetMMCAnchorVer(&blockbuf[i*nand.BlockSz]);
			if(tmp_ver >= prev_mobi_ver) 
			{
				prev_mobi_ver = tmp_ver;
				anchor_num = i;
			}
		}
		
		if(prev_mobi_ver == 0)
		{
			printk(KERN_INFO "MMC Anchor block wasn't found!");
			vfree(blockbuf);
			return false;
		}


		for(mobi = 0x30; mobi < 0x3F; mobi++)
		{
			blk = xenon_nandfs_GetMMCMobileBlock(&blockbuf[anchor_num*nand.BlockSz], mobi);
			size = xenon_nandfs_GetMMCMobileSize(&blockbuf[anchor_num*nand.BlockSz], mobi);

			if(blk == 0)
				continue;

			if(mobi == MOBILE_FSROOT)
			{
				printk(KERN_INFO "FSRoot found at block 0x%x (off: 0x%x), v %i, size %d (0x%x) bytes\n", blk, (blk*nand.BlockSz), prev_mobi_ver, nand.BlockSz, nand.BlockSz);
				xenon_sfc_ReadMapData(fsrootbuf, blk*nand.BlockSz, nand.BlockSz);
				xenon_nandfs_SplitFsRootBuf(fsrootbuf);
				dumpdata.FSRootBlock = blk;
				dumpdata.FSRootVer = prev_mobi_ver; // anchor version
				ret  = true;
			}
			else
			{
				mobileName[6] = mobi+0x11;
				printk(KERN_INFO "%s found at block 0x%x (off: 0x%x), v %i, size %d (0x%x) bytes\n", mobileName, blk, (blk*nand.BlockSz), prev_mobi_ver, size*nand.BlockSz, size*nand.BlockSz);
				dumpdata.MobileBlock[mobi-MOBILE_BASE] = blk;
				dumpdata.MobileSize[mobi-MOBILE_BASE] = size * nand.BlockSz;
				dumpdata.MobileVer[mobi-MOBILE_BASE] = prev_mobi_ver; // anchor version
			}
		}
		vfree(blockbuf);
		vfree(fsrootbuf);
	}
	else
	{
		unsigned char* userbuf = (unsigned char *)vmalloc(nand.BlockSz);
		unsigned char* sparebuf = (unsigned char *)vmalloc(nand.MetaSz*nand.PagesInBlock);
		
		for(blk=0; blk < nand.BlocksCount; blk++)
		{
			xenon_sfc_ReadBlockSeparate(userbuf, sparebuf, blk);
			meta = (METADATA*)sparebuf;

			lba = xenon_nandfs_GetLBA(meta);
			dumpdata.LBAMap[blk] = lba; // Create LBA map
			
			for(mobi = (nand.isBB ? BB_MOBILE_FSROOT:MOBILE_FSROOT); mobi < MOBILE_END; mobi++)
			{
				if(xenon_nandfs_GetBlockType(meta) == mobi)
					tmp_ver = xenon_nandfs_GetFsSequence(meta);
				else
					continue;
				
				prev_mobi_ver = dumpdata.Mobile->Version; // get current version
				prev_fsroot_ver = dumpdata.FSRootVer; // get current version
				
				if(mobi == (nand.isBB ? BB_MOBILE_FSROOT:MOBILE_FSROOT)) // fs root
				{
					if(tmp_ver >= prev_fsroot_ver) 
					{
						dumpdata.FSRootVer = tmp_ver; // assign new version number
						dumpdata.FSRootBlock = blk;
					}	
					else
						continue;
					
					printk(KERN_INFO "FSRoot found at block 0x%x (off: 0x%x), v %i, size %d (0x%x) bytes\n", blk, (blk*nand.BlockSzPhys), dumpdata.FSRootVer, nand.BlockSz, nand.BlockSz);
					xenon_nandfs_SplitFsRootBuf(userbuf);
					ret = true;
				}
				else // MobileB - MobileH
				{	
					if(tmp_ver >= prev_mobi_ver)
					{
						dumpdata.MobileVer[mobi-MOBILE_BASE] = tmp_ver; // assign new version number
						dumpdata.MobileSize[mobi-MOBILE_BASE] = size;
						dumpdata.MobileBlock[mobi-MOBILE_BASE] = blk;
					}
					else
						continue;
						
					page_each = nand.PagesInBlock - xenon_nandfs_GetFsFreepages(meta);
					//printf("pageEach: %x\n", pageEach);
					// find the most recent instance in the block and dump it
					j = 0;
					for(i=0; i < nand.PagesInBlock; i += page_each)
					{
						meta = (METADATA*)&sparebuf[nand.MetaSz*i];
						//printf("i: %d type: %x\n", i, meta->FsBlockType);
						if(xenon_nandfs_GetBlockType(meta) == mobi)
							j = i;
						if(xenon_nandfs_GetBlockType(meta) == 0x3f)
							i = nand.PagesInBlock;
					}
				
					meta = (METADATA*)&sparebuf[j*nand.MetaSz];
					size = xenon_nandfs_GetFsSize(meta);
					
					mobileName[6] = mobi+0x11;
					printk(KERN_INFO "%s found at block 0x%x (off: 0x%x), page %d, v %i, size %d (0x%x) bytes\n", mobileName, blk, (blk*nand.BlockSzPhys), j, tmp_ver, size, size);
				}
			}
		}
		vfree(userbuf);
		vfree(sparebuf);
	}
	return ret;
}

bool xenon_nandfs_init_one(void)
{
	xenon_sfc_GetNandStruct(&nand);
	xenon_nandfs_init();
#ifdef DEBUG
	xenon_nandfs_bb_test();
#endif
	return true;
}
