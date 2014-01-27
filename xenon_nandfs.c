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
#define DEBUG

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

#include "xenon_sfc.h"
#include "xenon_nandfs.h"

static xenon_nand nand = {0};
static DUMPDATA dumpdata = {0};

#ifdef DEBUG

	u8 fixed_type;
	FILE * pFile;
	
	static inline u16 __builtin_bswap16(u16 a)
	{
	  return (a<<8)|(a>>8);
	}
	
	int xenon_sfc_readblock_separate(u8* user, u8* spare, u32 block)
	{
		u16 i;
		u8* buf = malloc(nand.block_sz_phys);
		u32 addr = block * nand.block_sz_phys;
		//printf("Reading block %04x from addr %08x\n", block, addr);
		fseek(pFile, addr, SEEK_SET);
		fread(buf ,nand.block_sz_phys, 1, pFile);
		for(i=0;i<nand.pages_in_block; i++){
			memcpy(&user[i*nand.page_sz], &buf[i*nand.page_sz_phys], nand.page_sz);
			memcpy(&spare[i*nand.meta_sz], &buf[(i*nand.page_sz_phys)+nand.page_sz], nand.meta_sz);
		}
	}
	void xenon_sfc_readmapdata(u8* buf, u32 startaddr, u32 total_len)
	{
		fseek(pFile, startaddr, SEEK_SET);
		fread(buf, total_len, 1, pFile);
	}
	void xenon_sfc_getnandstruct(xenon_nand* xe_nand)
	{	
		xe_nand->pages_in_block = 32;
		xe_nand->meta_sz = 0x10;
		xe_nand->page_sz = 0x200;
		xe_nand->page_sz_phys = nand.page_sz + nand.meta_sz;
		
		xe_nand->meta_type = META_TYPE_SM;
		xe_nand->block_sz = 0x4000;
		xe_nand->block_sz_phys = 0x4200;
		xe_nand->is_bb = false;
		xe_nand->is_bb_cont = false;
		
		switch(fixed_type)
		{
			case META_TYPE_SM:
					xe_nand->size_dump = 0x1080000;
					xe_nand->size_data = 0x1000000;
					xe_nand->size_spare = 0x80000;
					xe_nand->size_usable_fs = 0x3E0;
				break;
			case META_TYPE_BOS:
					xe_nand->meta_type = META_TYPE_BOS;
					xe_nand->is_bb_cont = true;
					xe_nand->size_dump = 0x1080000;
					xe_nand->size_data = 0x1000000;
					xe_nand->size_spare = 0x80000;
					xe_nand->size_usable_fs = 0x3E0;
				break;
			case META_TYPE_BG:
					xe_nand->meta_type = META_TYPE_BG;
					xe_nand->is_bb_cont = true;
					xe_nand->is_bb = true;
					xe_nand->size_dump = 0x4200000;
					xe_nand->block_sz_phys = 0x21000;
					xe_nand->size_data = 0x4000000;
					xe_nand->size_spare = 0x200000;
					xe_nand->pages_in_block = 256;
					xe_nand->block_sz = 0x20000;
					xe_nand->size_usable_fs = 0x1E0;
				break;
			case META_TYPE_NONE:
				xe_nand->mmc = true;
				xe_nand->meta_type = META_TYPE_NONE;
				xe_nand->block_sz = 0x4000;
				xe_nand->block_sz_phys = 0x20000;
				xe_nand->meta_sz = 0;
			
				xe_nand->size_dump = 0x3000000;
				xe_nand->size_data = 0x3000000;
				xe_nand->size_spare = 0;
				xe_nand->size_usable_fs = 0xC00; // (nand.size_dump/nand.block_sz)
				break;
				
		}
		xe_nand->config_block = xe_nand->size_usable_fs - CONFIG_BLOCKS;
		xe_nand->blocks_count = xe_nand->size_dump / xe_nand->block_sz_phys;
		xe_nand->pages_count = xe_nand->blocks_count * xe_nand->pages_in_block;
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

#endif

void xenon_nandfs_calcecc(unsigned int *data, unsigned char* edc) {
	u32 i=0, val=0;
	u32 v=0;

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

u16 xenon_nandfs_get_lba(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return (((meta->sm.BlockID0&0xF)<<8)+(meta->sm.BlockID1));
		case META_TYPE_BOS:
			return (((meta->bos.BlockID0<<8)&0xF)+(meta->bos.BlockID1&0xFF));
		case META_TYPE_BG:
			return (((meta->bg.BlockID0&0xF)<<8)+(meta->bg.BlockID1&0xFF));
	}
}

u8 xenon_nandfs_get_blocktype(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return (meta->sm.FsBlockType&0x3F);
		case META_TYPE_BOS:
			return (meta->bos.FsBlockType&0x3F);
		case META_TYPE_BG:
			return (meta->bg.FsBlockType&0x3F);
	}
}

