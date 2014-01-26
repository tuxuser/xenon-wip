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


#include <linux/vmalloc.h>
#include "xenon_sfc.h"
#include "xenon_nandfs.h"

#define DEBUG_OUT

static xenon_nand nand;
static DUMPDATA dumpdata;

void xenon_nandfs_calcecc(unsigned int *data, unsigned char* edc) {
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

int xenon_nandfs_get_lba(METADATA* meta)
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
	return INVALID;
}

int xenon_nandfs_get_blocktype(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return meta->sm.FsBlockType;
		case META_TYPE_BOS:
			return meta->bos.FsBlockType;
		case META_TYPE_BG:
			return meta->bg.FsBlockType;
	}
	return INVALID;
}

int xenon_nandfs_get_badblock_mark(METADATA* meta)
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
	return INVALID;
}

int xenon_nandfs_get_fssize(METADATA* meta)
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
	return INVALID;
}

int xenon_nandfs_get_fsfreepages(METADATA* meta)
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
	return INVALID;
}

int xenon_nandfs_get_fssequence(METADATA* meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return meta->sm.FsSequence0+(meta->sm.FsSequence1<<8)+(meta->sm.FsSequence2<<16);
		case META_TYPE_BOS:
			return meta->bos.FsSequence0+(meta->bos.FsSequence1<<8)+(meta->bos.FsSequence2<<16);
		case META_TYPE_BG:
			return meta->bg.FsSequence0+(meta->bg.FsSequence1<<8)+(meta->bg.FsSequence2<<16);
	}
	return INVALID;
}

unsigned int xenon_nandfs_check_mmc_anchor_sha(unsigned char* buf)
{
	//unsigned char* data = buf;
	//CryptSha(&data[MMC_ANCHOR_HASH_LEN], (0x200-MMC_ANCHOR_HASH_LEN), NULL, 0, NULL, 0, sha, MMC_ANCHOR_HASH_LEN);
	return 0;
}

unsigned int xenon_nandfs_get_mmc_anchor_ver(unsigned char* buf)
{
	unsigned char* data = buf;
	return __builtin_bswap32(data[MMC_ANCHOR_VERSION_POS]);
}

unsigned int xenon_nandfs_get_mmc_mobileblock(unsigned char* buf, int mobile_num)
{
	unsigned char* data = buf;
	int mob = mobile_num - MOBILE_BASE;
	int offset = MMC_ANCHOR_MOBI_START+(mob*MMC_ANCHOR_MOBI_SIZE);
	
	return __builtin_bswap16(data[offset]);
}

unsigned int xenon_nandfs_get_mmc_mobilesize(unsigned char* buf, int mobile_num)
{
	unsigned char* data = buf;
	int mob = mobile_num - MOBILE_BASE;
	int offset = MMC_ANCHOR_MOBI_START+(mob*MMC_ANCHOR_MOBI_SIZE)+0x4;
	
	return __builtin_bswap16(data[offset]);
}

int xenon_nandfs_check_ecc(PAGEDATA* pdata)
{
	unsigned char ecd[4];
	xenon_nandfs_calcecc((unsigned int*)pdata->user, ecd);
	if ((ecd[0] == pdata->meta.sm.ECC0) && (ecd[1] == pdata->meta.sm.ECC1) && (ecd[2] == pdata->meta.sm.ECC2) && (ecd[3] == pdata->meta.sm.ECC3))
		return 0;
	return 1;
}

int xenon_nandfs_find_mobile(METADATA* metadata, int mobi)
{
	unsigned int ver = 0;
	METADATA* meta = metadata;

	if((xenon_nandfs_get_blocktype(meta)&0x3F) == mobi)
	{
		ver = xenon_nandfs_get_fssequence(meta);
		return ver;
	}
	return -1;
}

