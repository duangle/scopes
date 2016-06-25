bang

extern bang_parse_file
    function-type (pointer-type void) ((pointer-type i8))

extern printf
    function-type i32 ((pointer-type i8) ...)
extern sin
    function-type double (double)

function computesin ()
    function-type double ()
    call sin
        const-real double 0.5

function dostuff (a b)
    function-type i32 (i32 i32)
    ?
        const-int i1 0
        ()
        call printf
            getelementptr "value = %f! (false) %i %i\n"
                const-int i32 0
                const-int i32 0
            call computesin
            \ a b
    const-int i32 0

call dostuff
    const-int i32 1
    const-int i32 2

call printf
    getelementptr "Hello World! %p\n"
        const-int i32 0
        const-int i32 0
    call bang_parse_file
        getelementptr "bang1.b"
            const-int i32 0
            const-int i32 0


