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

#define DRV_NAME	"xenon_sfc"
#define DRV_VERSION	"0.1"

#define SFC_SIZE 0x400

struct xenon_sfc
{
	void __iomem *base;
	wait_queue_head_t wait_q;
	spinlock_t fifo_lock;

	unsigned long (*readreg)(int addr);
	void (*writereg)(int addr, unsigned long data);
	int (*readpage)(unsigned char *data, int addr, int raw);
	int (*writepage)(unsigned char *data, int addr);
	int (*eraseblock)(int addr);
};

static struct xenon_sfc sfc;

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

static int _xenon_sfc_readpage(unsigned char *data, int addr, int raw)
{
	int status;

	sfc.writereg(SFCX_STATUS, sfc.readreg(SFCX_STATUS));

	// Set flash address (logical)
	//address &= 0x3fffe00; // Align to page
	sfc.writereg(SFCX_ADDRESS, addr);

	// Command the read
	// Either a logical read (0x200 bytes, no meta data)
	// or a Physical read (0x210 bytes with meta data)
	sfc.writereg(SFCX_COMMAND, raw ? PHY_PAGE_TO_BUF : LOG_PAGE_TO_BUF);

	// Wait Busy
	while ((status = sfc.readreg(SFCX_STATUS)) & STATUS_BUSY);

	if (!SFCX_SUCCESS(status))
	{
		if (status & STATUS_BB_ER)
			printf(" ! SFCX: Bad block found at %08X\n", sfc.address_to_block(addr));
		else if (status & STATUS_ECC_ER)
		//	printf(" ! SFCX: (Corrected) ECC error at address %08X: %08X\n", address, status);
			status = status;
		else if (!raw && (status & STATUS_ILL_LOG))
			printf(" ! SFCX: Illegal logical block at %08X (status: %08X)\n", sfc.address_to_block(addr), status);
		else
			printf(" ! SFCX: Unknown error at address %08X: %08X. Please worry.\n", addr, status);
	}

	// Set internal page buffer pointer to 0
	sfc.writereg(SFCX_ADDRESS, 0);

	int i;
	int page_sz = raw ? sfc.page_sz_phys : sfc.page_sz;

	for (i = 0; i < page_sz ; i += 4)
	{
		// Transfer data from buffer to register
		sfc.writereg(SFCX_COMMAND, PAGE_BUF_TO_REG);

		// Read out our data through the register
		*(int*)(data + i) = __builtin_bswap32(sfc.readreg(SFCX_DATA));
	}

	return status;
}

static int _xenon_sfc_writepage(unsigned char *data, int addr)
{
	sfc.writereg(SFCX_STATUS, 0xFF);

	// Enable Writes
	sfc.writereg(SFCX_CONFIG, sfc.readreg(SFCX_CONFIG) | CONFIG_WP_EN);

	// Set internal page buffer pointer to 0
	sfc.writereg(SFCX_ADDRESS, 0);

	int i;
	for (i = 0; i < sfc.page_sz_phys; i+=4)
	{
		// Write out our data through the register
		sfc.writereg(SFCX_DATA, __builtin_bswap32(*(int*)(data + i)));

		// Transfer data from register to buffer
		sfc.writereg(SFCX_COMMAND, REG_TO_PAGE_BUF);
	}

	// Set flash address (logical)
	//address &= 0x3fffe00; // Align to page
	sfc.writereg(SFCX_ADDRESS, addr);

	// Unlock sequence (for write)
	sfc.writereg(SFCX_COMMAND, UNLOCK_CMD_0);
	sfc.writereg(SFCX_COMMAND, UNLOCK_CMD_1);

	// Wait Busy
	while (sfc.readreg(SFCX_STATUS) & STATUS_BUSY);

	// Command the write
	sfc.writereg(SFCX_COMMAND, WRITE_PAGE_TO_PHY);

	// Wait Busy
	while (sfc.readreg(SFCX_STATUS) & STATUS_BUSY);

	int status = sfc.readreg(SFCX_STATUS);
	if (!SFCX_SUCCESS(status))
		printf(" ! SFCX: Unexpected sfc.writepage status %08X\n", status);

	// Disable Writes
	sfc.writereg(SFCX_CONFIG, sfc.readreg(SFCX_CONFIG) & ~CONFIG_WP_EN);

	return status;
}

static int _xenon_sfc_eraseblock(int addr)
{
	// Enable Writes
	sfc.writereg(SFCX_CONFIG, sfc.readreg(SFCX_CONFIG) | CONFIG_WP_EN);
	sfc.writereg(SFCX_STATUS, 0xFF);

	// Set flash address (logical)
	//address &= 0x3fffe00; // Align to page
	sfc.writereg(SFCX_ADDRESS, addr);

	// Wait Busy
	while (sfc.readreg(SFCX_STATUS) & STATUS_BUSY);

	// Unlock sequence (for erase)
	sfc.writereg(SFCX_COMMAND, UNLOCK_CMD_1);
	sfc.writereg(SFCX_COMMAND, UNLOCK_CMD_0);

	// Wait Busy
	while (sfc.readreg(SFCX_STATUS) & STATUS_BUSY);

	// Command the block erase
	sfc.writereg(SFCX_COMMAND, BLOCK_ERASE);

	// Wait Busy
	while (sfc.readreg(SFCX_STATUS) & STATUS_BUSY);

	int status = sfc.readreg(SFCX_STATUS);
	//if (!SFCX_SUCCESS(status))
	//	printf(" ! SFCX: Unexpected sfc.erase_block status %08X\n", status);
	sfc.writereg(SFCX_STATUS, 0xFF);

	// Disable Writes
	sfc.writereg(SFCX_CONFIG, sfc.readreg(SFCX_CONFIG) & ~CONFIG_WP_EN);

	return status;
}

unsigned long xenon_sfc_readreg(int addr)
{
	return _xenon_sfc_readreg(addr);
}

void xenon_sfc_writereg(int addr, unsigned long data)
{
	return _xenon_sfc_writereg(addr, data);
}

int xenon_sfc_readpage_phy(unsigned char *data, int addr)
{
	return sfc.readpage(data, addr, 1);
}

int xenon_sfc_readpage_log(unsigned char *data, int addr)
{
	return sfc.readpage(data, addr, 0);
}

int xenon_sfc_writepage(unsigned char *data, int addr)
{
	return sfc.writepage(data, addr);
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

/*
	if (request_irq(pdev->irq, xenon_sfc_irq, IRQF_SHARED,
		"xenon-sfc", pdev)) {
		printk(KERN_ERR "xenon-sfc: request_irq failed\n");
		goto err_out_ioremap;
	}
*/

	sfc.readreg = _xenon_sfc_readreg;
	sfc.writereg = _xenon_sfc_writereg;
	sfc.readpage = _xenon_sfc_readpage;
	sfc.writepage = _xenon_sfc_writepage;
	sfc.eraseblock = _xenon_sfc_eraseblock;
	
	return 0;

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
	sfc.eraseblock = NULL;
	sfc.emmc = NULL;
	sfc.flashconfig = NULL;


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
