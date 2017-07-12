#
    Bangra Compiler
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
            compiler-error!
                string-join "type expected, not " (Any-repr (Any-wrap T))
    assert-type a
    assert-type b
    icmp== (ptrtoint a size_t) (ptrtoint b size_t)

fn todo! (msg)
    compiler-error!
        string-join "TODO: " msg

fn error! (msg)
    io-write "runtime error: "
    io-write msg
    io-write "\n"
    abort!;
    unreachable!;

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

fn Any-new (val)
    fn construct (outval)
        insertvalue (insertvalue (undef Any) (typeof val) 0) outval 1

    if (type== (typeof val) Any) val
    elseif (constant? val)
        Any-wrap val
    else
        let val =
            bitcast val
                type-storage (typeof val)
        if (pointer? val)
            #compiler-message "wrapping pointer"
            construct
                ptrtoint val u64
        elseif (integer? val)
            construct
                if (signed? (typeof val))
                    sext val u64
                else
                    zext val u64
        elseif (real? val)
            let T = (typeof val)
            if (type== T f32)
                construct
                    zext (bitcast val u32) u64
            else
                construct
                    bitcast val u64
        else
            compiler-error!
                string-join "unable to wrap value of storage type "
                    Any-repr (Any-wrap (typeof val))

fn repr (val)
    Any-repr
        Any-new val

fn list-new (...)
    fn loop (i tail)
        if (icmp== i 0) tail
        else
            loop (sub i 1)
                list-cons (Any-new (va@ (sub i 1) ...)) tail
    loop (va-countof ...) eol

syntax-extend
    set-type-symbol! list 'apply-type list-new
    set-type-symbol! Any 'apply-type Any-new

    fn gen-type-op2 (op)
        fn (a b flipped)
            if (type== (typeof a) (typeof b))
                op a b
            else;

    set-type-symbol! type '== (gen-type-op2 type==)
    set-type-symbol! string '.. (gen-type-op2 string-join)

    set-type-symbol! Nothing '==
        fn (a b flipped)
            type== (typeof a) (typeof b)
    set-type-symbol! Nothing '!=
        fn (a b flipped)
            bxor (type== (typeof a) (typeof b)) true

    fn setup-int-type (T)
        set-type-symbol! T '== (gen-type-op2 icmp==)
        set-type-symbol! T '!= (gen-type-op2 icmp!=)
        set-type-symbol! T '+ (gen-type-op2 add)
        set-type-symbol! T '-
            fn (a b flipped)
                let Ta Tb = (typeof a) (typeof b)
                if (type== Ta Tb)
                    sub a b
                elseif (type== Tb Nothing)
                    sub (Ta 0) a
                else;
        set-type-symbol! T '* (gen-type-op2 mul)
        set-type-symbol! T '<< (gen-type-op2 shl)
        set-type-symbol! T '& (gen-type-op2 band)
        set-type-symbol! T '| (gen-type-op2 bor)
        set-type-symbol! T '^ (gen-type-op2 bxor)
        set-type-symbol! T 'apply-type
            fn (val)
                let vT = (typeof val)
                if (type== T vT) val
                elseif (integer-type? vT)
                    let Tw vTw = (bitcountof T) (bitcountof vT)
                    if (icmp== Tw vTw)
                        bitcast val T
                    elseif (icmp>s vTw Tw)
                        trunc val T
                    elseif (signed? vT)
                        sext val T
                    else
                        zext val T
                elseif (real-type? vT)
                    if (signed? T)
                        fptosi val T
                    else
                        fptoui val T
                else
                    compiler-error! "integer or float expected"
        if (signed? T)
            set-type-symbol! T '> (gen-type-op2 icmp>s)
            set-type-symbol! T '>= (gen-type-op2 icmp>=s)
            set-type-symbol! T '< (gen-type-op2 icmp<s)
            set-type-symbol! T '<= (gen-type-op2 icmp<=s)
            set-type-symbol! T '/ (gen-type-op2 sdiv)
            set-type-symbol! T '% (gen-type-op2 srem)
            set-type-symbol! T '>> (gen-type-op2 ashr)
        else
            set-type-symbol! T '> (gen-type-op2 icmp>u)
            set-type-symbol! T '>= (gen-type-op2 icmp>=u)
            set-type-symbol! T '< (gen-type-op2 icmp<u)
            set-type-symbol! T '<= (gen-type-op2 icmp<=u)
            set-type-symbol! T '/ (gen-type-op2 udiv)
            set-type-symbol! T '% (gen-type-op2 urem)
            set-type-symbol! T '>> (gen-type-op2 lshr)

    fn setup-real-type (T)
        set-type-symbol! T 'apply-type
            fn (val)
                let vT = (typeof val)
                if (type== T vT) val
                elseif (integer-type? vT)
                    if (signed? vT)
                        sitofp val T
                    else
                        uitofp val T
                elseif (real-type? vT)
                    let Tw vTw = (bitcountof T) (bitcountof vT)
                    if (icmp>s vTw Tw)
                        fptrunc val T
                    else
                        fpext val T
                else
                    compiler-error! "integer or float expected"
        set-type-symbol! T '== (gen-type-op2 fcmp==o)
        set-type-symbol! T '!= (gen-type-op2 fcmp!=o)
        set-type-symbol! T '> (gen-type-op2 fcmp>o)
        set-type-symbol! T '>= (gen-type-op2 fcmp>=o)
        set-type-symbol! T '< (gen-type-op2 fcmp<o)
        set-type-symbol! T '<= (gen-type-op2 fcmp<=o)
        set-type-symbol! T '+ (gen-type-op2 fadd)
        set-type-symbol! T '-
            fn (a b flipped)
                let Ta Tb = (typeof a) (typeof b)
                if (type== Ta Tb)
                    fsub a b
                elseif (type== Tb Nothing)
                    fsub (Ta 0) a
                else;
        set-type-symbol! T '* (gen-type-op2 fmul)
        set-type-symbol! T '/
            fn (a b flipped)
                let Ta Tb = (typeof a) (typeof b)
                if (type== Ta Tb)
                    fdiv a b
                elseif (type== Tb Nothing)
                    fdiv (Ta 1) a
                else;
        set-type-symbol! T '% (gen-type-op2 frem)

    setup-int-type bool
    setup-int-type i8
    setup-int-type i16
    setup-int-type i32
    setup-int-type i64
    setup-int-type u8
    setup-int-type u16
    setup-int-type u32
    setup-int-type u64

    setup-real-type f32
    setup-real-type f64

    syntax-scope

