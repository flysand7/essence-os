# Build system

Building an operating system is tricky: there are many different components to be built that all support varying configurations, and there are lots of dependencies (including compilers and assemblers) that need to be prepared. The Essence build system has been designed to make this as much of a streamlined process as is possible. And hopefully it is a lot easier than building other operating systems from scratch (for a comparison, see the wonderful "Linux From Scratch" books).

The build system is divided into three components. `start.sh`, `util/build.c` and `util/build_core.c` ("build core"). 

- `start.sh` first verifies the environment is acceptable for building Essence, and then it compiles and runs `util/build.c`.
- `util/build.c` provides a command prompt interface where the developer can issue commands to build and test project, and additionally manage configurations, emulators, ports and the source tree. It should be invoked by running `start.sh`. If you pass arguments to `start.sh`, then they will be forwarded to `util/build.c` and executed as a command; it will exit after this command completes. For example, `./start.sh b` will build the system without requiring interaction.
- Build core is responsible for actually building Essence components, and producing a finished drive image. It expects all dependencies and the toolchain to be prepared before it is executed. It is able to run within Essence itself (where dependencies and a toolchain are already provided). It has minimal interaction with the platform it is running on. It is typically invoked automatically by `util/build.c`, which passes a description of the requested build in `bin/build.ini`. This includes information of the toolchain to use, enabled options, applications and drivers to compile, fonts to bundle, installation settings (such as how large the drive image should be), and other general settings (like how many threads to compile with, and whether optimisations are enabled).

`util/build_common.h` contains common definitions shared between these components.

## util/build.c

The following commands are available in the interactive prompt:

- `help` Print a list of the most commonly used commands, with a short description.
- `exit`, `x`, `quit`, `q` Exit the interactive prompt.
- `compile`, `c` Compile the operating system components with optimisations disabled. This does not produce a drive image.
- `build`, `b` Build the operating system to `bin/drive` with optimisations disabled.
- `build-optimised`, `opt` Build the operating system to `bin/drive` with optimisations enabled.
- `debug`, `d` Build the operating system to `bin/drive` with optimisations disabled, and then launch Qemu configured to wait for GDB to attach to it before starting.
- `vbox`, `v` Build the operating system to `bin/vbox.vdi` with optimisations enabled, and then launch VBox. To use this, you need to make a 128MB drive called `vbox.vdi` in the `bin` folder, and attach it to a virtual machine called "Essence" (choose "Windows 7 64-bit" as the OS).
- `vbox-without-opt`, `v2` Same as `v` but with optimisations disabled.
- `vbox-without-compile`, `v3` Same as `v` but compilation is skipped.
- `qemu-with-opt`, `t` Build the operating system to `bin/drive` with optimisations enabled, and then launch Qemu.
- `test`, `t2` Same as `t` but with optimisations disabled.
- `qemu-without-compile`, `t3` Same as `t` but compilation is skipped.
- `qemu-with-kvm`, `k` Same as `t` but Qemu is launched with the `-enable-kvm` flag. This results in significantly faster emulation. The command also disables the `Flag.DEBUG_BUILD` option; see below for a description of this.
- `build-cross` Force a rebuild of the cross compiler toolchain.
- `build-utilities`, `u` Build the utility applications only, which will run on the host development system. This is done automatically as needed by the other commands.
- `make-installer-archive` Compress the contents of `root/` to make a archive for use by the system installer application. This outputs the files `bin/installer_archive.dat` and `bin/installer_metadata.dat`.
- `make-installer-root` Copy all the needed files to make an installer image to `root/`. You need to run `make-installer-archive` first.
- `font-editor` Compile and launch the bitmap font editor utility application. This requires X11.
- `config` Compile and launch the config editor utility application. This requires X11.
- `designer2` Compile and launch the theme editor utility application. This requires X11.
- `replace-many` Replace a list of strings in the source code. A file containing the strings to replace and their replacements is passed. Each line should be of the form `<thing to replace> <what to replace it with>`.
- `replace` Replace a string in the source code. Only full words are matched.
- `replace-in` Same as `replace`, except it only replaces in a single folder.
- `find` Find a string in the source code. Ignores binary files.
- `find-word` Same as `find`, except only the full word is matched. For example, `at` would not match `cat`.
- `find-in` Same as `find`, except it looks in a single folder.
- `fix-replaced-field-name` After renaming a structure field, run this to change all references to it. It parses the compiler errors to do this automatically.
- `ascii <string>` Converts a string to ASCII codepoints.
- `build-optional-ports` Runs the `port.sh` script in each of the folders in `ports/`. If there is no script, the folder is skipped. Note that to use many of the ports the POSIX subsystem must be enabled.
- `do <string>` Run a command through `system()`.
- `live` Build a live CD or USB image. Work in progress.
- `line-count` Calculate the number of lines of code in the project.
- `a2l <symbols file>` Run the a2l utility. See the "a2l" section.
- `build-port` Build a single port. A list of availble ports are listed.
- `get-source <folder name> <url>` The file at the URL is downloaded and cached in `bin/cache`. The download is skipped if the file was already cached. It is then extracted and untar'd. The folder of the given name is then moved to `bin/source`.
- `get-source-checked <sha256> <folder name> <url>` Same as `get-source` except the downloaded file has its SHA-256 checksum compared against the given value.
- `make-crash-report` Copies various system files and logs into a `.tar.gz` which can be used to report a crash.
- `setup-pre-built-toolchain` Setup the pre-built toolchain for use by the build system. You can download and prepare it by running `./start.sh get-source prefix https://github.com/nakst/build-gcc/releases/download/gcc-11.1.0/gcc-x86_64-essence.tar.xz` followed by `./start.sh setup-pre-built-toolchain`.
- `run-tests` Run the API tests. `desktop/api_tests.ini` must be added to `bin/extra_applications.ini`, and `Emulator.SerialToFile` must be enabled.

