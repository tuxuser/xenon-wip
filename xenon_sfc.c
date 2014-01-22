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

#define DRV_NAME	"xenon_sfc"
#define DRV_VERSION	"0.1"

#define SFC_SIZE 0x400
#define DMA_SIZE 0x10000

#define DEBUG_OUT 1

struct xenon_sfc
{
	void __iomem *base;
	wait_queue_head_t wait_q;
	spinlock_t fifo_lock;
	dma_addr_t dmaaddr;
	unsigned char *dmabuf;

	unsigned long (*readreg)(int addr);
	void (*writereg)(int addr, unsigned long data);
	int (*readpage)(unsigned char data, int page, int raw);
	int (*writepage)(unsigned char data, int page);
	int (*readblock)(unsigned char data, int block);
	int (*writeblock)(unsigned char data, int block);
	int (*eraseblock)(int block);
};

struct xenon_nand
{
	bool is_bb_cont;
	bool is_bb;
	int meta_type;

	int page_sz;
	int page_sz_phys;
	int meta_sz;

	int pages_in_block;
	int block_sz;
	int block_sz_phys;

	int pages_count;
	int blocks_count;

	int size_spare;
	int size_data;
	int size_dump;
	int size_write;

	int size_usable_fs;
	int config_block;
}

static struct xenon_sfc sfc;
static struct xenon_nand nand = {0};



static const struct pci_device_id xenon_sfc_pci_tbl[] = {
	{ PCI_VDEVICE(MICROSOFT, /* TODO - fill it in*/), 0 },
	{ }	/* terminate list */
};

static inline unsigned long _xenon_sfc_readreg(int addr)
{
	return __builtin_bswap32(*(volatile unsigned int*)(sfc.base | addr));
}

static inline void _xenon_sfc_writereg(int addr, unsigned long data)
{
	*(volatile unsigned int*)(sfc.base | addr) = __builtin_bswap32(data);
}

static int _xenon_sfc_readpage(unsigned char data, int page, int raw)
{
	
	int addr = page * nand.page_sz;
	unsigned char* nbCur = data;
	
	int sta;

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
		if (sta & STATUS_BB_ER)
			printk(KERN_INFO " ! SFCX: Bad block found at %08X\n", sfc.address_to_block(addr));
		else if (status & STATUS_ECC_ER)
		//	printf(" ! SFCX: (Corrected) ECC error at address %08X: %08X\n", address, status);
			status = status;
		else if (!raw && (status & STATUS_ILL_LOG))
			printk(KERN_INFO " ! SFCX: Illegal logical block at %08X (status: %08X)\n", sfc.address_to_block(addr), status);
		else
			printk(KERN_INFO " ! SFCX: Unknown error at address %08X: %08X. Please worry.\n", addr, status);
	}

	// Set internal page buffer pointer to 0
	_xenon_sfc_writereg(SFCX_ADDRESS, 0);

	int i;
	int page_sz = raw ? nand.page_sz_phys : nand.page_sz;

	for (i = 0; i < page_sz ; i += 4)
	{
		// Transfer data from buffer to register
		_xenon_sfc_writereg(SFCX_COMMAND, PAGE_BUF_TO_REG);

		// Read out our data through the register
		*(int*)(nbCur + i) = __builtin_bswap32(_xenon_sfc_readreg(SFCX_DATA));
	}

	return status;
}