fn op2-dispatch (symbol)
    fn (a b)
        let Ta Tb = (typeof a) (typeof b)
        let op success = (type@ Ta symbol)
        if success
            let result... = (op a b false)
            if (icmp== (va-countof result...) 0)
            else
                return result...
        else;
        let op success = (type@ Tb symbol)
        if success
            let result... = (op a b true)
            if (icmp== (va-countof result...) 0)
            else
                return result...
        else;
        compiler-error!
            string-join "operation does not apply to types "
                string-join
                    Any-repr (Any-wrap Ta)
                    string-join " and "
                        Any-repr (Any-wrap Tb)

fn op2-ltr-multiop (f)
    fn (a b ...)
        let [loop] i result = 0 (f a b)
        if (icmp<s i (va-countof ...))
            let x = (va@ i ...)
            loop (add i 1) (f result x)
        else result

fn == (a b) ((op2-dispatch '==) a b)
fn != (a b) ((op2-dispatch '!=) a b)
fn > (a b) ((op2-dispatch '>) a b)
fn >= (a b) ((op2-dispatch '>=) a b)
fn < (a b) ((op2-dispatch '<) a b)
fn <= (a b) ((op2-dispatch '<=) a b)
fn + (...) ((op2-ltr-multiop (op2-dispatch '+)) ...)
fn - (a b) ((op2-dispatch '-) a b)
fn * (...) ((op2-ltr-multiop (op2-dispatch '*)) ...)
fn / (a b) ((op2-dispatch '/) a b)
fn % (a b) ((op2-dispatch '%) a b)
fn & (a b) ((op2-dispatch '&) a b)
fn | (...) ((op2-ltr-multiop (op2-dispatch '|)) ...)
fn ^ (a b) ((op2-dispatch '^) a b)
fn << (a b) ((op2-dispatch '<<) a b)
fn >> (a b) ((op2-dispatch '>>) a b)
fn .. (...) ((op2-ltr-multiop (op2-dispatch '..)) ...)

fn type-mismatch-string (want-T have-T)
    .. "type " (repr want-T) " expected, not " (repr have-T)

fn assert-typeof (a T)
    if (type== T (typeof a))
    else
        compiler-error!
            type-mismatch-string T (typeof a)

fn not (x)
    assert-typeof x bool
    bxor x true

fn Any-typeof (val)
    assert-typeof val Any
    extractvalue val 0

fn Any-payload (val)
    assert-typeof val Any
    extractvalue val 1

fn Any-extract (val T)
    assert-typeof val Any
    let valT = (Any-typeof val)
    if (== valT T)
        if (constant? val)
            Any-extract-constant val
        else
            let payload = (Any-payload val)
            let storageT = (type-storage T)
            if (pointer-type? storageT)
                inttoptr payload T
            elseif (integer-type? storageT)
                trunc payload T
            elseif (real-type? storageT)
                bitcast
                    trunc payload (integer-type (bitcountof storageT) false)
                    T
            else
                compiler-error!
                    .. "unable to extract value of type " T
    elseif (constant? val)
        compiler-error!
            type-mismatch-string T valT
    else
        error!
            type-mismatch-string T valT

fn string->rawstring (s)
    assert-typeof s string
    getelementptr s 0 1 0

fn syntax->anchor (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 0
fn syntax->datum (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 1
fn syntax-quoted? (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 2

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
        extractvalue (load (Any-extract val Syntax)) 1
    else val

fn list-reverse (l)
    assert-typeof l list
    fn loop (l next)
        if (list-empty? l) next
        else
            loop (list-next l) (list-cons (list-at l) next)
    loop l eol

fn tie-const (a b)
    if (constant? a) b
    else (unconst b)

fn powi (base exponent)
    assert-typeof base i32
    assert-typeof exponent i32
    let [loop] result cur exponent =
        tie-const exponent 1
        tie-const exponent base
        exponent
    if (icmp== exponent 0) result
    else
        loop
            if (icmp== (band exponent 1) 0) result
            else
                mul result cur
            mul cur cur
            lshr exponent 1

# print function
fn print (...)
    fn load-printf ()
        let lib =
            import-c "printf.c" "
                int stb_printf(const char *fmt, ...);
                " eol
        let printf =
            Any-extract-constant
                Scope@ lib 'stb_printf
        fn (fmt ...)
            printf (string->rawstring fmt) ...

    let printf =
        load-printf;

    fn print-element (val)
        let T = (typeof val)
        if (== T string)
            io-write val
        elseif (== T i32)
            printf "%i" val
        elseif (== T f32)
            printf "%g" val
        else
            io-write (repr val)
            #io-write "<value of type "
            #io-write (repr (typeof val))
            #io-write ">"

    let [loop] i = 0
    if (< i (va-countof ...))
        if (> i 0)
            io-write " "
        else; # do nothing
        print-element (unconst (va@ i ...))
        loop (+ i 1)
    else
        io-write "\n"

fn print-spaces (depth)
    assert-typeof depth i32
    if (icmp== depth 0)
    else
        io-write "    "
        print-spaces (sub depth 1)

fn walk-list (on-leaf l depth)
    let [loop] l = l
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
                Any-extract value list
                add depth 1
        else
            on-leaf value depth
            true
        loop next

#print "yes" "this" "is" "dog"

# install general list hook for this scope
# is called for every list the expander sees
fn list-handler (topexpr env)
    fn Any-Syntax-extract (val T)
        Any-extract (syntax->datum (Any-extract val Syntax)) T

    let expr = (Any-Syntax-extract (list-at topexpr) list)
    let head = (syntax->datum (Any-extract (list-at expr) Syntax))
    if (== (Any-typeof head) Symbol)
        #print head env
        let head success = (Scope@ env (Any-extract head Symbol))
        #print head success
        none
    else;
    #walk-list
        fn on-leaf (value depth)
            print-spaces depth
            #Any-dispatch value
            io-write
                Any-repr value
            io-write "\n"
        unconst (Any-extract-list expr)
        unconst 0
    #print (i32 (list-countof topexpr))
    return topexpr env

syntax-extend
    set-scope-symbol! syntax-scope (string->Symbol "#list")
        compile (typify list-handler list Scope) #'dump-disassembly # 'skip-opts 
    syntax-scope

# deferring remaining expressions to bootstrap parser
#syntax-apply-block
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
            compiler-error! "static assertion failed: argument not i32"
    branch (constant? (add 1 2))
        fn/cc (_)
        fn/cc (_)
            compiler-error! "static assertion failed: argument not constant"

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
            eol

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
            x
        io-write "done stuff.\n"
        true

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
            n

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
fn test-explicit-instantiation ()
    fn test-add (x1 y1 z1 w1 x2 y2 z2 w2)
        return
            fadd x1 x2
            fadd y1 y2
            fadd z1 z2
            fadd w1 w2

    dump-label test-add
    dump-label
        typify test-add f32 f32 f32 f32 f32 f32 f32 f32
    let f =
        compile
            typify test-add f32 f32 f32 f32 f32 f32 f32 f32
            \ 'dump-disassembly 'dump-module
    dump f
    print
        f 1. 2. 3. 4. 5. 6. 7. 8.

#test-explicit-instantiation

fn test-select-optimization ()
    fn conditional-select (opt i)
        if opt
            return
                add i 5
        else
            return
                mul i 5

    dump-label
        typify conditional-select bool i32

    compile
        typify conditional-select bool i32
        \ 'dump-module 'dump-disassembly #'skip-opts

#test-select-optimization

# return function dynamically
fn test-dynamic-function-return ()
    fn square-brackets (s)
        io-write "["; io-write s; io-write "]"
    fn round-brackets (s)
        io-write "("; io-write s; io-write ")"
    fn bracket (use-square?)
        if (unconst use-square?) square-brackets
        else round-brackets
    fn apply-brackets (f s)
        f s
        io-write "\n"

    apply-brackets (bracket true) "hello"
    apply-brackets (bracket true) "world"
    apply-brackets (bracket false) "hello"
    apply-brackets (bracket false) "world"

#test-dynamic-function-return

# polymorphic return type and inlined type checking
fn test-polymorphic-return-type ()
    fn print-value (value)
        let value-type = (typeof value)
        if (type== value-type i32)
            io-write "<number>\n"
            "hello"
        elseif (type== value-type string)
            io-write value
            io-write "\n"
            false
        else
            io-write "???\n"
    print-value
        print-value
            print-value (unconst 3)

#test-polymorphic-return-type

#-------------------------------------------------------------------------------
# main
#-------------------------------------------------------------------------------

fn print-help (exename)
    print "usage:" exename "[option [...]] [filename]
Options:
   -h, --help                  print this text and exit.
   -v, --version               print program version and exit.
   --                          terminate option list."
    exit 0
    unreachable!;

fn print-version ()
    let vmin vmaj vpatch = (compiler-version)
    print "Bangra"
        .. (Any-string (Any-wrap vmin)) "." (Any-string (Any-wrap vmaj))
            if (== vpatch 0) ""
            else
                .. "." (Any-string (Any-wrap vpatch))
            " ("
            if debug-build? "debug build, " 
            else ""
            \ compiler-timestamp ")"
    print "Executable path:" compiler-path
    exit 0
    unreachable!;

#fn run-main (args...)
    # running in interpreter mode
    let [loop] i sourcepath parse-options = 1 none true
    let k = i + 1
    let arg = (va@ i args...)
    if (not (arg == none))
        if (parse-options and ((@ arg 0) == "-"))
            if (arg == "--help" or arg == "-h")
                print-help (va@ 0 args...)
            elseif (arg == "--version" or arg == "-v")
                print-version
            elseif (arg == "--")
                loop k sourcepath false
            else
                print
                    .. "unrecognized option: " arg
                        \ ". Try --help for help."
                exit 1
                unreachable!
        elseif (none? sourcepath)
            loop k arg parse-options
        else
            print
                .. "unrecognized argument: " arg
                    \ ". Try --help for help."
            exit 1
            unreachable!
    else

    io-write "\n"
    if (sourcepath == none)
        read-eval-print-loop
    else
        let expr =
            syntax->datum
                list-load sourcepath
        let eval-scope =
            Scope (globals)
        set-scope-symbol! eval-scope 'module-path sourcepath
        call
            eval expr eval-scope sourcepath
        exit 0
        unreachable!

#run-main (args)
true

