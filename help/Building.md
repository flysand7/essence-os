# Building Essence

**Warning: This software is still in development. Expect bugs.**

## Linux

These instructions expect you to be running an updated modern Linux distribution. See below for additional information about specific distributions.

Download this project's source. 

    git clone --depth=1 https://gitlab.com/nakst/essence.git/

Start the build system.

    ./start.sh

The compiler toolchain will be downloaded.

Once complete, you can test the operating system in an emulator. 
* If you have Qemu and KVM installed, run `k` in the build system. **Recommended!**
* If you have Qemu installed, run `t2` in the build system.
* If you have VirtualBox installed, make a 128MB drive called `vbox.vdi` in the `bin` folder, attach it to a virtual machine called "Essence" (choose "Windows 7 64-bit" as the OS), and run `v` in the build system.

Run `build-port` in the build system to view a list of optional ports that can be built.

### Arch Linux

You can install all the needed dependencies with `sudo pacman -S base-devel curl nasm gmp mpfr mpc ctags qemu`. Run `./start k` to build and run the system in Qemu.

### Fedora Linux

You can install all the needed dependencies with `dnf install nasm gmp gmp-devel mpfr mpfr-devel libmpc libmpc-devel`. Run `./start k` to build the system. Then run `qemu-kvm bin/drive` to launch the built image in Qemu.

### Ubuntu

Essence is built using GitHub Actions using their Ubuntu virtual machines. [This file contains a list of additional dependencies that need to be installed](https://github.com/nakst/build-essence/blob/main/.github/workflows/build-essence.yml), and [this is the build script that actually performs the build](https://github.com/nakst/build-essence/blob/main/build.sh).

## macOS

1) Install `gcc@11` through brew
2) Install `nasm` through brew
3) Install `ctags` through brew
4) Install `xz` through brew

Follow the instructions above for Linux to build and test the system.

## Configuration

From within the build system, run the command `config` to open the configuration editor. Click an option to change its value, and then click the `Save` button. You changes are saved locally, and will not be uploaded by Git. Not all configurations are likely to work; if you don't know what you're doing, it's probably best to stick with the defaults.

### Keyboard layout

To set the default keyboard layout for use in the emulator to match your current one, run:

    setxkbmap -query | grep layout | awk '{OFS=""; print "General.keyboard_layout=", $2}' >> bin/config.ini

## Generating the API header

If you want your project to target Essence, you need to generate the API header for your programming language of choice.

    gcc -o bin/build_core util/build_core.c
    bin/build_core headers <language> <path-to-output-file>

Currently supported languages are 'c' (also works for C++), 'zig' and 'odin'.

Documentation for the API is still a work in progress. For examples of how to use the API, consult the standard applications in the `apps/` folder of the source tree. Minimal sample applications are placed in the `apps/samples` folder. By placing your application's `.ini` file in the `apps/` folder, it will be automatically built by the build system.

## Where to next?

For a more thorough documentation of the build system, see `help/Build System.md`.
For an overview of the files in the source tree, see `help/Source Map.md`.
For a list of things that need to be worked on, see `help/TODO.md`.
For guidance on contributing to the project, see `help/Contributing.md`.
For discussion, join our Discord server: https://discord.gg/skeP9ZGDK8.
