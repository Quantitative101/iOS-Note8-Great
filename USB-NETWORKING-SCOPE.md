# USB networking on the Note 8: scope (nothing started)

Goal: the phone shows up on the Mac as a USB Ethernet device, so there is a shell and file transfer without TWRP.

## What exists on the hardware (from ref/note8-hw6.dts and Samsung's universal8895 sources)
- DWC3 USB 3.0 dual-role controller at 0x10C00000 (IRQ SPI 0x151), clocks "aclk" (CMU id 0x2ca) and "sclk" (0x2bf).
- USB 2/3 PHY ("samsung,exynos8895-usbdrd-phy") at 0x10E00000 and 0x10E10000, ref-clock select at 0x10E50070, PMU syscon for PHY power, pin config `pinctrl-0 0xea`, and a PHY-port gpio (phandle 0x8a, pin 7; bank not yet identified).
- Cable detection and VBUS go through the MAX77865 MUIC at I2C 0x66 (the same chip family as the gauge). The device-mode decision is software.
- Android ran it in `dr_mode = "otg"`, super-speed. For us, high-speed device mode is enough.

## Pieces needed, in dependency order
1. **Clocks**: the USB clock gates are probably closed, as they were for HSI2C. Offsets are derivable from `cmucal-node.c` / `cmucal-sfr.h` in Samsung's cal-if (CLK_CON_GAT_* for USBDRD30 in the FSYS0 CMU). Risk: a wrong write hangs the bus; needs read-only probing first, like the ACPM probe.
2. **PHY bring-up**: the register sequence in `phy-exynos-usbdrd.c` plus the SoC-specific tuning tables (exynos8895 cal). About 100 register writes, order matters. PMU power-on bit for the PHY (PMU 0x16480000, offset in the usbdrd node's syscon).
3. **DWC3 in device mode**: core reset, GCTL/DCFG, event buffer, endpoint 0 control state machine (setup/data/status TRBs), then bulk IN/OUT endpoints. About 1000 lines of C. This is the largest piece, and there is no iOS/XNU open-source driver to adapt (Apple's DWC3 gadget stack is closed). The Linux dwc3 gadget code is the reference.
4. **USB function**: CDC-ECM (macOS loads it with no driver) or RNDIS-free CDC-NCM. Descriptors are small.
5. **XNU side**: register an `ifnet` (kpi_interface, `ifnet_allocate`) named en-something, push frames between it and the bulk endpoints. IP config at boot from the drop-zone `autorun` script, which launchd already tries to spawn (`/private/var/herolte/autorun`).
6. **A shell on the phone**: the iOS root has no sshd. Needs a statically built dropbear (or a tiny TCP shell) for arm64 iOS, built with the Xcode SDK, put in the drop zone.
7. **Host side**: macOS shows a new interface; give it a link-local or static IP.

## Risks and unknowns
- Steps 1 and 2 are blind hardware bring-up with no documentation beyond Samsung's Linux sources. Expect 2 to 4 flash cycles just to see the controller alive (read DWC3 `GSNPSID` at 0x10C0C120 as the first proof).
- Power/VBUS: if the MUIC doesn't route the data lines to the SoC in our state, the PHY works but the Mac sees nothing. Samsung's MUIC driver configures this.
- The same cable carries charging; device mode must not break that.

## Rough effort
Read-only probe (clock gate, PHY power, GSNPSID): one flash cycle. Controller + enumeration as a bare USB device the Mac sees: 3 to 4 cycles. Working ECM link with ping: 2 to 3 more. A shell on top: 1 more plus the dropbear build.

## Cheaper options that already exist
- Persistent kernel log and drop zone, pulled from TWRP with `note8/tools/log-pull.py` and `drop.py`.
- Status lines on the panel.
- If only logs are wanted live, a USB serial-style function (CDC-ACM) is the same DWC3 work without the network stack.

## Recommendation
Do the read-only probe first (like greatlte_acpm=1): map the CMU gate, report `GSNPSID` and PHY state on status lines. If the controller reads back, the rest is mostly known work. If not, stop there at low cost.
