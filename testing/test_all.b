
set-scope-symbol! bangra
    quote path
    cons
        .. interpreter-dir "/testing/?.b"
        get-scope-symbol bangra (quote path)

let
    modules =
        quote (
            test_assorted
            test_iterator
            test_let
            test_loop
            test_pointer
            test_scope
            test_semicolon
            test_structof
            test_tableof
            test_tuple_array
            test_xlet)
    total =
        int (countof modules)

loop-for module in modules
    with
        failed = 0
    let module = (syntax->datum module)

    print
    print "* running:" module
    print "***********************************************"
    let ok =
        do
            try
                clear-traceback
                require module
                \ true
            except (e)
                print
                    traceback
                print "error running module:" e
                \ false
    continue
        ? ok failed (failed + 1)
else
    print
    print total "tests executed," (total - failed) "succeeded," failed "failed."

print "done."
