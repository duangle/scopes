Getting Started
===============

Installation
------------

You can either download a binary distribution of Scopes from the
`website <https://bitbucket.org/duangle/scopes>`_ or build Scopes from source.

The main repository for scopes is on 
`bitbucket <https://bitbucket.org/duangle/scopes>`_. A 
`github mirror <https://github.com/duangle/scopes>`_ is also available.

Building Scopes on Windows
^^^^^^^^^^^^^^^^^^^^^^^^^^

Scopes only supports the mingw64 toolchain for the foreseeable future.

* Beware: Make sure your MSYS2 installation resides in ``C:\msys64``.
* Install `MSYS2 <http://msys2.github.io>`_ and
  `install <https://github.com/valtron/llvm-stuff/wiki/Build-LLVM-3.8-with-MSYS2>`_
  clang, LLVM 4.0.x, libffi and make for ``x86_64``. The packages are named
  ``mingw64/mingw-w64-x86_64-llvm``, ``mingw64/mingw-w64-x86_64-clang``,
  ``mingw64/mingw-w64-x86_64-libffi`` and ``make``.
* Nice to have: ``mingw-w64-x86_64-gdb``
* Lastly, you need a build of `GENie <https://github.com/bkaradzic/GENie>`_ (binaries
  available on the page).
* In the base directory, run ``genie gmake`` once to generate the project Makefiles.
* To build in debug mode, run ``make -C build``. For release mode, use 
  ``make -C build config=release``. 
* There should now be a ``scopes.exe`` executable in the repo root folder.
* For a fresh rebuild, just remove the ``build`` directory before running make.

Building Scopes on Linux
^^^^^^^^^^^^^^^^^^^^^^^^

* You need build-essentials, clang, libclang and LLVM 4.0.x installed - preferably
  locally:
* Put ``clang++`` and ``llvm-config`` in your path **OR** extract the clang distro into
  the repo folder and rename it to ``clang``. 
* You also need the latest source distribution of
  `libffi <https://sourceware.org/libffi/>`_.
* Lastly, you need a build of `GENie <https://github.com/bkaradzic/GENie>`_ (binaries
  available on the page).
* Build libffi using ``./configure --enable-shared=no --enable-static=yes && make`` and
  softlink or copy the generated build folder (e.g. ``x86_64-unknown-linux-gnu``)
  as ``libffi`` in the repo folder.
* In the base directory, run ``genie gmake`` once to generate the project Makefiles.
* To build in debug mode, run ``make -C build``. For release mode, use 
  ``make -C build config=release``. 
* There should now be a ``scopes`` executable in the repo root folder.
* For a fresh rebuild, just remove the ``build`` directory before running make.
