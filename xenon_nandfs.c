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
		unsigned char* buf = (unsigned char*)vmalloc(nand.BlockSzPhys);
		unsigned int addr = block * nand.BlockSzPhys;
		//printf("Reading block %04x from addr %08x\n", block, addr);
		fseek(pFile, addr, SEEK_SET);
		fread(buf ,nand.BlockSzPhys, 1, pFile);
		for(i=0;i<nand.PagesInBlock; i++){
			memcpy(&user[i*nand.PageSz], &buf[i*nand.PageSzPhys], nand.PageSz);
			memcpy(&spare[i*nand.MetaSz], &buf[(i*nand.PageSzPhys)+nand.PageSz], nand.MetaSz);
		}
		vfree(buf);
	}
	
	int xenon_sfc_ReadSmallBlockSeparate(unsigned char* user, unsigned char* spare, unsigned int block)
	{
		unsigned short i;
		unsigned char* buf = (unsigned char*)vmalloc(0x4200);
		unsigned int addr = block * 0x4200;
		//printf("Reading block %04x from addr %08x\n", block, addr);
		fseek(pFile, addr, SEEK_SET);
		fread(buf ,0x4200, 1, pFile);
		for(i=0;i<32; i++){
			memcpy(&user[i*0x200], &buf[i*0x210], 0x200);
			memcpy(&spare[i*0x10], &buf[(i*0x210)+0x200], 0x10);
		}
		vfree(buf);
	}

	int xenon_sfc_ReadBlockUser(unsigned char* buf, unsigned int block)
	{
		unsigned char* tmp = (unsigned char *)vmalloc(nand.MetaSz*nand.PagesInBlock);
		xenon_sfc_ReadBlockSeparate(buf, tmp, block);
		vfree(tmp);
		return 0;
	}
	
	int xenon_sfc_ReadBlockSpare(unsigned char* buf, unsigned int block)
	{
		unsigned char* tmp = (unsigned char *)vmalloc(nand.BlockSz);
		xenon_sfc_ReadBlockSeparate(tmp, buf, block);
		vfree(tmp);
		return 0;
	}
	
	int xenon_sfc_ReadSmallBlockUser(unsigned char* buf, unsigned int block)
	{
		unsigned char PagesInBlock = 32;
		unsigned char MetaSz = 0x10;
		unsigned char* tmp = (unsigned char *)vmalloc(MetaSz*PagesInBlock);
		xenon_sfc_ReadSmallBlockSeparate(buf, tmp, block);
		vfree(tmp);
		return 0;
	}
	
	int xenon_sfc_ReadSmallBlockSpare(unsigned char* buf, unsigned int block)
	{
		unsigned short BlockSz = 0x4000;
		unsigned char* tmp = (unsigned char *)vmalloc(BlockSz);
		xenon_sfc_ReadSmallBlockSeparate(tmp, buf, block);
		vfree(tmp);
		return 0;
	}

	void xenon_sfc_ReadMapData(unsigned char* buf, unsigned int startaddr, unsigned int total_len)
	{
		fseek(pFile, startaddr, SEEK_SET);
		fread(buf, total_len, 1, pFile);
	}
	
	bool xenon_sfc_GetNandStruct(xenon_nand* xe_nand)
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
					xe_nand->init = true;
					xe_nand->SizeDump = 0x1080000;
					xe_nand->SizeData = 0x1000000;
					xe_nand->SizeSpare = 0x80000;
					xe_nand->SizeUsableFs = 0x3E0;
					break;
			case META_TYPE_BOS:
					xe_nand->init = true;
					xe_nand->MetaType = META_TYPE_BOS;
					xe_nand->isBBCont = true;
					xe_nand->SizeDump = 0x1080000;
					xe_nand->SizeData = 0x1000000;
					xe_nand->SizeSpare = 0x80000;
					xe_nand->SizeUsableFs = 0x3E0;
					break;
			case META_TYPE_BG:
					xe_nand->init = true;
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
					xe_nand->init = true;
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
		
