Getting Started
===============

Installation
------------

You can either download a binary distribution of Bangra from the
`website <https://bitbucket.org/duangle/bangra>`_ or build Bangra from source.

How to build Bangra on Linux:

* You need build-essentials, clang 3.9, libclang 3.9 and LLVM 3.9 installed
* put ``clang++`` and ``llvm-config`` in your path OR extract the clang distro into
  the repo folder and rename it to ``clang``.
* You also need the latest source distribution of
  `libffi <https://sourceware.org/libffi/>`_.
* build libffi using `./configure --enable-shared=no --enable-static=yes && make` and
  softlink the generated build folder (e.g. `x86_64-unknown-linux-gnu`) as `libffi`
  in the repo folder.

* execute ``./makebangra``

How to build Bangra on Windows:

* Install `MSYS2 <http://msys2.github.io>`_ and
  `install <https://github.com/valtron/llvm-stuff/wiki/Build-LLVM-3.8-with-MSYS2>`_
  both llvm and clang 3.9 for ``x86_64``. The packages are named
  ``mingw64/mingw-w64-x86_64-llvm`` and ``mingw64/mingw-w64-x86_64-clang``.
* You also need to install the ``mingw64/mingw-w64-x86_64-libffi`` package.
* put ``clang++`` in your path OR make sure msys2 resides in ``C:\msys64`` OR edit
  ``makebangra.bat`` and change the path accordingly.
* copy ``libstdc++-6.dll``, ``libgcc_s_seh-1.dll``, ``libwinpthread-1.dll`` and
  ``libffi-6.dll`` from the msys2 installation into the repo folder.
  ``bangra.exe`` will depend on them.
* run ``makebangra.bat``

There should now be a ``bangra`` executable in your root folder.

You can verify that everything works by running::

    bangra testing/test_all.b

Running
-------

To execute a Bangra program, pass the source file as first argument to the
``bangra`` interpreter::

    bangra <path-to-file.b>

Hello World
-----------

A simple "Hello World" program in Bangra looks as follows::

    print "Hello world!"

*Note that in order to be valid, a Bangra program must not contain any tabs,
and each sub-block must be indented by four spaces.*

TODO

