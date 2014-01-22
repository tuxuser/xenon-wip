#ifndef _XENON_MTD_H
#define _XENON_MTD_H

#include <linux/ioport.h>
#include <linux/mtd/mtd.h>

/* MS02-NV memory offsets. */
#define XENONFLASH_ADDR_LOG				0xc8000000	/* Memory Adress logical mapped flash */

#define XENONFLASH_SYSTEM_START			0x0			/* System Area */
#define XENONFLASH_MU_START				0x4200000	/* Memory Unit Area  */

/* MS02-NV general constants. */
#define XENONFLASH_START_MAGIC		0xff4f0760	/* XenonFlash magic ID value */

#define MAX_PAGE_SZ 			0x210			//Max known hardware physical page size
#define MAX_BLOCK_SZ 			0x42000 		//Max known hardware physical block size

#define META_TYPE_SM				0x00 			//Pre Jasper - Small Block
#define META_TYPE_BOS				0x01 			//Jasper 16MB - Big Block on Small NAND
#define META_TYPE_BG				0x02			//Jasper 256MB and 512MB Big Block

#define CONFIG_BLOCKS			0x04			//Number of blocks assigned for config data

#define SFCX_INITIALIZED		1

#define INVALID					-1

typedef volatile u32 xenonflash_uint;

struct xenonflash_private {
	struct mtd_info *next;
	struct resource *module;
	u_char *addr;
	size_t size;
	u_char *uaddr;
};

struct xenonflash_public {
	int initialized;
	int meta_type;

	int page_sz;
	int meta_sz;
	int page_sz_phys;

	int pages_in_block;
	int block_sz;
	int block_sz_phys;

	int size_bytes;
	int size_bytes_phys;
	int size_pages;
	int size_blocks;

	int blocks_per_lg_block;
	int size_usable_fs;
	int addr_config;
	int len_config;
};

#endif