#if 1
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
		return xe_nand->init;
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
	unsigned int fsStartBlock = dumpdata.FSStartBlock<<3;
	unsigned int offsetUser = block*nand.BlockSz;
	unsigned char *userbuf = (unsigned char *)vmalloc(nand.BlockSz);
	unsigned char* sparebuf = (unsigned char *)vmalloc(nand.MetaSz*nand.PagesInBlock);


	if(nand.MMC)
		xenon_sfc_ReadMapData(userbuf, offsetUser, nand.BlockSz); 
	else
		xenon_sfc_ReadSmallBlockSeparate(userbuf, sparebuf, fsStartBlock + block);
	
	if(fileExists(filename))
		outfile = fopen(filename, "ab+");
	else
		outfile = fopen(filename, "wb");
	fwrite(userbuf, len, 1, outfile);
	fclose(outfile);
	
	vfree(userbuf);
	vfree(sparebuf);
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
			ret =  (((meta->bos.BlockID0&0xF)<<8)+(meta->bos.BlockID1&0xFF));
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
	if ((ecd[0] == pdata->Meta.sm.ECC0) &&
		(ecd[1] == pdata->Meta.sm.ECC1) &&
		(ecd[2] == pdata->Meta.sm.ECC2) &&
		(ecd[3] == pdata->Meta.sm.ECC3))
		return 0;
	return 1;
}

int xenon_nandfs_ExtractFsEntry(void)
{
	unsigned int i, k;
	unsigned int fsBlock, realBlock;
	unsigned int fsFileSize;
	unsigned int fsStartBlock = dumpdata.FSStartBlock<<3; // Convert to Small Block
//	FS_TIME_STAMP timeSt;
	
	dumpdata.pFSRootBufShort = (unsigned short*)dumpdata.FSRootBuf;
	
	for(i=0; i<256; i++)
	{
		dumpdata.FsEnt[i] = (FS_ENT*)&dumpdata.FSRootFileBuf[i*sizeof(FS_ENT)];
		if(dumpdata.FsEnt[i]->FileName[0] != 0)
		{
			printk(KERN_INFO "file: %s ", dumpdata.FsEnt[i]->FileName);
			for(k=0; k< (22-strlen(dumpdata.FsEnt[i]->FileName)); k++)
			{
				printk(KERN_INFO " ");
			}
			
			fsBlock = __builtin_bswap16(dumpdata.FsEnt[i]->StartCluster);
			fsFileSize = __builtin_bswap32(dumpdata.FsEnt[i]->ClusterSz);
			
			printk(KERN_INFO "start: %04x size: %08x stamp: %08x\n", fsBlock, fsFileSize, (unsigned int)__builtin_bswap32(dumpdata.FsEnt[i]->TypeTime));

			// extract the file
			if(dumpdata.FsEnt[i]->FileName[0] != 0x5) // file is erased but still in the record
			{
				realBlock = fsBlock;
				if(nand.isBB)
				{
					realBlock = ((dumpdata.LBAMap[fsBlock]<<3)-fsStartBlock);
				}
				
				while(fsFileSize > 0x4000)
				{
#ifdef DEBUG
					printk(KERN_INFO "%04x:%04x, ", fsBlock, realBlock);
#endif
#ifdef WRITE_OUT
					appendBlockToFile(dumpdata.FsEnt[i]->FileName, realBlock, 0x4000);
#endif
					fsFileSize = fsFileSize-0x4000;
					fsBlock = __builtin_bswap16(dumpdata.pFSRootBufShort[fsBlock]); // gets next block
					realBlock = dumpdata.LBAMap[fsBlock];
					if(nand.isBB)
					{
						realBlock = (realBlock<<3); // to SmallBlock
						realBlock -= fsStartBlock; // relative Adress
						realBlock += (fsBlock % 8); // smallBlock inside bigBlock 
					}
				}
				if((fsFileSize > 0)&&(fsBlock<0x1FFE))
				{
#ifdef DEBUG
					printk(KERN_INFO "%04x:%04x, ", fsBlock, realBlock);
#endif
#ifdef WRITE_OUT
					appendBlockToFile(dumpdata.FsEnt[i]->FileName, realBlock, fsFileSize);
#endif
				}
				else
					printk(KERN_INFO "** Couldn't write file tail! %04x:%04x, ", fsBlock, realBlock);
			}
			else
				printk(KERN_INFO "   erased still has entry???");		
			printk(KERN_INFO "\n\n");
		}
	} 
	return 0;
}

