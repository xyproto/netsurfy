Quick Build Steps for NetSurfy
==============================

NetSurfy builds on Linux (x86_64, i686, x32 and aarch64) with CMake. All
  NetSurf project libraries are included in `src/libs`.


Install the build dependencies
------------------------------

On Arch Linux:

      $ pacman -S --needed base-devel cmake curl expat gtk4 libjpeg-turbo libpng libwebp openssl quickjs-ng

Install `check` as well to build the unit tests.


Build and run NetSurfy
----------------------

      $ cmake -B build
      $ cmake --build build
      $ ./build/netsurfy

Run the unit tests with:

      $ ctest --test-dir build

Install with:

      $ cmake --install build


Build options
-------------

Options are set when configuring, for example:

      $ cmake -B build -DNETSURFY_USE_JPEGXL=ON -DNETSURFY_USE_QUICKJS=OFF

Run `cmake -LH build` to list them all.


Cross compiling
---------------

Pass a CMake toolchain file for the target. The tools that run during the
  build are compiled for the build machine, using the compiler given in
  `NETSURFY_HOST_C_COMPILER` (or the default `cc`):

      $ cmake -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=aarch64.cmake
      $ cmake --build build-aarch64
