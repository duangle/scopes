IR

# the bootstrap language is feature equivalent to LLVM IR, and translates
# directly to LLVM API commands. It sounds like a Turing tarpit, but the fact
# that it gives us simple means to get out of it and extend those means
# turns it more into Turing lube.

# from here, outside the confines of the horribly slow and tedious C++ compiler
# context, we're going to implement our own language.

################################################################################
# declare the bang API as we don't have the means to comfortably import clang
# declarations yet.

# opaque declarations for the bang compiler Environment and the Values of
# its S-Expression tree, which can be Pointer, String, Symbol, Integer, Real.
struct _Environment
struct _Value
struct opaque

deftype rawstring (* i8)

declare printf (function i32 rawstring ...)
declare strcmp (function i32 rawstring rawstring)

declare bang_print (function void rawstring)

deftype Environment (* _Environment)
deftype Value (* _Value)

defvalue dump-value
    declare "bang_dump_value" (function void Value)

deftype preprocessor-func
    function Value Environment Value

defvalue set-preprocessor
    declare "bang_set_preprocessor" (function void (* preprocessor-func))

defvalue kind-of
    declare "bang_get_kind" (function i32 Value)

defvalue at
    declare "bang_at" (function Value Value)

defvalue next
    declare "bang_next" (function Value Value)

defvalue set-next
    declare "bang_set_next" (function Value Value Value)

defvalue ref
    declare "bang_ref" (function Value Value)

defvalue table
    declare "bang_table" (function Value)

defvalue set-key
    declare "bang_set_key" (function void Value Value Value)

defvalue get-key
    declare "bang_get_key" (function Value Value Value)

defvalue handle-value
    declare "bang_handle_value" (function (* opaque) Value)

defvalue handle
    declare "bang_handle" (function Value (* opaque))

defvalue value==
    declare "bang_eq" (function i1 Value Value)

defvalue string-value
    declare "bang_string_value" (function rawstring Value)

defvalue error-message
    declare "bang_error_message" (function void Value rawstring ...)

defvalue set-anchor
    declare "bang_set_anchor" (function Value Value Value)

# redeclare pointer types to specialize our mapping handler
deftype bang-mapper-func
    function Value Value i32 Value
defvalue bang-map
    declare "bang_map" (function Value Value (* bang-mapper-func) Value)

defvalue value-type-none 0
defvalue value-type-pointer 1
defvalue value-type-string 2
defvalue value-type-symbol 3
defvalue value-type-integer 4
defvalue value-type-real 5

################################################################################
# fundamental helper functions

defvalue pointer?
    define "" (value)
        function i1 Value
        label ""
            ret
                icmp ==
                    call kind-of value
                    value-type-pointer

# non-pointer or null pointer
defvalue atom?
    define "" (value)
        function i1 Value
        label ""
            cond-br
                call pointer? value
                label $is-pointer
                    ret
                        icmp ==
                            call at value
                            null Value
                label $is-not-pointer
                    ret (int i1 1)

defvalue symbol?
    define "" (value)
        function i1 Value
        label ""
            ret
                icmp ==
                    call kind-of value
                    value-type-symbol

defvalue expression?
    define "" (value expected-head)
        function i1 Value Value
        label ""
            cond-br
                call atom? value
                label $is-atom
                    ret (int i1 0)
                label $is-not-atom
                    ret
                        call value==
                            call at value
                            expected-head

defvalue type-key
    quote _Value "#bang-type"

defvalue set-type
    define "" (value value-type)
        function void Value Value
        label ""
            call set-key
                value
                type-key
                value-type
            ret;

defvalue get-type
    define "" (value)
        function Value Value
        label ""
            ret
                call get-key
                    value
                    type-key

defvalue ref-set-next
    define "" (lhs rhs)
        function Value Value Value
        label ""
            ret
                call ref
                    call set-next lhs rhs

# appends ys ... to xs ...
define join (xs ys)
    function Value Value Value
    label ""
        cond-br
            icmp == xs
                null Value
            label $is-null
                ret ys
            label $is-not-null
                ret
                    call set-next xs
                        call join
                            call next xs
                            ys