int xenon_nandfs_init(void)
{
	unsigned short mobi;
	int i, j, tmp, lba, blk, mobi_blk, mobi_size, mobi_ver, page_each, mmc_anchor_blk;
	int root_off, file_off, ttl_off;
	int ret = 0;
	int anchor_num = -1;
	char mobileName[] = {"MobileA"};
	METADATA* meta;

	if(nand.mmc)
	{
		unsigned char* blockbuf = (unsigned char *)vmalloc(nand.block_sz * 2);
		mmc_anchor_blk = nand.config_block - MMC_ANCHOR_BLOCKS;
		mobi_ver = 0;
		
		for(blk=0;blk<nand.blocks_count;blk++) // Create LBA map
			dumpdata.lba_map[blk] = blk; // Hail to the phison, just this one time
		
		xenon_sfc_readmapdata(blockbuf, (mmc_anchor_blk * nand.block_sz), (nand.block_sz * 2));
		
		for(i=0; i < MMC_ANCHOR_BLOCKS; i++)
		{
			tmp = xenon_nandfs_get_mmc_anchor_ver(&blockbuf[i*nand.block_sz]);
			if(tmp >= mobi_ver) 
			{
				mobi_ver = tmp;
				anchor_num = i;
			}
		}
		
		if(anchor_num == -1)
		{
			printk(KERN_INFO "MMC Anchor block wasn't found!");
			vfree(blockbuf);
			return 0;
		}

		for(mobi = 0x30; mobi < 0x3F; mobi++)
		{
			mobi_blk = xenon_nandfs_get_mmc_mobileblock(&blockbuf[anchor_num*nand.block_sz], mobi);
			mobi_size = xenon_nandfs_get_mmc_mobilesize(&blockbuf[anchor_num*nand.block_sz], mobi);
			mobi_size *= nand.block_sz;
			mobi_blk *= nand.block_sz;
			if(mobi_blk == 0)
				continue;

			if(mobi == MOBILE_FSROOT)
			{
				ret  = 1;
				printk(KERN_INFO "FSRoot found at block 0x%x, size %d (0x%x) bytes\n", mobi_blk, nand.block_sz, nand.block_sz);
			}
			else
			{
				mobileName[6] = mobi+0x11;
				printk(KERN_INFO "%s found at block 0x%x, size %d (0x%x) bytes\n", mobileName, mobi_blk, mobi_size, mobi_size);	
			}
			dumpdata.mobile_blocks[mobi-MOBILE_BASE] = mobi_blk;
			dumpdata.mobile_size[mobi-MOBILE_BASE] = mobi_size;
			dumpdata.mobile_ver[mobi-MOBILE_BASE] = mobi_ver; // actually the anchor version	
		}
		vfree(blockbuf);
	}
	else
	{
		unsigned char* userbuf = (unsigned char *)vmalloc(nand.block_sz);
		unsigned char* sparebuf = (unsigned char *)vmalloc(nand.meta_sz*nand.pages_in_block);
		
		for(blk=0; blk < nand.blocks_count; blk++)
		{	
			xenon_sfc_readblock_separate(userbuf, sparebuf, blk);
			meta = (METADATA*)sparebuf;
		
			lba = xenon_nandfs_get_lba(meta);
			dumpdata.lba_map[blk] = lba;
		
			for(mobi = 0x30; mobi < 0x3F; mobi++)
			{
				tmp = xenon_nandfs_find_mobile(meta, mobi);
				if(tmp == -1)
					continue; // not the mobile we are looking for
					
				mobi_ver = dumpdata.mobile_ver[mobi-MOBILE_BASE]; // version per mobile
				mobi_blk = blk;
				
				if((mobi_blk != 0) && (tmp > mobi_ver))
				{
					unsigned char* fsrootbuf = (unsigned char *)vmalloc(FSROOT_SIZE);
					unsigned char* rootbuf = (unsigned char *)vmalloc(FSROOT_SIZE);
					
					mobi_ver = tmp;
					
					if(mobi == MOBILE_FSROOT) // fs root
					{
						printk(KERN_INFO "FSRoot found at block 0x%x, v %i, size %d (0x%x) bytes\n", mobi_blk, mobi_ver, nand.block_sz, nand.block_sz);
						root_off = 0;
						file_off = 0;
						ttl_off = 0;
						for(i=0; i<16; i++) // copy alternating 512 bytes into each buf
						{
							for(j=0; j<512; j++)
							{
								rootbuf[root_off+j] = userbuf[ttl_off+j];
								fsrootbuf[file_off+j] = userbuf[ttl_off+j+512];
							}
							root_off += 512;
							file_off += 512;
							ttl_off  += 1024;
						}
					
						for(i=0; i<256; i++)
						{
							dumpdata.fs_ent = (FS_ENT*)&fsrootbuf[i*sizeof(FS_ENT)];
							dumpdata.fs_ent++;
						}
						mobi_size = 0;
						ret = 1;
					}
					else
					{
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
						mobi_size = xenon_nandfs_get_fssize(meta);
					
						mobileName[6] = mobi+0x11;
						printk(KERN_INFO "%s found at block 0x%x, page %d, v %i, size %d (0x%x) bytes\n", mobileName, mobi_blk, j, mobi_ver, mobi_size, mobi_size);
					}
					dumpdata.mobile_blocks[mobi-MOBILE_BASE] = mobi_blk;
					dumpdata.mobile_size[mobi-MOBILE_BASE] = mobi_size;
					dumpdata.mobile_ver[mobi-MOBILE_BASE] = mobi_ver;
					
					vfree(fsrootbuf);
					vfree(rootbuf);
				}
			}
		}
		vfree(userbuf);
		vfree(sparebuf);
	}
	return ret;
}

int xenon_nandfs_init_one(void)
{
	xenon_sfc_getnandstruct(&nand);
	xenon_nandfs_init();

	return 0;
}
