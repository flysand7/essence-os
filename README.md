# **Essence** — An Operating System

![Screenshot of the OS running in an emulator, showing File Manager, and the new tab screen.](https://handmade.network/public/media/image/p-essence-s1.png)
![Screenshot of the OS running in an emulator, showing GCC running under the POSIX subsystem.](https://handmade.network/public/media/image/p-essence-s3.png)
![Screenshot of the OS running in an emulator, showing the shutdown dialog.](https://handmade.network/public/media/image/p-essence-s5.png)

## Support

To support development, you can donate to my Patreon: https://www.patreon.com/nakst.

## Features

Kernel
* Audio mixer.
* Filesystem independent cache manager.
* Memory manager with shared memory, memory-mapped files and multithreaded paging zeroing and working set balancing.
* Networking stack for TCP/IP.
* Scheduler with multiple priority levels and priority inversion.
* On-demand module loading.
* Virtual filesystem.
* Window manager.
* Optional POSIX subsystem, capable of running GCC and some Busybox tools.

Applications
* File Manager
* Text Editor
* IRC Client
* System Monitor

Ports
* Bochs
* GCC and Binutils
* FFmpeg
* Mesa (for software-rendered OpenGL)
* Musl

Drivers
* Power management: ACPI with ACPICA.
* Secondary storage: IDE, AHCI and NVMe.
* Graphics: BGA and SVGA.
* Read-write filesystems: EssenceFS.
* Read-only filesystems: Ext2, FAT, NTFS, ISO9660.
* Audio: HD Audio.
* NICs: 8254x.
* USB: XHCI, bulk storage devices, human interface devices.

Desktop
* Custom user interface library.
* Software vector renderer with complex animation support.
* Tabbed windows.
* Multi-lingual text rendering and layout with FreeType and Harfbuzz.

## Discussion

Visit https://essence.handmade.network/forums.

## Building

**Warning: This software is still in development. Expect bugs.**

### Linux

Download this project's source. 

    git clone --depth=1 https://gitlab.com/nakst/essence.git/

Start the build system.

    ./start.sh

Follow the on-screen instructions to build a cross compiler.

Once complete, you can test the operating system in an emulator. 
* Please note that by default a checked build is produced, which runs additional checks at runtime (such as heap validation on every allocation and deallocation). This may impact the performance during testing.
* If you have Qemu installed, run `t2` in the build system.
* If you have VirtualBox installed, make a 128MB drive called `vbox.vdi` in the `bin` folder, attach it as a to a virtual machine called "Essence" (choose "Windows 7 64-bit" as the OS), and run `v` in the build system.

## Configuration

From within the build system, run the command `config` to open the configuration editor. Click an option to change its value, and then click the `Save` button. You changes are saved locally, and will not be uploaded by Git. Not all configurations are likely to work; if you don't know what you're doing, it's probably best to stick with the defaults.

## Generating the API header

If you want your project to target Essence, you need to generate the API header for your programming language of choice.

    g++ -o bin/build_core util/build_core.c
    bin/build_core headers <language> <path-to-output-file>

Currently supported languages are 'c' (also works for C++), 'zig' and 'odin'.

There is currently no documentation for the API; for examples of how to use the API, consult the standard applications in the `apps/` folder of the source tree. A minimal application is demonstrated in `apps/hello.c` and `apps/hello.ini`. By placing your application's `.ini` file in the `apps/` folder, it will be automatically built by the build system.
