/*
 *  Xenon System Flash Controller for Xbox 360
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>

#include "xenon_sfc.h"

#define DRV_NAME	"xenon_sfc"
#define DRV_VERSION	"0.1"

#define SFC_SIZE 0x400
#define DMA_SIZE 0x10000

#define MAP_ADDR 0x80000200C8000000ULL

#define DEBUG_OUT 1

typedef struct _xenon_sfc
{
	void __iomem *base;
	void __iomem *mappedflash;
	wait_queue_head_t wait_q;
	spinlock_t fifo_lock;
	
	dma_addr_t dmaaddr;
	unsigned char *dmabuf;
	xenon_nand nand;
} xenon_sfc, *pxenon_sfc;

static xenon_sfc sfc;


static const struct pci_device_id xenon_sfc_pci_tbl[] = {
	{ PCI_VDEVICE(MICROSOFT, 0x1234), 0 },
	{ },	/* terminate list */
};

static inline unsigned long _xenon_sfc_readreg(int addr)
{
	return __builtin_bswap32(readl(sfc.base + addr));
}

static inline void _xenon_sfc_writereg(int addr, unsigned long data)
{
	writel(__builtin_bswap32(data), (sfc.base + addr));
}

static int _xenon_sfc_eraseblock(int block)
{
	int status;
	int addr = block * sfc.nand.block_sz;
	
	// Enable Writes
	_xenon_sfc_writereg(SFCX_CONFIG, _xenon_sfc_readreg(SFCX_CONFIG) | CONFIG_WP_EN);
	_xenon_sfc_writereg(SFCX_STATUS, 0xFF);

	// Set flash address (logical)
	//address &= 0x3fffe00; // Align to page
	_xenon_sfc_writereg(SFCX_ADDRESS, addr);

	// Wait Busy
	while (_xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);

	// Unlock sequence (for erase)
	_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_1);
	_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_0);

	// Wait Busy
	while (_xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);

	// Command the block erase
	_xenon_sfc_writereg(SFCX_COMMAND, BLOCK_ERASE);

	// Wait Busy
	while (_xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);

	status = _xenon_sfc_readreg(SFCX_STATUS);
	//if (!SFCX_SUCCESS(status))
	//	printf(" ! SFCX: Unexpected sfc.erase_block status %08X\n", status);
	_xenon_sfc_writereg(SFCX_STATUS, 0xFF);

	// Disable Writes
	_xenon_sfc_writereg(SFCX_CONFIG, _xenon_sfc_readreg(SFCX_CONFIG) & ~CONFIG_WP_EN);

	return status;
}

static int _xenon_sfc_readpage(unsigned char* buf, int page, int raw)
{
	int status, i, page_sz;
	int addr = page * sfc.nand.page_sz;
	unsigned char* data = buf;

	_xenon_sfc_writereg(SFCX_STATUS, _xenon_sfc_readreg(SFCX_STATUS));

	// Set flash address (logical)
	//address &= 0x3fffe00; // Align to page
	_xenon_sfc_writereg(SFCX_ADDRESS, addr);

	// Command the read
	// Either a logical read (0x200 bytes, no meta data)
	// or a Physical read (0x210 bytes with meta data)
	_xenon_sfc_writereg(SFCX_COMMAND, raw ? PHY_PAGE_TO_BUF : LOG_PAGE_TO_BUF);

	// Wait Busy
	while ((status = _xenon_sfc_readreg(SFCX_STATUS)) & STATUS_BUSY);

	if (!SFCX_SUCCESS(status))
	{
		if (status & STATUS_BB_ER)
			printk(KERN_INFO " ! SFCX: Bad block found at %08X\n", addr/sfc.nand.block_sz);
		else if (status & STATUS_ECC_ER)
		//	printf(" ! SFCX: (Corrected) ECC error at address %08X: %08X\n", address, status);
			status = status;
		else if (!raw && (status & STATUS_ILL_LOG))
			printk(KERN_INFO " ! SFCX: Illegal logical block at %08X (status: %08X)\n", addr/sfc.nand.block_sz, status);
		else
			printk(KERN_INFO " ! SFCX: Unknown error at address %08X: %08X. Please worry.\n", addr, status);
	}

	// Set internal page buffer pointer to 0
	_xenon_sfc_writereg(SFCX_ADDRESS, 0);

	page_sz = raw ? sfc.nand.page_sz_phys : sfc.nand.page_sz;

	for (i = 0; i < page_sz ; i += 4)
	{
		// Transfer data from buffer to register
		_xenon_sfc_writereg(SFCX_COMMAND, PAGE_BUF_TO_REG);

		// Read out our data through the register
		*(int*)(data + i) = __builtin_bswap32(_xenon_sfc_readreg(SFCX_DATA));
	}

	return status;
}

