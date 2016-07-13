First Steps
===========


Installation
------------

How to build Bang on Linux:

* You need build-essentials, clang 3.8, libclang 3.8 and LLVM 3.8 installed
* put ``clang++`` and ``llvm-config`` in your path OR extract the clang distro into
  the repo folder and rename it to ``clang``
* execute ``./makebang``

How to build Bang on Windows:

* Install `MSYS2 <http://msys2.github.io>`_ and
  `install <https://github.com/valtron/llvm-stuff/wiki/Build-LLVM-3.8-with-MSYS2>`_
  both llvm and clang 3.8 for ``x86_64``. The packages are named
  ``mingw64/mingw-w64-x86_64-llvm`` and ``mingw64/mingw-w64-x86_64-clang``.
* put ``clang++`` in your path OR make sure msys2 resides in ``C:\msys64`` OR edit
  ``makebang.bat`` and change the path accordingly.
* copy ``libstdc++-6.dll``, ``libgcc_s_seh-1.dll`` and ``libwinpthread-1.dll`` from
  the msys2 installation into the repo folder. ``bang.exe`` will depend on them.
* execute ``./makebang.bat``

TODO

