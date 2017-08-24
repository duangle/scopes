
let modules =
    quote
        .test_ansi_colors
        .test_assorted
        .test_call_override
        .test_clang
        .test_closure
        .test_dots
        .test_extraparams
        .test_folding
        .test_fwdecl
        .test_glsl
        .test_iter2
        #.test_iterator
        .test_let
        .test_locals
        .test_loop
        .test_overload
        .test_reference
        .test_regexp
        .test_scope
        .test_scope_iter
        .test_submod
        .test_semicolon
        #.test_structof
        #.test_tableof
        #.test_tuple_array
        #.test_xlet
        .test_using
        .test_while
        .test_varargs
        .test_vector

fn run-tests ()
    let total =
        i32 (countof modules)

    let loop (modules failed) = (unconst modules) (unconst 0)
    if (empty? modules)
        print
        print total "tests executed," (total - failed) "succeeded," failed "failed."
        print "done."
        return;

    let module modules = (decons modules)
    let module = (module as Symbol)
    print
    print "* running:" module
    print "***********************************************"
    let ok =
        xpcall
            fn ()
                require-from module-dir module
                unconst true
            fn (exc)
                io-write!
                    format-exception exc
                unconst false
    loop modules
        ? ok failed (failed + 1)

run-tests;
