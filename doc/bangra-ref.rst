Bangra Language Reference
=========================

Built-in Constants
------------------

.. define:: none
.. define:: true
.. define:: false

Runtime Constants
-----------------

.. define:: empty-list
.. define:: empty-tuple

Built-in Types
--------------

.. type:: closure
.. type:: flow
.. type:: frame
.. type:: half
.. type:: list
.. type:: parameter
.. type:: rawstring
.. type:: ssize_t
.. type:: string
.. type:: table
.. type:: usize_t
.. type:: void

Runtime Types
-------------

.. type:: bool
.. type:: double
.. type:: float
.. type:: int
.. type:: int8
.. type:: int16
.. type:: int32
.. type:: int64
.. type:: real16
.. type:: real32
.. type:: real64
.. type:: size_t
.. type:: uint
.. type:: uint8
.. type:: uint16
.. type:: uint32
.. type:: uint64

Built-in Supertypes
-------------------

.. type:: array
.. type:: cfunction
.. type:: enum
.. type:: integer
.. type:: pointer
.. type:: qualifier
.. type:: real
.. type:: tag
.. type:: tuple
.. type:: struct
.. type:: symbol
.. type:: vector

Runtime Supertypes
------------------

.. type:: iterator

Built-in Type Factories
-----------------------

.. type-factory:: (array type count)
.. type-factory:: (cfunction return-type parameter-tuple-type varargs?)
.. type-factory:: (integer bit-width signed?)
.. type-factory:: (pointer type)
.. type-factory:: (real bit-width)
.. type-factory:: (struct name-symbol)
.. type-factory:: (symbol name-string)
.. type-factory:: (tag name-symbol)
.. type-factory:: (tuple (? element-type ...))
.. type-factory:: (vector type count)

Builtin-in Special Forms
------------------------

.. special:: (call callable (? expression ...))
.. special:: (cc/call return-callable callable (? expression ...))
.. special:: (do (? expression ...) return-expression)
.. special:: (form:fn/cc (? name) ((? parameter-name ...)) (? expression ...) return-expression)
.. special:: (splice expression)

Built-in Macros
---------------

.. macro:: (fn/cc (? name) ((? parameter-name ...)) (? expression ...) return-expression)
.. macro:: (syntax-extend (? name) (return-callable scope-table) (? expression ...) scope-expression)

Runtime Macros
--------------

.. macro:: (::@ expression)
.. macro:: (::* expression)
.. macro:: (: name (? value))
.. macro:: (. value name)
.. macro:: (? condition-bool then-expression else-expression)
.. macro:: (and first-expression second-expression ...)
.. macro:: (assert expression (? error-message))
.. macro:: (define name compile-time-expression ...)
.. macro:: (dump-syntax expression (? ...))
.. macro:: (fn (? name) ((? parameter-name ...)) (? expression ...) return-expression)
.. macro:: (if condition-bool ... expression)
.. macro:: (elseif condition-bool ... expression)
.. macro:: (else expression ...)
.. macro:: (for name ... _:in iterable (? (_:with (name _:= value ...) ...)) body-expression ...)
.. macro:: (else expression ...)
.. macro:: (let name ... _:= expression ...)
.. macro:: (let (name ... _:= expression ...) ...)
.. macro:: (loop (| ((? name ...)) (_:with (name _:= value ...) ...)) body-expression ...)
.. macro:: (max first-expression second-expression ...)
.. macro:: (min first-expression second-expression ...)
.. macro:: (or first-expression second-expression ...)
.. macro:: (qquote value (? value ...))
.. macro:: (quote value (? value ...))
.. macro:: (set! parameter value)
.. macro:: (try expression ...)
.. macro:: (except (parameter) expression ...)
.. macro:: (xlet name _:= expression ...)
.. macro:: (xlet (name _:= expression ...) ...)

Built-in Functions
------------------

.. function:: (bitcast type value)
.. function:: (block-scope-macro closure)
.. function:: (branch condition-bool true-continuation false-continuation)
.. function:: (cons head (? list ...))
.. function:: (countof container-value)
.. function:: (cstr value-rawstring)
.. function:: (dump expression)
.. function:: (element-type type)
.. function:: (error message-string)
.. function:: (escape expression)
.. function:: (eval expression (? globals-table))
.. function:: (exit (? code))
.. function:: (expand expression-block-list scope-table)
.. function:: (external name-symbol cfunction-type)
.. function:: (get-exception-handler)
.. function:: (globals)
.. function:: (import-c path-string option-tuple)
.. function:: (list-load path-string)
.. function:: (list-parse expression-string)
.. function:: (next-key table key)
.. function:: (print (? expression ...))
.. function:: (prompt prompt-string (? prepend-string))
.. function:: (repr expression)
.. function:: (raise error-value)
.. function:: (set-exception-handler! closure)
.. function:: (set-globals! globals-table)
.. function:: (set-key! table key value)
.. function:: (structof (? key-value-tuple ...))
.. function:: (tableof (? key-value-tuple ...))
.. function:: (tupleof (? expression ...))
.. function:: (typeof expression)
.. function:: (va-arg index vararg-parameter...)
.. function:: (va-countof vararg-parameter...)

Runtime Functions
-----------------

.. function:: (block-macro function)
.. function:: (disqualify tag-type value)
.. function:: (empty? countable-value)
.. function:: (enumerate iterable (? from (? step)))
.. function:: (iter iterable-value)
.. function:: (iterator? value)
.. function:: (key? table key)
.. function:: (list-atom? value)
.. function:: (list-head? list symbol)
.. function:: (list? value)
.. function:: (load table key)
.. function:: (macro function)
.. function:: (none? value)
.. function:: (qualify tag-type value)
.. function:: (range count)
.. function:: (range start stop (? step))
.. function:: (require module-name-string)
.. function:: (symbol? value)
.. function:: (xpcall callable exception-callable)
.. function:: (zip left-iterable right-iterable)

Built-in Operators
------------------

.. function:: (== first-value second-value)
.. function:: (!= first-value second-value)
.. function:: (> first-value second-value)
.. function:: (>= first-value second-value)
.. function:: (< first-value second-value)
.. function:: (<= first-value second-value)
.. function:: (.. first-value second-value)
.. function:: (+ first-value second-value (? ...))
.. function:: (- first-value (? second-value))
.. function:: (* first-value second-value (? ...))
.. function:: (** base-number exponent-number)
.. function:: (/ first-value (? second-value))
.. function:: (// first-value second-value)
.. function:: (% first-value second-value)
.. function:: (| first-value second-value (? ...))
.. function:: (& first-value second-value)
.. function:: (^ first-value second-value)
.. function:: (~ expression)
.. function:: (<< value offset)
.. function:: (>> value offset)
.. function:: (@ container-value (| index-value name-symbol))
.. function:: (hash expression)
.. function:: (not expression)
.. function:: (slice expression start-index (? end-index))
.. function:: (string expression)

Runtime Operators
-----------------

.. function:: (@ container-value (| index-value name-symbol) ...)
