/*
 *  Xenon System Flash MTD driver
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

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "xenon_mtd.h"

/*
 * https://www.kernel.org/doc/htmldocs/mtdnand/
 */


static char version[] __initdata =
	"xenon_mtd.c: v.1.0.0  19 Jan 2014  Maciej W. Rozycki.\n";

MODULE_AUTHOR("tuxuser <tuxuser360@gmail.com>");
MODULE_DESCRIPTION("Xenon FLASH module driver");
MODULE_LICENSE("GPL");

static const char xenonflash_name[] = "Xenon FLASH";
static const char xenonflash_res_system[] = "System Area";
static const char xenonflash_res_memoryunit[] = "Memory Unit Area";

static struct mtd_info *root_xenonflash_mtd;

static struct xenonflash_public *xenonflash;

static int xenonflash_read(struct mtd_info *mtd, loff_t from,
			size_t len, size_t *retlen, u_char *buf)
{
	struct xenonflash_private *mp = mtd->priv;

	memcpy(buf, mp->uaddr + from, len);
	*retlen = len;
	return 0;
}

static int xenonflash_write(struct mtd_info *mtd, loff_t to,
			size_t len, size_t *retlen, const u_char *buf)
{
	struct xenonflash_private *mp = mtd->priv;

	memcpy(mp->uaddr + to, buf, len);
	*retlen = len;
	return 0;
}

int xenonflash_get_metatype()
{
	return xenon_flash.meta_type;
}

int xenonflash_get_pagesz
{
	return xenon_flash.page_sz;
}

int xenonflash_get_pagesz_phys
{
	return xenon_flash.page_sz_phys;
}

int xenonflash_get_metasz
{
	return xenon_flash.meta_sz;
}

int xenonflash_get_pagecnt_in_block
{
	return xenon_flash.pages_in_block;
}

int xenonflash_get_blocksz
{
	return xenon_flash.block_sz;
}

int xenonflash_get_blocksz_phys
{
	return xenon_flash.block_sz_phys;
}

int xenonflash_get_nandsz
{
	return xenon_flash.size_bytes;
}

int xenonflash_get_nandsz_phys
{
	return xenon_flash.size_bytes_phys;
}

int xenonflash_get_total_pagecnt
{
	return xenon_flash.size_pages;
}

int xenonflash_get_total_blockcnt
{
	return xenon_flash.size_blocks;
}

static inline uint xenonflash_probe_one(ulong addr)
{

}

static int __init xenonflash_init_one(ulong addr)
{

}

static void __exit xenonflash_remove_one(void)
{

}


static int __init xenonflash_init(void)
{

}

static void __exit xenonflash_cleanup(void)
{
	while (root_xenonflash_mtd)
		xenonflash_remove_one();
}


module_init(xenonflash_init);
module_exit(xenonflash_cleanup);