static int _xenon_sfc_writepage(unsigned char* buf, int page)
{
	int i, status;
	int addr = page * sfc.nand.page_sz;
	unsigned char* data = buf;
	
	_xenon_sfc_writereg(SFCX_STATUS, 0xFF);

	// Enable Writes
	_xenon_sfc_writereg(SFCX_CONFIG, _xenon_sfc_readreg(SFCX_CONFIG) | CONFIG_WP_EN);

	// Set internal page buffer pointer to 0
	_xenon_sfc_writereg(SFCX_ADDRESS, 0);

	for (i = 0; i < sfc.nand.page_sz_phys; i+=4)
	{
		// Write out our data through the register
		_xenon_sfc_writereg(SFCX_DATA, __builtin_bswap32(*(int*)(data + i)));

		// Transfer data from register to buffer
		_xenon_sfc_writereg(SFCX_COMMAND, REG_TO_PAGE_BUF);
	}

	// Set flash address (logical)
	//address &= 0x3fffe00; // Align to page
	_xenon_sfc_writereg(SFCX_ADDRESS, addr);

	// Unlock sequence (for write)
	_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_0);
	_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_1);

	// Wait Busy
	while (_xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);

	// Command the write
	_xenon_sfc_writereg(SFCX_COMMAND, WRITE_PAGE_TO_PHY);

	// Wait Busy
	while (_xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);

	status = _xenon_sfc_readreg(SFCX_STATUS);
	if (!SFCX_SUCCESS(status))
		printk(KERN_INFO " ! SFCX: Unexpected sfc.writepage status %08X\n", status);

	// Disable Writes
	_xenon_sfc_writereg(SFCX_CONFIG, _xenon_sfc_readreg(SFCX_CONFIG) & ~CONFIG_WP_EN);

	return status;
}

// returns 1 on bad block, 2 on unrecoverable ECC
// data NULL to skip keeping data
static int _xenon_sfc_readblock(unsigned char* buf, int block)
{
	int i, status, block_read;
	int addr = block * sfc.nand.block_sz;
	
	unsigned char* data = buf;
	unsigned int cur_addr = addr;
	
	if(buf != NULL)
		memset(data, 0, (sfc.nand.pages_in_block*sfc.nand.page_sz_phys));

	for(block_read = 0; block_read < sfc.nand.block_sz_phys; block_read += 0x2000, cur_addr += 0x2000)
	{
		_xenon_sfc_writereg(SFCX_STATUS, _xenon_sfc_readreg(SFCX_STATUS));
		_xenon_sfc_writereg(SFCX_DATAPHYADDR, sfc.dmaaddr);
		_xenon_sfc_writereg(SFCX_SPAREPHYADDR, sfc.dmaaddr+0xC000);
		_xenon_sfc_writereg(SFCX_ADDRESS, cur_addr);
		_xenon_sfc_writereg(SFCX_COMMAND, DMA_PHY_TO_RAM);
		
		while(_xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);
		
		status = _xenon_sfc_readreg(SFCX_STATUS);
		if(status&STATUS_ERROR)
		{
			printk(KERN_INFO "error in status, %08x\n", status);
			if(status&STATUS_BB_ER)
			{
				printk(KERN_INFO "Bad block error block 0x%x\n", block);
				return 1;
			}
			if(STSCHK_ECC_ERR(status))
			{
				printk(KERN_INFO "unrecoverable ECC error block 0x%x\n", block);
				return 2;
			}
		}
		if(buf != NULL)
		{
			for(i = 0; i < 16; i++)
			{
				memcpy(data, &sfc.dmabuf[i*sfc.nand.page_sz], sfc.nand.page_sz);
				memcpy(&data[sfc.nand.page_sz], &sfc.dmabuf[0xC000+(i*sfc.nand.meta_sz)], sfc.nand.meta_sz);
				data += sfc.nand.page_sz_phys;
			}
		}
	}
	return 0;
}