static int _xenon_sfc_writepage(unsigned char data, int page)
{
	int addr = page * nand.page_sz;
	unsigned char* nbCur = data;
	
	_xenon_sfc_writereg(SFCX_STATUS, 0xFF);

	// Enable Writes
	_xenon_sfc_writereg(SFCX_CONFIG, _xenon_sfc_readreg(SFCX_CONFIG) | CONFIG_WP_EN);

	// Set internal page buffer pointer to 0
	_xenon_sfc_writereg(SFCX_ADDRESS, 0);

	int i;
	for (i = 0; i < nand.page_sz_phys; i+=4)
	{
		// Write out our data through the register
		_xenon_sfc_writereg(SFCX_DATA, __builtin_bswap32(*(int*)(nbCur + i)));

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

	int status = _xenon_sfc_readreg(SFCX_STATUS);
	if (!SFCX_SUCCESS(status))
		printk(KERN_INFO " ! SFCX: Unexpected sfc.writepage status %08X\n", status);

	// Disable Writes
	_xenon_sfc_writereg(SFCX_CONFIG, _xenon_sfc_readreg(SFCX_CONFIG) & ~CONFIG_WP_EN);

	return status;
}

// returns 1 on bad block, 2 on unrecoverable ECC
// data NULL to skip keeping data
static int _xenon_sfc_readblock(unsigned char data, int block)
{
	int addr = block * nand.block_sz;
	
	unsigned char* nbCur = data;
	unsigned int sta, bRead;
	unsigned int curAddr = addr;
	
	if(data != NULL)
		ZeroMemory(data, pagesPerBlock*nand.page_sz_phys);

	for(bRead = 0; bRead < w_writeSize; bRead += 0x2000, curAddr += 0x2000)
	{
		_xenon_sfc_writereg(SFCX_STATUS, _xenon_sfc_readreg(SFCX_STATUS));
		_xenon_sfc_writereg(SFCX_DATAPHYADDR, sfc.dmaaddr);
		_xenon_sfc_writereg(SFCX_SPAREPHYADDR, sfc.dmaaddr+0xC000);
		_xenon_sfc_writereg(SFCX_ADDRESS, curAddr);
		_xenon_sfc_writereg(SFCX_COMMAND, DMA_PHY_TO_RAM);
		
		while(sta = _xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);
		
		sta = _xenon_sfc_readreg(SFCX_STATUS);
		if(sta&STATUS_ERROR)
		{
			printk(KERN_INFO "error in status, %08x\n", sta);
			if(sta&STATUS_BB_ER)
			{
				printk(KERN_INFO "Bad block error block 0x%x\n", block);
				return 1;
			}
			if(STSCHK_ECC_ERR(sta))
			{
				printk(KERN_INFO "unrecoverable ECC error block 0x%x\n", block);
				return 2;
			}
		}
		if(data != NULL)
		{
			for(DWORD i = 0; i < 16; i++)
			{
				memcpy(nbCur, &sfc.dmabuf[i*nand.page_sz], nand.page_sz);
				memcpy(&nbCur[nand.page_sz], &sfc.dmabuf[0xC000+(i*nand.meta_sz)], nand.meta_sz);
				nbCur += nand.page_sz_phys;
			}
		}
	}
	return 0;
}

static int _xenon_sfc_writeblock(unsigned char data, int block)
{
	int addr = block * nand.block_sz;
	
	unsigned char* nbCur = data;
	unsigned int sta, i, bWrote;
	unsigned int curAddr = addr;
	
	// one erase per block
	sta = _xenon_sfc_eraseblock(addr);
	if(sta&STATUS_ERROR)
		printk(KERN_INFO "error in erase status, %08x\n", sta);

	if(data != NULL)
	{
		for(bWrote = 0; bWrote < w_writeSize; bWrote += 0x2000, curAddr += 0x2000)
		{
			for(i = 0; i < 16; i++)
			{
				memcpy(&sfc.dmabuf[i*nand.page_sz], nbCur, nand.page_sz);
				memcpy(&sfc.dmabuf[0xC000+(i*nand.meta_sz)], &nbCur[nand.page_sz], nand.meta_sz);
				nbCur += nand.page_sz_phys;
			}
			_xenon_sfc_writereg(SFCX_STATUS, _xenon_sfc_readreg(SFCX_STATUS));
			_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_0);
			_xenon_sfc_writereg(SFCX_COMMAND, UNLOCK_CMD_1);
			_xenon_sfc_writereg(SFCX_DATAPHYADDR, sfc.dmaaddr);
			_xenon_sfc_writereg(SFCX_SPAREPHYADDR, sfc.dmaaddr+0xC000);
			_xenon_sfc_writereg(SFCX_ADDRESS, curAddr);
			_xenon_sfc_writereg(SFCX_COMMAND, DMA_RAM_TO_PHY);
			
			while(sta = _xenon_sfc_readreg(SFCX_STATUS) & STATUS_BUSY);
			
			sta = _xenon_sfc_readreg(SFCX_STATUS);
			if(sta&STATUS_ERROR)
				printk(KERN_INFO "error in status, %08x\n", sta);
		}
	}
	return 0;
}

static int _xenon_sfc_eraseblock(int block)
{
	int addr = block * nand.block_sz;
	
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

	int status = _xenon_sfc_readreg(SFCX_STATUS);
	//if (!SFCX_SUCCESS(status))
	//	printf(" ! SFCX: Unexpected sfc.erase_block status %08X\n", status);
	_xenon_sfc_writereg(SFCX_STATUS, 0xFF);

	// Disable Writes
	_xenon_sfc_writereg(SFCX_CONFIG, _xenon_sfc_readreg(SFCX_CONFIG) & ~CONFIG_WP_EN);

	return status;
}

int _xenon_sfc_isMMCdevice(void)
{
	unsigned long eMMC = _xenon_sfc_readreg(SFCX_PHISON);
	if (eMMC != 0)
		return 1;
	else
		return 0;
}