u8 xenon_nandfs_get_badblock_mark(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return meta->sm.BadBlock;
		case META_TYPE_BOS:
			return meta->bos.BadBlock;
		case META_TYPE_BG:
			return meta->bg.BadBlock;
	}
}

u32 xenon_nandfs_get_fssize(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return ((meta->sm.FsSize0<<8)+meta->sm.FsSize1);
		case META_TYPE_BOS:
			return (((meta->bos.FsSize0<<8)&0xFF)+(meta->bos.FsSize1&0xFF));
		case META_TYPE_BG:
			return (((meta->bg.FsSize0&0xFF)<<8)+(meta->bg.FsSize1&0xFF));
	}
}

u32 xenon_nandfs_get_fsfreepages(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return meta->sm.FsPageCount;
		case META_TYPE_BOS:
			return meta->bos.FsPageCount;
		case META_TYPE_BG:
			return (meta->bg.FsPageCount * 4);
	}
}

u32 xenon_nandfs_get_fssequence(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return (meta->sm.FsSequence0+(meta->sm.FsSequence1<<8)+(meta->sm.FsSequence2<<16));
		case META_TYPE_BOS:
			return (meta->bos.FsSequence0+(meta->bos.FsSequence1<<8)+(meta->bos.FsSequence2<<16));
		case META_TYPE_BG:
			return (meta->bg.FsSequence0+(meta->bg.FsSequence1<<8)+(meta->bg.FsSequence2<<16));
	}
}

bool xenon_nandfs_check_mmc_anchor_sha(unsigned char* buf)
{
	//unsigned char* data = buf;
	//CryptSha(&data[MMC_ANCHOR_HASH_LEN], (0x200-MMC_ANCHOR_HASH_LEN), NULL, 0, NULL, 0, sha, MMC_ANCHOR_HASH_LEN);
	return 0;
}

u16 xenon_nandfs_get_mmc_anchor_ver(u8* buf)
{
	u8* data = buf;
	u16 tmp = (data[MMC_ANCHOR_VERSION_POS]<<8|data[MMC_ANCHOR_VERSION_POS+1]);

	return tmp;
}

u16 xenon_nandfs_get_mmc_mobileblock(u8* buf, u8 mobi)
{
	u8* data = buf;
	u8 mob = mobi - MOBILE_BASE;
	u8 offset = MMC_ANCHOR_MOBI_START+(mob*MMC_ANCHOR_MOBI_SIZE);
	u16 tmp = data[offset]<<8|data[offset+1];
	
	return tmp;
}

u16 xenon_nandfs_get_mmc_mobilesize(u8* buf, u8 mobi)
{
	u8* data = buf;
	u8 mob = mobi - MOBILE_BASE;
	u8 offset = MMC_ANCHOR_MOBI_START+(mob*MMC_ANCHOR_MOBI_SIZE)+0x2;
	u16 tmp = data[offset]<<8|data[offset+1];
	
	return __builtin_bswap16(tmp);
}

bool xenon_nandfs_check_ecc(PAGEDATA* pdata)
{
	unsigned char ecd[4];
	xenon_nandfs_calcecc((unsigned int*)pdata->user, ecd);
	if ((ecd[0] == pdata->meta.sm.ECC0) && (ecd[1] == pdata->meta.sm.ECC1) && (ecd[2] == pdata->meta.sm.ECC2) && (ecd[3] == pdata->meta.sm.ECC3))
		return 0;
	return 1;
}

