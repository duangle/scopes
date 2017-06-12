
#

    external sinf : float <- float
    external sin : double <- double

    print
        sinf 0.5
        sin (double 0.5)

# (typedef name [value])

fn unpack-list (values)
    if (empty? values)
    else
        return (list-at values)
            unpack-list (list-next values)

fn align (offset align)
    & (offset + align - (u64 1))
        ~ (align - (u64 1))

define stacktype
    type (quote stacktype)

define array
    let T =
        type (quote array)
    set-type-symbol! T (quote super) stacktype
    \ T

define arrayof
    fn (atype ...)
        let count =
            va-countof ...
        let T =
            array atype count
        let ET = T.element
        let ptr =
            bitcast size_t
                malloc (sizeof T)
        let stride = T.stride
        if (ET <: stacktype)
            print "stacktype!"
            let sz =
                sizeof ET
            # memory is at pointer address
            loop-for i k in (enumerate (va-iter ...))
                memcpy
                    bitcast pointer
                        ptr + stride * (u64 i)
                    bitcast pointer k
                    \ sz
                continue
        else
            loop-for i k in (enumerate (va-iter ...))
                store
                    bitcast pointer
                        ptr + stride * (u64 i)
                    \ k
                continue

define struct
    let T =
        type (quote struct)
    set-type-symbol! T (quote super) stacktype
    \ T

define union
    let T =
        type (quote union)
    set-type-symbol! T (quote super) stacktype
    \ T
define cfunction
    type (quote cfunction)
define null
    bitcast pointer (size_t 0)

syntax-extend
    fn config-numtype (ty)
        let sz = (type-sizeof ty)
        set-type-symbol! ty (quote size) sz
        set-type-symbol! ty (quote alignment) sz

    config-numtype i8
    config-numtype i16
    config-numtype i32
    config-numtype i64

    config-numtype u8
    config-numtype u16
    config-numtype u32
    config-numtype u64

    config-numtype r32
    config-numtype r64

    config-numtype pointer

    set-type-symbol! pointer (quote apply-type)
        fn (ET)
            assert (type? ET)
            let T =
                type
                    Symbol
                        .. "&" (string (Symbol ET))
            if (none? (type@ T (quote super)))
                set-type-symbol! T (quote super) pointer
                set-type-symbol! T (quote element) ET
            \ T

    set-type-symbol! array (quote apply-type)
        fn (ET size)
            let size = (u64 size)
            let T =
                type
                    Symbol
                        .. "[" (string (Symbol ET)) " x " (string size) "]"
            let stride =
                align
                    sizeof ET
                    alignof ET

            if (none? (type@ T (quote super)))
                set-type-symbol! T (quote super) array
                set-type-symbol! T (quote element) ET
                set-type-symbol! T (quote alignment) (alignof ET)
                set-type-symbol! T (quote stride) stride
                set-type-symbol! T (quote count) size
                set-type-symbol! T (quote size) (stride * size)
            \ T

    \ syntax-scope

define rawstring
    let rawstring = (pointer i8)
    set-type-symbol! rawstring (quote apply-type)
        fn (s)
            assert ((typeof s) == string)
            let s = (bitcast u64 s)
            let s = s + (u64 8)
            bitcast rawstring s

    \ rawstring

