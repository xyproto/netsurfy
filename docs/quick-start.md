Quick Build Steps for NetSurfy
=============================

Last Updated: 28th May 2024

This document provides steps for building NetSurfy.

These instructions use a shell script to perform several operations.
  This script has only been tested with the bash and zsh bourne style
  shell interpreters. The latest version of this script should be
  retrieved from the official NetSurfy source repository.

This shell script is used by the NetSurfy Developers but you should
  satisfy yourself that the script is not malicious. It should be noted
  that building the browser will also be executing shell code and
  requires a similar level of trust.


Native build
============

Grab a temporary env.sh
-----------------------

     $ wget https://raw.githubusercontent.com/xyproto/netsurfy/main/docs/env.sh
     $ unset HOST
     $ source env.sh


Install any packages you need
-----------------------------

The package install helper installs all packages required to build NetSurfy
  and the NetSurfy project libraries. By *default* no libraries for a graphical
  toolkit are installed.

     $ ns-package-install

If NetSurfy is to be built to target a graphical toolkit the development
  packages for that toolkit can be installed by setting the TARGET_TOOLKIT
  variable to one of framebuffer, gtk2, gtk3 or qt6

     $ TARGET_TOOLKIT=qt6 ns-package-install

If your package manager is not supported, you will have to install third
  party packages manually.


Update the environment settings after package installation
----------------------------------------------------------

     $ unset HOST
     $ source env.sh


Get the NetSurfy project source code from Git
--------------------------------------------

All the sources for the browser and support libraries is available
  from the public git server.

Local copies may be easily obtained with the ns-clone command.

     $ ns-clone


Build and install NetSurfy project libraries
-------------------------------------------

Updates NetSurfy project library sources to latest, builds and installs them.

      $ ns-pull-install


Switch to new NetSurfy workspace
-------------------------------

Remove the bootstrap script and use the newly installed one

      $ rm env.sh
      $ cd ~/dev-netsurfy/workspace
      $ source env.sh


Build and run NetSurfy
---------------------

      $ cd netsurfy

To build the native front end (the GTK front end on Linux, BSDs, etc)
  you could do:

      $ make
      $ ./netsurfy

To build the framebuffer front end, you could do:

      $ make TARGET=framebuffer
      $ ./nsfb

More detailed documentation on using the [framebuffer](docs/using-framebuffer.md)
  frontend are available.

Cross Compiling
===============

If you are cross compiling, you can follow the above steps, but when
  sourcing env.sh, you should set HOST environment variable to the
  appropriate triplet for your cross compiler.

The example in the command below shows cross compiling for Windows:

      $ HOST=x86_64-w64-mingw32 source env.sh

After that, the commands such as `ns-package-install` and
  `ns-pull-install` will do what is appropriate for the platform you are
  building for.

To do the final build of NetSurfy, pass the appropriate TARGET to
  make. For example, to cross compile for Windows:

      $ make TARGET=windows

Finally, you can package up your build to transfer to the system you
  are developing for.  For example, to produce a package for Windows:

      $ make TARGET=windows package

Getting a cross compiler set up
-------------------------------

We maintain cross compilation environments and an SDK for a number of
  platforms.  These may be found in our toolchains repository.

      $ git clone git://git.netsurf-browser.org/toolchains

Pre-built versions of the toolchains for 64bit x86 Debian systems are
  available via our [automated build and test
  infrastructure](https://ci.netsurf-browser.org/builds/toolchains/)


Not working?
============

If the above steps are inapplicable, or don't work, you can build
  manually. Follow the instructions in the BUILDING-* documents in the
  docs/ directory the NetSurfy browser source tree.

