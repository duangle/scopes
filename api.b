IR

include "libc.b"

################################################################################
# declare the bang API as we don't have the means to comfortably import clang
# declarations yet.

# opaque declarations for the bang compiler Environment and the Values of
# its S-Expression tree, which can be Pointer, String, Symbol, Integer, Real.
struct _Environment
struct _Value

declare bang_print (function void rawstring)

deftype Environment (* _Environment)
deftype Value (* _Value)

defvalue value-type-none 0
defvalue value-type-pointer 1
defvalue value-type-string 2
defvalue value-type-symbol 3
defvalue value-type-integer 4
defvalue value-type-real 5

# methods that apply to all types
#-------------------------------------------------------------------------------

defvalue kind-of
    declare "bang_get_kind" (function i32 Value)
defvalue value==
    declare "bang_eq" (function i1 Value Value)

defvalue next
    declare "bang_next" (function Value Value)
defvalue set-next
    declare "bang_set_next" (function Value Value Value)

defvalue dump-value
    declare "bang_dump_value" (function void Value)

defvalue anchor-path
    declare "bang_anchor_path" (function rawstring Value)
defvalue anchor-lineno
    declare "bang_anchor_lineno" (function i32 Value)
defvalue anchor-column
    declare "bang_anchor_column" (function i32 Value)
defvalue anchor-offset
    declare "bang_anchor_offset" (function i32 Value)
defvalue set-anchor
    declare "bang_set_anchor" (function Value Value rawstring i32 i32 i32)

# pointer
#-------------------------------------------------------------------------------

defvalue ref
    declare "bang_ref" (function Value Value)
defvalue at
    declare "bang_at" (function Value Value)

# string and symbol
#-------------------------------------------------------------------------------

defvalue new-string
    declare "bang_string" (function Value rawstring)
defvalue new-symbol
    declare "bang_symbol" (function Value rawstring)
defvalue string-value
    declare "bang_string_value" (function rawstring Value)

# real
#-------------------------------------------------------------------------------

defvalue new-real
    declare "bang_real" (function Value double)
defvalue real-value
    declare "bang_real_value" (function double Value)

# integer
#-------------------------------------------------------------------------------

defvalue new-integer
    declare "bang_integer" (function Value i64)
defvalue integer-value
    declare "bang_integer_value" (function i64 Value)

# table
#-------------------------------------------------------------------------------

defvalue new-table
    declare "bang_table" (function Value)
defvalue set-key
    declare "bang_set_key" (function void Value Value Value)
defvalue get-key
    declare "bang_get_key" (function Value Value Value)

# handle
#-------------------------------------------------------------------------------

defvalue new-handle
    declare "bang_handle" (function Value (* opaque))
defvalue handle-value
    declare "bang_handle_value" (function (* opaque) Value)

# preprocessing
#-------------------------------------------------------------------------------

deftype preprocessor-func
    function Value Environment Value

defvalue error-message
    declare "bang_error_message" (function void Value rawstring ...)
defvalue set-preprocessor
    declare "bang_set_preprocessor" (function void (* preprocessor-func))

# redeclare pointer types to specialize our mapping handler
deftype bang-mapper-func
    function Value Value i32 Value
defvalue bang-map
    declare "bang_map" (function Value Value (* bang-mapper-func) Value)

# helpers
################################################################################

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
