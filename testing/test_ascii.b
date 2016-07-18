IR
# include C stdlib definitions
include "../libc.b"
include "../macros.b"

# our main function
define main ()
    # the function type of this function
    function void

    call dump-value
        quote "hello w\xaa\x1förld!\n"

    call printf
        @str "hello wörld!\n"

    # return without argument
    ret;

# run the main function
execute main
