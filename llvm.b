IR

# not much is here yet; waiting for the bangra-clang bridge to get finished

defstruct LLVMModule
defstruct LLVMValue
defstruct LLVMType
defstruct LLVMExecutionEngine
defstruct LLVMTargetMachine

deftype LLVMModuleRef (& LLVMModule)
deftype LLVMValueRef (& LLVMValue)
deftype LLVMTypeRef (& LLVMType)
deftype LLVMExecutionEngineRef (& LLVMExecutionEngine)
deftype LLVMTargetMachineRef (& LLVMTargetMachine)

# Core.h
#-------------------------------------------------------------------------------

declare LLVMDisposeMessage
    function void rawstring

declare LLVMDumpModule (function void LLVMModuleRef)

# ExecutionEngine.h
#-------------------------------------------------------------------------------

declare LLVMGetExecutionEngineTargetMachine
    function LLVMTargetMachineRef LLVMExecutionEngineRef
declare LLVMAddModule
    function void LLVMExecutionEngineRef LLVMModuleRef

# TargetMachine.h
#-------------------------------------------------------------------------------

defvalue LLVMAssemblyFile 0
defvalue LLVMObjectFile 1

declare LLVMTargetMachineEmitToFile
    function i1 LLVMTargetMachineRef LLVMModuleRef rawstring
        i32 # LLVMCodeGenFileType
        & rawstring

#-------------------------------------------------------------------------------
