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
- Touch (sec_ts over bit-banged I2C, with bus recovery), power and volume keys, battery level
- Wi-Fi (BCM4361 over PCIe): open and WPA2 networks; WPA3 is not supported by the firmware
- Mali-G71 runs jobs; Mesa Panfrost draws through a small shim (slower than the CPU path for the UI for now)

Not working: cellular, Bluetooth, audio, camera, sensors, USB device mode, CPU frequency scaling (ACPM accepts requests
but the clocks stay at the bootloader's ~1.05 GHz).

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