static int _xenon_sfc_readblock_separate(unsigned char* user, unsigned char* spare, int block)
{
	int cur_blk, config, wconfig;
	unsigned char* data = (unsigned char *)vmalloc(sfc.nand.block_sz_phys);
	
	if(user && spare)
	{
		config = _xenon_sfc_readreg(SFCX_CONFIG);
		wconfig = (config&~(CONFIG_DMA_LEN|CONFIG_INT_EN|CONFIG_WP_EN));
		if(sfc.nand.is_bb)
			wconfig = wconfig|CONFIG_DMA_PAGES(4); // change to 4 pages, bb 4 pages = 16 small pages
		else
			wconfig = wconfig|CONFIG_DMA_PAGES(16); // change to 16 pages
		_xenon_sfc_writereg(SFCX_CONFIG, wconfig);

// 		printk(KERN_INFO "Reading block %x of %x at block %x (%x)\n", blk, block_cnt, blk+block, (blk+block)*sfc.nand.block_sz_phys);
		_xenon_sfc_readblock(buf, block);

		_xenon_sfc_writereg(SFCX_CONFIG, config);
	}
	else
		printk(KERN_INFO "supplied buffers weren't allocated for readblock_separate\n");
	
	for(i = 0; i < sfc.nand.pages_in_block; i++)
	{
		memcpy(user, &data[(i*sfc.nand.page_sz_phys)], sfc.nand.page_sz);
		memcpy(spare, &data[(i*sfc.nand.page_sz_phys)+sfc.nand.page_sz], sfc.nand.meta_sz); 
		user += sfc.nand.page_sz;
		spare += sfc.nand.meta_sz;
	}
	return 0;	
}

static int _xenon_sfc_writeblock(unsigned char* buf, int block)
{
	int status, i, block_wrote;
	int addr = block * sfc.nand.block_sz;
	
	unsigned char* data = buf;
	unsigned int cur_addr = addr;
	
	// one erase per block
	status = _xenon_sfc_eraseblock(addr);
	if(status&STATUS_ERROR)
		printk(KERN_INFO "error in erase status, %08x\n", status);

	if(buf != NULL)
	{
		for(block_wrote = 0; block_wrote < sfc.nand.block_sz_phys; block_wrote += 0x2000, cur_addr += 0x2000)
		{
			for(i = 0; i < 16; i++)
			{
				memcpy(&sfc.dmabuf[i*sfc.nand.page_sz], data, sfc.nand.page_sz);
				memcpy(&sfc.dmabuf[0xC000+(i*sfc.nand.meta_sz)], &data[sfc.nand.page_sz], sfc.nand.meta_sz);
				data += sfc.nand.page_sz_phys;
			}
			_xenon_sfc_writereg(SFCX_STATUS, _xenon_sfc_readreg(SFCX_STATUS));
			_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_0);
			_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_1);
			_xenon_sfc_writereg(SFCX_DATAPHYADDR, sfc.dmaaddr);
			_xenon_sfc_writereg(SFCX_SPAREPHYADDR, sfc.dmaaddr+0xC000);
			_xenon_sfc_writereg(SFCX_ADDRESS, cur_addr);
			_xenon_sfc_writereg(SFCX_COMMAND, DMA_RAM_TO_PHY);
			
			while(_xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);
			
			status = _xenon_sfc_readreg(SFCX_STATUS);
			if(status&STATUS_ERROR)
				printk(KERN_INFO "error in status, %08x\n", status);
		}
	}
	return 0;
}

static int _xenon_sfc_readblocks(unsigned char* buf, int block, int block_cnt)
{
	int cur_blk, config, wconfig;
	//int sz = (block_cnt*sfc.nand.block_sz_phys);
	//unsigned char *buf = (unsigned char *)vmalloc(block_cnt*sfc.nand.block_sz_phys);
	//unsigned char *buf = (unsigned char *)VirtualAlloc(0, (block_cnt*sfc.nand.block_sz_phys), MEM_COMMIT|MEM_LARGE_PAGES, PAGE_READWRITE);
	
	if(buf)
	{
// 		printk(KERN_INFO "reading %d blocks starting at %x block, %x sz\n", block_cnt, block, sz);
		if(sfc.nand.mmc)
		{
/*
			int rd = ReadFlash(block*sfc.nand.block_sz_phys, buf, sz, sfc.nand.block_sz_phys, NULL);
			if(rd != sz)
			{
				printk(KERN_INFO "trying to read mmc yielded 0x%x bytes instead of 0x%x!\n", rd, sz);
				VirtualFree(buf,0, MEM_RELEASE );
				return NULL;
			}
*/
			printk(KERN_INFO "MMC not yet implemented!\n");
		}
		else
		{
			config = _xenon_sfc_readreg(SFCX_CONFIG);
			wconfig = (config&~(CONFIG_DMA_LEN|CONFIG_INT_EN|CONFIG_WP_EN));
			if(sfc.nand.is_bb)
				wconfig = wconfig|CONFIG_DMA_PAGES(4); // change to 4 pages, bb 4 pages = 16 small pages
			else
				wconfig = wconfig|CONFIG_DMA_PAGES(16); // change to 16 pages
			_xenon_sfc_writereg(SFCX_CONFIG, wconfig);
			
			for(cur_blk = 0; cur_blk < block_cnt; cur_blk++)
			{
// 				printk(KERN_INFO "Reading block %x of %x at block %x (%x)\n", blk, block_cnt, blk+block, (blk+block)*sfc.nand.block_sz_phys);
				_xenon_sfc_readblock(&buf[cur_blk*sfc.nand.block_sz_phys], cur_blk+block);
			}
			_xenon_sfc_writereg(SFCX_CONFIG, config);
		}
// 		printk(KERN_INFO "flash read complete\n");
	}
	else
		printk(KERN_INFO "supplied buffer wasn't allocated for readblocks\n");
		
	return 0;
}

