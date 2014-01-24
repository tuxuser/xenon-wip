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

static int metatype = INVALID;

static xenon_nand nand;

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

int xenon_nandfs_get_lba(PMETADATA meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return (((meta.sm.BlockID0&0xF)<<8)+(meta.sm.BlockID1));
		case META_TYPE_BOS:
			return (((meta.bos.BlockID0<<8)&0xF)+(meta.bos.BlockID1&0xFF));
		case META_TYPE_BG:
			return (((meta.bg.BlockID0&0xF)<<8)+(meta.bg.BlockID1&0xFF));
	}
	return INVALID;
}

int xenon_nandfs_get_blocktype(PMETADATA meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return meta.sm.FsBlockType;
		case META_TYPE_BOS:
			return meta.bos.FsBlockType;
		case META_TYPE_BG:
			return meta.bg.FsBlockType;
	}
	return INVALID;
}

int xenon_nandfs_get_badblock_mark(PMETADATA meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return meta.sm.BadBlock;
		case META_TYPE_BOS:
			return meta.bos.BadBlock;
		case META_TYPE_BG:
			return meta.bg.BadBlock;
	}
	return INVALID;
}

int xenon_nandfs_get_fssize(PMETADATA meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return ((meta.sm.FsSize0<<8)+meta.sm.FsSize1);
		case META_TYPE_BOS:
			return (((meta.bos.FsSize0<<8)&0xFF)+(meta.bos.FsSize1&0xFF));
		case META_TYPE_BG:
			return ((meta.bg.FsSize0<<16)+(meta.bg.FsSize1<<8)+meta.bg.FsSize2);
	}
	return INVALID;
}

int xenon_nandfs_get_fsfreepages(PMETADATA meta)
{
	switch (nand.meta_type)
	{
		case META_TYPE_SM:
			return meta.sm.FsPageCount;
		case META_TYPE_BOS:
			return meta.bos.FsPageCount;
		case META_TYPE_BG:
			return (meta.bg.FsPageCount * 4);
	}
	return INVALID;
}

unsigned int xenon_nandfs_check_mmc_anchor_sha(unsigned char* buf)
{
	unsigned char* data = buf;
	//CryptSha(&data[MMC_ANCHOR_HASH_LEN], (0x200-MMC_ANCHOR_HASH_LEN), NULL, 0, NULL, 0, sha, MMC_ANCHOR_HASH_LEN);
}

unsigned int xenon_nandfs_get_mmc_anchor_ver(unsigned char* buf)
{
	unsigned char* data = buf;
	return __builtin_bswap32(data[MMC_ANCHOR_VERSION_POS]);
}

unsigned int xenon_nandfs_get_mmc_mobilesector(unsigned char* buf, int mobile_num)
{
	unsigned char* data = buf;
	int offset = MMC_ANCHOR_MOBI_START+(mobile_num*MMC_ANCHOR_MOBI_SIZE);
	
	return __builtin_bswap16(data[offset]);
}

unsigned int xenon_nandfs_get_mmc_mobilesize(unsigned char* buf, int mobile_num)
{
	unsigned char* data = buf;
	int offset = MMC_ANCHOR_MOBI_START+(mobile_num*MMC_ANCHOR_MOBI_SIZE)+0x4;
	
	return __builtin_bswap16(data[offset]);
}

int xenon_nandfs_check_ecc(PMETADATA meta, char pagedata)
{
}

int xenon_nandfs_find_mobile(PMETADATA meta)
{
}

unsigned char* xenon_nandfs_dump_mobile(PMETADATA meta, int mobile_num)
{
	int mob_sect, mob_size;
	
	if(nand.mmc)
	{
		int anchor_ver = 0;
		int i, tmp, mmc_anchor_blk;
		unsigned char* blockbuf = kzalloc(sfc.nand.block_sz, GFP_KERNEL);
		mmc_anchor_blk = nand.config_block - MMC_ANCHOR_BLOCKS;
		xenon_sfc_readmapdata(&blockbuf, (mmc_anchor_blk * nand.block_sz), (nand.block_sz * 2));
		
		for(i=0; i < MMC_ANCHOR_BLOCKS; i++)
		{
			tmp = xenon_nandfs_get_mmc_anchor_ver(&blockbuf[i*nand.block_sz]);
			if(tmp > anchor_ver)
			{
				anchor_ver = tmp;
				mob_sect = xenon_nandfs_get_mmc_mobilesector(&blockbuf[i*nand.block_sz], mobile_num);
				mob_size = xenon_nandfs_get_mmc_mobilesize(&blockbuf[i*nand.block_sz], mobile_num);
			}		
		}
		
		mob_sect *= nand.block_sz;
		mob_size *= nand.block_sz;
	}
	else
	{
	}
}

int xenon_nandfs_init_one(void)
{
	nand = xenon_sfc_getnandstruct();
	// Check struct
	// Find Mobile(s)
	// Dump Mobile(s)
	// Parse FS-entries
	// Parse LBA Map
}
