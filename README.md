# **Essence** — An Operating System

<iframe width="560" height="315" src="https://www.youtube.com/embed/aGxt-tQ5BtM" frameborder="0" allow="accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture" allowfullscreen></iframe>

![Screenshot of the operating system running in an emulator, showing File Manager, and the new tab screen.](https://handmade.network/public/media/image/p-essence-s1.png)
![Screenshot of the operating system running in an emulator, showing GCC running under the POSIX subsystem.](https://handmade.network/public/media/image/p-essence-s3.png)
![Screenshot of the operating system running in an emulator, showing the shutdown dialog.](https://handmade.network/public/media/image/p-essence-s5.png)

## Support

To support development, you can donate to my Patreon: https://www.patreon.com/nakst.

## Features

Kernel
* Filesystem independent cache manager.
* Memory manager with shared memory, memory-mapped files and multithreaded paging zeroing and working set balancing.
* Networking stack for TCP/IP.
* Scheduler with multiple priority levels and priority inversion.
* On-demand module loading.
* Virtual filesystem.
* Window manager.
* Audio mixer. (being rewritten)
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

Join our Discord server: https://discord.gg/skeP9ZGDK8

Alternatively, visit the forums (not very active): https://essence.handmade.network/forums.

## Building

**Warning: This software is still in development. Expect bugs.**

### Linux

Download this project's source. 

    git clone --depth=1 https://gitlab.com/nakst/essence.git/

Start the build system.

    ./start.sh

Follow the on-screen instructions to build a cross compiler.

Once complete, you can test the operating system in an emulator. 
* If you have Qemu and KVM installed, run `k` in the build system. **Recommended!**
* If you have Qemu installed, run `t2` in the build system.
* If you have VirtualBox installed, make a 128MB drive called `vbox.vdi` in the `bin` folder, attach it to a virtual machine called "Essence" (choose "Windows 7 64-bit" as the OS), and run `v` in the build system.

## Keyboard layout

To set the default keyboard layout for use in the emulator to match your current one, run:

    setxkbmap -query | grep layout | awk '{OFS=""; print "General.keyboard_layout=", $2}' >> bin/config.ini

## Configuration

From within the build system, run the command `config` to open the configuration editor. Click an option to change its value, and then click the `Save` button. You changes are saved locally, and will not be uploaded by Git. Not all configurations are likely to work; if you don't know what you're doing, it's probably best to stick with the defaults.

## Generating the API header

If you want your project to target Essence, you need to generate the API header for your programming language of choice.

    g++ -o bin/build_core util/build_core.c
    bin/build_core headers <language> <path-to-output-file>

Currently supported languages are 'c' (also works for C++), 'zig' and 'odin'.

There is currently no documentation for the API; for examples of how to use the API, consult the standard applications in the `apps/` folder of the source tree. Minimal sample applications are placed in the `apps/samples` folder. By placing your application's `.ini` file in the `apps/` folder, it will be automatically built by the build system.

## TODO

**This list is incomplete!**

- Applications
	- PDF viewer
	- Media player
	- Automator
	- Web browser
- Desktop environment
	- Alt-tab
	- Saving application state on shut down
	- Task list popup
	- Add more settings, and organize them
	- Print screen key
	- Storing images in the clipboard
	- Combined search and Lua REPL
	- Clipboard viewer
	- Improved color eyedropper
- File Manager
	- Deleting files
	- Improving thumbnail storage
	- Property store
	- Searching
	- Search indexing
	- Tagging; metadata
	- Tracking source of files
	- Localizing system folder names
	- Previewing other file types
	- Filtering
	- Multi-rename
	- Compressed archives
	- Creating new files
	- Automation
	- Grouping
- GUI
	- Drag and drop
	- Keyboard navigation in menus
	- Date/time entry
	- Tokens in textbox
	- Right-to-left layout
	- Accessibility hooks (e.g. for a screen reader)
	- Choice boxes
	- Resizable scrollbars
	- Asynchronously painted element
	- Reorder lists
	- Dialogs as separate windows
	- File dialogs
	- Textbox auto-complete
	- Media display
	- Progress bars
	- Dockable UI
	- Charts
	- Gauges
	- Password editor
	- Scrollbar animations
	- Scaling at the paint level (as well as layout)
	- Simpler control of line clipping in `EsTextDisplay`
	- Fast scroll
	- Keeping items anchored when resizing 
	- Instance reference counting
- Text rendering
	- Variable font support
	- Fix custom font renderer
	- Unicode bidirectional text algorithm
	- Unicode vertical text layout algorithm
	- OpenType features
	- Grapheme splitter (for textbox)
	- Fallback fonts for unsupported languages
	- Consistent font size specification
- API
	- Date/time
	- 2D graphics
	- Tweening
	- Media decoding
	- Debugging other processes
	- `io_uring`-like file access
	- String sorting; normalization; case conversion
- Shared code
	- Better heap allocator
	- Faster rendering routines
	- Color spaces
- Theming
	- Dark theme
	- High contrast theme
	- Bitmapped font
- Kernel
	- Networking protocol implementation
	- Networking hardware drivers
	- Rewrite audio layer
	- Asynchronous IO
	- Better IPC messaging
	- Double fault handler
	- `x86_32` support
	- ARM support
	- Graphics card drivers
	- Support for USB 3.0 devices
	- OHCI/UHCI/EHCI drivers
	- USB hub support
	- Built-in debugger
	- Decrease startup time
	- Decrease memory usage
	- Shared memory permissions
	- Proper worker threads
	- Read-write file systems: FAT, ext4, NTFS
- Documentation!