u32 xenon_nandfs_extract_fsentry(FS_ENT *entry)
{
	u16 block = __builtin_bswap16(entry->startCluster);
	u32 size = __builtin_bswap32(entry->clusterSz);
	
	if(nand.mmc)
	{
		u8* blockbuf = (u8 *)vmalloc(nand.block_sz);
		xenon_sfc_readmapdata(blockbuf, (block * nand.block_sz), nand.block_sz);
		printf("Header: %c%c%c\n",blockbuf[0],blockbuf[1],blockbuf[2]);
		vfree(blockbuf);
	}
	else
	{
		u8* userbuf = (u8 *)vmalloc(nand.block_sz);
		u8* sparebuf = (u8 *)vmalloc(nand.meta_sz*nand.pages_in_block);
		xenon_sfc_readblock_separate(userbuf, sparebuf, block);
		printf("Header: %c%c%c\n",userbuf[0],userbuf[1],userbuf[2]);
		vfree(userbuf);
		vfree(sparebuf);
	}
}

int xenon_nandfs_parse_fsentries(u8* userbuf)
{
	u32 i, j, root_off, file_off, ttl_off;
	u8* data = userbuf;
	u8* fsrootbuf = (u8 *)vmalloc(FSROOT_SIZE);
	u8* rootbuf = (u8 *)vmalloc(FSROOT_SIZE);
	
	root_off = 0;
	file_off = 0;
	ttl_off = 0;
	for(i=0; i<16; i++) // copy alternating 512 bytes into each buf
	{
		for(j=0; j<512; j++)
		{
			rootbuf[root_off+j] = data[ttl_off+j];
			fsrootbuf[file_off+j] = data[ttl_off+j+512];
		}
		root_off += 512;
		file_off += 512;
		ttl_off  += 1024;
	}
					
	for(i=0; i<256; i++)
	{
		dumpdata.fs_ent= (FS_ENT*)&fsrootbuf[i*sizeof(FS_ENT)];
#ifdef DEBUG
		if(dumpdata.fs_ent->fileName[0] != 0)
		{
			printf("fileName: %s, block: %x, size: %x\n", dumpdata.fs_ent->fileName, __builtin_bswap16(dumpdata.fs_ent->startCluster), __builtin_bswap32(dumpdata.fs_ent->clusterSz));
			xenon_nandfs_extract_fsentry(dumpdata.fs_ent);
		}
#endif
		dumpdata.fs_ent++;
	}
	vfree(fsrootbuf);
	vfree(rootbuf);
	return 0;
}