static int _xenon_sfc_blockhasdata(unsigned char* buf)
{
	int i;
	unsigned char *data = buf;
	
	for(i = 0; i < sfc.nand.block_sz_phys; i++)
		if((data[i]&0xFF) != 0xFF)
			return 1;
	return 0;
}

static int _xenon_sfc_writeblocks(unsigned char *buf, int block, int block_cnt)
{
	int cur_blk, config, wconfig;
	
	unsigned char* blk_data;
	unsigned char* data = buf;
	//int sz = (block_cnt*sfc.nand.block_sz_phys);
	unsigned char* blockbuf = (unsigned char *)vmalloc(sfc.nand.block_sz_phys);
	
	if(((block+block_cnt)*sfc.nand.block_sz_phys) > sfc.nand.size_dump)
	{
		printk(KERN_INFO "error, write exceeds system area!\n");
		return 0;
	}
	
	if(sfc.nand.mmc)
	{
/*
		int wr;
		wr = WriteFlash(block*sfc.nand.block_sz_phys, data, sz, sfc.nand.block_sz_phys, NULL);
		if(wr != sz)
		{
			printk(KERN_INFO "trying to write mmc yielded 0x%x bytes instead of 0x%x!\n", wr, sz);
			return 0;
		}
*/
		printk(KERN_INFO "MMC not yet implemented!\n");
	}
	else
	{
		config = _xenon_sfc_readreg(SFCX_CONFIG);
		wconfig = (config &~(CONFIG_DMA_LEN|CONFIG_INT_EN))|CONFIG_WP_EN; // for the write
		if(sfc.nand.is_bb)
			wconfig = wconfig|CONFIG_DMA_PAGES(4); // change to 4 pages, bb 4 pages = 16 small pages
		else
			wconfig = wconfig|CONFIG_DMA_PAGES(16); // change to 16 pages
		_xenon_sfc_writereg(SFCX_CONFIG, (wconfig));
		
		for(cur_blk = 0; cur_blk < block_cnt; cur_blk++)
		{
			blk_data = &data[cur_blk*sfc.nand.block_sz_phys];
			// check for bad block, do NOT modify bad blocks!!!
			if(_xenon_sfc_readblock(blockbuf, cur_blk+block) == 0)
			{
				// check if data needs to be written
				if(memcmp(blockbuf, blk_data, sfc.nand.block_sz_phys) != 0)
				{
					// check if block has data to write, or if it's only an erase
					if(_xenon_sfc_blockhasdata(blk_data))
					{
						//printk(KERN_INFO "Writing block %x of %x at %x (%x)\n", cur_blk, block_cnt, cur_blk+block, (cur_blk+block)*sfc.nand.block_sz_phys);
						_xenon_sfc_writeblock(blk_data, cur_blk+block);
					}
					else
					{
						//printk(KERN_INFO "Erase only block %x of %x at %x (%x)\n", cur_blk, block_cnt, cur_blk+block, (cur_blk+block)*sfc.nand.block_sz_phys);
						_xenon_sfc_writeblock(NULL, cur_blk+block);
					}
				}
				//else
				//	printk(KERN_INFO "skipping write block %x at %x of %x, data identical\n", cur_blk, cur_blk*sfc.nand.block_sz_phys, sfc.nand.size_data/sfc.nand.block_sz_phys);
			}
			else
				printk(KERN_INFO "not writing to block %x, it is bad!\n", cur_blk+block);
		}
		_xenon_sfc_writereg(SFCX_CONFIG, config);
	}
// 	printk(KERN_INFO "flash write complete\n");
	return 0;
}

