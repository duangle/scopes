# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.
bangra

# types are first-class values
let printf-cdecl
    cdecl int (rawstring ...)

let puts
    external "puts"
        cdecl int (rawstring)

let printf
    external "printf" printf-cdecl

let generate-a-function
    function (use-printf)
        let text
            "hello %s %i %f\n"

        function ()
            select use-printf
                do
                    printf "yes it's true!\n"
                do
                    let count
                        printf text "world" 0xff 2.5
                    printf "%i %i\n" count false

let call-a-function
    function (f use-printf)
        apply
            f use-printf

call-a-function generate-a-function false
call-a-function generate-a-function true

let k 5
printf "k=%i\n" k
do
    let k 2
    printf "k=%i\n" k
printf "k=%i\n" k

let recurse
set recurse
    function (val)
        puts "recurse!"
        select val
            recurse false
            null

recurse true

let store-state
    function ()
        let defvar false
        function (newvar)
            select defvar
                puts "was true"
                puts "was false"
            select newvar
                puts "setting to true"
                puts "setting to false"
            set defvar newvar
let state
    store-state;

state true
state false
state true
state true

///
    include "libc.b"
    include "macros.b"
    include "lang.b"

    run
        call printf
            &str "startup script loaded.\n"