bool xenon_nandfs_init(void)
{
	u8 mobi;
	u32 i, j, mmc_anchor_blk, prev_mobi_ver, prev_fsroot_ver, tmp_ver, lba, blk, size, page_each;
	bool ret = false;
	u8 anchor_num = 0;
	char mobileName[] = {"MobileA"};
	METADATA* meta;

	if(nand.mmc)
	{
		u8* blockbuf = (u8 *)vmalloc(nand.block_sz * 2);
		u8* fsrootbuf = (u8 *)vmalloc(nand.block_sz);
		mmc_anchor_blk = nand.config_block - MMC_ANCHOR_BLOCKS;
		prev_mobi_ver = 0;
		
		for(blk=0;blk<nand.blocks_count;blk++) // Create LBA map
			dumpdata.lba_map[blk] = blk; // Hail to the phison, just this one time
		
		xenon_sfc_readmapdata(blockbuf, (mmc_anchor_blk * nand.block_sz), (nand.block_sz * 2));
		
		for(i=0; i < MMC_ANCHOR_BLOCKS; i++)
		{
			tmp_ver = xenon_nandfs_get_mmc_anchor_ver(&blockbuf[i*nand.block_sz]);
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
			blk = xenon_nandfs_get_mmc_mobileblock(&blockbuf[anchor_num*nand.block_sz], mobi);
			size = xenon_nandfs_get_mmc_mobilesize(&blockbuf[anchor_num*nand.block_sz], mobi);

			if(blk == 0)
				continue;

			if(mobi == MOBILE_FSROOT)
			{
				printk(KERN_INFO "FSRoot found at block 0x%x, v %i, size %d (0x%x) bytes\n", blk, prev_mobi_ver, nand.block_sz, nand.block_sz);
				xenon_sfc_readmapdata(fsrootbuf, blk*nand.block_sz, nand.block_sz);
				xenon_nandfs_parse_fsentries(fsrootbuf);
				dumpdata.fsroot_block = blk;
				dumpdata.fsroot_v = prev_mobi_ver; // anchor version
				ret  = true;
			}
			else
			{
				mobileName[6] = mobi+0x11;
				printk(KERN_INFO "%s found at block 0x%x, v %i, size %d (0x%x) bytes\n", mobileName, blk, prev_mobi_ver, size*nand.block_sz, size*nand.block_sz);
				dumpdata.mobile_block[mobi-MOBILE_BASE] = blk;
				dumpdata.mobile_size[mobi-MOBILE_BASE] = size * nand.block_sz;
				dumpdata.mobile_ver[mobi-MOBILE_BASE] = prev_mobi_ver; // anchor version
			}
		}
		vfree(blockbuf);
		vfree(fsrootbuf);
	}
	else
	{
		u8* userbuf = (u8 *)vmalloc(nand.block_sz);
		u8* sparebuf = (u8 *)vmalloc(nand.meta_sz*nand.pages_in_block);
		
		for(blk=0; blk < nand.blocks_count; blk++)
		{
			xenon_sfc_readblock_separate(userbuf, sparebuf, blk);
			meta = (METADATA*)sparebuf;

			lba = xenon_nandfs_get_lba(meta);
			dumpdata.lba_map[blk] = lba; // Create LBA map
		
			for(mobi = (nand.is_bb ? BB_MOBILE_FSROOT:MOBILE_FSROOT); mobi < MOBILE_END; mobi++)
			{
				if(xenon_nandfs_get_blocktype(meta) == mobi)
					tmp_ver = xenon_nandfs_get_fssequence(meta);
				else
					continue;
				
				prev_mobi_ver = dumpdata.mobile_ver[mobi-MOBILE_BASE]; // get current version
				prev_fsroot_ver = dumpdata.fsroot_v; // get current version
				
				if(tmp_ver >= 0)
				{
					if(mobi == (nand.is_bb ? BB_MOBILE_FSROOT:MOBILE_FSROOT)) // fs root
					{
						if(tmp_ver >= prev_fsroot_ver) 
						{
							dumpdata.fsroot_v = tmp_ver; // assign new version number
							dumpdata.fsroot_block = blk;
						}	
						else
							continue;
						
						printk(KERN_INFO "FSRoot found at block 0x%x, v %i, size %d (0x%x) bytes\n", blk, dumpdata.fsroot_v, nand.block_sz, nand.block_sz);
						xenon_nandfs_parse_fsentries(userbuf);
						ret = true;
					}
					else // MobileB - MobileH
					{	
						if(tmp_ver >= prev_mobi_ver)
						{
							dumpdata.mobile_ver[mobi-MOBILE_BASE] = tmp_ver; // assign new version number
							dumpdata.mobile_size[mobi-MOBILE_BASE] = size;
							dumpdata.mobile_block[mobi-MOBILE_BASE] = blk;
						}
						else
							continue;
							
						page_each = nand.pages_in_block - xenon_nandfs_get_fsfreepages(meta);
						//printf("pageEach: %x\n", pageEach);
						// find the most recent instance in the block and dump it
						j = 0;
						for(i=0; i < nand.pages_in_block; i += page_each)
						{
							meta = (METADATA*)&sparebuf[nand.meta_sz*i];
							//printf("i: %d type: %x\n", i, meta->FsBlockType);
							if(xenon_nandfs_get_blocktype(meta) == mobi)
								j = i;
							if(xenon_nandfs_get_blocktype(meta) == 0x3f)
								i = nand.pages_in_block;
						}
					
						meta = (METADATA*)&sparebuf[j*nand.meta_sz];
						size = xenon_nandfs_get_fssize(meta);
						
						mobileName[6] = mobi+0x11;
						printk(KERN_INFO "%s found at block 0x%x, page %d, v %i, size %d (0x%x) bytes\n", mobileName, blk, j, tmp_ver, size, size);
					}
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
	xenon_sfc_getnandstruct(&nand);
	xenon_nandfs_init();
	return true;
}
