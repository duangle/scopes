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