unsigned long xenon_sfc_readreg(int addr)
{
	return sfc.readreg(addr);
}

void xenon_sfc_writereg(int addr, unsigned long data)
{
	return sfc.writereg(addr, data);
}

int xenon_sfc_readpage_phy(unsigned char data, int page)
{
	return sfc.readpage(data, page, 1);
}

int xenon_sfc_readpage_log(unsigned char data, int page)
{
	return sfc.readpage(data, page, 0);
}

int xenon_sfc_writepage(unsigned char data, int page)
{
	return sfc.writepage(data, page);
}



static int _xenon_sfc_enum_nand(void)
{
	nand.pages_in_block = 32;
	nand.meta_sz = 0x10;
	nand.page_sz = 0x200;
	nand.page_sz_phys = nand.page_sz + nand.meta_sz;

	if(_xenon_sfc_isMMCdevice()) // corona MMC
	{
		nand.meta_type = META_TYPE_NONE;
		nand.block_sz = 0x4000;
		nand.block_sz_phys = 0x20000;
		nand.meta_sz = 0;
		
		nand.size_dump = 0x3000000;
		nand.size_data = 0x3000000;
		nand.size_spare = 0;
		nand.size_usable_fs = 0xC00;

#ifdef DEBUG_OUT
		printk(KERN_INFO "MMC console detected\n");
#endif
	}
	else
	{
		configSave = _xenon_sfc_readreg(SFCX_CONFIG);

		// defaults for 16/64M small block
		nand.meta_type = META_TYPE_SM;
		nand.block_sz = 0x4000;
		nand.block_sz_phys = 0x4200;
		nand.is_bb = FALSE;
		nand.is_bb_cont = FALSE;

#ifdef DEBUG_OUT
		printk(KERN_INFO "SFC config %08x ver: %d type: %d\n", configSave, ((configSave>>17)&3), ((configSave >> 4) & 0x3));
#endif

		switch((configSave>>17)&3) // 00043000
		{
			case 0: // small block flash controller
				switch ((configSave >> 4) & 0x3)
				{
// 					 case 0: // 8M, not really supported
// 						nand.size_dump = 0x840000;
// 						nand.size_data = 0x800000;
// 						nand.size_spare = 0x40000;
// 						break;
					case 1: // 16MB
						nand.size_dump = 0x1080000;
						nand.size_data = 0x1000000;
						nand.size_spare = 0x80000;
						nand.size_usable_fs = 0x3E0;
						break;
// 					 case 2: // 32M, not really supported
// 						nand.size_dump = 0x2100000;
// 						nand.size_data = 0x2000000;
// 						nand.size_spare = 0x100000;
						nand.size_usable_fs = 0x7C0;
// 						break;
					case 3: // 64MB
						nand.size_dump = 0x4200000;
						nand.size_data = 0x4000000;
						nand.size_spare = 0x200000;
						nand.size_usable_fs = 0xF80;
						break;
					default:
						printk(KERN_INFO ("unknown T%s NAND size! (%x)\n", ((configSave >> 4) & 0x3), (configSave >> 4) & 0x3);
						return FALSE;
				}
				break;
			case 1: // big block flash controller
				switch ((configSave >> 4) & 0x3)// TODO: FIND OUT FOR 64M!!! IF THERE IS ONE!!!
				{
					case 1: // Small block 16MB setup
						nand.meta_type = META_TYPE_BOS;
						nand.is_bb_cont = TRUE;
						nand.size_dump = 0x1080000;
						nand.size_data = 0x1000000;
						nand.size_spare = 0x80000;
						nand.size_usable_fs = 0x3E0;
						break;
					case 2: // Large Block: Current Jasper 256MB and 512MB
						nand.meta_type = META_TYPE_BG;
						nand.is_bb_cont = TRUE;
						nand.is_bb = TRUE;
						nand.size_dump = 0x4200000;
						nand.block_sz_phys = 0x21000;
						nand.size_data = 0x4000000;
						nand.size_spare = 0x200000;
						nand.pages_in_block = 256;
						nand.block_sz = 0x20000;
						nand.size_usable_fs = 0x1E0;
						break;
					default:
						printk(KERN_INFO "unknown T%s NAND size! (%x)\n", ((configSave >> 4) & 0x3), (configSave >> 4) & 0x3);
						return FALSE;
				}
				break;
			case 2: // MMC capable big block flash controller ie: 16M corona 000431c4
				switch ((configSave >> 4) & 0x3) 
				{
					case 0: // 16M
						nand.meta_type = META_TYPE_BOS;
						nand.is_bb_cont = TRUE;
						nand.size_dump = 0x1080000;
						nand.size_data = 0x1000000;
						nand.size_spare = 0x80000;
						sfc.size_usable_fs = 0x3E0;
						break;
					case 1: // 64M
						nand.meta_type = META_TYPE_BOS;
						nand.is_bb_cont = TRUE;
						nand.size_dump = 0x4200000;
						nand.size_data = 0x4000000;
						nand.size_spare = 0x200000;
						nand.size_usable_fs = 0xF80;
						break;
					case 2: // Big Block
						nand.meta_type = META_TYPE_BG;
						nand.is_bb_cont = TRUE;
						nand.is_bb = TRUE;
						nand.size_dump = 0x4200000;
						nand.block_sz_phys = 0x21000;
						nand.size_data = 0x4000000;
						nand.size_spare = 0x200000;
						nand.pages_in_block = 256;
						nand.block_sz = 0x20000;
						nand.size_usable_fs = 0x1E0;
						break;
					//case 3: // big block, but with blocks twice the size of known big blocks above...
					//	break;
					default:
						printk(KERN_INFO "unknown T%s NAND size! (%x)\n", ((configSave >> 4) & 0x3), (configSave >> 4) & 0x3);
						return FALSE;
				}
				break;
			default:
				printk(KERN_INFO "unknown NAND type! (%x)\n", (configSave>>17)&3);
				return FALSE;
		}
	}

	nand.config_block = nand.size_usable_fs - CONFIG_BLOCKS;
	nand.blocks_count = nand.size_dump / nand.block_sz_phys;
	nand.pages_count = nand.blocks_count * nand.pages_in_block;

#ifdef DEBUG_OUT
	printk(KERN_INFO "is_bb_cont : %s\n", nand.is_bb_cont == TRUE ? "TRUE":"FALSE");
	printk(KERN_INFO "is_bb     : %s\n", nand.is_bb == TRUE ? "TRUE":"FALSE");
	printk(KERN_INFO "size_dump       : 0x%x\n", nand.size_dump);
	printk(KERN_INFO "block_sz      : 0x%x\n", nand.block_sz);
	printk(KERN_INFO "size_data         : 0x%x\n", nand.size_data);
	printk(KERN_INFO "size_spare        : 0x%x\n", nand.size_spare);
	printk(KERN_INFO "pages_in_block  : 0x%x\n", nand.pages_in_block);
	printk(KERN_INFO "size_write    : 0x%x\n", nand.size_write);
#endif
	return TRUE;
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

	init_waitqueue_head(&sfc.wait_q);
	spin_lock_init(&sfc.fifo_lock);
	
	if(_xenon_sfc_enum_nand() != TRUE) {
		printk(KERN_INFO "NAND Enumeration failed!\n");
		goto err_out_ioremap;
	}
	
	sfc.dmabuf = dma_alloc_coherent(&pdev->dev, DMA_SIZE, &sfc.dmaaddr, GFP_KERNEL);
	if (!sfc.dmabuf) {
		goto err_zero_struct;
	}
	

	sfc.readreg = _xenon_sfc_readreg;
	sfc.writereg = _xenon_sfc_writereg;
	sfc.readpage = _xenon_sfc_readpage;
	sfc.writepage = _xenon_sfc_writepage;
	sfc.readblock = _xenon_sfc_readblock;
	sfc.writeblock = _xenon_sfc_writeblock;
	sfc.eraseblock = _xenon_sfc_eraseblock;
	
	return 0;

err_zero_struct:
	nand = {0};

err_out_ioremap:
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
	sfc.readreg = NULL;
	sfc.writereg = NULL;
	sfc.readpage = NULL;
	sfc.writepage = NULL;
	sfc.readblock = NULL;
	sfc.writeblock = NULL;
	sfc.eraseblock = NULL;

	dma_free_coherent(&pdev->dev, DMA_SIZE, sfc.dmabuf, sfc.dmaaddr);
	iounmap(sfc.base);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver xenon_smc_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= xenon_sfc_pci_tbl,
	.probe			= xenon_sfc_init_one,
	.remove			= xenon_sfc_remove
};


static int __init xenon_sfc_init(void)
{
	return pci_register_driver(&xenon_sfc_pci_driver);
}

static void __exit xenon_smc_exit(void)
{
	pci_unregister_driver(&xenon_sfc_pci_driver);
}

module_init(xenon_sfc_init);
module_exit(xenon_sfc_exit);


MODULE_DESCRIPTION("Driver for Xenon System Flash Controller");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, xenon_sfc_pci_tbl);
