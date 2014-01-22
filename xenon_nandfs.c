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

static int metatype = INVALID;

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

int xenon_nandfs_get_lba(METADATA *meta)
{
	switch (metatype)
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

int xenon_nandfs_get_blocktype(METADATA *meta)
{
	switch (metatype)
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

int xenon_nandfs_get_badblock_mark(METADATA *meta)
{
	switch (metatype)
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

int xenon_nandfs_get_fssize(METADATA *meta)
{
	switch (metatype)
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

int xenon_nandfs_get_fsfreepages(METADATA *meta)
{
	switch (metatype)
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

int xenon_nandfs_check_ecc(METADATA *meta, char *pagedata)
{
}

int xenon_nandfs_search_fsroot(METADATA *meta)
{
}

int xenon_nandfs_parse_fsroot(METADATA *meta)
{
}

int xenon_nandfs_init_one(void)
{
	
}
