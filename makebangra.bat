@echo off
rem you need msys2 and the llvm and clang 3.8 packages installed.
rem additionally, the built executable depends on
rem libstdc++-6.dll, libgcc_s_seh-1.dll and libwinpthread-1.dll
setlocal
set DIR=%~dp0
set CLANGPATH=C:\msys64\mingw64
set PATH=%CLANGPATH%\bin;%PATH%
gcc -x c -o %DIR%bangra.h -C -E %DIR%bangra.cpp
if errorlevel 1 goto :fail
gcc -c -o mman.o %DIR%win32\mman.c -O2 -Wno-shift-count-overflow
if errorlevel 1 goto :fail
g++ -shared -o libbangra.dll %DIR%bangra.cpp mman.o -Wl,--output-def,libbangra.def,--out-implib,libbangra.a -I%DIR%win32 -I%CLANGPATH%/include -Wno-vla -DBANGRA_CPP_IMPL -D_GNU_SOURCE -D_LIBCPP_HAS_NO_CONSTEXPR -Wall -W -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wno-missing-field-initializers -pedantic -Wno-long-long -Wno-maybe-uninitialized -Wdelete-non-virtual-dtor -Wno-comment -std=gnu++11 -O2 -DNDEBUG -fno-exceptions -fno-rtti -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -L%CLANGPATH%/lib -lclangFrontend -lclangDriver -lclangSerialization -lclangCodeGen -lclangParse -lclangSema -lclangAnalysis -lclangEdit -lclangAST -lclangLex -lclangBasic -lLLVMLTO -lLLVMObjCARCOpts -lLLVMMIRParser -lLLVMSymbolize -lLLVMDebugInfoPDB -lLLVMDebugInfoDWARF -lLLVMTableGen -lLLVMLineEditor -lLLVMOrcJIT -lLLVMCppBackendCodeGen -lLLVMCppBackendInfo -lLLVMARMDisassembler -lLLVMARMCodeGen -lLLVMARMAsmParser -lLLVMARMDesc -lLLVMARMInfo -lLLVMARMAsmPrinter -lLLVMLibDriver -lLLVMOption -lLLVMX86Disassembler -lLLVMX86AsmParser -lLLVMX86CodeGen -lLLVMSelectionDAG -lLLVMAsmPrinter -lLLVMX86Desc -lLLVMMCDisassembler -lLLVMX86Info -lLLVMX86AsmPrinter -lLLVMX86Utils -lLLVMMCJIT -lLLVMPasses -lLLVMipo -lLLVMVectorize -lLLVMLinker -lLLVMIRReader -lLLVMAsmParser -lLLVMDebugInfoCodeView -lLLVMInterpreter -lLLVMExecutionEngine -lLLVMRuntimeDyld -lLLVMCodeGen -lLLVMTarget -lLLVMScalarOpts -lLLVMInstCombine -lLLVMInstrumentation -lLLVMProfileData -lLLVMObject -lLLVMMCParser -lLLVMTransformUtils -lLLVMMC -lLLVMBitWriter -lLLVMBitReader -lLLVMAnalysis -lLLVMCore -lLLVMSupport -lole32 -luuid
if errorlevel 1 goto :fail
g++ -o bangra.exe %DIR%bangra.cpp -DBANGRA_MAIN_CPP_IMPL -L%DIR% -lbangra -O2
if errorlevel 1 goto :fail
%DIR%bangra.exe %DIR%bangra.b
if errorlevel 1 goto :fail
echo success.
goto :done
:fail
echo failed.
:done