## Levels of optimisation

Commands in the build system provide two levels of optimisation, either with optimisations enabled or disabled. However, there is an additional configuration flag, `Flag.DEBUG_BUILD` which can be toggled in the configuration editor. This determines whether the `DEBUG_BUILD` flag will be defined in the source code or not. When this flag is defined, many expensive checks are performed to constantly validate the operating system's data structures. To make a "fully optimised" build of Essence, you should first disable this flag, and then build with optimisations enabled. 

Note that some commands will force disable the `Flag.DEBUG_BUILD` option. Most notably, the `k` command.

## Build core

Build core can run in three modes. The mode is passed as the first argument.

- `headers <language> <path-to-output-file>` Generates the API header for `c` (also works for C++), `odin` or `zig`, and saves it to the provided output path.
- `application <configuration>` Builds a single application, given its configuration file. This will not install the application. See the "Application configuration options" section below.
- `standard <configuration>` Builds the system, given the configuration file. See the "Standard build configuration options" section below.

### Application configuration options

In the `[build]` section:

- `source` Path to the source file. This can be repeated, if the application has multiple translation units.
- `compile_flags` Additional flags to be passed to the compiler.
- `link_flags` Additional flags to be passed to the linker.
- `with_cstdlib` Set to 1 to link to the C standard library (currently Musl). This requires enabling the POSIX subsystem. Defaults to 0.
- `require` Optional. Set to the path of a file that must exist for the application to be built. If the file is not found, then the application will not be built.
- `custom_compile_command` A custom build command for the application. If this is set, then the default calls to the compiler and linker will be skipped. This command will be passed directly to `system()`. It should output an ELF executable to `bin/<application name>`. The application name is taken from the `[general]` section.

In the `[general]` section:

- `name` The name of the application.
- `disabled` Set to 1, and the application will not be built.
- `needs_native_toolchain` Set to 1 if the application requires a native toolchain to build. TODO Merge this with `with_cstdlib` from `[build]`?
- `icon` The name of the icon from `desktop/icons.header` to use for the application. This method of setting the application's icon should only be used for builtin application; third party application can set the icon of their application in the `[embed]` section. See below.
- `use_single_process` Set to 1 if the application should use a single process, shared between all its instances. Default is 0.
- `use_single_instance` Set to 1 if the application can only have a single instance open at a time. If the application is already open and the user tries to open it again, then the tab containing the loaded instance will be switched to. Default is 0.
- `hidden` Set to 1 if the application should not be listed on the "New Tab" screen.
- `permission_<permission>` Set to 1 to grant the application a specific application permission.
- `is_file_manager` Set to 1 if the application is the file manager. For system use only!
- `is_installer` Set to 1 if the application is the installer. For system use only!
- `background_service` Set to 1 if the application should be loaded at startup as a background service. For system use only!

In the `[embed]` section there is a list of files that should be embedded into the application bundle. These embedded file can be accessed at runtime using the `EsBundleFind` API. The entries in this section are of the form `<embedded name>=<path>`. Embedded name prefixed with `$` are reserved for definition by the system. The ELF executable files are automatically embedded into the application's bundle, with names of the form `$Executables/<target>`. The application's icon should be embedded as PNG images with names of the form `$Icons/<size>`. You must, as a minimum, provide sizes `16` (16x16 pixels) and `32` (32x32 pixels).

Each `[@file_type]` section provides information about a file type.

