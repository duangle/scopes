set-key! bangra
    : path
        cons
            .. interpreter-dir "/testing/?.b"
            bangra.path

let
    modules =
        quote
            test_assorted
            test_iterator
            test_batchfold
            test_semicolon
    total =
        countof modules
    failed = 0
loop (modules failed)
    if (not (empty? modules))
        let
            module =
                @ modules 0
            next-modules =
                slice modules 1

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
        repeat next-modules
            ? ok failed (failed + 1)
    else
        print;
        print total "tests executed," (total - failed) "succeeded," failed "failed."

print "done."
