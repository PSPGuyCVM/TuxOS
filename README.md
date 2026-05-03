# TuxOS

**A tiny operating system written from scratch – boots into a simple shell.**

## About

TuxOS is a minimal 32‑bit operating system for x86.  
It has its own bootloader, a VGA text‑mode console, PS/2 keyboard driver with shift support, and a command‑line shell.

**Version:** Early 0.1 (development preview – not for public use)

## Features

- Custom bootloader (sector 1) that loads the kernel and switches to protected mode
- Write‑direct‑to‑VGA text console (80×25, 16 colours)
- PS/2 keyboard input with uppercase/lowercase and backspace
- Built‑in shell with commands:
  - `help` – list available commands
  - `whoami` – prints `root`
  - `echo <text>` – print text back
  - `clear` – clear the screen
  - `uname` – prints `TuxOS`
  - `date` – shows a hardcoded date (real clock coming soon)
  - `ls`, `pwd` – fake filesystem placeholders
  - `ver` – version information
  - `tux` – draws a small ASCII penguin
  - `shutdown` – power off (QEMU/Bochs/VirtualBox)
  - `reboot` – reboot the machine
- APM shutdown fallback
- QEMU and real‑hardware bootable (via floppy image)

## Building

### Requirements

- Linux (or WSL on Windows)
- `nasm` (assembler)
- `gcc` with 32‑bit support (`gcc‑multilib` or cross‑compiler)
- `ld` (GNU linker)
- `make`
- `qemu-system-i386` (for testing)

Install on Debian/Ubuntu:

```bash
sudo apt update
sudo apt install nasm gcc-multilib build-essential qemu-system-x86 make
```

### Build & Run

```bash
make clean
make
make run
```

This assembles the bootloader, compiles the kernel, creates a 1.44 MB floppy image (`os-image.bin`), and starts QEMU.

## Creating a USB bootable image

Write the floppy image directly to a USB drive:

```bash
sudo dd if=os-image.bin of=/dev/sdX bs=512 count=2880 status=progress
sync
```

(Replace `/dev/sdX` with your USB device – double‑check it’s correct.)

## File Overview

| File               | Purpose                                      |
|--------------------|----------------------------------------------|
| `boot.asm`         | First‑stage bootloader (VGA text mode)       |
| `disk.asm`         | BIOS disk read routine                       |
| `gdt.asm`          | Global Descriptor Table for protected mode   |
| `pm-switch.asm`    | Switch from real mode to 32‑bit protected mode |
| `kernel_entry.asm` | Kernel entry point (sets up stack)           |
| `kernel.c`         | Kernel: console, keyboard, shell             |
| `linker.ld`        | Linker script (loads kernel at 0x10000)     |
| `Makefile`         | Build automation                             |

## License

This project is licensed under the **GNU General Public License v2.0** – see the [LICENSE](LICENSE) file for details.

The 8×16 console font is derived from `font_sun8x16.c` in the Linux kernel, which is also licensed under the GPL‑v2.

## Author & Credits

TuxOS is a personal learning project.  
Inspired by countless OS‑dev tutorials and the Linux kernel.  
Special thanks to the osdev.org community.

---

**Warning:** This is a hobby project. Do not run on production hardware (unless you know what you’re doing).  
There is no filesystem, no memory protection, and the shutdown command might not physically power off all real machines – it works in emulators and most vintage PCs.
