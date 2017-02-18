set-key! bangra
    : path
        cons
            .. interpreter-dir "/testing/?.b"
            bangra.path

let modules =
    quote
        test_iterator
        test_assorted

loop (modules)
    if (not (empty? modules))
        let module =
            @ modules 0
        print "* running:" module
        print "***********************************************"
        require module
        repeat
            slice modules 1

print "done."
