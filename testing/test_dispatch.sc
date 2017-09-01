
fn param-types (types...)
    let typesz = (va-countof types...)
    fn make-typesig ()
        let keys... = (va-keys types...)
        let loop (i s) = 0 ""
        if (i == typesz)
            return s
        let T = (va@ i types...)
        let k = (va@ i keys...)
        loop (add i 1)
            .. s
                if (k == unnamed)
                    repr T
                else
                    .. (k as string) "=" (repr T)
                " "
    fn "with-target" (f)
        fn "with-error-fn" (f-error)
            fn (args...)
                let sz = (va-countof args...)
                if (sz != typesz)
                    return
                        f-error
                            fn ()
                                .. "argument mismatch (expected "
                                    repr typesz
                                    " but got "
                                    repr sz
                                    ")"
                            make-typesig
                let loop (i outargs...) = sz
                if (icmp== i 0)
                    return
                        f outargs...
                let i = (sub i 1)
                let T = (va@ i types...)
                let arg = (va@ i args...)
                let result... = (forward-imply arg T)
                if (va-empty? result...)
                    return
                        f-error
                            fn ()
                                .. "couldn't cast argument type from "
                                    repr (typeof arg)
                                    " to parameter type "
                                    repr T
                            make-typesig
                loop i result... outargs...

fn chain-type-dispatch2 (f1 f2)
    fn "with-error-fn" (f-error)
        fn (args...)
            call
                f1
                    fn (msgf typesigf1)
                        call
                            f2
                                fn error-handler (msgf typesigf2...)
                                    f-error "could not match arguments to function" typesigf1 typesigf2...
                            args...
                args...
let chain-type-dispatch = (op2-rtl-multiop chain-type-dispatch2)

fn type-dispatch-finalize (f)
    f
        fn error-handler (msgf sigfs...)
            compiler-message "expected one of"
            let sz = (va-countof sigfs...)
            let loop (i) = 0
            if (i == sz)
                compiler-error! msgf
            else
                let sigf = (va@ i sigfs...)
                compiler-message (sigf)
                loop (add i 1)

fn fn-dispatch (args...)
    type-dispatch-finalize
        chain-type-dispatch args...

let m =
    fn-dispatch
        call
            param-types
                x = i32; y = i32; z = i32
            fn (a b c)
                add a (add b c)
        call
            param-types f32 f32 f32
            fn (a b c)
                fadd a (fadd b c)
        call
            param-types string string string
            fn (a b c)
                .. a b c

var x = 1
assert ((m x 2 3) == 6)
assert ((m 1.0 2.0 3.0) == 6.0)
assert ((m "a" "b" "c") == "abc")
#dump
    m true

false

