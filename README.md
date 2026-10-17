# Aoostar WTR PRO (AMD 5825u) SATA-Passthrough Fix

> [!WARNING]
> This fix has not been tested long-term yet. It was verified with benchmarks, VM restarts and host reboots on one machine. Use at your own risk and test on a testbench first. 

## Information

On the **AOOSTAR WTR Pro with a Ryzen 7 5825U**, passing **both** onboard SATA controllers (`1022:7901`) through to a VM (for example a TrueNAS or Unraid VM on Proxmox) makes the whole host slow. The CPU stops boosting, the `Tctl` temperature reading freezes, and single-thread performance drops by about half. Stopping the VM won't reset the bug; only a host reboot recovers it. Passing through only one of the two controllers works fine.

**Likely cause:** both controllers are functions of one PCIe device (`1022:7901`) and are the only devices behind the internal bridge `00:08.2`. When a VM owns every device behind that bridge, QEMU resets the whole bridge (bus reset) at VM start instead of resetting each controller individually. That bus reset hangs the SMU, the CPU's power-management firmware. With only one controller passed through, the bus reset isn't possible, so QEMU uses the harmless per-device power-cycle (PM) reset instead.

**Fix:** a small kernel module, `sata_nobusreset`, marks both controllers with the kernel's existing `PCI_DEV_FLAGS_NO_BUS_RESET` flag. The kernel then refuses the bus reset, and QEMU/vfio falls back to the PM reset.

| | Baseline (no passthrough) | Both passed through | Both passed through + fix |
|---|---|---|---|
| Max clock | 4384 MHz | 2396 MHz | 4347 MHz |
| sysbench single-thread | 5045 ev/s | 2714 ev/s | 4729 ev/s |
| sysbench all-thread | 31096 ev/s | 20528 ev/s | 31321 ev/s |
| Tctl under load | 58 → 73 °C | frozen at 51.5 °C | 65.6 → 73.5 °C |

## Install on Proxmox VE

Run everything as root on the Proxmox host. If the slowdown is currently active, reboot the host first.

### 1. Install kernel headers, build tools and DKMS

```bash
apt update
apt install proxmox-default-headers proxmox-headers-$(uname -r) build-essential dkms
```

`proxmox-default-headers` makes future kernel updates pull in matching headers, so DKMS can rebuild the module automatically.

### 2. Build and install the module with DKMS

```bash
git clone https://github.com/OdinsEntropy/aoostar-wtr-pro-5825u-sata-passthrough-fix.git
cd aoostar-wtr-pro-5825u-sata-passthrough-fix/module
dkms add "$(pwd)"
dkms install sata_nobusreset/1.0
```

### 3. Make sure it loads before vfio-pci

The module must set its flag **before** `vfio-pci` takes over the controllers, which happens when the VM starts. Load it at boot:

```bash
echo sata_nobusreset > /etc/modules-load.d/sata_nobusreset.conf
```

If you bind the controllers to `vfio-pci` already at boot (for example with `options vfio-pci ids=1022:7901`), also tell modprobe to load this module first (not tested by me):

```bash
echo "softdep vfio-pci pre: sata_nobusreset" > /etc/modprobe.d/sata_nobusreset.conf
update-initramfs -u -k all
```

Reboot Proxmox to load the module, then check that it's active:

```bash
lsmod | grep sata_nobusreset
dmesg | grep "bus reset disabled"   # one line per controller
dkms status                         # after kernel updates: built for the new kernel?
```

### Secure Boot

DKMS signs the module with its own self-signed key. With Secure Boot disabled, the kernel only logs `module verification failed ... tainting kernel`, which is harmless. With Secure Boot enabled, enroll the DKMS key (`mokutil --import /var/lib/dkms/mok.pub`, then confirm in the MOK manager on the next boot), or the module won't load.

### Uninstall

```bash
modprobe -r sata_nobusreset
dkms remove sata_nobusreset/1.0 --all
rm /etc/modules-load.d/sata_nobusreset.conf
```

Without the module, passing both controllers to a VM triggers the slowdown again.

## Test setup

- AOOSTAR WTR Pro (2026), AMD Ryzen 7 5825U, BIOS 0.33 (01/28/2026)
- Proxmox VE 9.2.21, kernel 7.0.14-24-pve
- TrueNAS VM (q35, OVMF), both controllers passed through as two separate `hostpci` entries
- verified beforehand that passing through only one controller works without the slowdown
- no drives attached to the controllers during testing

A future BIOS with a newer AGESA may fix the underlying bug and make this module unnecessary.

### How to test

Install the tools with `apt install sysbench stress-ng lm-sensors` and save this as `cputest.sh`:

```bash
#!/bin/bash
echo "single-thread: $(sysbench cpu --threads=1 --time=10 run | awk '/events per second/{print $4}') ev/s"
echo "multi-thread:  $(sysbench cpu --threads=$(nproc) --time=10 run | awk '/events per second/{print $4}') ev/s"
stress-ng --cpu 1 --timeout 10 >/dev/null &
sleep 1; t0=$(sensors | awk '/Tctl/{print $2}')
sleep 8
echo "max clock:     $(grep MHz /proc/cpuinfo | sort -k4 -n | tail -1 | awk '{print $4}') MHz"
echo "Tctl:          $t0 -> $(sensors | awk '/Tctl/{print $2}')"
wait
```

1. Run it on a freshly booted host with the VM stopped to get a baseline.
2. Pass both SATA controllers through to the VM, start it, wait a minute and run it again.
3. Restart the VM, then reboot the host, and run it again each time.

Healthy: the max clock is above 4 GHz, `Tctl` rises under load and the results are close to the baseline. Broken: the clock is stuck around 2.4 GHz, `Tctl` doesn't change and single-thread drops to about half. If you see the broken values, only a host reboot recovers it.

## Sources

- [Proxmox forum: WTR Pro CPU throttling](https://forum.proxmox.com/threads/wtr-pro-cpu-throttling.160039/page-2): the same issue reported by other WTR Pro owners. [Post #39](https://forum.proxmox.com/threads/wtr-pro-cpu-throttling.160039/post-831918) reports that a custom BIOS with an updated AGESA fixes it (the poster warns it may brick the device). The thread also mentions other workarounds such as `amd_pstate=passive` or passing through individual disks.
- [Linux kernel `drivers/pci/quirks.c`](https://github.com/torvalds/linux/blob/master/drivers/pci/quirks.c) (search for `quirk_no_bus_reset`): mainline Linux sets the same `PCI_DEV_FLAGS_NO_BUS_RESET` flag for devices with broken bus resets. One of them is the ASMedia ASM1164 SATA controller, which had a similar problem with passthrough on Proxmox ([forum thread](https://forum.proxmox.com/threads/problems-with-pcie-passthrough-with-two-identical-devices.149003/)). It also falls back to the PM reset.

## Disclaimer

This issue was debugged and the fix written with Opus 5.5/medium. While I checked the code and saw no issue, I'm a robotics engineer, not a Linux kernel expert. Use this at your own risk: I take no responsibility for any damage, data loss or other problems caused by using it. Not affiliated with AOOSTAR.