static int _xenon_sfc_eraseblocks(int block, int block_cnt)
{
	int cur_blk, config, wconfig, status;
	
	//int sz = (block_cnt*sfc.nand.block_sz_phys);
	if(sfc.nand.mmc)
	{
/*
		int i;
		memset(g_blockBuf, 0xFF, sfc.nand.block_sz_phys);
		for(i = 0; i < block_cnt; i++)
		{
			int wr = WriteFlash((block+i)*sfc.nand.block_sz_phys, g_blockBuf, sfc.nand.block_sz_phys, sfc.nand.block_sz_phys, NULL);
			if(wr != sz)
			{
				printk(KERN_INFO "trying to fake-erase mmc yielded 0x%x bytes instead of 0x%x!\n", wr, sz);
				return 0;
			}
		}
*/
		printk(KERN_INFO "MMC not yet implemented!\n");
	}
	else
	{
		config = _xenon_sfc_readreg(SFCX_CONFIG);
		wconfig = (config &~(CONFIG_DMA_LEN|CONFIG_INT_EN))|CONFIG_WP_EN; // for the write
		if(sfc.nand.is_bb)
			wconfig = wconfig|CONFIG_DMA_PAGES(4); // change to 4 pages, bb 4 pages = 16 small pages
		else
			wconfig = wconfig|CONFIG_DMA_PAGES(16); // change to 16 pages
		_xenon_sfc_writereg(SFCX_CONFIG, (wconfig));
		
		for(cur_blk = 0; cur_blk < block_cnt; cur_blk++)
		{
			status = _xenon_sfc_eraseblock(cur_blk+block);
			if(status&STATUS_ERROR)
				printk(KERN_INFO "error in erase status, %08x\n", status);
		}
		_xenon_sfc_writereg(SFCX_CONFIG, config);
	}
	return 0;
}

static void _xenon_sfc_readmapdata(unsigned char* buf, int startaddr, int total_len)
{
	memcpy(buf, (sfc.mappedflash + startaddr), total_len);
} 

static int _xenon_sfc_writefullflash(unsigned char* buf)
{
	int cur_blk, config, wconfig;
	unsigned char* data;
	unsigned char* blockbuf = (unsigned char *)vmalloc(sfc.nand.block_sz_phys);
// 	printk(KERN_INFO "writing flash\n");

	if(sfc.nand.mmc)
	{
/*		int wr;
		wr = WriteFlash(0, nandData, sfc.nand.size_dump, sfc.nand.block_sz_phys, &workerSize);
		if(wr != sfc.nand.size_dump)
		{
			printk(KERN_INFO "trying to write mmc yielded 0x%x bytes instead of 0x%x!\n", wr, sfc.nand.size_dump);
			return 0;
		}
*/
		printk(KERN_INFO "MMC not yet implemented!\n");
	}
	else
	{
		config = _xenon_sfc_readreg(SFCX_CONFIG);
		wconfig = (config&~(CONFIG_DMA_LEN|CONFIG_INT_EN))|CONFIG_WP_EN; // for the write
		if(sfc.nand.is_bb)
			wconfig = wconfig|CONFIG_DMA_PAGES(4); // change to 4 pages, bb 4 pages = 16 small pages
		else
			wconfig = wconfig|CONFIG_DMA_PAGES(16); // change to 16 pages
		_xenon_sfc_writereg(SFCX_CONFIG, (wconfig));
		for(cur_blk = 0; cur_blk < (sfc.nand.size_data/sfc.nand.block_sz_phys); cur_blk++)
		{
			data = &buf[cur_blk*sfc.nand.block_sz_phys];

			// check for bad block, do NOT modify bad blocks!!!
			if(_xenon_sfc_readblock(blockbuf, cur_blk) == 0)
			{
				// check if data needs to be written
				if(memcmp(blockbuf, data, sfc.nand.block_sz_phys) != 0)
				{
					// check if block has data to write, or if it's only an erase
					if(_xenon_sfc_blockhasdata(data))
					{
 						//printk(KERN_INFO "Writing block %x at %x of %x\n", cur_blk, cur_blk*sfc.nand.block_sz_phys, sfc.nand.size_data/sfc.nand.block_sz_phys);
						_xenon_sfc_writeblock(data, cur_blk);
					}
					else
					{
						if(sfc.nand.is_bb)
						{
							//PPAGEDATA ppd = (PPAGEDATA)blockbuf;	/* TODO: Check if block needs to be written at all */
							//if((ppd[0].meta.bg.FsBlockType >= 0x2a)||(ppd[0].meta.bg.FsBlockType == 0))
								_xenon_sfc_writeblock(NULL, cur_blk);
// 							else
// 								printk(KERN_INFO "block 0x%x contains nandmu data! type: %x\n", cur_blk, ppd[0].meta.bg.FsBlockType);
							//if((ppd[0].meta.bg.FsBlockType < 0x2a)&&(ppd[0].meta.bg.FsBlockType != 0))
							//	printk(KERN_INFO "block 0x%x contains nandmu data! type: %x\n", cur_blk, ppd[0].meta.bg.FsBlockType);
							//else
							//	writeBlock(NULL, cur_blk);
						}
						else
						{
							//printk(KERN_INFO "Erase only block %x at %x of %x\n", cur_blk, cur_blk*sfc.nand.block_sz_phys, sfc.nand.size_data/sfc.nand.block_sz_phys);
							_xenon_sfc_writeblock(NULL, cur_blk);
						}
					}
				}
				//else
				//	printk(KERN_INFO "skipping write block %x at %x of %x, data identical\n", cur_blk, cur_blk*sfc.nand.block_sz_phys, sfc.nand.size_data/sfc.nand.block_sz_phys);
			}
			else
				printk(KERN_INFO "not writing to block %x, it is bad!\n", cur_blk);
		}
		_xenon_sfc_writereg(SFCX_CONFIG, config);
	}
	kfree(blockbuf);
// 	printk(KERN_INFO "flash write complete\n");
	return 0;
}

