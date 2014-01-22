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

typedef volatile u32 xenonflash_uint;

struct xenonflash_private {
	struct mtd_info *next;
	struct resource *module;
	u_char *addr;
	size_t size;
	u_char *uaddr;
};

#endif
