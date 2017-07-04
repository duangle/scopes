#
    Bangra Interpreter
    Copyright (c) 2017 Leonard Ritter

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

    This is the bangra boot script. It implements the remaining standard
    functions and macros, parses the command-line and optionally enters
    the REPL.

fn type? (T)
    icmp== (ptrtoint type size_t) (ptrtoint (typeof T) size_t)

fn type== (a b)
    fn assert-type (T)
        if (type? T)
        else
            compiler-error
                string-join "type expected, not " (Any-repr (Any-wrap T))
    assert-type a
    assert-type b
    icmp== (ptrtoint a size_t) (ptrtoint b size_t)

fn assert-typeof (a T)
    if (type== T (typeof a))
    else
        compiler-error
            string-join "type "
                string-join (Any-repr (Any-wrap T))
                    string-join " expected, not "
                        Any-repr (Any-wrap (typeof a))

fn string->rawstring (s)
    assert-typeof s string
    getelementptr s 0 1 0

fn Any-typeof (val)
    assert-typeof val Any
    extractvalue val 0
fn Any-payload (val)
    assert-typeof val Any
    extractvalue val 1

fn Any-extract-list (val)
    assert-typeof val Any
    inttoptr (Any-payload val) list

fn Any-extract-Syntax (val)
    assert-typeof val Any
    inttoptr (Any-payload val) Syntax

fn Any-extract-Symbol (val)
    assert-typeof val Any
    bitcast (Any-payload val) Symbol

fn Any-extract-i32 (val)
    assert-typeof val Any
    trunc (Any-payload val) i32

fn list-empty? (l)
    assert-typeof l list
    icmp== (ptrtoint l size_t) 0:usize

fn list-at (l)
    assert-typeof l list
    if (list-empty? l)
        Any-wrap none
    else
        extractvalue (load l) 0

fn list-next (l)
    assert-typeof l list
    if (list-empty? l) eol
    else
        bitcast (extractvalue (load l) 1) list

fn list-at-next (l)
    assert-typeof l list
    if (list-empty? l)
        return (Any-wrap none) eol
    else
        return
            extractvalue (load l) 0
            bitcast (extractvalue (load l) 1) list

fn list-countof (l)
    assert-typeof l list
    if (list-empty? l) 0:u64
    else
        extractvalue (load l) 2

fn Any-list? (val)
    assert-typeof val Any
    type== (Any-typeof val) list

fn maybe-unsyntax (val)
    if (type== (Any-typeof val) Syntax)
        extractvalue (load (Any-extract-Syntax val)) 1
    else val

fn Any-dispatch (val)
    assert-typeof val Any
    let T = (Any-typeof val)
    if (type== T list)
        Any-extract-list val
    elseif (type== T Syntax)
        Any-extract-Syntax val
    elseif (type== T Symbol)
        Any-extract-Symbol val
    elseif (type== T i32)
        Any-extract-i32 val
    else none

fn list-reverse (l)
    assert-typeof l list
    fn loop (l next)
        if (list-empty? l) next
        else
            loop (list-next l) (list-cons (list-at l) next)
    loop l eol

fn integer-type? (T)
    icmp== (type-kind T) type-kind-integer
fn real-type? (T)
    icmp== (type-kind T) type-kind-real
fn pointer-type? (T)
    icmp== (type-kind T) type-kind-pointer
fn integer? (val)
    integer-type? (typeof val)
fn real? (val)
    real-type? (typeof val)
fn pointer? (val)
    pointer-type? (typeof val)

fn powi (base exponent)
    assert-typeof base i32
    assert-typeof exponent i32
    label loop (result cur exponent)
        if (icmp== exponent 0) result
        else
            loop
                if (icmp== (band exponent 1) 0) result
                else
                    mul result cur
                mul cur cur
                lshr exponent 1
    if (constant? exponent)
        loop 1 base exponent
    else
        loop (unconst 1) (unconst base) exponent