int xenon_nandfs_ParseLBA(void)
{
	int block, spare;
	unsigned char* userbuf = (unsigned char *)vmalloc(nand.BlockSz);
	unsigned char* sparebuf = (unsigned char *)vmalloc(nand.MetaSz*nand.PagesInBlock);
	int FsStart = dumpdata.FSStartBlock;
	int FsSize = dumpdata.FSSize;
	unsigned short lba, lba_cnt=0;
	METADATA *meta;
		
	if(nand.MMC)
	{
		for(block=0;block<nand.BlocksCount;block++)
		{
			dumpdata.LBAMap[lba_cnt] = block; // Hail to the phison, just this one time
			lba_cnt++;
		}
	}
	else
	{
		for(block=FsStart;block<(FsStart+FsSize);block++)
		{
			xenon_sfc_ReadBlockSeparate(userbuf, sparebuf, block);
			if(nand.isBB)
			{
				for(spare=0;spare<(nand.MetaSz*nand.PagesInBlock);spare+=(nand.MetaSz*32))
				{
					meta = (METADATA*)&sparebuf[spare]; // 8 SmBlocks inside BgBlock for LBA
					lba = xenon_nandfs_GetLBA(meta);
					dumpdata.LBAMap[lba_cnt] = lba;
					lba_cnt++;
				}	
			}
			else
			{
				meta = (METADATA*)sparebuf;
				lba = xenon_nandfs_GetLBA(meta);
				dumpdata.LBAMap[lba_cnt] = lba;
				lba_cnt++;
			}
		}
	}
	printk(KERN_INFO "Read 0x%x LBA entries\n",lba_cnt);
	vfree(userbuf);
	vfree(sparebuf);
	return 0;
}

int xenon_nandfs_SplitFsRootBuf()
{
	int block = dumpdata.FSRootBlock;
	unsigned int i, j, root_off, file_off, ttl_off;
	unsigned char* data = (unsigned char *)vmalloc(nand.BlockSz);
	
	xenon_sfc_ReadBlockUser(data, block);
	
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
	vfree(data);
	return 0;
}

