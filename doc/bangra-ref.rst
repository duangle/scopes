Bangra Language Reference
=========================

Built-in Constants
------------------

.. define:: none
.. define:: true
.. define:: false

Built-in Types
--------------

.. type:: bool
.. type:: closure
.. type:: double
.. type:: float
.. type:: flow
.. type:: frame
.. type:: half
.. type:: int
.. type:: int8
.. type:: int16
.. type:: int32
.. type:: int64
.. type:: list
.. type:: parameter
.. type:: rawstring
.. type:: uint
.. type:: uint8
.. type:: uint16
.. type:: uint32
.. type:: uint64
.. type:: usize_t
.. type:: void

Built-in Supertypes
-------------------

.. type:: array
.. type:: cfunction
.. type:: enum
.. type:: integer
.. type:: pointer
.. type:: real
.. type:: tuple
.. type:: struct
.. type:: symbol
.. type:: vector

Built-in Type Factories
-----------------------

.. type-factory:: (array type count)
.. type-factory:: (cfunction return-type parameter-type varargs?)
.. type-factory:: (integer bit-width signed?)
.. type-factory:: (pointer type)
.. type-factory:: (real bit-width)
.. type-factory:: (struct name-symbol)
.. type-factory:: (symbol name-string)
.. type-factory:: (tuple (? element-type ...))
.. type-factory:: (vector type count)

Builtin-in Special Forms
------------------------

.. special:: (call callable (? expression ...))
.. special:: (do (? expression ...) return-expression)
.. special:: (form:continuation (? name) ((? parameter-name ...)) (? expression ...) return-expression)
.. special:: (splice expression)

Built-in Macros
---------------

.. macro:: (continuation (? name) ((? parameter-name ...)) (? expression ...) return-expression)
.. macro:: (syntax-extend (? name) ((? parameter-name ...)) (? expression ...) return-expression)

Built-in Functions
------------------

.. function:: (block-scope-macro closure)
.. function:: (branch condition-bool true-continuation false-continuation)
.. function:: (cons head (? list ...))
.. function:: (countof container-value)
.. function:: (cstr value-rawstring)
.. function:: (dump expression)
.. function:: (error message-string)
.. function:: (escape expression)
.. function:: (eval expression (? globals-table))
.. function:: (exit (? code))
.. function:: (expand expression-block-list scope-table)
.. function:: (external name-symbol cfunction-type)
.. function:: (flowcall callable (? expression ...))
.. function:: (get-exception-handler)
.. function:: (globals)
.. function:: (import-c path-string option-tuple)
.. function:: (list-load path-string)
.. function:: (next-key table key)
.. function:: (print (? expression ...))
.. function:: (repr expression)
.. function:: (set-exception-handler! closure)
.. function:: (set-globals! globals-table)
.. function:: (set-key! table key value)
.. function:: (structof (? key-value-tuple ...))
.. function:: (table (? key-value-tuple ...))
.. function:: (table-join left-table right-table)
.. function:: (tupleof (? expression ...))
.. function:: (typeof expression)

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