- `extension` Gives the file name extension for the file type.
- `name` Gives the readable name of the file type, which will be shown to the user. TODO Translations.
- `icon` Gives the name of the icon from `desktop/icons.header` to show for files of this type. TODO Bundled icons.
- `has_thumbnail_generator` Set to 1 if the file type has a thumbnail generator. Only images are supported at the moment. TODO Custom thumbnail generators.

Each `[@handler]` section describes this application's support to manage files of a given file type.

- `extension` The file name extension of the file type.
- `action` The action that this application support for the file type. Currently only "open" is supported.

### Standard build configuration options

In the `[toolchain]` section:

- `path` Path to the `bin` folder of the toolchain.
- `tmpdir` A path to use to store temporary files. Passed to the toolchain in the `TMPDIR` environment variable.
- `ar`, `cc`, `cxx`, `ld`, `nm`, `strip`, `nasm`, `convert_svg` Paths to the toolchain executables.
- `linker_scripts` Path to the linker scripts. This should be set to the `util/` folder of the source tree.
- `compiler_objects` The path containing the `crt*.o` files provided by the toolchain.
- `crt_objects` The path where `crt*.o` files will be output.

In the `[general]` section:

- `system_build` Set to 1 to build the bootloader, Desktop and Kernel.
- `minimal_rebuild` Set to 1 to enable minimal rebuilds. Dependency information is stored in `bin/dependencies.ini`. Only components where the dependent source files have been modified will be rebuilt. All components are rebuilt if any of the options in this configuration file are modified.
- `colored_output` Set to 1 to enable ANSI color codes in the output.
- `thread_count` Set to the number of threads to use for calling the toolchain.
- `target` Set the name of the target platform, e.g. `x86_64`.
- `skip_header_generation` Set to 1 if the C/C++ API header does not need to be regenerated.
- `verbose` Set to 1 to list every toolchain invocation. See `thread_count=1` if you are using this.
- `common_compile_flags` Additional flags to pass to the C/C++ compiler invocations for all components.
- `for_emulator` Set to 1 to enable a few features that improve the experience of running the system on an emulator.
- `optimise` Enable compiler optimisations.
- `skip_compile` Skip compiling components and only build a drive image from the previously built executables.

In the `[install]` section:

- `file` The output file to put the drive image in.
- `partition_size` The size of the drive image in megabytes.
- `partition_label` The label of the partition. This can be set to anything you like, as long as it doesn't exceed `ESFS_MAXIMUM_VOLUME_NAME_LENGTH`.

In the `[options]` section there is a copy of the options in "Configuration options", from `bin/config.ini`. Note that many of these options are not handled by build core, but rather by `util/build.c`. See the "Configuration options" section for more details.

Each `[@application]` section should contain a single key, `manifest`, giving the path to the application configuration options (see the above section).

Each `[@driver]` section should contain:

- `name` The name of the driver. This must be found in `kernel/config.ini`.
- `source` The source file of the driver. This must be C/C++.
- `builtin` Set to 1 if the driver should be linked directly into the kernel. Set to 0 if the driver should be built as a loadable module.

`[@font <name>]` sections specify the fonts that are to be bundled in the desktop executable. Each section should contain:

- `category` One of `Sans`, `Serif` or `Mono`. More categories will likely be added in the future.
- `scripts` The scripts supported by the font. See `hb_script_t` in `bin/harfbuzz/src/hb-common.h` for a list of scripts. Separate each script with a `,`.
- `license` The license file to bundle. This is a path relative to `res/Fonts/`.
- `.<digit>` and `.<digit>i` The font file for a weight 1-9, with `i` for italics. More possibilities will likely be added in the future. This is a path relative to `res/Fonts/`.

## Configuration editor

You can start the configuration editor by typing `config` at the build system prompt. Click `Save` to save your changes. Click `Defaults` to load the defaults. Left click an option in the table to toggle it or otherwise modify it. You will be presented with a warning if modifying the option is not recommended. Right click to reset a specific option. The changes are saved to `bin/config.ini`, which will not be uploaded to source control.

See the section "Configuration options" for a description of what each option does.

## Configuration options

Options starting with `Driver.` are used to enable and disable drivers. The defaults will be configured to work optimally for development work in emulators.

### Emulator options

