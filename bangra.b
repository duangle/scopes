# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let fac
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

inspect
    fac 5
