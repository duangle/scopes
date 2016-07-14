IR

include "../libc.b"

define main ()
    function void
    label ""
        call printf
            bitcast (global "" "hello from main!\n") rawstring
        ret;

# use a new module to write the parent module to an object file
module "submodule" IR

    include "../api.b"
    include "../libc.b"

    execute
        define "" (env)
            function void Environment
            label ""
                call printf
                    bitcast (global "" "running in sub module!\n") rawstring
                defvalue module
                    call llvm-module
                        call meta-env env
                call LLVMDumpModule module
                defvalue engine
                    call llvm-engine env
                defvalue machine
                    call LLVMGetExecutionEngineTargetMachine engine
                defvalue msg
                    alloca rawstring
                store (null rawstring) msg
                defvalue result
                    call LLVMTargetMachineEmitToFile machine module
                        bitcast (global "" "test.o") rawstring
                        LLVMObjectFile
                        msg
                cond-br result
                    label $error
                    label $done
            label $error
                call printf
                    bitcast (global "" "%s\n") rawstring
                    load msg
                br $done
            label $done
                call LLVMDisposeMessage (load msg)
                ret;