fn Any-new (val)
    fn construct (outval)
        insertvalue (insertvalue (undef Any) (typeof val) 0) outval 1

    if (constant? val)
        Any-wrap val
    else
        let val =
            bitcast val
                type-storage (typeof val)
        if (pointer? val)
            compiler-message "wrapping pointer"
            construct
                ptrtoint val u64
        elseif (integer? val)
            construct
                if (signed? (typeof val))
                    sext val u64
                else
                    zext val u64
        elseif (real? val)
            construct
                bitcast (fpext val f64) u64
        else
            compiler-error
                string-join "unable to wrap value of storage type "
                    Any-repr (Any-wrap (typeof val))

fn list-new (...)
    fn loop (i tail)
        if (icmp== i 0) tail
        else
            loop (sub i 1)
                list-cons (Any-new (va@ (sub i 1) ...)) tail
    loop (va-countof ...) eol

#compile (typify Any-new string) 'dump-module

# calling polymorphic function
    Any-dispatch (Any-wrap 10)

# importing C code
#call
    fn/cc (_ lib)
        call
            fn/cc (_ sinf printf)
                printf
                    string->rawstring "test: %f\n"
                    sinf 0.5235987755982989
                #printf
                    string->rawstring "fac: %i\n"
                    fac
                        unconst 5
            purify
                Any-extract
                    Scope@ lib 'sinf
            Any-extract
                Scope@ lib 'printf

    import-c "testdata.c" "
        float sinf(float);
        int printf( const char* format, ... );
        "
        \ eol

# print function
fn print (...)
    fn load-printf ()
        #compiler-message "loading printf..."
        let lib =
            import-c "printf.c" "
                int stb_printf(const char *fmt, ...);
                "
                \ eol
        let printf =
            Any-extract
                Scope@ lib 'stb_printf
        fn (fmt ...)
            printf (string->rawstring fmt) ...

    let printf =
        load-printf

    fn print-element (val)
        let T = (typeof val)
        if (type== T string)
            io-write val
        elseif (type== T i32)
            printf "%i" val
        elseif (type== T f32)
            printf "%g" val
        else
            io-write "<value of type "
            io-write (Any-repr (Any-wrap (typeof val)))
            io-write ">"
    call
        label loop (i)
            if (icmp<s i (va-countof ...))
                if (icmp>s i 0)
                    io-write " "
                else # do nothing
                print-element (unconst (va@ i ...))
                loop (add i 1)
            else
                io-write "\n"
        \ 0

fn print-spaces (depth)
    assert-typeof depth i32
    if (icmp== depth 0)
    else
        io-write "    "
        print-spaces (sub depth 1)

fn walk-list (on-leaf l depth)
    call
        label loop (l)
            if (list-empty? l) true
            else
                let at next =
                    list-at-next l
                let value =
                    maybe-unsyntax at
                if (Any-list? value)
                    print-spaces depth
                    io-write ";\n"
                    walk-list on-leaf
                        Any-extract-list value
                        add depth 1
                else
                    on-leaf value depth
                    \ true
                loop next
        \ l

# deferring remaining expressions to bootstrap parser
syntax-apply-block
    fn (anchor exprs env)
        walk-list
            fn on-leaf (value depth)
                print-spaces depth
                #Any-dispatch value
                io-write
                    Any-repr value
                io-write "\n"
            unconst exprs
            unconst 0

# static assertion
    branch (type== (typeof 1) i32)
        fn/cc (_)
        fn/cc (_)
            compiler-error "static assertion failed: argument not i32"
    branch (constant? (add 1 2))
        fn/cc (_)
        fn/cc (_)
            compiler-error "static assertion failed: argument not constant"

# naive factorial
    fn/cc fac (_ n)
        branch (icmp<=s n 1)
            fn/cc (_) 1
            fn/cc (_)
                mul n
                    call
                        fn/cc (_ n-1)
                            fac n-1
                        sub n 1

    fac
        unconst 5

# importing C code
    call
        fn/cc (_ lib)
            call
                fn/cc (_ sinf printf)
                    printf
                        string->rawstring "test: %f\n"
                        sinf 0.5235987755982989
                    printf
                        string->rawstring "fac: %i\n"
                        fac
                            unconst 5
                purify
                    Any-extract
                        Scope@ lib 'sinf
                Any-extract
                    Scope@ lib 'printf

        import-c "testdata.c" "
            float sinf(float);
            int printf( const char* format, ... );
            "
            \ eol

