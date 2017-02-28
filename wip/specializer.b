# specializer: specializes and translates flow graph to LLVM

syntax-extend (_ scope)
    # inject all symbols directly into namespace
    let T =
        extern-library
            parse-c
                "llvm-import.c"
                tupleof
                    \ "-I" (.. interpreter-dir "/clang/include")
                    \ "-I" (.. interpreter-dir "/clang/lib/clang/3.9.1/include")
                "
                #include <llvm-c/Core.h>
                #include <llvm-c/ExecutionEngine.h>
                "
    for k v in T
        let kstr = (string k)
        # import only LLVM-related symbols, avoid the stddef garbage
        if ((slice kstr 0 4) == "LLVM")
            set-key! scope k v
        repeat;
    scope

fn ANSI-color (num bright)
    .. "\x1b["
        string num
        ? bright ";1m" "m"

let
    ANSI_RESET              = (ANSI-color 0  false)
    ANSI_COLOR_BLACK        = (ANSI-color 30 false)
    ANSI_COLOR_RED          = (ANSI-color 31 false)
    ANSI_COLOR_GREEN        = (ANSI-color 32 false)
    ANSI_COLOR_YELLOW       = (ANSI-color 33 false)
    ANSI_COLOR_BLUE         = (ANSI-color 34 false)
    ANSI_COLOR_MAGENTA      = (ANSI-color 35 false)
    ANSI_COLOR_CYAN         = (ANSI-color 36 false)
    ANSI_COLOR_GRAY60       = (ANSI-color 37 false)

    ANSI_COLOR_GRAY30       = (ANSI-color 30 true)
    ANSI_COLOR_XRED         = (ANSI-color 31 true)
    ANSI_COLOR_XGREEN       = (ANSI-color 32 true)
    ANSI_COLOR_XYELLOW      = (ANSI-color 33 true)
    ANSI_COLOR_XBLUE        = (ANSI-color 34 true)
    ANSI_COLOR_XMAGENTA     = (ANSI-color 35 true)
    ANSI_COLOR_XCYAN        = (ANSI-color 36 true)
    ANSI_COLOR_WHITE        = (ANSI-color 37 true)

    ANSI_STYLE_STRING       = ANSI_COLOR_XMAGENTA
    ANSI_STYLE_NUMBER       = ANSI_COLOR_XGREEN
    ANSI_STYLE_KEYWORD      = ANSI_COLOR_XBLUE
    ANSI_STYLE_OPERATOR     = ANSI_COLOR_XCYAN
    ANSI_STYLE_INSTRUCTION  = ANSI_COLOR_YELLOW
    ANSI_STYLE_TYPE         = ANSI_COLOR_XYELLOW
    ANSI_STYLE_COMMENT      = ANSI_COLOR_GRAY30
    ANSI_STYLE_ERROR        = ANSI_COLOR_XRED
    ANSI_STYLE_LOCATION     = ANSI_COLOR_XCYAN

    ANSI-wrapper =
        ? support-ANSI?
            fn (code)
                fn (content)
                    .. code content ANSI_RESET
            fn (code)
                fn (content) content

    style-string        = (ANSI-wrapper ANSI_COLOR_XMAGENTA)
    style-number        = (ANSI-wrapper ANSI_COLOR_XGREEN)
    style-keyword       = (ANSI-wrapper ANSI_COLOR_XBLUE)
    style-operator      = (ANSI-wrapper ANSI_COLOR_XCYAN)
    style-instruction   = (ANSI-wrapper ANSI_COLOR_YELLOW)
    style-type          = (ANSI-wrapper ANSI_COLOR_XYELLOW)
    style-comment       = (ANSI-wrapper ANSI_COLOR_GRAY30)
    style-error         = (ANSI-wrapper ANSI_COLOR_XRED)
    style-location      = (ANSI-wrapper ANSI_COLOR_XCYAN)

    LAMBDA_CHAR =
        style-keyword "λ"

fn flow-label (aflow)
    let name =
        string (flow-name aflow)
    .. LAMBDA_CHAR
        string
            flow-id aflow
        ? (empty? name) ""
            style-string
                .. "\""
                    string
                        flow-name aflow
                    "\""

fn closure-label (aclosure)
    ..
        style-comment "<"
        flow-label aclosure.entry
        style-comment ">"

fn iter-f (f arange args...)
    fn (yield)
        for i in arange
            yield
                tupleof i
                    f args... i
            repeat;