fn parse-c-defs (defs)
    let env = (Scope)

    let struct-map = (Scope)
    let union-map = (Scope)
    let enum-map = (Scope)
    let typedef-map = (Scope)
    set-scope-symbol! env (quote struct) struct-map
    set-scope-symbol! env (quote union) union-map
    set-scope-symbol! env (quote enum) enum-map

    let handlers = (Scope)

    fn parse (def)
        if (list? def)
            let head = (@ def 0)
            let handler = (@ handlers head)
            if (none? handler)
                dump def
                error
                    .. "no handler for " (repr head)
            else
                handler (unpack-list (slice def 1))
        else def

    fn make-handler (supertype map)
        let nsname =
            string (Symbol supertype)
        fn handle-struct (name fields anchor size align)
            let value = (@ map name)
            let T =
                if (none? value)
                    let T =
                        type
                            Symbol
                                .. "(" nsname " " (string name) ")"
                    if (none? (type@ T (quote super)))
                        set-type-symbol! T (quote super) supertype
                        set-scope-symbol! map name T
                    \ T
                else value
            if (not (none? fields))
                loop-for entry in fields
                    let field-name type-expr anchor offset = (unpack-list entry)
                    parse type-expr
                    continue
            \ T

    set-scope-symbol! handlers (quote struct) (make-handler struct struct-map)
    set-scope-symbol! handlers (quote union) (make-handler union union-map)

    set-scope-symbol! handlers (quote enum)
        fn handle-enum (name tag-type anchor tags)
            if (none? tag-type)
                let value = (@ enum-map name)
                assert (not (none? value)) "no such enum"
                \ value
            else
                assert (type? tag-type)
                set-scope-symbol! enum-map name tag-type
                loop-for entry in tags
                    let tagname tagvalue = (unpack-list entry)
                    set-scope-symbol! env tagname
                        tag-type tagvalue
                    continue
                \ tag-type

    set-scope-symbol! handlers (quote external)
        fn handle-external (name type-expr)
            let T =
                parse type-expr
            assert (type? T)
            let ptr =
                ffi-symbol (string name)
            assert ((bitcast size_t ptr) != 0)
            set-scope-symbol! env name
                bitcast T ptr

    set-scope-symbol! handlers (quote fntype)
        fn handle-fntype (vararg retarg args...)
            let argtypes argstr =
                loop-for arg in (va-iter args...)
                    let AT =
                        parse arg
                    assert (type? AT)
                    let a b = (continue)
                    break
                        list-cons AT a
                        .. (string (Symbol AT))
                            ? (empty? b) b (.. " " b)
                else
                    _ (list) ""
            let RT = (parse retarg)
            assert (type? RT)
            let T =
                type
                    Symbol
                        .. "(" (string (Symbol RT)) " <- " argstr
                            ? vararg " ..." ""
                            \ ")"
            if (none? (type@ T (quote super)))
                set-type-symbol! T (quote super) cfunction
                set-type-symbol! T (quote return) RT
                set-type-symbol! T (quote arguments) argtypes
                set-type-symbol! T (quote vararg) vararg
                set-type-symbol! T (quote call)
                    fn (self ...)
                        # validate arguments
                        loop-for n t in (zip-fill (va-iter ...) argtypes none none)
                            assert (not (none? t))
                                .. "excess argument, "
                                    repr (i32 (countof argtypes))
                                    \ " argument(s) required"
                            assert ((typeof n) == t)
                                .. "argument of type " (repr t) " expected, got " (repr (typeof n))
                            continue
                        ffi-call
                            bitcast pointer self
                            \ RT ...
            \ T

    set-scope-symbol! handlers (quote *)
        fn handle-pointer (type-expr)
            pointer
                parse type-expr

    set-scope-symbol! handlers (quote array)
        fn handle-array (type-expr size)
            array
                parse type-expr
                \ size

    set-scope-symbol! handlers (quote typedef)
        fn handle-typedef (name type-expr anchor)
            if (none? type-expr)
                let value = (@ typedef-map name)
                assert (not (none? value)) "no such typedef"
                \ value
            else
                let value = (parse type-expr)
                set-scope-symbol! typedef-map name value
                set-scope-symbol! env name value
                \ value

    loop-for def in defs
        parse def
        continue

    \ env

syntax-extend
    let llvm-defs =
        parse-c "llvm-import.c" "
            #include <llvm-c/Core.h>
            #include <llvm-c/ExecutionEngine.h>
            #include <stdlib.h>
            "
            \ "-I" (.. interpreter-dir "/clang/include")
            \ "-I" (.. interpreter-dir "/clang/lib/clang/3.9.1/include")

    let llvm =
        parse-c-defs llvm-defs
    set-scope-symbol! syntax-scope (quote malloc) llvm.malloc
    loop-for k v in llvm
        if ((slice (string k) 0 4) == "LLVM")
            set-scope-symbol! syntax-scope k v
        continue

    \ syntax-scope


do
    let
        LLVMFalse = 0
        LLVMTrue = 1
    #LLVMDumpType
        LLVMInt32Type;



    let builder = (LLVMCreateBuilder)
    let module = (LLVMModuleCreateWithName (rawstring "testmodule"))
    let functype = (LLVMFunctionType
            (LLVMInt32Type) (bitcast (pointer LLVMTypeRef) null) (u32 0) LLVMFalse)
    let func = (LLVMAddFunction module (rawstring "testfunc") functype)
    let bb = (LLVMAppendBasicBlock func (rawstring "entry"))
    LLVMPositionBuilderAtEnd builder bb
    LLVMBuildRet builder (LLVMConstInt (LLVMInt32Type) (u64 303) LLVMFalse)
    LLVMDumpValue func
    # outputs:
    # define i32 @testfunc() {
    #   ret i32 303
    # }
    print
        arrayof
            array int 4
            arrayof int 1 2 3 4
            arrayof int 5 6 7 8
            arrayof int 9 10 11 12
    print "yup"
 #
    let ee1 = (arrayof LLVMExecutionEngineRef (bitcast LLVMExecutionEngineRef null))
    let errormsg1 = (arrayof rawstring (bitcast rawstring null))
    if ((LLVMCreateJITCompilerForModule ee1 module (uint 0) errormsg1) == LLVMTrue)
        error (string (@ errormsg1 0))
    let ee = (@ ee1 0)
    let fptr = (bitcast (pointer (cfunction int32 (tuple) false))
            (LLVMGetPointerToGlobal ee func))
    assert ((fptr) == 303)
