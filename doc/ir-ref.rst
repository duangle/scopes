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

Type Constructors
-----------------

.. ir-special:: (function return-type (? param-type ...) (? _:...))

.. ir-special:: (* type)

.. ir-special:: (array type count)

.. ir-special:: (vector type count)

.. ir-special:: (struct name (? _:packed) (? type ...))

.. ir-special:: (typeof value)

Definitions
-----------

.. ir-special:: (defvalue value)

.. ir-special:: (deftype value)

.. ir-special:: (define name ((? param ...)) type (? expression ...))

.. ir-special:: (declare c-symbol-string type)

Constant Values
---------------

.. ir-special:: (int type integer-value)

    Constructs an integer constant of ``type``.

    A naked integer is shorthand for ``(int i32 <number>)``.

.. ir-special:: (real type real-value)

    Constructs a real constant of ``type``.

.. ir-special:: (null type)

    Constructs a zero initializer for ``type``.

Flow Control
------------

.. ir-special:: (label name (? expression ...))

.. ir-special:: (call callee (? argument-value ...))

.. ir-special:: (ret (? return-value))

.. ir-special:: (br label-value)

.. ir-special:: (cond-br value then-label-value else-label-value)

.. ir-special:: (phi type (? (value label-value) ...))

.. ir-special:: (incoming phi-value (? (value label-value) ...))

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

Accessors
---------

.. ir-special:: (getelementptr value (? index-value ...))

.. ir-special:: (extractelement value index)

.. ir-special:: (extractvalue value index)

Memory
------

.. ir-special:: (align value bytes)

.. ir-special:: (load from-value)

.. ir-special:: (store from-value to-value)

.. ir-special:: (alloca type (? count-value))

Global Values
-------------

.. ir-special:: (constant global-value)

.. ir-special:: (global name constant-value)

Casting
-------

.. ir-special:: (bitcast value type)

.. ir-special:: (ptrtoint value type)

.. ir-special:: (intotptr value type)

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

.. ir-special:: (run function-value)

    Runs a function in the module as it is being defined. The function must
    match the signature ``(function void [Environment])``. If the environment
    parameter is defined, then the currently active translation environment
    will be passed.

.. ir-special:: (module name (? expression ...))

    Declares a new LLVM module with a new empty namespace.

.. ir-special:: (quote type element)

    Adds the symbolic expression ``element`` as global constant pointer to the
    module currently being defined and returns its value handle. This allows
    programs to create and process properly anchored expressions.

.. ir-special:: (nop)

    Ponders the futility of existence.
