#!/bin/sh

# Set the current directory to the source root.
cd "$(dirname "$0")"

# Create the bin directory.
mkdir -p bin

# Check that we are running on a sensible platform.
uname -o | grep Cygwin > /dev/null
if [ $? -ne 1 ]; then
	echo Cygwin is not supported. Please install a modern GNU/Linux distro.
	exit
fi

# Check that the source code is valid.
md5sum util/test.txt | grep 9906c52f54b2769da1c31e77d3213d0a > /dev/null
if [ $? -ne 0 ]; then
	echo "--------------------------------------------------------------------"
	echo "                  The source has been corrupted!!                   "
	echo "  Please check that you have disabled any automatic line-ending or  " 
	echo " encoding conversions in Git and archive extraction tools you use.  "
	echo "--------------------------------------------------------------------"
	exit
fi

# Check the system compiler is reasonably recent.
if [ ! -f "bin/good_compiler.txt" ]; then
	echo "int main() { return __GNUC__ < 9; }" > bin/check_gcc.c
	g++ -o bin/check_gcc bin/check_gcc.c 
	if [ $? -ne 0 ]; then
		echo "GCC/G++ could not be found. Please install the latest version of GCC/G++."
		exit
	fi
	bin/check_gcc
	if [ $? -ne 0 ]; then
		echo "Your system compiler is out of date. Please update to the latest version of GCC/G++."
		exit
	fi
	rm bin/check_gcc.c bin/check_gcc
	echo yes > "bin/good_compiler.txt"
fi

# Compile and run Build.
gcc -o bin/build -g util/build.c -Wall -Wextra -Wno-format-security -Wno-format-overflow \
		-Wno-missing-field-initializers -Wno-unused-function -Wno-format-truncation -pthread -DPARALLEL_BUILD \
	&& bin/build "$@"
