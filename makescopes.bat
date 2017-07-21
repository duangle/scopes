@echo off
rem you need msys2 and the llvm, clang 4.0, libffi and vim packages installed.
rem see documentation for detailed installation instructions.
rem additionally, the built executable depends on
rem libstdc++-6.dll, libgcc_s_seh-1.dll, libffi-6.dll and libwinpthread-1.dll
setlocal
set DIR=%~dp0
set MSYSPATH=C:\msys64
set MINGWPATH=%MSYSPATH%\mingw64
set PATH=%MINGWPATH%\bin;%PATH%
rem set DEBUGOPTS=-O2 -DNDEBUG
set DEBUGOPTS=-O0 -g -DSCOPES_DEBUG
clang -x c -o %DIR%scopes.h -P -E %DIR%scopes.cpp -DSCOPES_WIN32 -I%DIR%win32 -I%MINGWPATH%/lib/libffi-3.2.1/include
if errorlevel 1 goto :fail
echo scopes.bin.h
xxd -i scopes.h %DIR%scopes.bin.h
if errorlevel 1 goto :fail
echo core.sc.bin.h
xxd -i core.sc %DIR%core.sc.bin.h
if errorlevel 1 goto :fail
echo mman.o
clang -c -o mman.o %DIR%win32\mman.c -O2 -Wno-shift-count-overflow
if errorlevel 1 goto :fail
echo realpath.o
clang -c -o realpath.o %DIR%win32\realpath.c -O2
if errorlevel 1 goto :fail
echo dlfcn.o
clang -c -o dlfcn.o %DIR%win32\dlfcn.c -O2
if errorlevel 1 goto :fail
echo linenoise.o
clang -c -o linenoise.o %DIR%external\linenoise-ng\src\linenoise.cpp -O2 ^
    -I%DIR%external\linenoise-ng\include -std=gnu++11 -fno-rtti
if errorlevel 1 goto :fail
echo ConvertUTF.o
clang -c -o ConvertUTF.o %DIR%external\linenoise-ng\src\ConvertUTF.cpp -O2 ^
    -I%DIR%external\linenoise-ng\include -std=gnu++11 -fno-rtti
if errorlevel 1 goto :fail
echo wcwidth.o
clang -c -o wcwidth.o %DIR%external\linenoise-ng\src\wcwidth.cpp -O2 ^
    -I%DIR%external\linenoise-ng\include -std=gnu++11 -fno-rtti
if errorlevel 1 goto :fail
echo scopes.exe
clang++ -o scopes.exe ^
    %DIR%scopes.cpp ^
    -I%DIR%win32 ^
    -I%MINGWPATH%/lib/libffi-3.2.1/include -I%MINGWPATH%/include ^
    -Wno-vla -DSCOPES_CPP_IMPL -DSCOPES_MAIN_CPP_IMPL ^
    -D_GNU_SOURCE -pedantic -DSCOPES_DEBUG -ferror-limit=1 ^
    -D_LIBCPP_HAS_NO_CONSTEXPR -DSCOPES_WIN32 ^
    -std=c++11 %DEBUGOPTS% ^
    -fno-exceptions -fno-rtti ^
    -Wl,--allow-multiple-definition ^
    -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS ^
    -L%MINGWPATH%/lib -lclangFrontend -lclangDriver -lclangSerialization ^
    -lclangCodeGen -lclangParse -lclangSema -lclangAnalysis -lclangEdit ^
    -lclangAST -lclangLex -lclangBasic ^
    -Wl,--whole-archive ^
    -Wl,--export-all-symbols ^
    -Wl,--no-whole-archive ^
    mman.o realpath.o dlfcn.o linenoise.o ConvertUTF.o wcwidth.o ^
    -lLLVMCoverage -lLLVMObjCARCOpts -lLLVMOption -lLLVMPasses -lLLVMipo ^
    -lLLVMVectorize -lLLVMLinker -lLLVMIRReader -lLLVMAsmParser ^
    -lLLVMX86Disassembler -lLLVMX86AsmParser -lLLVMX86CodeGen ^
    -lLLVMSelectionDAG -lLLVMAsmPrinter -lLLVMDebugInfoCodeView -lLLVMCodeGen ^
    -lLLVMDebugInfoMSF -lLLVMCoroutines -lLLVMGlobalISel -lLLVMLTO ^
    -lLLVMScalarOpts -lLLVMInstCombine -lLLVMInstrumentation ^
    -lLLVMTransformUtils -lLLVMBitWriter -lLLVMX86Desc -lLLVMMCDisassembler ^
    -lLLVMX86Info -lLLVMX86AsmPrinter -lLLVMX86Utils -lLLVMMCJIT ^
    -lLLVMExecutionEngine -lLLVMTarget -lLLVMAnalysis -lLLVMProfileData ^
    -lLLVMRuntimeDyld -lLLVMObject -lLLVMMCParser -lLLVMBitReader -lLLVMMC ^
    -lLLVMCore -lLLVMSupport ^
    -lffi -lole32 -luuid -lversion -lpsapi ^
    -fexceptions
if errorlevel 1 goto :fail
%DIR%scopes.exe %DIR%testing\test_all.sc
if errorlevel 1 goto :fail
echo success.
goto :done
:fail
echo failed.
:done
