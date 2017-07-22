Getting Started
===============

Installation
------------

You can either download a binary distribution of Scopes from the
`website <https://bitbucket.org/duangle/scopes>`_ or build Scopes from source.

Building Scopes on Windows
^^^^^^^^^^^^^^^^^^^^^^^^^^

Scopes only supports the mingw64 toolchain for the foreseeable future.

* Install `MSYS2 <http://msys2.github.io>`_ and
  `install <https://github.com/valtron/llvm-stuff/wiki/Build-LLVM-3.8-with-MSYS2>`_
  clang, LLVM 4.0.x, libffi and xxd for ``x86_64``. The packages are named
  ``mingw64/mingw-w64-x86_64-llvm``, ``mingw64/mingw-w64-x86_64-clang``,
  ``mingw64/mingw-w64-x86_64-libffi`` and ``msys/vim`` (which contains xxd).
* Nice to have: ``mingw-w64-x86_64-gdb``
* put ``clang++`` in your path **OR** make sure msys2 resides in ``C:\msys64`` OR edit
  ``makescopes.bat`` and change the path accordingly.
* copy ``libstdc++-6.dll``, ``libgcc_s_seh-1.dll``, ``libwinpthread-1.dll`` and
  ``libffi-6.dll`` from the msys2 installation into the repo folder.
  ``scopes.exe`` will depend on them.
* run ``makescopes.bat``
* There should now be a ``scopes.exe`` executable in the repo root folder.

Building Scopes on Linux
^^^^^^^^^^^^^^^^^^^^^^^^

* You need build-essentials, clang, libclang and LLVM 4.0.x installed,
  as well as xxd.
* Put ``clang++`` and ``llvm-config`` in your path **OR** extract the clang distro into
  the repo folder and rename it to ``clang``. 
* You also need the latest source distribution of
  `libffi <https://sourceware.org/libffi/>`_.
* Build libffi using ``./configure --enable-shared=no --enable-static=yes && make`` and
  softlink or copy the generated build folder (e.g. ``x86_64-unknown-linux-gnu``)
  as ``libffi`` in the repo folder.
* run ``./makescopes``
* There should now be a ``scopes`` executable in the repo root folder.