# continuation skip
    fn/cc mainf (return x)
        io-write "doin stuff...\n"
        call
            fn/cc subf (_ x)
                branch (icmp== x 0)
                    fn/cc (_) true
                    fn/cc (_)
                        io-write "terminated early!\n"
                        return false
            \ x
        io-write "done stuff.\n"
        \ true

    mainf
        unconst 0
    mainf
        unconst 1


# mutual recursion
    fn/cc even? (eret ei)
        fn/cc odd? (oret oi)
            branch (icmp>s oi 0)
                fn/cc (_)
                    even? (sub oi 1)
                fn/cc (_)
                    eret false

        branch (icmp>s ei 0)
            fn/cc (_)
                odd? (sub ei 1)
            fn/cc (_)
                _ true

    branch
        even?
            unconst 30
        fn/cc (_)
            io-write "even\n"
        fn/cc (_)
            io-write "odd\n"


# tail-recursive program that avoids closures
    fn/cc puts (return s n)
        fn/cc loop (() i n)
            branch (icmp== i n)
                fn/cc ()
                    return
                fn/cc ()
                    io-write s
                    io-write "\n"
                    cc/call loop none (add i 1) n
        cc/call loop none
            branch (constant? n)
                fn/cc (_) 0
                fn/cc (_) (unconst 0)
            \ n

    puts "hello world"
        unconst 8

# tail-recursive program with closures
    fn/cc puts (return s n)
        fn/cc loop (_ i n)
            branch (icmp== i n)
                fn/cc (_)
                    return
                fn/cc (_)
                    io-write s
                    io-write "\n"
                    loop (add i 1) n
        loop
            branch (constant? n)
                fn/cc (_) 0
                fn/cc (_) (unconst 0)
            \ n

    puts "hello world" (unconst 8)

# explicit instantiation
fn/cc test-explicit-instantiation (_)
    fn/cc test-add (return x1 y1 z1 w1 x2 y2 z2 w2)
        return
            fadd x1 x2
            fadd y1 y2
            fadd z1 z2
            fadd w1 w2

    dump-label test-add
    dump-label
        typify test-add f32 f32 f32 f32 f32 f32 f32 f32
    call
        fn/cc (_ f)
            dump f
            print
                f 1. 2. 3. 4. 5. 6. 7. 8.
        compile
            typify test-add f32 f32 f32 f32 f32 f32 f32 f32
            \ 'dump-disassembly 'dump-module

#test-explicit-instantiation

fn/cc test-select-optimization (_)
    fn/cc conditional-select (return opt i)
        branch opt
            fn/cc (_)
                return
                    add i 5
            fn/cc (_)
                return
                    mul i 5

    dump-label
        typify conditional-select bool i32

    compile
        typify conditional-select bool i32
        \ 'dump-module 'dump-disassembly #'skip-opts

# return function dynamically
fn/cc test-dynamic-function-return (_)
    fn/cc square-brackets (_ s)
        io-write "["; io-write s; io-write "]"
    fn/cc round-brackets (_ s)
        io-write "("; io-write s; io-write ")"
    fn/cc bracket (_ use-square?)
        branch (unconst use-square?)
            fn/cc (_) square-brackets
            fn/cc (_) round-brackets
    fn/cc apply-brackets (_ f s)
        f s
        io-write "\n"

    apply-brackets (bracket true) "hello"
    apply-brackets (bracket true) "world"
    apply-brackets (bracket false) "hello"
    apply-brackets (bracket false) "world"

# polymorphic return type and inlined type checking
fn/cc print-value (_ value)
    call
        fn/cc (_ value-type)
            branch (type== value-type i32)
                fn/cc (_)
                    io-write "<number>\n"
                    \ "hello"
                fn/cc (_)
                    branch (type== value-type string)
                        fn/cc (_)
                            io-write value
                            io-write "\n"
                            \ false
                        fn/cc (_)
                            io-write "???\n"
        typeof value
fn/cc test-polymorphic-return-type (_)
    print-value
        print-value
            print-value 3

\ true





