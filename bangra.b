# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

/// let fac
    ///function (n)
        select (<= n 1)
            1
            * n (this-function (- n 1))
    function (n)
        let fac-times
            function (n acc)
                select (== n 0)
                    acc
                    this-function (- n 1) (* acc n)
        fac-times n 1

print "hello world"

print
    repr "hi"
    | 1 2
    ~ 0xff
    not true
    not false
    + "hi" "ho"
    != "a" "b"
    * 2 3
    % 4.3 3
    == 4 5
    == 3 2
    == 3 3
    == 3 3.0
    == 3.0 3
    == 3 3.5
    == 2.5 2.5


