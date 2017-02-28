set-key! bangra
    : path
        cons
            .. interpreter-dir "/testing/?.b"
            bangra.path

let
    modules =
        quote
            test_array
            test_assorted
            test_iterator
            test_batchfold
            test_let
            test_loop
            test_semicolon
            test_xlet
    total =
        countof modules

for module in modules
    with
        failed = 0

    print;
    print "* running:" module
    print "***********************************************"
    let ok =
        try
            require module
            true
        except (e)
            print "error running module:" e
            false
    repeat
        ? ok failed (failed + 1)
else
    print;
    print total "tests executed," (total - failed) "succeeded," failed "failed."

print "done."
