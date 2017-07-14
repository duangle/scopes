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

fn tie-const (a b)
    if (constant? a) b
    else (unconst b)

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
fn function-type? (T)
    icmp== (type-kind T) type-kind-function
fn function-pointer-type? (T)
    if (pointer-type? T)
        function-type? (element-type T 0)
    else false
fn integer? (val)
    integer-type? (typeof val)
fn real? (val)
    real-type? (typeof val)
fn pointer? (val)
    pointer-type? (typeof val)
fn function-pointer? (val)
    function-pointer-type? (typeof val)

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
            let val = (va@ (sub i 1) ...)
            loop (sub i 1)
                list-cons (Any-new val) tail
    loop (va-countof ...) eol

syntax-extend
    set-type-symbol! list 'apply-type list-new
    set-type-symbol! Any 'apply-type Any-new
    set-type-symbol! Symbol 'apply-type string->Symbol
    set-type-symbol! Scope 'apply-type
        fn (parent)
            if (type== (typeof parent) Nothing)
                Scope-new;
            else
                Scope-new-subscope parent

    fn gen-type-op2 (op)
        fn (a b flipped)
            if (type== (typeof a) (typeof b))
                op a b

    set-type-symbol! type '== (gen-type-op2 type==)
    set-type-symbol! string '.. (gen-type-op2 string-join)

    set-type-symbol! Symbol '==
        gen-type-op2
            fn (a b)
                icmp== (bitcast a u64) (bitcast b u64)

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
        set-type-symbol! T '* (gen-type-op2 fmul)
        set-type-symbol! T '/
            fn (a b flipped)
                let Ta Tb = (typeof a) (typeof b)
                if (type== Ta Tb)
                    fdiv a b
                elseif (type== Tb Nothing)
                    fdiv (Ta 1) a
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

fn op1-dispatch (symbol)
    fn (x)
        let T = (typeof x)
        let op success = (type@ T symbol)
        if success
            return (op x)
        compiler-error!
            string-join "operation does not apply to type "
                Any-repr (Any-wrap T)

fn op2-dispatch (symbol)
    fn (a b)
        let Ta Tb = (typeof a) (typeof b)
        let op success = (type@ Ta symbol)
        if success
            let result... = (op a b)
            if (icmp== (va-countof result...) 0)
            else
                return result...
        compiler-error!
            string-join "operation does not apply to types "
                string-join
                    Any-repr (Any-wrap Ta)
                    string-join " and "
                        Any-repr (Any-wrap Tb)

fn op2-dispatch-bidi (symbol fallback)
    fn (a b)
        let Ta Tb = (typeof a) (typeof b)
        let op success = (type@ Ta symbol)
        if success
            let result... = (op a b false)
            if (icmp== (va-countof result...) 0)
            else
                return result...
        let op success = (type@ Tb symbol)
        if success
            let result... = (op a b true)
            if (icmp== (va-countof result...) 0)
            else
                return result...
        if (type== (typeof fallback) Nothing)
        else
            return (fallback a b)
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

