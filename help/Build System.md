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
- `make-crash-report` Copies various system files and logs into a `.tar.gz` which can be used to report a crash.
- `setup-pre-built-toolchain` Setup the pre-built toolchain for use by the build system. You can download and prepare it by running `./start.sh get-source prefix https://github.com/nakst/build-gcc/releases/download/gcc-11.1.0/gcc-x86_64-essence.tar.xz` followed by `./start.sh setup-pre-built-toolchain`.

## Levels of optimisation

Commands in the build system provide two levels of optimisation, either with optimisations enabled or disabled. However, there is an additional configuration flag, `Flag.DEBUG_BUILD` which can be toggled in the configuration editor. This determines whether the `DEBUG_BUILD` flag will be defined in the source code or not. When this flag is defined, many expensive checks are performed to constantly validate the operating system's data structures. To make a "fully optimised" build of Essence, you should first disable this flag, and then build with optimisations enabled. 

Note that some commands will force disable the `Flag.DEBUG_BUILD` option. Most notably, the `k` command.

## Build core

TODO

## Configuration editor

TODO

## Configuration options

TODO

## a2l

TODO
