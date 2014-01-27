#ifndef _XENON_SFC_H
#define _XENON_SFC_H

#include <stdbool.h>

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long s64;
typedef unsigned long u64;

//Registers
#define SFCX_CONFIG				(0x00)
#define SFCX_STATUS 			(0x04)
#define SFCX_COMMAND			(0x08)
#define SFCX_ADDRESS			(0x0C)
#define SFCX_DATA				(0x10)
#define SFCX_LOGICAL 			(0x14)
#define SFCX_PHYSICAL			(0x18)
#define SFCX_DATAPHYADDR		(0x1C)
#define SFCX_SPAREPHYADDR		(0x20)
#define SFCX_MMC_IDENT			(0xFC)

//Commands for Command Register
#define PAGE_BUF_TO_REG			(0x00) 			//Read page buffer to data register
#define REG_TO_PAGE_BUF			(0x01) 			//Write data register to page buffer
#define LOG_PAGE_TO_BUF			(0x02) 			//Read logical page into page buffer
#define PHY_PAGE_TO_BUF			(0x03) 			//Read physical page into page buffer
#define WRITE_PAGE_TO_PHY		(0x04) 			//Write page buffer to physical page
#define BLOCK_ERASE				(0x05) 			//Block Erase
#define DMA_LOG_TO_RAM			(0x06) 			//DMA logical flash to main memory
#define DMA_PHY_TO_RAM			(0x07) 			//DMA physical flash to main memory
#define DMA_RAM_TO_PHY			(0x08) 			//DMA main memory to physical flash
#define UNLOCK_CMD_0			(0x55) 			//Unlock command 0
#define UNLOCK_CMD_1			(0xAA) 			//Unlock command 1

//Config Register bitmasks
#define CONFIG_DBG_MUX_SEL  	(0x7C000000)	//Debug MUX Select
#define CONFIG_DIS_EXT_ER   	(0x2000000)		//Disable External Error (Pre Jasper?)
#define CONFIG_CSR_DLY      	(0x1FE0000)		//Chip Select to Timing Delay
#define CONFIG_ULT_DLY      	(0x1F800)		//Unlocking Timing Delay
#define CONFIG_BYPASS       	(0x400)			//Debug Bypass
#define CONFIG_DMA_LEN      	(0x3C0)			//DMA Length in Pages
#define CONFIG_FLSH_SIZE    	(0x30)			//Flash Size (Pre Jasper)
#define CONFIG_WP_EN        	(0x8)			//Write Protect Enable
#define CONFIG_INT_EN       	(0x4)			//Interrupt Enable
#define CONFIG_ECC_DIS      	(0x2)			//ECC Decode Disable : TODO: make sure this isn't disabled before reads and writes!!
#define CONFIG_SW_RST       	(0x1)			//Software reset

#define CONFIG_DMA_PAGES(x)		(((x-1)<<6)&CONFIG_DMA_LEN)

//Status Register bitmasks
#define STATUS_MASTER_ABOR		(0x4000)		// DMA master aborted if not zero
#define STATUS_TARGET_ABOR		(0x2000)		// DMA target aborted if not zero
#define STATUS_ILL_LOG      	(0x800)			//Illegal Logical Access
#define STATUS_PIN_WP_N     	(0x400)			//NAND Not Write Protected
#define STATUS_PIN_BY_N     	(0x200)			//NAND Not Busy
#define STATUS_INT_CP       	(0x100)			//Interrupt
#define STATUS_ADDR_ER      	(0x80)			//Address Alignment Error
#define STATUS_BB_ER        	(0x40)			//Bad Block Error
#define STATUS_RNP_ER       	(0x20)			//Logical Replacement not found
#define STATUS_ECC_ER       	(0x1C)			//ECC Error, 3 bits, need to determine each
#define STATUS_WR_ER        	(0x2)			//Write or Erase Error
#define STATUS_BUSY         	(0x1)			//Busy
#define STATUS_ECC_ERROR		(0x10)			// controller signals unrecoverable ECC error when (!((stat&0x1c) < 0x10))
#define STATUS_DMA_ERROR		(STATUS_MASTER_ABOR|STATUS_TARGET_ABOR)
#define STATUS_ERROR			(STATUS_ILL_LOG|STATUS_ADDR_ER|STATUS_BB_ER|STATUS_RNP_ER|STATUS_ECC_ERROR|STATUS_WR_ER|STATUS_MASTER_ABOR|STATUS_TARGET_ABOR)
#define STSCHK_WRIERA_ERR(sta)	((sta & STATUS_WR_ER) != 0)
#define STSCHK_ECC_ERR(sta)		(!((sta & STATUS_ECC_ER) < 0x10))
#define STSCHK_DMA_ERR(sta)		((sta & (STATUS_DMA_ERROR) != 0)