fn == (a b) ((op2-dispatch-bidi '==) a b)
fn != (a b) 
    call
        op2-dispatch-bidi '!=
            fn (a b)
                bxor true (== a b)
        \ a b
fn > (a b) ((op2-dispatch-bidi '>) a b)
fn >= (a b) ((op2-dispatch-bidi '>=) a b)
fn < (a b) ((op2-dispatch-bidi '<) a b)
fn <= (a b) ((op2-dispatch-bidi '<=) a b)
fn + (...) ((op2-ltr-multiop (op2-dispatch-bidi '+)) ...)
fn - (a b) ((op2-dispatch-bidi '-) a b)
fn * (...) ((op2-ltr-multiop (op2-dispatch-bidi '*)) ...)
fn / (a b) ((op2-dispatch-bidi '/) a b)
fn % (a b) ((op2-dispatch-bidi '%) a b)
fn & (a b) ((op2-dispatch-bidi '&) a b)
fn | (...) ((op2-ltr-multiop (op2-dispatch-bidi '|)) ...)
fn ^ (a b) ((op2-dispatch-bidi '^) a b)
fn << (a b) ((op2-dispatch-bidi '<<) a b)
fn >> (a b) ((op2-dispatch-bidi '>>) a b)
fn .. (...) ((op2-ltr-multiop (op2-dispatch-bidi '..)) ...)
fn @ (...) ((op2-ltr-multiop (op2-dispatch '@)) ...)
fn countof (x) ((op1-dispatch 'countof) x)

fn empty? (x)
    == (countof x) 0:u64

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

fn Syntax-anchor (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 0
fn Syntax->datum (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 1
fn Syntax-quoted? (sx)
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
    if (list-empty? l) 0:usize
    else
        extractvalue (load l) 2

fn string-countof (s)
    assert-typeof s string
    extractvalue (load s) 0

fn min (a b)
    if (< a b) a
    else b

fn max (a b)
    if (> a b) a
    else b

fn clamp (x mn mx)
    if (> x mx) mx
    elseif (< x mn) mn
    else x

fn string-compare (a b)
    assert-typeof a string
    assert-typeof b string
    let ca = (string-countof a)
    let cb = (string-countof b)
    if (< ca cb)
        return -1
    elseif (> ca cb)
        return 1
    let pa pb = 
        bitcast (getelementptr a 0 1 0) (pointer-type i8)
        bitcast (getelementptr b 0 1 0) (pointer-type i8)
    let cc =
        if (constant? ca) ca
        else cb
    let [loop] i = 
        tie-const cc 0:usize
    if (== i cc)
        return 0
    let x y =
        load (getelementptr pa i)
        load (getelementptr pb i)
    if (< x y)
        return -1
    elseif (> x y)
        return 1
    else
        loop (+ i 1:usize)

syntax-extend
    fn gen-string-cmp (op)
        fn (a b flipped)
            if (type== (typeof a) (typeof b))
                op (string-compare a b) 0

    set-type-symbol! string '== (gen-string-cmp ==)
    set-type-symbol! string '!= (gen-string-cmp !=)
    set-type-symbol! string '< (gen-string-cmp <)
    set-type-symbol! string '<= (gen-string-cmp <=)
    set-type-symbol! string '> (gen-string-cmp >)
    set-type-symbol! string '>= (gen-string-cmp >=)

    set-type-symbol! string 'countof string-countof
    set-type-symbol! string '@ 
        fn string-at (s i)
            assert-typeof s string
            let i = (i64 i)
            let len = (i64 (string-countof s))
            let i = 
                if (< i 0:i64)
                    if (>= i (- len))
                        + len i
                    else
                        return ""
                elseif (>= i len)
                    return ""
                else i
            let data = 
                bitcast (getelementptr s 0 1 0) (pointer-type i8)
            string-new (getelementptr data i) 1:usize

    syntax-scope

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

fn set-scope-symbol! (scope sym value)
    __set-scope-symbol! scope sym (Any value)

fn typify(f types...)
    let vacount = (va-countof types...)
    let atype = (array-type type (size_t vacount))
    let types = (getelementptr (alloca atype) 0 0)
    let [loop] i = 0
    if (== i vacount)
        return 
            __typify f vacount types
    let T = (va@ i types...)
    store T (getelementptr types i)
    loop (+ i 1)        

fn compile (f opts...)
    let vacount = (va-countof opts...)
    let [loop] i flags = 0 0:u64
    if (== i vacount)
        return
            __compile f flags
    let flag = (va@ i opts...)
    if (not (constant? flag))
        compiler-error! "symbolic flags must be constant"
    assert-typeof flag Symbol
    loop (+ i 1)
        | flags
            if (== flag 'dump-disassembly) compile-flag-dump-disassembly
            elseif (== flag 'dump-module) compile-flag-dump-module
            elseif (== flag 'skip-opts) compile-flag-skip-opts
            else
                compiler-error!
                    .. "illegal flag: " (repr flag)

syntax-extend
    let Macro = (typename-type "Macro")
    let BlockScopeFunction =
        pointer-type
            function-type (tuple-type Any list Scope) list list Scope
    set-typename-storage! Macro BlockScopeFunction
    fn fn->macro (f)
        assert-typeof f BlockScopeFunction
        bitcast f Macro
    fn macro->fn (f)
        assert-typeof f Macro
        bitcast f BlockScopeFunction

    fn block-scope-macro (f)
        fn->macro
            Any-extract
                compile (typify f list list Scope) #'dump-module #'skip-opts
                BlockScopeFunction
    fn macro (f)
        block-scope-macro
            fn (at next scope)
                return (f (list-next at)) next scope
    
    # install general list hook for this scope
    # is called for every list the expander sees
    fn list-handler (topexpr env)
        label failed ()
            return topexpr env
        fn Any-Syntax-extract (val T)
            let sx =
                (Any-extract val Syntax)
            return
                Any-extract (Syntax->datum sx) T
                Syntax-anchor sx

        let expr expr-anchor = (Any-Syntax-extract (list-at topexpr) list)
        let head-key = (Syntax->datum (Any-extract (list-at expr) Syntax))
        let head =
            if (== (Any-typeof head-key) Symbol)
                #print head env
                let head success = (Scope@ env (Any-extract head-key Symbol))
                if success head
                else
                    print "failed."
                    failed;
            else head-key
        if (== (Any-typeof head) Macro)
            let head = 
                macro->fn (Any-extract head Macro)
            let next = (list-next topexpr)
            let expr next env = (head expr next env)
            let expr = (Syntax-wrap expr-anchor expr false)
            return (list-cons expr next) env
        else
            return topexpr env

    set-scope-symbol! syntax-scope 'Macro Macro
    set-scope-symbol! syntax-scope 'fn->macro fn->macro
    set-scope-symbol! syntax-scope 'macro->fn macro->fn
    set-scope-symbol! syntax-scope 'block-scope-macro block-scope-macro
    set-scope-symbol! syntax-scope 'macro macro
    set-scope-symbol! syntax-scope (Symbol "#list")
        compile (typify list-handler list Scope) #'dump-disassembly 'skip-opts 

    fn make-expand-and-or (flip)
        fn (expr)
            if (list-empty? expr)
                error! "at least one argument expected"
            elseif (== (list-countof expr) 1:u64)
                return (list-at expr)
            let expr = (list-reverse expr)
            let [loop] head result = (list-next expr) (list-at expr)
            if (list-empty? head)
                return result
            let tmp = 
                Parameter-new 
                    Syntax-anchor (Any-extract (list-at head) Syntax)
                    \ 'tmp void
            loop
                list-next head
                Any
                    list do
                        list let tmp '= (list-at head)
                        list if tmp 
                            if flip tmp
                            else result
                        list 'else 
                            if flip result
                            else tmp

    set-scope-symbol! syntax-scope 'and (macro (make-expand-and-or false))
    set-scope-symbol! syntax-scope 'or (macro (make-expand-and-or true))
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
#fn test-explicit-instantiation ()
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

#fn test-select-optimization ()
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
# REPL
#-------------------------------------------------------------------------------

fn compiler-version-string ()
    let vmin vmaj vpatch = (compiler-version)
    .. "Bangra " (Any-string (Any-wrap vmin)) "." (Any-string (Any-wrap vmaj))
        if (== vpatch 0) ""
        else
            .. "." (Any-string (Any-wrap vpatch))
        " ("
        if debug-build? "debug build, "
        else ""
        \ compiler-timestamp ")"

fn read-eval-print-loop ()
    fn repeat-string (n c)
        let [loop] i s = 
            tie-const n (size_t 0)
            tie-const n ""
        if (== i n)
            return s
        loop (+ i (size_t 1))
            .. s c

    fn leading-spaces (s)
        let len = (i32 (countof s))
        let [loop] i out = 
            tie-const len 0 
            tie-const len ""
        if (== i len)
            return out
        let c = (@ s i)
        if (!= c " ")
            return out
        loop (+ i 1)
            .. out c

    fn blank? (s)
        let len = (i32 (countof s))
        let [loop] i =
            tie-const len 0
        if (== i len)
            return true
        if (!= (@ s i) " ")
            return false
        loop (+ i 1)

    print
        compiler-version-string;

    let [loop] preload cmdlist counter =
        unconst ""
        unconst ""
        unconst 0
    fn make-idstr (counter)
        .. "$" (Any-string (Any counter))

    let idstr = (make-idstr counter)
    #let id = (Symbol idstr)
    #let styler = default-styler
    let promptstr =
        .. idstr " "
            default-styler style-comment "▶"
    let promptlen = (+ (countof idstr) (size_t 2))
    let cmd success =
        prompt
            ..
                if (empty? cmdlist) promptstr
                else
                    repeat-string promptlen "."
                " "
            preload
    if (not success)
        return
    let terminated? =
        or (blank? cmd)
            and (empty? cmdlist) (== (@ cmd 0) "\\")
    let cmdlist = (.. cmdlist cmd "\n")
    let preload =
        if terminated? ""
        else (leading-spaces cmd)
    if (not terminated?)
        loop preload cmdlist counter
    let expr = (list-parse cmdlist)
    let eval-scope = (Scope (globals))
    let f = (compile (eval expr eval-scope))
    let ModuleFunctionType = (pointer-type (function-type void))
    if (function-pointer-type? (Any-typeof f))
        call (inttoptr (Any-payload f) ModuleFunctionType)
    else
        error! "function pointer expected"
    #loop preload cmdlist counter
    #
            let expr = (list-parse cmdlist)
            if (none? expr)
                error "parsing failed"
            let eval-env = (@ state (quote env))
            let code =
                .. expr
                    syntax-list expression-suffix
            let f =
                eval code eval-env
            let result... = (f)
            if (not (none? result...))
                loop-for i in (range (va-countof result...))
                    let idstr = (make-idstr)
                    let value = (va@ i result...)
                    print
                        .. idstr "= "
                            repr value
                    set-scope-symbol! eval-env id value
                    set-scope-symbol! state (quote counter)
                        (@ state (quote counter)) + 1
                    continue
        except (msg anchor frame)
            print "Traceback:"
            print
                Frame-format frame
            print "error:" msg
        \ ""

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
    print
        compiler-version-string;
    print "Executable path:" compiler-path
    exit 0
    unreachable!;

fn run-main (args...)
    let argcount = (va-countof args...)
    let [loop] i sourcepath parse-options = 1 none true
    if (< i argcount)
        let k = (+ i 1)
        let arg = (va@ i args...)
        if (and parse-options (== (@ arg 0) "-"))
            if (or (== arg "--help") (== arg "-h"))
                print-help args...
            elseif (or (== arg "--version") (== arg "-v"))
                print-version;
            elseif (or (== arg "--"))
                loop k sourcepath false
            else
                print
                    .. "unrecognized option: " arg
                        \ ". Try --help for help."
                exit 1
                unreachable!;
        elseif (== sourcepath none)
            loop k arg parse-options
        else
            print
                .. "unrecognized argument: " arg
                    \ ". Try --help for help."
            exit 1
            unreachable!;

    if (== sourcepath none)
        read-eval-print-loop;
    else
        let expr = (list-load sourcepath)
        let eval-scope = (Scope (globals))
        set-scope-symbol! eval-scope 'module-path sourcepath
        let f = (compile (eval expr eval-scope))
        let ModuleFunctionType = (pointer-type (function-type void))
        if (function-pointer-type? (Any-typeof f))
            call (inttoptr (Any-payload f) ModuleFunctionType)
        else
            error! "function pointer expected"
        exit 0
        unreachable!

run-main (args)
true

