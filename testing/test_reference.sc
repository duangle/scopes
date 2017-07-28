

let reference = (typename "reference")
set-type-symbol! reference 'apply-type
    fn (element)
        # due to auto-memoization, we'll always get the same type back
            provided the element type is a constant
        assert (constant? element)
        assert-typeof element type
        let T = (typename (.. "&" (type-name element)))
        let ptrtype = (pointer element)
        set-typename-super! T reference
        set-typename-storage! T ptrtype
        set-type-symbol! T 'apply-type
            fn (value)
                bitcast value T
        set-type-symbol! T 'repr
            fn (self)
                repr (load (bitcast self ptrtype))
        set-type-symbol! T '=
            fn (self value)
                store value (bitcast self ptrtype)
                true
        T

fn = (obj value)
    (op2-dispatch '=) obj value
    return;

define-infix< 800 =

let x = ((reference i32) (alloca i32))
x = 5
print x