//Page bitmasks
#define PAGE_VALID          	(0x4000000)
#define PAGE_PID            	(0x3FFFE00)

// status ok or status ecc corrected
//#define SFCX_SUCCESS(status) (((int) status == STATUS_PIN_BY_N) || ((int) status & STATUS_ECC_ER))
// define success as no ecc error and no bad block error
#define SFCX_SUCCESS(status) ((status&STATUS_ERROR)==0)

#define MAX_PAGE_SZ 			0x210			//Max known hardware physical page size
#define MAX_BLOCK_SZ 			0x42000 		//Max known hardware physical block size

#define META_TYPE_SM				0x00 			//Pre Jasper - Small Block
#define META_TYPE_BOS				0x01 			//Jasper 16MB - Big Block on Small NAND
#define META_TYPE_BG				0x02			//Jasper 256MB and 512MB Big Block
#define META_TYPE_NONE				0x04				//eMMC doesn't have Spare Data

#define CONFIG_BLOCKS			0x04			//Number of blocks assigned for config data

#define SFCX_INITIALIZED		1

#define INVALID					-1

// status ok or status ecc corrected
//#define SFCX_SUCCESS(status) (((int) status == STATUS_PIN_BY_N) || ((int) status & STATUS_ECC_ER))
// define success as no ecc error and no bad block error
#define SFCX_SUCCESS(status) ((status&STATUS_ERROR)==0)

typedef struct _xenon_nand
{
	bool is_bb_cont;
	bool is_bb;
	bool mmc;
	u8 meta_type;

	u16 page_sz;
	u16 page_sz_phys;
	u8 meta_sz;

	u16 pages_in_block;
	u32 block_sz;
	u32 block_sz_phys;

	u16 pages_count;
	u16 blocks_count;

	u32 size_spare;
	u32 size_data;
	u32 size_dump;
	u32 size_write;

	u16 size_usable_fs;
	u16 config_block;
} xenon_nand, *pxenon_nand;

unsigned long xenon_sfc_readreg(u32 addr);
void xenon_sfc_writereg(u32 addr, unsigned long data);

int xenon_sfc_readpage_phy(u8* buf, u32 page);
int xenon_sfc_readpage_log(u8* buf, u32 page);
int xenon_sfc_writepage(u8* buf, u32 page);
int xenon_sfc_readblock(u8* buf, u32 block);
int xenon_sfc_readblock_separate(u8* user, u8* spare, u32 block);
int xenon_sfc_writeblock(u8* buf, u32 block);
int xenon_sfc_readblocks(u8* buf, u32 block, u32 block_cnt);
int xenon_sfc_writeblocks(u8* buf, u32 block, u32 block_cnt);
int xenon_sfc_readfullflash(u8* buf);
int xenon_sfc_writefullflash(u8* buf);
int xenon_sfc_eraseblock(u32 block);
int xenon_sfc_eraseblocks(u32 block, u32 block_cnt);

void xenon_sfc_readmapdata(u8* buf, u32 startaddr, u32 total_len);

void xenon_sfc_getnandstruct(xenon_nand* xe_nand);


#endif
