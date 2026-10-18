// SPDX-License-Identifier: GPL-2.0
/*
 * Mark the AMD FCH SATA controllers as not bus-resettable.
 *
 * On the AOOSTAR WTR Pro (Ryzen 7 5825U) both SATA functions sit alone behind
 * bridge 00:08.2. When both are passed through, QEMU/vfio prefers a secondary
 * bus reset of that bridge, which hangs the SMU (no boost, frozen Tctl) until
 * the next host reboot. With this flag set, vfio falls back to the PM reset.
 *
 * Must be loaded before vfio-pci binds the devices.
 */
#include <linux/module.h>
#include <linux/pci.h>

/* AMD FCH SATA device IDs, add yours here (lspci -nn | grep -i sata) */
static const unsigned short fch_sata_device_ids[] = {
	0x7901,	/* WTR Pro 5825u */
	0x7904,	/* WTR Pro 5825u user report */
};

static int __init sata_nobusreset_init(void)
{
	struct pci_dev *pdev;
	int i, n = 0;

	for (i = 0; i < ARRAY_SIZE(fch_sata_device_ids); i++) {
		pdev = NULL;
		while ((pdev = pci_get_device(PCI_VENDOR_ID_AMD, fch_sata_device_ids[i], pdev))) {
			pdev->dev_flags |= PCI_DEV_FLAGS_NO_BUS_RESET;
			pci_info(pdev, "sata_nobusreset: bus reset disabled\n");
			n++;
		}
	}

	return n ? 0 : -ENODEV;
}

static void __exit sata_nobusreset_exit(void)
{
	struct pci_dev *pdev;
	int i;

	for (i = 0; i < ARRAY_SIZE(fch_sata_device_ids); i++) {
		pdev = NULL;
		while ((pdev = pci_get_device(PCI_VENDOR_ID_AMD, fch_sata_device_ids[i], pdev))) {
			pdev->dev_flags &= ~PCI_DEV_FLAGS_NO_BUS_RESET;
			pci_info(pdev, "sata_nobusreset: bus reset re-enabled\n");
		}
	}
}

module_init(sata_nobusreset_init);
module_exit(sata_nobusreset_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Disable bus reset for AMD FCH SATA (SMU hang workaround)");