fn flow-iter-eval-arguments (aflow aframe)
    let acount =
        flow-argument-count aflow
    iter-f
        fn (aflow index)
            let arg =
                flow-argument aflow index
            ? ((typeof arg) == parameter)
                tupleof arg
                    frame-eval aframe index arg
                tupleof arg arg
        range acount
        aflow

fn flow-iter-arguments (aflow aframe)
    let acount =
        flow-argument-count aflow
    iter-f flow-argument
        range acount
        aflow

fn param-label (aparam)
    let name =
        string aparam.name
    ..
        style-keyword "@"
        ? (empty? name)
            string aparam.index
            style-instruction
                string aparam.name

fn flow-decl-label (aflow aframe)
    ..
        do
            let
                pcount =
                    flow-parameter-count aflow
                idx = 1
                s =
                    ..
                        param-label
                            flow-parameter aflow 0
                        style-operator " ⮕ "
                        flow-label aflow
                        " "
                        style-operator "("

            loop
                with idx s
                if (idx < pcount)
                    let param =
                        flow-parameter aflow idx
                    repeat
                        idx + 1
                        .. s
                            ? (idx == 1) "" " "
                            param-label param
                else
                    .. s
                        style-operator "):"
        "\n    "
        do
            fn is (a b)
                and
                    (typeof a) == (typeof b)
                    a == b

            for i args in (flow-iter-eval-arguments aflow aframe)
                with
                    str = ""
                let arg exp-arg = args
                let
                    argtype =
                        typeof arg
                repeat
                    ..
                        str
                        ? (i == 1)
                            style-operator " ←  ["
                            " "
                        if (argtype == parameter)
                            ..
                                ? (arg.flow == aflow) ""
                                    flow-label arg.flow
                                param-label arg
                                ? (is arg exp-arg)
                                    ""
                                    ..
                                        style-operator "="
                                        string exp-arg
                        elseif (argtype == closure)
                            closure-label arg
                        elseif (argtype == flow)
                            flow-label arg
                        else
                            repr arg
            else
                str
        style-operator "]"

fn dump-function (afunc)
    let visited = (tableof)
    fn dump-closure (aclosure)
        fn dump-flow (aflow aframe)
            if (none? (visited @ aflow))
                print
                    flow-decl-label aflow aframe
                set-key! visited aflow true
                for i args in (flow-iter-eval-arguments aflow aframe)
                    let oarg arg = args
                    let argtype =
                        typeof arg
                    if (argtype == closure)
                        dump-closure arg
                    elseif (argtype == flow)
                        dump-flow arg aframe
                    repeat;
        dump-flow aclosure.entry aclosure.frame
    dump-closure afunc

#### test #####

fn pow2 (x)
    * x x

fn pow (x n)
    if (n == 0) 1
    elseif ((n % 2) == 0) (pow2 (pow x (n // 2)))
    else (x * (pow x (n - 1)))

assert
    (pow 2 5) == 32

dump-function pow2
#dump-function testfunc

let
    LLVMFalse = 0
    LLVMTrue = 1
/// LLVMDumpType
    LLVMInt32Type;


let builder = (LLVMCreateBuilder)
let module = (LLVMModuleCreateWithName (rawstring "testmodule"))
let functype = (LLVMFunctionType
        (LLVMInt32Type) (bitcast (pointer LLVMTypeRef) null) (uint 0) LLVMFalse)
let func = (LLVMAddFunction module (rawstring "testfunc") functype)
let bb = (LLVMAppendBasicBlock func (rawstring "entry"))
LLVMPositionBuilderAtEnd builder bb
LLVMBuildRet builder (LLVMConstInt (LLVMInt32Type) (uint64 303) LLVMFalse)
LLVMDumpValue func
# outputs:
# define i32 @testfunc() {
#   ret i32 303
# }
let ee1 = (arrayof LLVMExecutionEngineRef (bitcast LLVMExecutionEngineRef null))
let errormsg1 = (arrayof rawstring null (bitcast rawstring null))
if ((LLVMCreateJITCompilerForModule ee1 module (uint 0) errormsg1) == LLVMTrue)
    error (string (@ errormsg1 0))
let ee = (@ ee1 0)
let fptr = (bitcast (pointer (cfunction int32 (tuple) false))
        (LLVMGetPointerToGlobal ee func))
assert ((fptr) == 303)




