@echo off
rem you need msys2 and the llvm and clang 3.8 packages installed.
setlocal
set DIR=%~dp0
set CLANGPATH=C:\msys64\mingw64
set PATH=%CLANGPATH%\bin;%PATH%
rem -lpthread -lm -ldl -ltinfo -lz
rem -lclangFrontend -lclangDriver -lclangSerialization -lclangCodeGen -lclangParse -lclangSema -lclangAnalysis -lclangEdit -lclangAST -lclangLex -lclangBasic
rem -lLLVMLTO -lLLVMObjCARCOpts -lLLVMMIRParser -lLLVMSymbolize -lLLVMDebugInfoPDB -lLLVMDebugInfoDWARF -lLLVMTableGen -lLLVMLineEditor -lLLVMOrcJIT -lLLVMCppBackendCodeGen -lLLVMCppBackendInfo -lLLVMARMDisassembler -lLLVMARMCodeGen -lLLVMARMAsmParser -lLLVMARMDesc -lLLVMARMInfo -lLLVMARMAsmPrinter -lLLVMLibDriver -lLLVMOption -lLLVMX86Disassembler -lLLVMX86AsmParser -lLLVMX86CodeGen -lLLVMSelectionDAG -lLLVMAsmPrinter -lLLVMX86Desc -lLLVMMCDisassembler -lLLVMX86Info -lLLVMX86AsmPrinter -lLLVMX86Utils -lLLVMMCJIT -lLLVMPasses -lLLVMipo -lLLVMVectorize -lLLVMLinker -lLLVMIRReader -lLLVMAsmParser -lLLVMDebugInfoCodeView -lLLVMInterpreter -lLLVMExecutionEngine -lLLVMRuntimeDyld -lLLVMCodeGen -lLLVMTarget -lLLVMScalarOpts -lLLVMInstCombine -lLLVMInstrumentation -lLLVMProfileData -lLLVMObject -lLLVMMCParser -lLLVMTransformUtils -lLLVMMC -lLLVMBitWriter -lLLVMBitReader -lLLVMAnalysis -lLLVMCore -lLLVMSupport
g++ -c -o mman.o mman.c -Wno-shift-count-overflow
if errorlevel 1 goto :fail
g++ -o bang0 %DIR%bang0.cpp mman.o -I%CLANGPATH%/include -Wno-vla -D_GNU_SOURCE -D_LIBCPP_HAS_NO_CONSTEXPR -Wall -W -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wno-missing-field-initializers -pedantic -Wno-long-long -Wno-maybe-uninitialized -Wdelete-non-virtual-dtor -Wno-comment -std=gnu++11 -O2 -DNDEBUG -fno-exceptions -fno-rtti -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -L%CLANGPATH%/lib -lclangFrontend -lclangDriver -lclangSerialization -lclangCodeGen -lclangParse -lclangSema -lclangAnalysis -lclangEdit -lclangAST -lclangLex -lclangBasic -lLLVMLTO -lLLVMObjCARCOpts -lLLVMMIRParser -lLLVMSymbolize -lLLVMDebugInfoPDB -lLLVMDebugInfoDWARF -lLLVMTableGen -lLLVMLineEditor -lLLVMOrcJIT -lLLVMCppBackendCodeGen -lLLVMCppBackendInfo -lLLVMARMDisassembler -lLLVMARMCodeGen -lLLVMARMAsmParser -lLLVMARMDesc -lLLVMARMInfo -lLLVMARMAsmPrinter -lLLVMLibDriver -lLLVMOption -lLLVMX86Disassembler -lLLVMX86AsmParser -lLLVMX86CodeGen -lLLVMSelectionDAG -lLLVMAsmPrinter -lLLVMX86Desc -lLLVMMCDisassembler -lLLVMX86Info -lLLVMX86AsmPrinter -lLLVMX86Utils -lLLVMMCJIT -lLLVMPasses -lLLVMipo -lLLVMVectorize -lLLVMLinker -lLLVMIRReader -lLLVMAsmParser -lLLVMDebugInfoCodeView -lLLVMInterpreter -lLLVMExecutionEngine -lLLVMRuntimeDyld -lLLVMCodeGen -lLLVMTarget -lLLVMScalarOpts -lLLVMInstCombine -lLLVMInstrumentation -lLLVMProfileData -lLLVMObject -lLLVMMCParser -lLLVMTransformUtils -lLLVMMC -lLLVMBitWriter -lLLVMBitReader -lLLVMAnalysis -lLLVMCore -lLLVMSupport -lole32 -luuid
if errorlevel 1 goto :fail
%DIR%bang0.exe %DIR%bang1.b
if errorlevel 1 goto :fail
echo success.
goto :done
:fail
echo failed.
:done