- `Emulator.AHCI` Use AHCI/SATA for the drive in Qemu.
- `Emulator.ATA` Use ATA/IDE for the drive in Qemu.
- `Emulator.NVMe` Use NVMe for the drive in Qemu.
- `Emulator.CDROMImage` A path to a CD-ROM image that will be passed to Qemu.
- `Emulator.USBImage` A path to a drive image that will be passed to Qemu to appear as a USB mass storage device.
- `Emulator.USBHostVendor`, `Emulator.USBHostProduct` The vendor and product of a USB device to pass-through to Qemu. You will probably need to set `Emulator.RunWithSudo` for this to work.
- `Emulator.RunWithSudo` Run Qemu with `sudo`.
- `Emulator.Audio` Enable audio output in Qemu, saved to `bin/audio.wav`.
- `Emulator.MemoryMB` The amount of memory for Qemu. Probably needs to be at least 32 MB.
- `Emulator.Cores` The number of CPU cores to use in the emulator. Use 1 for a better debugging experience. Note that the build system command "k" automatically uses the maximum number of CPU cores possible.
- `Emulator.PrimaryDriveMB` The size of the primary drive image.
- `Emulator.SecondaryDriveMB` The size of a secondary drive image that will be connected to the emulator.
- `Emulator.VBoxEFI` Use UEFI boot in VBox.
- `Emulator.QemuEFI` Use Qemu boot in VBox.
- `Emulator.SerialToFile` Save the serial output from Qemu to `bin/Logs/qemu_serial1.txt`. If disabled, then the output will be available as a separate display in Qemu, but will not be saved after Qemu exits! You can view the serial output saved to a file as it is output using the shell command `tail -f bin/Logs/qemu_serial1.txt`.

### Build core options

- `BuildCore.Verbose` Set to enable `verbose` flag passed to build core. See the `[general]` section of the build core options.
- `BuildCore.NoImportPOSIX` Do not import the `root/Applications/POSIX` folder onto the drive image. This will allow building much smaller drive images.
- `BuildCore.RequiredFontsOnly` Only import the minimal number of fonts onto the drive image. This will allow building much smaller drive images.

### Flag options

- `Flag.ENABLE_POSIX_SUBSYSTEM` Enable the POSIX subsystem. Needed to use most ports and the POSIX Launcher.
- `Flag.DEBUG_BUILD` Set to 1 to define the `DEBUG_BUILD` flag. See the "Levels of optimisation" section above.
- `Flag.USE_SMP` Enable symmetric multiprocessing. You may wish to disable this for easier debugging.
- `Flag.PROFILE_DESKTOP_FUNCTIONS` Set to 1 to enable gf profiling integration for the Desktop.
- `Flag.BGA_RESOLUTION_WIDTH` The default resolution width to use for BGA, the graphics adapter used by Qemu. Must be a multiple of 4.
- `Flag.BGA_RESOLUTION_HEIGHT` The default resolution height to use for BGA, the graphics adapter used by Qemu. Must be a multiple of 4.
- `Flag.VGA_TEXT_MODE` Use VGA text mode at startup. Use with `Flag.START_DEBUG_OUTPUT` for early debugging.
- `Flag.CHECK_FOR_NOT_RESPONDING` Set to enable the Desktop's feature of checking whether the foreground application is responding. Disabling this will reduce the number of thread switches, making debugging slightly easier.
- `Flag._ALWAYS_USE_VBE` Use VBE mode setting. Needed for real hardware, if using the MBR bootloader.
- `Flag.COM_OUTPUT` Set to enable serial output. Should be disabled before making a build to run on real hardware.
- `Flag.POST_PANIC_DEBUGGING` Set to enable the special debugger that runs after a `KernelPanic`. Requires a PS/2 keyboard.
- `Flag.START_DEBUG_OUTPUT` Log kernel messages to the screen immediately during startup.
- `Flag.PAUSE_ON_USERLAND_CRASH` Send a userland application into a spin loop when it crashes. This makes it possible to attach a debugger (see `Debugging.md`) and get a stack trace.

### General options

- `General.first_application` The name of application to launch at startup. Useful if you don't want to have to manually open it from the "New Tab" page every time you restart the emulator.
- `General.wallpaper` The path to the wallpaper file. This is an Essence path; paths beginning with `0:/` will correspond to the files in the `root/` folder of the source tree.
- `General.window_color` The default window color.
- `General.installation_state` The installation state. If set to `1`, then the installer will be invoked. `make-installer-root` should have been run before doing this.
- `General.ui_scale` The default UI scale, as a percentage.
- `General.keyboard_layout` The default keyboard layout. See `Building.md` for instructions on how to set this.

### Dependency options

TODO

- `Dependency.ACPICA`
- `Dependency.stb_image`
- `Dependency.stb_image_write`
- `Dependency.stb_sprintf`
- `Dependency.FreeTypeAndHarfBuzz`

## a2l

This utility is used to convert hexadecimal code addresses into line numbers. Start it with `./start.sh a2l <path to symbol file>`. Recall that symbol files are saved to `bin/<application name>`. Paste in any text containing addresses and they will be converted to line numbers, if possible. Usually you will want to do this for the crash information logged in `bin/Logs/qemu_serial1.txt`.
