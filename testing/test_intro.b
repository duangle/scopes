#!/usr/bin/env bangra
IR

# include C stdlib definitions
include "../libc.b"

# define global string constant and include
# constant bitcast instruction
defvalue hello-world
    bitcast
        global "" "hello world!\n"
        rawstring

# our main function
define main ()
    # the function type of this function
    function void

    # call printf with argument hello-world
    call printf hello-world

    # return without argument
    ret;

# run the main function
execute main