static int _xenon_sfc_readfullflash(unsigned char* buf)
{
	int cur_blk, config, wconfig;
	unsigned char* data = buf;
	
	if(sfc.nand.mmc)
	{
/*
		int rd = ReadFlash(0, nandData, sfc.nand.size_dump, 0x20000, &workerSize);
		if(rd != sfc.nand.size_dump)
		{
			printk(KERN_INFO "trying to read mmc yielded 0x%x bytes instead of 0x%x!\n", rd, sfc.nand.size_dump);
			return 0;
		}
*/
	}
	else
	{
 		config = _xenon_sfc_readreg(SFCX_CONFIG);
   		wconfig = (config&~(CONFIG_DMA_LEN|CONFIG_INT_EN|CONFIG_WP_EN));
   		if(sfc.nand.is_bb)
   			wconfig = wconfig|CONFIG_DMA_PAGES(4); // change to 4 pages, bb 4 pages = 16 small pages
   		else
   			wconfig = wconfig|CONFIG_DMA_PAGES(16); // change to 16 pages
 		_xenon_sfc_writereg(SFCX_CONFIG, wconfig);
 		
 		for(cur_blk = 0; cur_blk < (sfc.nand.size_data/sfc.nand.block_sz_phys); cur_blk++)
 		{
  			//printk(KERN_INFO "Reading block %x at %x of %x\n", cur_blk, cur_blk*sfc.nand.block_sz_phys, sfc.nand.size_data/sfc.nand.block_sz_phys);
 			_xenon_sfc_readblock(&data[cur_blk*sfc.nand.block_sz_phys], cur_blk);
 		}
 		_xenon_sfc_writereg(SFCX_CONFIG, config);
	}
// 	printk(KERN_INFO "flash read complete\n");
	return 0;
}

unsigned long xenon_sfc_readreg(int addr)
{
	return _xenon_sfc_readreg(addr);
}

void xenon_sfc_writereg(int addr, unsigned long data)
{
	_xenon_sfc_writereg(addr, data);
}

int xenon_sfc_readpage_phy(unsigned char* buf, int page)
{
	return _xenon_sfc_readpage(buf, page, 1);
}

int xenon_sfc_readpage_log(unsigned char* buf, int page)
{
	return _xenon_sfc_readpage(buf, page, 0);
}

int xenon_sfc_writepage(unsigned char* buf, int page)
{
	return _xenon_sfc_writepage(buf, page);
}

int xenon_sfc_readblock(unsigned char* buf, int block)
{
	return _xenon_sfc_readblock(buf, block);
}

int xenon_sfc_readblock_separate(unsigned char* user, unsigned char* spare, int block)
{
	return _xenon_sfc_readblock_separate(user, spare, block);
}

int xenon_sfc_writeblock(unsigned char* buf, int block)
{
	return _xenon_sfc_writeblock(buf, block);
}
int xenon_sfc_readblocks(unsigned char* buf, int block, int block_cnt)
{
	return _xenon_sfc_readblocks(buf, block, block_cnt);
}
int xenon_sfc_writeblocks(unsigned char* buf, int block, int block_cnt)
{
	return _xenon_sfc_writeblocks(buf, block, block_cnt);
}

int xenon_sfc_readfullflash(unsigned char* buf)
{
	return _xenon_sfc_readfullflash(buf);
}

int xenon_sfc_writefullflash(unsigned char* buf)
{
	return _xenon_sfc_writefullflash(buf);
}

int xenon_sfc_eraseblock(int block)
{
	return _xenon_sfc_eraseblock(block);
}

int xenon_sfc_eraseblocks(int block, int block_cnt)
{
	return _xenon_sfc_eraseblocks(block, block_cnt);
}

void xenon_sfc_readmapdata(unsigned char* buf, int startaddr, int total_len)
{
	xenon_sfc_readmapdata(buf, startaddr, total_len);
}

void xenon_sfc_getnandstruct(xenon_nand* xe_nand)
{
	xe_nand = sfc.nand;
}


