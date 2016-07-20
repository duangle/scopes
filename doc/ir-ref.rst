IR Language Reference
=====================

This chapter will provide reference information for Bangra IR in a future release.

Built-in Types
--------------

.. ir-type:: void
.. ir-type:: i1
.. ir-type:: i8
.. ir-type:: i16
.. ir-type:: i32
.. ir-type:: i64
.. ir-type:: float
.. ir-type:: double
.. ir-type:: rawstring
.. ir-type:: opaque

Type Constructors
-----------------

.. ir-special:: (function return-type (? param-type ...) (? _:...))

.. ir-special:: (& type)

.. ir-special:: (array type count)

.. ir-special:: (vector type count)

.. ir-special:: (struct name (? _:packed) (? type ...))

.. ir-special:: (typeof value)

.. ir-special:: (getelementtype type (? index ...))

Definitions
-----------

.. ir-special:: (defvalue value)

.. ir-special:: (deftype value)

.. ir-special:: (defstruct name (? _:packed) (? type ...))

.. ir-special:: (define name ((? param ...)) type (? expression ...))

.. ir-special:: (declare c-symbol-string type)

.. ir-special:: (declare-global c-symbol-string type)

Aggregate Constructors
----------------------

.. ir-special:: (structof (| (? "" (? _:packed)) type) (? const-value ...))

.. ir-special:: (arrayof type (? const-value ...))

.. ir-special:: (vectorof const-value (? ...))

Constant Values
---------------

.. ir-special:: (int type integer-value)

    Constructs an integer constant of ``type``.

    A naked integer is shorthand for ``(int i32 <number>)``.

.. ir-special:: (real type real-value)

    Constructs a real constant of ``type``.

.. ir-special:: (null type)

    Constructs a zero initializer for ``type``.

.. ir-special:: (alignof type)

.. ir-special:: (sizeof type)

.. ir-special:: (lengthof type)

.. ir-macro:: (&str string)

    Constructs a global string constant and returns it as ``rawstring``.

Flow Control
------------

.. ir-special:: (block name)

.. ir-special:: (set-block block-expr)

.. ir-special:: (call callee (? argument-value ...))

.. ir-special:: (ret (? return-value))

.. ir-special:: (br label-value)

.. ir-special:: (cond-br value then-label-value else-label-value)

.. ir-special:: (phi type (? (value label-value) ...))

.. ir-special:: (incoming phi-value (? (value label-value) ...))

.. ir-macro:: (? condition-expr then-expr else-expr)

.. ir-macro:: (if (condition-expr expression ...) ... (? (_:else expression ...)))

.. ir-macro:: (loop var-name init-expr condition-expr iterate-expr expression ...)

Binary Operators
----------------

.. ir-special:: (add lhs rhs)

.. ir-special:: (add-nsw lhs rhs)

.. ir-special:: (add-nuw lhs rhs)

.. ir-special:: (fadd lhs rhs)

.. ir-special:: (sub lhs rhs)

.. ir-special:: (sub-nsw lhs rhs)

.. ir-special:: (sub-nuw lhs rhs)

.. ir-special:: (fsub lhs rhs)

.. ir-special:: (mul lhs rhs)

.. ir-special:: (mul-nsw lhs rhs)

.. ir-special:: (mul-nuw lhs rhs)

.. ir-special:: (fmul lhs rhs)

.. ir-special:: (udiv lhs rhs)

.. ir-special:: (sdiv lhs rhs)

.. ir-special:: (exact-sdiv lhs rhs)

.. ir-special:: (urem lhs rhs)

.. ir-special:: (srem lhs rhs)

.. ir-special:: (frem lhs rhs)

.. ir-special:: (shl lhs rhs)

.. ir-special:: (lshr lhs rhs)

.. ir-special:: (ashr lhs rhs)

.. ir-special:: (and lhs rhs)

.. ir-special:: (or lhs rhs)

.. ir-special:: (xor lhs rhs)

Comparators
-----------

.. ir-special:: (icmp op lhs rhs)

.. ir-special:: (fcmp op lhs rhs)

.. ir-special:: (select condition-expr then-value else-value)

Composition
-----------

.. ir-special:: (getelementptr value (? index-value ...))

.. ir-special:: (extractelement value index)

.. ir-special:: (insertelement value element index)

.. ir-special:: (shufflevector value1 value2 mask)

.. ir-special:: (extractvalue value index)

.. ir-special:: (insertvalue value element index)

Memory
------

.. ir-special:: (align value bytes)

.. ir-special:: (load from-value)

.. ir-special:: (store from-value to-value)

.. ir-special:: (alloca type (? count-value))

.. ir-special:: (va_arg va_list-value type)

Global Values
-------------

.. ir-special:: (constant global-value)

.. ir-special:: (global name constant-value)

Casting
-------

.. ir-special:: (trunc value type)

.. ir-special:: (zext value type)

.. ir-special:: (sext value type)

.. ir-special:: (fptrunc value type)

.. ir-special:: (fpext value type)

.. ir-special:: (fptoui value type)

.. ir-special:: (fptosi value type)

.. ir-special:: (uitofp value type)

.. ir-special:: (sitofp value type)

.. ir-special:: (ptrtoint value type)

.. ir-special:: (intotptr value type)

.. ir-special:: (bitcast value type)

.. ir-special:: (addrspacecast value type)

Debugging
---------

.. ir-special:: (dump-module)

.. ir-special:: (dump value)

.. ir-special:: (dumptype type)

Metaprogramming
---------------

.. ir-special:: (include filename-string)

    Includes expressions from another source file into the module currently being
    defined. ``filename-string`` is the path to the source file to be included,
    relative to the path of the expression's anchor.

.. ir-special:: (execute function-value)

    Executes a function in the module as it is being defined. The function must
    match the signature ``(function void [Environment])``. If the environment
    parameter is defined, then the currently active translation environment
    will be passed.

.. ir-macro:: (run expression ...)

.. ir-special:: (module name (| _:IR language) (? expression ...))

    Declares a new LLVM module with a new empty namespace. ``language`` must be
    a name with which a preprocessor has been registered, or ``IR`` for the
    default.

.. ir-special:: (quote type element)

    Adds the symbolic expression ``element`` as global constant pointer to the
    module currently being defined and returns its value handle. This allows
    programs to create and process properly anchored expressions.

.. ir-special:: (splice (? expression ...))

.. ir-special:: (error message-string)

.. ir-special:: (nop)

    Ponders the futility of existence.
