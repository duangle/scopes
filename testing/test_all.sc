
set-scope-symbol! package 'path
    cons (.. compiler-dir "/testing/?.sc") package.path

let modules =
    quote
        test_assorted
        test_call_override
        test_dots
        test_extraparams
        test_fwdecl
        #test_iterator
        test_let
        test_loop
        test_reference
        test_regexp
        test_scope
        test_scope_iter
        test_semicolon
        #test_structof
        #test_tableof
        #test_tuple_array
        #test_xlet

fn run-tests ()
    let total =
        i32 (countof modules)

    let [loop] modules failed = (unconst modules) (unconst 0)
    if (empty? modules)
        print
        print total "tests executed," (total - failed) "succeeded," failed "failed."
        print "done."
        return;
    
    let module modules = (decons modules)
    let module = (cast Symbol module)
    print
    print "* running:" module
    print "***********************************************"
    let ok =
        xpcall
            fn ()
                require module
                true
            fn (exc)
                io-write!
                    format-exception exc
                false
    loop modules
        ? ok failed (failed + 1)

run-tests;