static int _xenon_sfc_enum_nand(void)
{
	int config;
	unsigned long eMMC;
	
	sfc.nand.pages_in_block = 32;
	sfc.nand.meta_sz = 0x10;
	sfc.nand.page_sz = 0x200;
	sfc.nand.page_sz_phys = sfc.nand.page_sz + sfc.nand.meta_sz;
	
	eMMC = _xenon_sfc_readreg(SFCX_MMC_IDENT);
	if (eMMC != 0)
		sfc.nand.mmc = 1;
	else
		sfc.nand.mmc = 0;

	if(sfc.nand.mmc) // corona MMC
	{
		sfc.nand.meta_type = META_TYPE_NONE;
		sfc.nand.block_sz = 0x4000;
		sfc.nand.block_sz_phys = 0x20000;
		sfc.nand.meta_sz = 0;
		
		sfc.nand.size_dump = 0x3000000;
		sfc.nand.size_data = 0x3000000;
		sfc.nand.size_spare = 0;
		sfc.nand.size_usable_fs = 0xC00; // (sfc.nand.size_dump/sfc.nand.block_sz)

#ifdef DEBUG_OUT
		printk(KERN_INFO "MMC console detected\n");
#endif
	}
	else
	{
		config = _xenon_sfc_readreg(SFCX_CONFIG);

		// defaults for 16/64M small block
		sfc.nand.meta_type = META_TYPE_SM;
		sfc.nand.block_sz = 0x4000;
		sfc.nand.block_sz_phys = 0x4200;
		sfc.nand.is_bb = 0;
		sfc.nand.is_bb_cont = 0;

#ifdef DEBUG_OUT
		printk(KERN_INFO "SFC config %08x ver: %d type: %d\n", config, ((config>>17)&3), ((config >> 4) & 0x3));
#endif

		switch((config>>17)&3) // 00043000
		{
			case 0: // small block flash controller
				switch ((config >> 4) & 0x3)
				{
// 					 case 0: // 8M, not really supported
// 						sfc.nand.size_dump = 0x840000;
// 						sfc.nand.size_data = 0x800000;
// 						sfc.nand.size_spare = 0x40000;
// 						break;
					case 1: // 16MB
						sfc.nand.size_dump = 0x1080000;
						sfc.nand.size_data = 0x1000000;
						sfc.nand.size_spare = 0x80000;
						sfc.nand.size_usable_fs = 0x3E0;
						break;
// 					 case 2: // 32M, not really supported
// 						sfc.nand.size_dump = 0x2100000;
// 						sfc.nand.size_data = 0x2000000;
// 						sfc.nand.size_spare = 0x100000;
						sfc.nand.size_usable_fs = 0x7C0;
// 						break;
					case 3: // 64MB
						sfc.nand.size_dump = 0x4200000;
						sfc.nand.size_data = 0x4000000;
						sfc.nand.size_spare = 0x200000;
						sfc.nand.size_usable_fs = 0xF80;
						break;
					default:
						printk(KERN_INFO "unknown T%i NAND size! (%x)\n", ((config >> 4) & 0x3), (config >> 4) & 0x3);
						return 0;
				}
				break;
			case 1: // big block flash controller
				switch ((config >> 4) & 0x3)// TODO: FIND OUT FOR 64M!!! IF THERE IS ONE!!!
				{
					case 1: // Small block 16MB setup
						sfc.nand.meta_type = META_TYPE_BOS;
						sfc.nand.is_bb_cont = 1;
						sfc.nand.size_dump = 0x1080000;
						sfc.nand.size_data = 0x1000000;
						sfc.nand.size_spare = 0x80000;
						sfc.nand.size_usable_fs = 0x3E0;
						break;
					case 2: // Large Block: Current Jasper 256MB and 512MB
						sfc.nand.meta_type = META_TYPE_BG;
						sfc.nand.is_bb_cont = 1;
						sfc.nand.is_bb = 1;
						sfc.nand.size_dump = 0x4200000;
						sfc.nand.block_sz_phys = 0x21000;
						sfc.nand.size_data = 0x4000000;
						sfc.nand.size_spare = 0x200000;
						sfc.nand.pages_in_block = 256;
						sfc.nand.block_sz = 0x20000;
						sfc.nand.size_usable_fs = 0x1E0;
						break;
					default:
						printk(KERN_INFO "unknown T%i NAND size! (%x)\n", ((config >> 4) & 0x3), (config >> 4) & 0x3);
						return 0;
				}
				break;
			case 2: // MMC capable big block flash controller ie: 16M corona 000431c4
				switch ((config >> 4) & 0x3) 
				{
					case 0: // 16M
						sfc.nand.meta_type = META_TYPE_BOS;
						sfc.nand.is_bb_cont = 1;
						sfc.nand.size_dump = 0x1080000;
						sfc.nand.size_data = 0x1000000;
						sfc.nand.size_spare = 0x80000;
						sfc.nand.size_usable_fs = 0x3E0;
						break;
					case 1: // 64M
						sfc.nand.meta_type = META_TYPE_BOS;
						sfc.nand.is_bb_cont = 1;
						sfc.nand.size_dump = 0x4200000;
						sfc.nand.size_data = 0x4000000;
						sfc.nand.size_spare = 0x200000;
						sfc.nand.size_usable_fs = 0xF80;
						break;
					case 2: // Big Block
						sfc.nand.meta_type = META_TYPE_BG;
						sfc.nand.is_bb_cont = 1;
						sfc.nand.is_bb = 1;
						sfc.nand.size_dump = 0x4200000;
						sfc.nand.block_sz_phys = 0x21000;
						sfc.nand.size_data = 0x4000000;
						sfc.nand.size_spare = 0x200000;
						sfc.nand.pages_in_block = 256;
						sfc.nand.block_sz = 0x20000;
						sfc.nand.size_usable_fs = 0x1E0;
						break;
					//case 3: // big block, but with blocks twice the size of known big blocks above...
					//	break;
					default:
						printk(KERN_INFO "unknown T%i NAND size! (%x)\n", ((config >> 4) & 0x3), (config >> 4) & 0x3);
						return 0;
				}
				break;
			default:
				printk(KERN_INFO "unknown NAND type! (%x)\n", (config>>17)&3);
				return 0;
		}
	}

	sfc.nand.config_block = sfc.nand.size_usable_fs - CONFIG_BLOCKS;
	sfc.nand.blocks_count = sfc.nand.size_dump / sfc.nand.block_sz_phys;
	sfc.nand.pages_count = sfc.nand.blocks_count * sfc.nand.pages_in_block;

#ifdef DEBUG_OUT
	printk(KERN_INFO "is_bb_cont : %s\n", sfc.nand.is_bb_cont == 1 ? "TRUE":"FALSE");
	printk(KERN_INFO "is_bb     : %s\n", sfc.nand.is_bb == 1 ? "TRUE":"FALSE");
	printk(KERN_INFO "size_dump       : 0x%x\n", sfc.nand.size_dump);
	printk(KERN_INFO "block_sz      : 0x%x\n", sfc.nand.block_sz);
	printk(KERN_INFO "size_data         : 0x%x\n", sfc.nand.size_data);
	printk(KERN_INFO "size_spare        : 0x%x\n", sfc.nand.size_spare);
	printk(KERN_INFO "pages_in_block  : 0x%x\n", sfc.nand.pages_in_block);
	printk(KERN_INFO "size_write    : 0x%x\n", sfc.nand.size_write);
#endif
	return 1;
}

