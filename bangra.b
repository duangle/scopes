# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

let fac
    function (n)
        select (<= n 1)
            1
            * n (this-function (- n 1))

fac 5