bool xenon_nandfs_init(void)
{
	unsigned char mobi, fsroot_ident;
	unsigned int i, j, mmc_anchor_blk, prev_mobi_ver, prev_fsroot_ver, tmp_ver, blk, size = 0, page_each;
	bool ret = false;
	unsigned char anchor_num = 0;
	char mobileName[] = {"MobileA"};
	METADATA* meta;

	if(nand.MMC)
	{
		unsigned char* blockbuf = (unsigned char *)vmalloc(nand.BlockSz * 2);
		mmc_anchor_blk = nand.ConfigBlock - MMC_ANCHOR_BLOCKS;
		prev_mobi_ver = 0;
		
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
				dumpdata.FSRootBlock = blk;
				dumpdata.FSRootVer = prev_mobi_ver; // anchor version
				ret  = true;
			}
			else
			{
				mobileName[6] = mobi+0x11;
				printk(KERN_INFO "%s found at block 0x%x (off: 0x%x), v %i, size %d (0x%x) bytes\n", mobileName, blk, (blk*nand.BlockSz), prev_mobi_ver, size*nand.BlockSz, size*nand.BlockSz);
				dumpdata.Mobile[mobi-MOBILE_BASE].Version = prev_mobi_ver;
				dumpdata.Mobile[mobi-MOBILE_BASE].Block = blk;
				dumpdata.Mobile[mobi-MOBILE_BASE].Size = size * nand.BlockSz;
			}
		}
		vfree(blockbuf);
	}
	else
	{
		unsigned char* userbuf = (unsigned char *)vmalloc(nand.BlockSz);
		unsigned char* sparebuf = (unsigned char *)vmalloc(nand.MetaSz*nand.PagesInBlock);
		
		if(nand.isBB) // Set FSroot Identifier, depending on nandtype
			fsroot_ident = BB_MOBILE_FSROOT;
		else
			fsroot_ident = MOBILE_FSROOT;
		
		for(blk=0; blk < nand.BlocksCount; blk++)
		{
			xenon_sfc_ReadBlockSeparate(userbuf, sparebuf, blk);
			meta = (METADATA*)sparebuf;
			
			mobi = xenon_nandfs_GetBlockType(meta);
			tmp_ver = xenon_nandfs_GetFsSequence(meta);

			if(mobi == fsroot_ident) // fs root
			{
				prev_fsroot_ver = dumpdata.FSRootVer; // get current version
				if(tmp_ver >= prev_fsroot_ver) 
				{
					dumpdata.FSRootVer = tmp_ver; // assign new version number
					dumpdata.FSRootBlock = blk;
				}	
				else
					continue;
					
				printk(KERN_INFO "FSRoot found at block 0x%x (off: 0x%x), v %i, size %d (0x%x) bytes\n", blk, (blk*nand.BlockSzPhys), dumpdata.FSRootVer, nand.BlockSz, nand.BlockSz);
				
				if(nand.isBB)
				{
					dumpdata.MUStart = meta->bg.FsSize1;
					dumpdata.FSSize = (meta->bg.FsSize0<<2);
					dumpdata.FSStartBlock = nand.SizeUsableFs - meta->bg.FsPageCount - dumpdata.FSSize;
				}
				else
				{
					dumpdata.MUStart = 0;
					dumpdata.FSSize = nand.BlocksCount;
					dumpdata.FSStartBlock = 0;
				}
				
				ret = true;
			}
			else if((mobi >= MOBILE_BASE) && (mobi < MOBILE_END)) //Mobile*.dat
			{	
				prev_mobi_ver = dumpdata.Mobile[mobi-MOBILE_BASE].Version;
				if(tmp_ver >= prev_mobi_ver)
					dumpdata.Mobile[mobi-MOBILE_BASE].Version = tmp_ver;
				else
					continue;

				page_each = nand.PagesInBlock - xenon_nandfs_GetFsFreepages(meta);
				//printk(KERN_INFO "pageEach: %x\n", pageEach);
				// find the most recent instance in the block and dump it
				j = 0;
				for(i=0; i < nand.PagesInBlock; i += page_each)
				{
					meta = (METADATA*)&sparebuf[nand.MetaSz*i];
					//printk(KERN_INFO "i: %d type: %x\n", i, meta->FsBlockType);
					if(xenon_nandfs_GetBlockType(meta) == (mobi))
						j = i;
					if(xenon_nandfs_GetBlockType(meta) == 0x3F)
						i = nand.PagesInBlock;
				}
			
				meta = (METADATA*)&sparebuf[j*nand.MetaSz];
				size = xenon_nandfs_GetFsSize(meta);
				
				dumpdata.Mobile[mobi-MOBILE_BASE].Size = size;
				dumpdata.Mobile[mobi-MOBILE_BASE].Block = blk;
				
				mobileName[6] = mobi+0x31;
				printk(KERN_INFO "%s found at block 0x%x (off: 0x%x), page %d, v %i, size %d (0x%x) bytes\n", mobileName, blk, (blk*nand.BlockSzPhys), j, tmp_ver, size, size);
			}
		}
		vfree(userbuf);
		vfree(sparebuf);
	}
	return ret;
}

bool xenon_nandfs_init_one(void)
{
	int ret;
	
	ret = xenon_sfc_GetNandStruct(&nand);
	if(!ret)
	{
		printk(KERN_INFO "Failed to get enumerated NAND information\n");
		goto err_out;
	}
	
	ret = xenon_nandfs_init();
	if(!ret)
	{
		printk(KERN_INFO "FSRoot wasn't found\n");
		goto err_out;
	}
	
	xenon_nandfs_ParseLBA();
	xenon_nandfs_SplitFsRootBuf();
	xenon_nandfs_ExtractFsEntry();
	return true;
	
	err_out:
		memset (&nand, 0, sizeof(DUMPDATA));
		memset (&dumpdata, 0, sizeof(xenon_nand));
		return false;
}
