// SPDX-License-Identifier: GPL-2.0
/*
 * Mark the AMD FCH SATA controllers (1022:7901) as not bus-resettable.
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

#define FCH_SATA_DEVICE_ID 0x7901

static int __init sata_nobusreset_init(void)
{
	struct pci_dev *pdev = NULL;
	int n = 0;

	while ((pdev = pci_get_device(PCI_VENDOR_ID_AMD, FCH_SATA_DEVICE_ID, pdev))) {
		pdev->dev_flags |= PCI_DEV_FLAGS_NO_BUS_RESET;
		pci_info(pdev, "sata_nobusreset: bus reset disabled\n");
		n++;
	}

	return n ? 0 : -ENODEV;
}

static void __exit sata_nobusreset_exit(void)
{
	struct pci_dev *pdev = NULL;

	while ((pdev = pci_get_device(PCI_VENDOR_ID_AMD, FCH_SATA_DEVICE_ID, pdev))) {
		pdev->dev_flags &= ~PCI_DEV_FLAGS_NO_BUS_RESET;
		pci_info(pdev, "sata_nobusreset: bus reset re-enabled\n");
	}
}

module_init(sata_nobusreset_init);
module_exit(sata_nobusreset_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Disable bus reset for AMD FCH SATA (SMU hang workaround)");
