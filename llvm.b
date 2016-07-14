IR

# not much is here yet; waiting for the bangra-clang bridge to get finished

struct LLVMModule
struct LLVMValue
struct LLVMType
struct LLVMExecutionEngine
struct LLVMTargetMachine

deftype LLVMModuleRef (* LLVMModule)
deftype LLVMValueRef (* LLVMValue)
deftype LLVMTypeRef (* LLVMType)
deftype LLVMExecutionEngineRef (* LLVMExecutionEngine)
deftype LLVMTargetMachineRef (* LLVMTargetMachine)

# Core.h
#-------------------------------------------------------------------------------

declare LLVMDisposeMessage
    function void rawstring

declare LLVMDumpModule (function void LLVMModuleRef)

# ExecutionEngine.h
#-------------------------------------------------------------------------------

declare LLVMGetExecutionEngineTargetMachine
    function LLVMTargetMachineRef LLVMExecutionEngineRef

# TargetMachine.h
#-------------------------------------------------------------------------------

defvalue LLVMAssemblyFile 0
defvalue LLVMObjectFile 1

declare LLVMTargetMachineEmitToFile
    function i1 LLVMTargetMachineRef LLVMModuleRef rawstring
        i32 # LLVMCodeGenFileType
        * rawstring

#-------------------------------------------------------------------------------
