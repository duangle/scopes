IR

include "../macros.b"

run
    print "hi"
    assert
        icmp == true true
    assert
        icmp == true true
        "that was wrong!"
    assert
        icmp == true true
        splice
            print "oops!"
            quote "aaargh"

    defvalue t
        new-table;
    defvalue t2
        clone t
    defvalue t3
        deep-clone t
    defvalue KEY
        quote KEY
    set-key! t KEY
        quote true
    print
        get-key t KEY
        get-key t2 KEY
        get-key t3 KEY
    assert
        value== t (clone t)
    assert
        not
            value== t (deep-clone t)
