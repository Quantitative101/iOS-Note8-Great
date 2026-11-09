# iOS-Note8-Great

iOS 16 for the Galaxy Note 8 N950F

Apple's XNU kernel (open-source `xnu-8796.141.3`) and the iOS 16.7.x userland of an iPhone X, booting on a Samsung Galaxy Note 8
(SM-N950F, `greatlte`, Exynos 8895). It is an experiment, not something to run on a phone you need.

This repository has only code we wrote or that is already open source. Nothing from Apple's or Samsung's binaries is here:
no IPSW contents, device trees, boot images, kernels, firmware or bootloader pieces. You supply those yourself.

## What works

- Boot through S-Boot with a small loader (`loader/`) appended to the stock boot image, all 8 CPUs
- UFS storage, root on `userdata` (HFS+), launchd, backboardd, SpringBoard home screen
- Panel through IOMobileFramebuffer on a 750x1542 logical canvas, scaled to 1440x2960 by the CPU (parallel bands)
- Tear-free display with `sweetfb_flip=1`: frames are drawn into a second panel buffer, then IDMA0's base address changes
  (0x128b1000 +0x40) and DECON_F +0x60 requests the update
- Scheduler, timer and frame-rate tuning ported from the Galaxy S7 port (UI threads to fast cores, no timer coalescing,
  CoreAnimation told 120 Hz, widget CPU cap, SpringBoard frame-rate caps raised)
- Touch (sec_ts over bit-banged I2C, with bus recovery), power and volume keys, battery level
- Wi-Fi (BCM4361 over PCIe): open and WPA2 networks; WPA3 is not supported by the firmware
- Mali-G71 runs jobs; Mesa Panfrost draws through a small shim (slower than the CPU path for the UI for now)

- CPU clocks above the bootloader's: Mongoose 1703 MHz and A53 1690 MHz through Samsung's ACPM firmware, with a thermal
  guard that steps the clocks down when the SoC gets hot (see below)
- Idle CPU use cut by stopping a HAL plug-in lookup loop in mediaserverd (`autorun/`)

iPhone 8 style navigation is the stable setup: an empty `gestures.off` in the drop zone skips the X-style gesture hooks,
whose top-edge Control Center pull crashes SpringBoard; the Bixby key is Home. App install from the drop zone
(`tools/drop.py app=FILE.ipa|DIR.app`) copies the app to `/Applications`, but registering it with LaunchServices still fails.

Not working: cellular, Bluetooth, audio, camera, sensors, USB device mode, joining networks from iOS Settings
(the kernel joins the network given in a drop-zone file instead).

## CPU clocks and thermal guard

S-Boot leaves the Mongoose cores (cpus 4-7) at about 1065 MHz and the A53 cores (cpus 0-3) at 1053 MHz. On the
Exynos 8895 the CPU PLLs and rails belong to the ACPM co-processor, and its stock firmware only answers clock requests
once Android's DVFS plug-in is attached. With boot argument `greatlte_cpu=MHZ` (Mongoose) and optionally
`greatlte_cpu_a53=MHZ` the kernel copies that plug-in into ACPM's SRAM, attaches it, and requests the rates (ACPM's
DVFS manager, channel 1, domain 2 = Mongoose, 3 = A53; rates from the ECT tables). ACPM picks the voltage itself.

The plug-in is Samsung firmware and is **not** in this repository: it is built into the TWRP/stock kernel for this phone
as `exynos8895_acpm_dvfs.fw` (0x5c00 bytes). Extract it from your own kernel image and put it in the drop zone at
`/private/var/herolte/acpm/dvfs.fw` (`tools/drop.py`). Without it the plug-in is not loaded and the requests change nothing.

The thermal guard (`gt_cpu_guard`, runs from the touch loop once a second) reads the TMU at `0x10080000` (per-sensor
trims at 0x00-0x14, 9-bit codes at 0x40-0x48) and takes the hottest sensor:

| Hottest sensor | Clocks |
|---|---|
| >= 75 C | Mongoose eased to 1469 MHz |
| >= 85 C | both clusters back to the bootloader's rates (1066 / 1053 MHz) |
| < 65 C | back to the requested rates |

2314 MHz on the Mongoose cluster works but reaches 86 C within seconds of boot and then keeps cycling, so the
everyday setting is 1703 / 1690 MHz. The boot arguments used on the test phone (image v130):

```
debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 ufsroot=USERDATA ufsrw=1 rootrw=1
cs_enforcement_disable=1 unentitled_ios_sim_launch=1 ignition_level=1 launchd_unsecure_cache=1 wdt=-1 -wdt_no_pmu
wdt_test=no_user_checkin herotouch=0 herofg=0 greatlte_touch=3 a5fb_noconsole=1 sweetfb_scale=1 sweetfb_lw=750
sweetfb_lh=1542 herosb=1 greatlte_fg=1 a5fb_nospan=1 greatlte_wifi=1 sweetfb_hz=30 greatlte_cpu=1703 greatlte_cpu_a53=1690
```

(one line; S-Boot cuts the payload command line at about 608 characters, extra arguments are silently lost).
Running the CPUs faster makes the phone hotter; this is an experiment, use it at your own risk.

Diagnostics: `greatlte_prof=1` (per-process CPU samples in the kernel log), `greatlte_pcprof=1` (user PC samples,
symbolized with `tools/pcprof-sym.sh` against your own root's shared cache), `tools/twrp-rails.sh` (CPU/GPU rail
voltages and PMIC registers from TWRP), `tools/acpm-run.sh` (timed ACPM experiments).

## Layout

| Path | What |
|---|---|
| `xnu/note8-xnu-8796.141.3.patch` | All kernel changes, against apple-oss-distributions/xnu tag `xnu-8796.141.3` |
| `loader/` | Bare-metal stage appended to the boot image: puts XNU, the device tree and boot args in place |
| `probe/` | Early hardware probe image used during bring-up |
| `tools/` | Device tree (ADT) and payload builders, boot image packer, drop-zone writer, log puller, unattended test runners |
| `autorun/` | Root helper run once per boot: launchd disabled-service lists, preference fixes |
| `wallpaper/`, `heroapp/` | Small injected libraries (wallpaper, app helpers) |
| `heropresent/` | Experimental GPU present helper |

## Building

The kernel builds with [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) and a macOS KDK:
apply the patch to a checkout of `xnu-8796.141.3`, then
`KERNEL_CONFIG=DEVELOPMENT MACHINE_CONFIG=HEROLTE MACOS_VERSION=13.5 ./build.sh`.
`tools/mkadt_greatlte.py` turns an iPhone device tree from your own IPSW into the Note 8's, `tools/mkpayload.py` packs
kernel + device tree + boot args, and `herolte-pack-boot.py` appends that to your phone's own stock `BOOT.img`.

## Credits

- [wardevsa/ios-a5xelte](https://github.com/wardevsa/ios-a5xelte) (`herolte` branch): the Galaxy S7 port this one grew out of,
  including the framebuffer, HID, storage and GPU groundwork.
- [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build): the XNU build setup.
- Apple's open-source XNU, HFS, libpthread and IOHIDFamily; BearSSL; Mesa.

## License

See `LICENSE`. The kernel patch modifies and adds to XNU and other Apple open-source projects and is under the
Apple Public Source License 2.0 like them (BearSSL files keep their MIT license). Everything else is MIT.
iOS, iPhone and XNU are Apple trademarks; Galaxy is a Samsung trademark. This project is not affiliated with either.