static int xenon_sfc_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	// static int printed_version;
	int rc;
	int pci_dev_busy = 0;
	unsigned long mmio_start;

	dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	pci_intx(pdev, 1);

	printk(KERN_INFO "attached to xenon SFC\n");

	mmio_start = pci_resource_start(pdev, 0);
	sfc.base = ioremap(mmio_start, SFC_SIZE);
	if (!sfc.base)
		goto err_out_regions;

	sfc.mappedflash = ioremap(MAP_ADDR, MAP_SIZE);
	if (!sfc.mappedflash)
		goto err_out_ioremap_base;

	init_waitqueue_head(&sfc.wait_q);
	spin_lock_init(&sfc.fifo_lock);
	
	if(_xenon_sfc_enum_nand() != 1) {
		printk(KERN_INFO "NAND Enumeration failed!\n");
		goto err_out_ioremap_map;
	}
	
	sfc.dmabuf = dma_alloc_coherent(&pdev->dev, DMA_SIZE, &sfc.dmaaddr, GFP_KERNEL);
	if (!sfc.dmabuf) {
		goto err_out_ioremap_map;
	}

	return 0;

err_out_ioremap_map:
	iounmap(sfc.mappedflash);
	
err_out_ioremap_base:
	iounmap(sfc.base);

err_out_regions:
	pci_release_regions(pdev);

err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;
}

static void xenon_sfc_remove(struct pci_dev *pdev)
{
	dma_free_coherent(&pdev->dev, DMA_SIZE, sfc.dmabuf, sfc.dmaaddr);
	iounmap(sfc.base);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver xenon_sfc_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= xenon_sfc_pci_tbl,
	.probe			= xenon_sfc_init_one,
	.remove			= xenon_sfc_remove
};


static int __init xenon_sfc_init(void)
{
	return pci_register_driver(&xenon_sfc_pci_driver);
}

static void __exit xenon_sfc_exit(void)
{
	pci_unregister_driver(&xenon_sfc_pci_driver);
}

module_init(xenon_sfc_init);
module_exit(xenon_sfc_exit);


MODULE_DESCRIPTION("Driver for Xenon System Flash Controller");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, xenon_sfc_pci_tbl);
