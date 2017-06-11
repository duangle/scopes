
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

define array
    type (quote array)
define struct
    type (quote struct)
define union
    type (quote union)
define cfunction
    type (quote cfunction)

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
            let ET =
                parse type-expr
            assert (type? ET)
            let T =
                type
                    Symbol
                        .. "&" (string (Symbol ET))
            if (none? (type@ T (quote super)))
                set-type-symbol! T (quote super) pointer
                set-type-symbol! T (quote element) ET
            \ T

    set-scope-symbol! handlers (quote array)
        fn handle-array (type-expr size)
            let ET =
                parse type-expr
            let T =
                type
                    Symbol
                        .. "[" (string (Symbol ET)) " x " (string size) "]"
            if (none? (type@ T (quote super)))
                set-type-symbol! T (quote super) array
                set-type-symbol! T (quote element) ET
                set-type-symbol! T (quote size) size
            \ T

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

let llvm-defs =
    parse-c "llvm-import.c" "
        #include <llvm-c/Core.h>
        #include <llvm-c/ExecutionEngine.h>
        "
        \ "-I" (.. interpreter-dir "/clang/include")
        \ "-I" (.. interpreter-dir "/clang/lib/clang/3.9.1/include")

let llvm =
    parse-c-defs llvm-defs

#loop-for k v in llvm
    print k v
    continue

print
    (llvm.LLVMInt32Type)
