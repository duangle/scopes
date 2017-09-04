#
      \\\
       \\\
     ///\\\
    ///  \\\

    Scopes Compiler
    Copyright (c) 2016, 2017 Leonard Ritter

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

    This is the scopes boot script. It implements the remaining standard
    functions and macros, parses the command-line and optionally enters
    the REPL.

fn tie-const (a b)
    if (constant? a) b
    else (unconst b)

fn cond-const (a b)
    if a b
    else (unconst b)

fn pointer== (a b)
    rawcall icmp== (rawcall ptrtoint a usize) (rawcall ptrtoint b usize)

fn type? (T)
    rawcall icmp== (rawcall ptrtoint type usize) (rawcall ptrtoint (rawcall typeof T) usize)

fn assert-type (T)
    if (type? T)
    else
        rawcall compiler-error!
            rawcall string-join "type expected, not " (rawcall Any-repr (rawcall Any-wrap T))
fn type== (a b)
    assert-type a
    assert-type b
    rawcall icmp== (rawcall ptrtoint a usize) (rawcall ptrtoint b usize)

fn unknownof (T)
    assert-type T
    bitcast T Unknown

fn todo! (msg)
    compiler-error!
        string-join "TODO: " msg

fn error! (msg)
    __error! msg
    unreachable!;

fn typename-type? (T)
    icmp== (type-kind T) type-kind-typename
fn integer-type? (T)
    icmp== (type-kind T) type-kind-integer
fn real-type? (T)
    icmp== (type-kind T) type-kind-real
fn pointer-type? (T)
    icmp== (type-kind T) type-kind-pointer
fn function-type? (T)
    icmp== (type-kind T) type-kind-function
fn tuple-type? (T)
    icmp== (type-kind T) type-kind-tuple
fn array-type? (T)
    icmp== (type-kind T) type-kind-array
fn vector-type? (T)
    icmp== (type-kind T) type-kind-vector
fn extern-type? (T)
    icmp== (type-kind T) type-kind-extern
fn function-pointer-type? (T)
    if (pointer-type? T)
        function-type? (element-type T 0)
    else (tie-const T false)
fn typename? (val)
    typename-type? (typeof val)
fn integer? (val)
    integer-type? (typeof val)
fn real? (val)
    real-type? (typeof val)
fn pointer? (val)
    pointer-type? (typeof val)
fn array? (val)
    array-type? (typeof val)
fn vector? (T)
    vector-type? (typeof T)
fn tuple? (val)
    tuple-type? (typeof val)
fn extern? (val)
    extern? (typeof val)
fn function-pointer? (val)
    function-pointer-type? (typeof val)
fn Symbol? (val)
    type== (typeof val) Symbol
fn list? (val)
    type== (typeof val) list
fn none? (val)
    type== (typeof val) Nothing

fn Any-new (val)
    fn construct (outval)
        insertvalue (insertvalue (undef Any) (typeof val) 0) outval 1

    if (type== (typeof val) Any) val
    elseif (constant? val)
        Any-wrap val
    else
        let T = (storageof (typeof val))
        let val =
            if (tuple-type? T) val
            else
                bitcast val T
        fn wrap-error ()
            compiler-error!
                string-join "unable to wrap value of storage type "
                    Any-repr (Any-wrap T)
        if (pointer-type? T)
            #compiler-message "wrapping pointer"
            construct
                ptrtoint val u64
        elseif (extern-type? T)
            Any-new (unconst val)
        elseif (tuple-type? T)
            if (icmp== (type-countof T) 0:usize)
                construct 0:u64
            else
                wrap-error;
        elseif (integer-type? T)
            construct
                if (signed? (typeof val))
                    sext val u64
                else
                    zext val u64
        elseif (real-type? T)
            if (type== T f32)
                construct
                    zext (bitcast val u32) u64
            else
                construct
                    bitcast val u64
        else
            wrap-error;

fn raise! (value)
    __raise! (Any-new value)
    unreachable!;

fn va-empty? (...)
    icmp== (va-countof ...) 0

fn va-types (params...)
    let sz = (va-countof params...)
    let loop (i result...) = sz
    if (icmp== i 0)
        return result...
    let i = (sub i 1)
    let arg = (va@ i params...)
    loop i (typeof arg) result...

fn cons (...)
    let i = (va-countof ...)
    if (icmp<s i 2)
        compiler-error! "at least two parameters expected"
    let i = (sub i 2)
    let loop (i at tail) = i (va@ i ...)
    if (icmp== i 0)
        list-cons (Any at) tail
    else
        let i = (sub i 1)
        loop i (va@ i ...)
            list-cons (Any at) tail

fn list-new (...)
    fn loop (i tail)
        if (icmp== i 0) tail
        else
            let val = (va@ (sub i 1) ...)
            loop (sub i 1)
                list-cons (Any-new val) tail
    loop (va-countof ...) eol

# forward decl
fn as
fn forward-as
fn imply
fn forward-imply

fn not (x)
    bxor (imply x bool) true

fn gen-type-op2 (f)
    fn (a b flipped)
        if (type== (typeof a) (typeof b))
            f a b
        elseif flipped
            let result... = (forward-imply a (typeof b))
            if (va-empty? result...)
            else
                f result... b
        else
            let result... = (forward-imply b (typeof a))
            if (va-empty? result...)
            else
                f a result...

syntax-extend
    set-type-symbol! type 'call
        fn (cls ...)
            let val ok = (type@ cls 'apply-type)
            if ok
                call val cls ...
            else
                compiler-error!
                    string-join "type "
                        string-join
                            Any-repr (Any-wrap cls)
                            " has no apply-type attribute"

    set-type-symbol! list 'apply-type
        fn (cls ...)
            list-new ...
    set-type-symbol! extern 'apply-type
        fn (cls ...)
            extern-new ...
    set-type-symbol! Any 'apply-type
        fn (cls value)
            Any-new value
    set-type-symbol! Symbol 'apply-type
        fn (cls value)
            string->Symbol value
    set-type-symbol! Scope 'apply-type
        fn (cls parent clone)
            let new? = (type== (typeof clone) Nothing)
            if (type== (typeof parent) Nothing)
                if new?
                    Scope-new;
                else
                    Scope-clone clone
            else
                if new?
                    Scope-new-expand parent
                else
                    Scope-clone-expand parent clone

    set-type-symbol! type '== (gen-type-op2 type==)
    set-type-symbol! Any '== (gen-type-op2 Any==)
    set-type-symbol! Closure '== (gen-type-op2 pointer==)
    set-type-symbol! Label '== (gen-type-op2 pointer==)
    set-type-symbol! Frame '== (gen-type-op2 pointer==)

    set-type-symbol! string '.. (gen-type-op2 string-join)
    set-type-symbol! list '.. (gen-type-op2 list-join)

    set-type-symbol! type 'getattr
        fn (cls name)
            let val ok = (type@ cls name)
            if ok
                return val
            else
                return;

    set-type-symbol! Symbol 'as
        fn (self destT)
            if (type== destT string)
                Symbol->string self

    set-type-symbol! Symbol '==
        gen-type-op2
            fn (a b)
                icmp== (bitcast a u64) (bitcast b u64)
    set-type-symbol! Builtin '==
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

        # more aggressive cast that converts from all numerical types
            and usize.
        set-type-symbol! T 'as
            fn hardcast (val destT)
                let vT = (typeof val)
                let destST =
                    if (type== destT usize) (storageof destT)
                    else destT
                if (integer-type? destST)
                    let valw destw = (bitcountof vT) (bitcountof destST)
                    if (icmp== destw valw)
                        bitcast val destT
                    elseif (icmp>s destw valw)
                        if (signed? vT)
                            sext val destT
                        else
                            zext val destT
                    else
                        itrunc val destT
                elseif (real-type? destST)
                    if (signed? vT)
                        sitofp val destT
                    else
                        uitofp val destT

        # only perform safe casts i.e. integer / usize conversions that expand width
        # unless the value is constant
        set-type-symbol! T 'imply
            fn (val destT)
                if (constant? val)
                    hardcast val destT
                else
                    let vT = (typeof val)
                    let destST =
                        if (type== destT usize) (storageof destT)
                        else destT
                    if (integer-type? destST)
                        let valw destw = (bitcountof vT) (bitcountof destST)
                        # must have same signed bit
                        if (icmp== (signed? vT) (signed? destST))
                            if (icmp== destw valw)
                                bitcast val destT
                            elseif (icmp>s destw valw)
                                if (signed? vT)
                                    sext val destT
                                else
                                    zext val destT

        # general constructor
        set-type-symbol! T 'apply-type
            fn (destT val)
                as val destT

        fn ufdiv(a b)
            let Ta Tb = (typeof a) (typeof b)
            if (type== Ta Tb)
                fdiv (uitofp a f32) (uitofp b f32)
            elseif (type== Tb Nothing)
                fdiv 1.0 (uitofp a f32)

        fn sfdiv(a b)
            let Ta Tb = (typeof a) (typeof b)
            if (type== Ta Tb)
                fdiv (sitofp a f32) (sitofp b f32)
            elseif (type== Tb Nothing)
                fdiv 1.0 (sitofp a f32)

        if (signed? (storageof T))
            set-type-symbol! T '> (gen-type-op2 icmp>s)
            set-type-symbol! T '>= (gen-type-op2 icmp>=s)
            set-type-symbol! T '< (gen-type-op2 icmp<s)
            set-type-symbol! T '<= (gen-type-op2 icmp<=s)
            set-type-symbol! T '// (gen-type-op2 sdiv)
            set-type-symbol! T '/ sfdiv
            set-type-symbol! T '% (gen-type-op2 srem)
            set-type-symbol! T '>> (gen-type-op2 ashr)
        else
            set-type-symbol! T '> (gen-type-op2 icmp>u)
            set-type-symbol! T '>= (gen-type-op2 icmp>=u)
            set-type-symbol! T '< (gen-type-op2 icmp<u)
            set-type-symbol! T '<= (gen-type-op2 icmp<=u)
            set-type-symbol! T '// (gen-type-op2 udiv)
            set-type-symbol! T '/ ufdiv
            set-type-symbol! T '% (gen-type-op2 urem)
            set-type-symbol! T '>> (gen-type-op2 lshr)

    fn setup-real-type (T)
        fn floordiv (a b)
            sdiv (fptosi a i32) (fptosi b i32)

        # only perform safe casts: i.e. float to double
        set-type-symbol! T 'imply
            fn (val destT)
                let vT = (typeof val)
                if (real-type? destT)
                    let valw destw = (bitcountof vT) (bitcountof destT)
                    if (icmp== destw valw)
                        bitcast val destT
                    elseif (icmp>s destw valw)
                        fpext val destT

        set-type-symbol! T 'apply-type
            fn (destT val)
                as val destT

        set-type-symbol! T '== (gen-type-op2 fcmp==o)
        set-type-symbol! T '!= (gen-type-op2 fcmp!=u)
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
        set-type-symbol! T '// (gen-type-op2 floordiv)
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

    setup-int-type usize

    setup-real-type f32
    setup-real-type f64

    syntax-scope

fn string-repr (val)
    Any-string (Any val)

fn opN-dispatch (symbol)
    fn (self ...)
        let T = (typeof self)
        let op success = (type@ T symbol)
        if success
            return (op self ...)
        compiler-error!
            string-join "operation "
                string-join (Any-repr (Any-wrap symbol))
                    string-join " does not apply to value of type "
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
            string-join "operation does not apply to values of type "
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
            string-join "operation does not apply to values of type "
                string-join
                    Any-repr (Any-wrap Ta)
                    string-join " and "
                        Any-repr (Any-wrap Tb)

fn op2-ltr-multiop (f)
    fn (a b ...)
        let sz = (va-countof ...)
        let loop (i result...) = 0 (f a b)
        if (icmp<s i sz)
            let x = (va@ i ...)
            loop (add i 1) (f result... x)
        else result...

fn op2-rtl-multiop (f)
    fn (...)
        let sz = (va-countof ...)
        let i = (sub sz 1)
        let x = (va@ i ...)
        let loop (i result...) = i x
        if (icmp>s i 0)
            let i = (sub i 1)
            let x = (va@ i ...)
            loop i (f x result...)
        else result...

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
fn // (a b) ((op2-dispatch-bidi '//) a b)
fn % (a b) ((op2-dispatch-bidi '%) a b)
fn & (a b) ((op2-dispatch-bidi '&) a b)
fn | (...) ((op2-ltr-multiop (op2-dispatch-bidi '|)) ...)
fn ^ (a b) ((op2-dispatch-bidi '^) a b)
fn << (a b) ((op2-dispatch-bidi '<<) a b)
fn >> (a b) ((op2-dispatch-bidi '>>) a b)
fn .. (...) ((op2-ltr-multiop (op2-dispatch-bidi '..)) ...)
fn countof (x) ((opN-dispatch 'countof) x)
fn unpack (x) ((opN-dispatch 'unpack) x)
fn @ (...)
    fn at (obj key)
        (op2-dispatch '@) obj
            if (constant? key)
                if (integer? key)
                    if (signed? (typeof key))
                        if (icmp<s key 0)
                            add (i64 (countof obj)) (i64 key)
                        else key
                    else key
                else key
            else key
    (op2-ltr-multiop at) ...

fn repr

fn type-mismatch-string (want-T have-T)
    .. "type " (repr want-T) " expected, not " (repr have-T)

fn assert-typeof (a T)
    if (type== T (typeof a))
    else
        compiler-error!
            type-mismatch-string T (typeof a)

fn Any-typeof (val)
    assert-typeof val Any
    extractvalue val 0

fn Any-payload (val)
    assert-typeof val Any
    extractvalue val 1

fn forward-repr (value)
    let op success = (type@ (typeof value) 'repr)
    if success
        op value
    else
        Any-repr (Any value)

fn repr (value)
    let T = (typeof value)
    let CT =
        if (type== T Any)
            Any-typeof value
        else T
    fn append-type? ()
        tie-const CT
            if (type== CT i32) false
            elseif (type== CT bool) false
            elseif (type== CT Nothing) false
            elseif (type== CT f32) false
            elseif (type== CT string) false
            elseif (type== CT list) false
            elseif (type== CT Symbol) false
            elseif (type== CT type) false
            elseif (vector-type? CT)
                let ET = (element-type CT 0)
                if (type== ET i32) false
                elseif (type== ET bool) false
                elseif (type== ET f32) false
                else true
            else true
    let op success = (type@ T 'repr)
    let text =
        if success
            op value
        else
            Any-repr (Any value)
    if (append-type?)
        .. text
            default-styler style-operator ":"
            default-styler style-type (type-name CT)
    else text

fn scalar-type (T)
    let ST = (storageof T)
    if (type== ST vector)
        element-type ST 0
    else ST

fn abs (x)
    let T = (scalar-type (typeof x))
    if (type== (superof T) integer)
        sabs x
    elseif (type== (superof T) real)
        fabs x
    else
        compiler-error! "invalid type"

fn getattr (self name)
    let T = (typeof self)
    let op success = (type@ T 'getattr)
    if success
        let result... = (op self name)
        if (icmp== (va-countof result...) 0)
        else
            return result...
    let result success = (type@ T name)
    if success
        return result
    compiler-error!
        string-join "no such attribute "
            string-join (Any-repr (Any-wrap name))
                string-join " in value of type "
                    Any-repr (Any-wrap T)

fn empty? (x)
    == (countof x) 0:usize

fn <: (T superT)
    let loop (T) = T
    let value = (superof T)
    if (type== value superT) true
    elseif (type== value typename) false
    else
        loop value

fn type<= (T superT)
    if (type== T superT)
        return true
    <: T superT

fn forward-as (value dest-type)
    let T = (typeof value)
    if (type<= T dest-type)
        return value
    let f ok = (type@ T 'imply)
    if ok
        let result... = (f value dest-type)
        if (icmp!= (va-countof result...) 0)
            return result...
    let f ok = (type@ T 'as)
    if ok
        let result... = (f value dest-type)
        if (icmp!= (va-countof result...) 0)
            return result...

fn as (value dest-type)
    let T = (typeof value)
    if (type<= T dest-type)
        return value
    let f ok = (type@ T 'imply)
    if ok
        let result... = (f value dest-type)
        if (icmp!= (va-countof result...) 0)
            return result...
    let f ok = (type@ T 'as)
    if ok
        let result... = (f value dest-type)
        if (icmp!= (va-countof result...) 0)
            return result...
    compiler-error!
        string-join "cannot convert value of type "
            string-join (Any-repr (Any-wrap T))
                string-join " to "
                    Any-repr (Any-wrap dest-type)

fn forward-imply (value dest-type)
    let T = (typeof value)
    if (type<= T dest-type)
        return value
    let f ok = (type@ T 'imply)
    if ok
        let result... = (f value dest-type)
        if (icmp!= (va-countof result...) 0)
            return result...

fn imply (value dest-type)
    let T = (typeof value)
    if (type<= T dest-type)
        return value
    let f ok = (type@ T 'imply)
    if ok
        let result... = (f value dest-type)
        if (icmp!= (va-countof result...) 0)
            return result...
    compiler-error!
        string-join "cannot implicitly convert value of type "
            string-join (Any-repr (Any-wrap T))
                string-join " to "
                    Any-repr (Any-wrap dest-type)

fn Any-extract (val T)
    assert-typeof val Any
    let valT = (Any-typeof val)
    if (== valT T)
        if (constant? val)
            Any-extract-constant val
        else
            let payload = (Any-payload val)
            let storageT = (storageof T)
            if (pointer-type? storageT)
                inttoptr payload T
            elseif (integer-type? storageT)
                itrunc payload T
            elseif (real-type? storageT)
                bitcast
                    itrunc payload (integer-type (bitcountof storageT) false)
                    T
            else
                compiler-error!
                    .. "unable to extract value of type " (Any-repr (Any-wrap T))
    elseif (constant? val)
        compiler-error!
            type-mismatch-string T valT
    else
        error!
            .. "while extracting from Any at runtime: "
                type-mismatch-string T valT

fn string->rawstring (s)
    assert-typeof s string
    getelementptr s 0 1 0
fn char (s)
    load (string->rawstring s)

fn Syntax-anchor (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 0
fn Syntax->datum (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 1
fn Syntax-quoted? (sx)
    assert-typeof sx Syntax
    extractvalue (load sx) 2

fn Anchor-file (x)
    assert-typeof x Anchor
    extractvalue (load x) 0
fn Anchor-lineno (x)
    assert-typeof x Anchor
    extractvalue (load x) 1
fn Anchor-column (x)
    assert-typeof x Anchor
    extractvalue (load x) 2

fn Exception-anchor (sx)
    assert-typeof sx Exception
    extractvalue (load sx) 0
fn Exception-message (sx)
    assert-typeof sx Exception
    extractvalue (load sx) 1

fn list-empty? (l)
    assert-typeof l list
    icmp== (ptrtoint l usize) 0:usize

fn list-at (l)
    assert-typeof l list
    if (list-empty? l)
        tie-const l (Any-wrap none)
    else
        extractvalue (load l) 0

fn list-next (l)
    assert-typeof l list
    if (list-empty? l)
        tie-const l eol
    else
        bitcast (extractvalue (load l) 1) list

fn list-at-next (l)
    assert-typeof l list
    if (list-empty? l)
        return
            tie-const l (Any-wrap none)
            tie-const l eol
    else
        return
            extractvalue (load l) 0
            bitcast (extractvalue (load l) 1) list

fn decons (val count)
    let at next = (list-at-next val)
    if (type== (typeof count) Nothing)
        return at next
    elseif (icmp<=s count 1)
        return at next
    else
        return at
            decons next (sub count 1)

fn list-countof (l)
    assert-typeof l list
    if (list-empty? l) (tie-const l 0:usize)
    else
        extractvalue (load l) 2

fn string-countof (s)
    assert-typeof s string
    extractvalue (load s) 0

fn min (a b)
    ? (<= a b) a b

fn max (a b)
    ? (>= a b) a b

fn clamp (x mn mx)
    ? (> x mx) mx
        ? (< x mn) mn x

fn slice (obj start-index end-index)
    # todo: this should be isize
    let zero count i0 = (i64 0) (i64 (countof obj)) (i64 start-index)
    let i0 =
        if (>= i0 zero) (min i0 count)
        else (max (+ i0 count) 0:i64)
    let i1 =
        if (type== (typeof end-index) Nothing) count
        else (i64 end-index)
    let i1 =
        max i0
            if (>= i1 zero) i1
            else (+ i1 count)
    (opN-dispatch 'slice) obj (usize i0) (usize i1)

fn string-compare (a b)
    assert-typeof a string
    assert-typeof b string
    let ca = (string-countof a)
    let cb = (string-countof b)
    let cc =
        if (< ca cb) ca
        else cb
    let pa pb =
        bitcast (getelementptr a 0 1 0) (pointer i8)
        bitcast (getelementptr b 0 1 0) (pointer i8)
    let loop (i) =
        tie-const cc 0:usize
    if (== i cc)
        if (< ca cb)
            return (tie-const cc -1)
        elseif (> ca cb)
            return (tie-const cc 1)
        else
            return (tie-const cc 0)
    let x y =
        load (getelementptr pa i)
        load (getelementptr pb i)
    if (< x y)
        return (tie-const cc -1)
    elseif (> x y)
        return (tie-const cc 1)
    else
        loop (+ i 1:usize)

fn list-reverse (l tail)
    assert-typeof l list
    let tail =
        if (type== (typeof tail) Nothing) eol
        else tail
    assert-typeof tail list
    let loop (l next) = l (tie-const l tail)
    if (list-empty? l) next
    else
        loop (list-next l) (list-cons (list-at l) next)

fn set-scope-symbol! (scope sym value)
    __set-scope-symbol! scope sym (Any value)

fn syntax-error! (anchor msg)
    let T = (typeof anchor)
    if (== T string)
        if (none? msg)
            __error! anchor
            unreachable!;
    set-anchor!
        if (== T Any)
            let T = (Any-typeof anchor)
            if (== T Syntax)
                Syntax-anchor (as anchor Syntax)
            else
                as anchor Anchor
        elseif (== T Syntax)
            Syntax-anchor anchor
        else anchor
    __anchor-error! msg
    unreachable!;

syntax-extend
    # a supertype to be used for conversions
    let immutable = (typename-type "immutable")
    set-scope-symbol! syntax-scope 'immutable immutable

    set-typename-super! integer immutable
    set-typename-super! real immutable
    set-typename-super! array immutable
    set-typename-super! vector immutable
    set-typename-super! tuple immutable

    set-type-symbol! integer 'apply-type
        fn (cls ...)
            integer-type ...
    #set-type-symbol! real 'apply-type
        fn (cls ...)
            real-type ...
    set-type-symbol! pointer 'set-element-type
        fn (cls ET)
            pointer-type-set-element-type cls ET
    set-type-symbol! pointer 'immutable
        fn (cls ET)
            pointer-type-set-flags cls
                bor (pointer-type-flags cls) pointer-flag-non-writable
    set-type-symbol! pointer 'strip-storage
        fn (cls ET)
            pointer-type-set-storage-class cls unnamed
    set-type-symbol! pointer 'readable?
        fn (cls)
            ((pointer-type-flags cls) & pointer-flag-non-readable) == 0:u64
    set-type-symbol! pointer 'writable?
        fn (cls)
            ((pointer-type-flags cls) & pointer-flag-non-writable) == 0:u64
    set-type-symbol! pointer 'apply-type
        fn (cls T opt)
            let flags =
                if (none? opt)
                    pointer-flag-non-writable
                else
                    assert-typeof opt Symbol
                    if (icmp== opt 'mutable) 0:u64
                    else
                        compiler-error! "invalid option passed to pointer type constructor"
            if (none? T)
                if (type== cls pointer)
                    compiler-error! "type expected"
                else
                    nullof cls
            else
                pointer-type T flags unnamed
    set-type-symbol! array 'apply-type
        fn (cls ...)
            array-type ...
    set-type-symbol! vector 'apply-type
        fn (cls ...)
            vector-type ...
    set-type-symbol! ReturnLabel 'apply-type
        fn (cls ...)
            ReturnLabel-type ...
    set-type-symbol! tuple 'apply-type
        fn (cls ...)
            tuple-type ...
    #set-type-symbol! union 'apply-type
        fn (cls ...)
            union-type ...
    set-type-symbol! typename 'apply-type
        fn (cls ...)
            typename-type ...
    set-type-symbol! function 'apply-type
        fn (cls ...)
            function-type ...

    set-type-symbol! Any 'typeof Any-typeof

    set-type-symbol! Any 'imply
        fn (src destT)
            Any-extract src destT

    set-type-symbol! Syntax 'imply
        fn (src destT)
            if (type== destT Any)
                Syntax->datum src
            elseif (type== destT Anchor)
                Syntax-anchor src
            else
                let anyval = (Syntax->datum src)
                let anyT = (Any-typeof anyval)
                if (type== anyT destT)
                    Any-extract anyval destT
                else
                    syntax-error! (Syntax-anchor src)
                        .. (repr destT) " expected, not " (repr anyT)

    set-type-symbol! type '@
        fn (self key)
            let keyT = (typeof key)
            if (type== keyT Symbol)
                type@ self key
            elseif (type== keyT i32)
                element-type self key
            elseif (type== keyT Nothing)
                element-type self 0
    set-type-symbol! type 'countof type-countof

    let empty-symbol = (Symbol "")

    set-type-symbol! Parameter 'apply-type
        fn (cls params...)
            let param1 param2 param3 = params...
            let TT = (tuple (typeof param1) (typeof param2) (typeof param3))
            if (type== TT (tuple Anchor Symbol type))
                Parameter-new param1 param2 param3
            elseif (type== TT (tuple Anchor Symbol Nothing))
                Parameter-new param1 param2 Unknown
            elseif (type== TT (tuple Symbol type Nothing))
                Parameter-new (active-anchor) param1 param2
            elseif (type== TT (tuple Symbol Nothing Nothing))
                Parameter-new (active-anchor) param1 Unknown
            else
                compiler-error! "usage: Parameter [anchor] symbol [type]"

    set-type-symbol! Parameter 'return-label?
        fn (self)
            icmp== (Parameter-index self) 0

    set-type-symbol! Symbol 'call
        fn (name self ...)
            (getattr self name) self ...

    set-type-symbol! Scope 'getattr
        fn (self key)
            if (constant? self)
                let value success = (Scope@ self key)
                if success
                    Any-extract-constant value
            else
                let value = (Scope@ self key)
                return value

    set-type-symbol! Scope '@
        fn (self key)
            let value success = (Scope@ self key)
            return
                if (constant? self)
                    Any-extract-constant value
                else value
                success

    set-type-symbol! list 'countof list-countof
    set-type-symbol! list 'getattr
        fn (self name)
            if (== name 'at)
                list-at self
            elseif (== name 'next)
                list-next self
            elseif (== name 'count)
                list-countof self
    set-type-symbol! list '@
        fn (self i)
            let loop (x i) = (tie-const i self) (i32 i)
            if (< i 0)
                Any none
            elseif (== i 0)
                list-at x
            else
                loop (list-next x) (- i 1)
    set-type-symbol! list 'slice
        fn (self i0 i1)
            # todo: use isize
            let i0 i1 = (i64 i0) (i64 i1)
            let skip-head (l i) =
                tie-const i0 self
                tie-const i0 (i64 0)
            if (< i i0)
                skip-head (list-next l) (+ i (i64 1))
            let count = (i64 (list-countof l))
            if (== (- i1 i0) count)
                return l
            let build-slice (l next i) =
                tie-const i1 l
                tie-const i1 eol
                tie-const i1 i
            if (== i i1)
                list-reverse next
            else
                build-slice (list-next l) (list-cons (list-at l) next) (+ i 1:i64)

    fn list== (a b)
        label xreturn (value)
            return (tie-const (tie-const a b) value)
        if (icmp!= (list-countof a) (list-countof b))
            xreturn false
        let loop (a b) = (tie-const b a) (tie-const a b)
        if (list-empty? a)
            xreturn true
        let u v = (list-at a) (list-at b)
        let uT vT = ('typeof u) ('typeof v)
        if (not (type== uT vT))
            xreturn false
        let un vn = (list-next a) (list-next b)
        if (type== uT list)
            if (list== (imply u list) (imply v list))
                loop un vn
            else
                xreturn false
        elseif (Any== u v)
            loop un vn
        else
            xreturn false

    set-type-symbol! list '==
        fn (a b flipped)
            if (type== (typeof a) (typeof b))
                list== a b

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

    let rawstring = (pointer i8)
    set-scope-symbol! syntax-scope 'rawstring (pointer i8)
    set-type-symbol! string 'imply
        fn (self destT)
            if (type== destT rawstring)
                getelementptr self 0 1 0

    set-type-symbol! string 'countof string-countof
    set-type-symbol! string '@
        fn string-at (s i)
            assert-typeof s string
            let i = (i64 i)
            if (< i 0:i64)
                return (tie-const i 0:i8)
            let len = (i64 (string-countof s))
            if (>= i len)
                return (tie-const (tie-const len i) 0:i8)
            let s = (bitcast (getelementptr s 0 1 0) (pointer i8))
            load (getelementptr s i)
    set-type-symbol! string 'slice
        fn (self i0 i1)
            string-new
                getelementptr (string->rawstring self) i0
                - i1 i0

    set-scope-symbol! syntax-scope 'min (op2-ltr-multiop min)
    set-scope-symbol! syntax-scope 'max (op2-ltr-multiop max)

    syntax-scope

fn Any-list? (val)
    assert-typeof val Any
    type== ('typeof val) list

fn maybe-unsyntax (val)
    if (type== ('typeof val) Syntax)
        extractvalue (load (as val Syntax)) 1
    else val

fn powi (base exponent)
    assert-typeof base i32
    assert-typeof exponent i32
    let loop (result cur exponent) =
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
    fn print-element (val)
        let T = (typeof val)
        if (== T string)
            io-write! val
        else
            io-write! (repr val)

    let loop (i) = 0
    if (< i (va-countof ...))
        if (> i 0)
            io-write! " "
        let arg = (va@ i ...)
        print-element arg
        loop (+ i 1)
    else
        io-write! "\n"

fn print-spaces (depth)
    assert-typeof depth i32
    if (icmp== depth 0)
    else
        io-write! "    "
        print-spaces (sub depth 1)

fn walk-list (on-leaf l depth)
    let loop (l) = l
    if (list-empty? l) true
    else
        let at next =
            list-at-next l
        let value =
            maybe-unsyntax at
        if (Any-list? value)
            print-spaces depth
            io-write! ";\n"
            walk-list on-leaf
                as value list
                add depth 1
        else
            on-leaf value depth
            true
        loop next

fn typify (f types...)
    let vacount = (va-countof types...)
    let loop (i types) = 0 (nullof (array-type type (usize vacount)))
    if (== i vacount)
        return
            __typify f vacount (bitcast (allocaof types) (pointer type))
    let T = (va@ i types...)
    let types = (insertvalue types T i)
    loop (+ i 1) types

fn compile-flags (opts...)
    let vacount = (va-countof opts...)
    let loop (i flags) = 0 0:u64
    if (== i vacount)
        return flags
    let flag = (va@ i opts...)
    if (not (constant? flag))
        compiler-error! "symbolic flags must be constant"
    assert-typeof flag Symbol
    loop (+ i 1)
        | flags
            if (== flag 'dump-disassembly) compile-flag-dump-disassembly
            elseif (== flag 'dump-module) compile-flag-dump-module
            elseif (== flag 'dump-function) compile-flag-dump-function
            elseif (== flag 'dump-time) compile-flag-dump-time
            elseif (== flag 'no-debug-info) compile-flag-no-debug-info
            elseif (== flag 'O1) compile-flag-O1
            elseif (== flag 'O2) compile-flag-O2
            elseif (== flag 'O3) compile-flag-O3
            else
                compiler-error!
                    .. "illegal flag: " (repr flag)

fn compile (f opts...)
    __compile f
        compile-flags opts...

fn compile-glsl (f target opts...)
    __compile-glsl f target
        compile-flags opts...

syntax-extend
    fn gen-type-op2 (op)
        fn (a b flipped)
            if (type== (typeof a) (typeof b))
                op a b
    set-type-symbol! type '< (gen-type-op2 <:)
    set-type-symbol! type '<=
        gen-type-op2
            fn (a b)
                if (type== a b) true
                else (<: a b)
    set-type-symbol! type '> (gen-type-op2 (fn (a b) (<: b a)))
    set-type-symbol! type '>=
        gen-type-op2
            fn (a b)
                if (type== a b) true
                else (<: b a)

    let Macro = (typename "Macro")
    let BlockScopeFunction =
        pointer
            function
                ReturnLabel (unknownof list) (unknownof Scope)
                \ list list Scope
    set-typename-storage! Macro BlockScopeFunction
    set-type-symbol! Macro 'apply-type
        fn (cls f)
            assert-typeof f BlockScopeFunction
            bitcast f Macro
    set-type-symbol! Macro 'as
        fn (self destT)
            if (type== destT function)
                bitcast self BlockScopeFunction
            elseif (type== destT BlockScopeFunction)
                bitcast self BlockScopeFunction
    # support for calling macro functions directly
    set-type-symbol! Macro 'call
        fn (self at next scope)
            (bitcast self BlockScopeFunction) at next scope

    fn block-scope-macro (f)
        Macro
            as (compile (typify f list list Scope))
                BlockScopeFunction
    fn scope-macro (f)
        block-scope-macro
            fn (at next scope)
                let at scope = (f (list-next at) scope)
                return (cons at next) scope
    fn macro (f)
        block-scope-macro
            fn (at next scope)
                return (cons (f (list-next at)) next) scope

    # dotted symbol expander
    # --------------------------------------------------------------------------

    fn dotted-symbol? (env head)
        let s = (Symbol->string head)
        let sz = (countof s)
        let loop (i) = (unconst 0:usize)
        if (== i sz)
            return (unconst false)
        elseif (== (@ s i) (char "."))
            return (unconst true)
        loop (+ i 1:usize)

    fn split-dotted-symbol (head start end tail)
        let s = (Symbol->string head)
        let loop (i) = (unconst start)
        if (== i end)
            # did not find a dot
            if (== start 0:usize)
                return (cons head tail)
            else
                return (cons (Symbol (slice s start)) tail)
        if (== (@ s i) (char "."))
            let tail =
                # no remainder after dot
                if (== i (- end 1:usize)) tail
                else # remainder after dot, split the rest first
                    split-dotted-symbol head (+ i 1:usize) end tail
            let dot = '.
            if (== i 0:usize)
                # no prefix before dot
                return (cons (unconst dot) tail)
            else
                # prefix before dot
                return
                    cons (Symbol (slice s start i)) dot tail
        loop (+ i 1:usize)

    # infix notation support
    # --------------------------------------------------------------------------

    fn get-ifx-symbol (name)
        Symbol (.. "#ifx:" (Symbol->string name))

    fn make-expand-define-infix (order)
        fn expand-define-infix (args scope)
            let prec token func = (decons args 3)
            let prec =
                as (as prec Syntax) i32
            let token =
                as (as token Syntax) Symbol
            let func =
                if (== ('typeof func) Nothing) token
                else
                    as (as func Syntax) Symbol
            set-scope-symbol! scope (get-ifx-symbol token)
                list prec order func
            return none scope

    fn get-ifx-op (env op)
        let sym = (Syntax->datum (as op Syntax))
        if (== ('typeof sym) Symbol)
            @ env (get-ifx-symbol (as sym Symbol))
        else
            return
                unconst (Any none)
                unconst false

    fn has-infix-ops? (infix-table expr)
        # any expression of which one odd argument matches an infix operator
            has infix operations.
        let loop (expr) = expr
        if (< (countof expr) 3:usize)
            return (unconst false)
        let expr = (list-next expr)
        let at next = (decons expr)
        let result ok = (get-ifx-op infix-table at)
        if ok
            return (unconst true)
        loop expr

    fn unpack-infix-op (op)
        let op-prec op-order op-func = (decons (as op list) 3)
        return
            as op-prec i32
            as op-order Symbol
            as op-func Symbol

    fn infix-op (infix-table token prec pred)
        let op ok =
            get-ifx-op infix-table token
        if ok
            let op-prec = (unpack-infix-op op)
            ? (pred op-prec prec) op (Any none)
        else
            syntax-error! token
                "unexpected token in infix expression"

    fn rtl-infix-op (infix-table token prec pred)
        let op ok =
            get-ifx-op infix-table token
        if ok
            let op-prec op-order = (unpack-infix-op op)
            if (== op-order '<)
                ? (pred op-prec prec) op (Any none)
            else
                unconst (Any none)
        else
            syntax-error! token
                "unexpected token in infix expression"

    fn parse-infix-expr (infix-table lhs state mprec)
        assert-typeof infix-table Scope
        assert-typeof lhs Any
        assert-typeof state list
        assert-typeof mprec i32
        let loop (lhs state) = lhs state
        if (empty? state)
            return lhs state
        let la next-state = (decons state)
        let op = (infix-op infix-table la mprec >=)
        if (== ('typeof op) Nothing)
            return lhs state
        let op-prec op-order op-name = (unpack-infix-op op)
        let rhs-loop (rhs state) = (decons next-state)
        if (empty? state)
            loop (Any (list op-name lhs rhs)) state
        let ra = (list-at state)
        let lop = (infix-op infix-table ra op-prec >)
        let nextop =
            if (== ('typeof lop) Nothing)
                rtl-infix-op infix-table ra op-prec ==
            else lop
        if (== ('typeof nextop) Nothing)
            loop (Any (list op-name lhs rhs)) state
        let nextop-prec = (unpack-infix-op nextop)
        let next-rhs next-state =
            parse-infix-expr infix-table rhs state nextop-prec
        rhs-loop next-rhs next-state

    #---------------------------------------------------------------------------

    # install general list hook for this scope
    # is called for every list the expander would otherwise consider a call
    fn list-handler (topexpr env)
        let sxexpr = (as (list-at topexpr) Syntax)
        let expr expr-anchor = (Syntax->datum sxexpr) (Syntax-anchor sxexpr)
        if (!= ('typeof expr) list)
            return topexpr env
        let expr = (as expr list)
        let head-key = (Syntax->datum (as (list-at expr) Syntax))
        let head =
            if (== ('typeof head-key) Symbol)
                let head success = (@ env (as head-key Symbol))
                if success head
                else head-key
            else head-key
        if (== ('typeof head) Macro)
            let head = (as head Macro)
            let next = (list-next topexpr)
            let expr env = (head expr next env)
            let expr = (Syntax-wrap expr-anchor (Any expr) false)
            return (as (as expr Syntax) list) env
        elseif (has-infix-ops? env expr)
            let at next = (decons expr)
            let expr =
                parse-infix-expr env at next (unconst 0)
            let next = (list-next topexpr)
            let expr = (Syntax-wrap expr-anchor expr false)
            return (list-cons expr next) env
        else
            return topexpr env

    # install general symbol hook for this scope
    # is called for every symbol the expander could not resolve
    fn symbol-handler (topexpr env)
        let at next = (decons topexpr)
        let sxname = (as at Syntax)
        let name name-anchor = (as sxname Symbol) (Syntax-anchor sxname)
        if (dotted-symbol? env name)
            let s = (Symbol->string name)
            let sz = (countof s)
            let expr =
                Any (split-dotted-symbol name (unconst 0:usize) sz eol)
            let expr = (Syntax-wrap name-anchor expr false)
            return (cons expr next) env
        return topexpr env

    set-scope-symbol! syntax-scope 'Macro Macro
    set-scope-symbol! syntax-scope 'block-scope-macro block-scope-macro
    set-scope-symbol! syntax-scope 'scope-macro scope-macro
    set-scope-symbol! syntax-scope 'macro macro
    set-scope-symbol! syntax-scope (Symbol "#list")
        compile (typify list-handler list Scope)
        #compile (typify list-handler list Scope)
    set-scope-symbol! syntax-scope (Symbol "#symbol")
        compile (typify symbol-handler list Scope)

    # (define name expr ...)
    fn expand-define (expr)
        let defname = (list-at expr)
        let content = (list-next expr)
        list syntax-extend
            list set-scope-symbol! 'syntax-scope
                list quote defname
                cons do content
            'syntax-scope

    fn make-expand-and-or (flip)
        fn (expr)
            if (list-empty? expr)
                syntax-error! "at least one argument expected"
            elseif (== (list-countof expr) 1:usize)
                return (list-at expr)
            let expr = (list-reverse expr)
            let loop (result head) = (decons expr)
            if (list-empty? head)
                return result
            let tmp =
                Parameter-new
                    Syntax-anchor (as (list-at head) Syntax)
                    \ 'tmp Unknown
            loop
                Any
                    list do-in
                        list let tmp '= (list-at head)
                        list if tmp
                            if flip tmp
                            else result
                        list 'else
                            if flip result
                            else tmp
                list-next head

    set-scope-symbol! syntax-scope 'define (macro expand-define)
    set-scope-symbol! syntax-scope 'and (macro (make-expand-and-or false))
    set-scope-symbol! syntax-scope 'or (macro (make-expand-and-or true))
    set-scope-symbol! syntax-scope 'define-infix>
        scope-macro (make-expand-define-infix '>)
    set-scope-symbol! syntax-scope 'define-infix<
        scope-macro (make-expand-define-infix '<)
    syntax-scope

# (define-macro name expr ...)
# implies builtin names:
    args : list
define define-macro
    macro
        fn "expand-define-macro" (expr)
            let name body = (decons expr)
            list define name
                list macro
                    cons fn '(args) body

# (define-scope-macro name expr ...)
# implies builtin names:
    args : list
    scope : Scope
define-macro define-scope-macro
    let name body = (decons args)
    list define name
        list scope-macro
            cons fn '(args syntax-scope) body

# (define-block-scope-macro name expr ...)
# implies builtin names:
    expr : list
    next-expr : list
    scope : Scope
define-macro define-block-scope-macro
    let name body = (decons args)
    list define name
        list block-scope-macro
            cons fn '(expr next-expr syntax-scope) body

define-macro assert
    fn assertion-error! (constant anchor msg)
        let assert-msg =
            .. "assertion failed: "
                if (== (typeof msg) string) msg
                else (repr msg)
        if constant
            compiler-error! assert-msg
        else
            syntax-error! anchor assert-msg
    let cond body = (decons args)
    let sxcond = (as cond Syntax)
    let anchor = (Syntax-anchor sxcond)
    let tmp =
        Parameter-new anchor 'tmp Unknown
    list do
        list let tmp '= cond
        list if tmp
        list 'else
            cons assertion-error!
                list constant? tmp
                active-anchor;
                if (empty? body)
                    list (repr (Syntax->datum sxcond))
                else body

# (. value symbol ...)
define-macro .
    fn op (a b)
        let sym = (as (as b Syntax) Symbol)
        list getattr a (list quote sym)
    let a b rest = (decons args 2)
    let loop (rest result) = rest (op a b)
    if (list-empty? rest) result
    else
        let c rest = (decons rest)
        loop rest (op result c)

fn = (obj value)
    (op2-dispatch '=) obj value
    return;

define-infix< 50 =
#define-infix> 70 :
define-infix> 100 or
define-infix> 200 and
define-infix> 240 |
define-infix> 250 ^
define-infix> 260 &

define-infix> 300 <
define-infix> 300 >
define-infix> 300 <=
define-infix> 300 >=
define-infix> 300 !=
define-infix> 300 ==

define-infix> 300 <:
#define-infix> 300 <>
#define-infix> 300 is

define-infix< 400 ..
define-infix> 450 <<
define-infix> 450 >>
define-infix> 500 -
define-infix> 500 +
define-infix> 600 %
define-infix> 600 /
define-infix> 600 //
define-infix> 600 *
#define-infix< 700 **
define-infix> 750 as
define-infix> 800 .
define-infix> 800 @
#define-infix> 800 .=
#define-infix> 800 @=
#define-infix> 800 =@

#-------------------------------------------------------------------------------
# type based function dispatch
#-------------------------------------------------------------------------------

# a lazily constructed type matcher that takes a target function, an error
    function, and finally a set of arguments and calls the target function
    if all arguments could be implicitly converted to the destination type,
    otherwise it calls the error function with a function that returns an
    error message, and a function that returns the original type arguments used.
fn type-matcher (types...)
    fn get-types ()
        types...
    let typesz = (va-countof types...)
    fn "with-target" (f)
        fn "with-error-fn" (f-error)
            fn (args...)
                let sz = (va-countof args...)
                if (icmp!= sz typesz)
                    return
                        f-error
                            fn ()
                                .. "argument mismatch (expected "
                                    repr typesz
                                    " but got "
                                    repr sz
                                    ")"
                            get-types
                let loop (i outargs...) = sz
                if (icmp== i 0)
                    return
                        f outargs...
                let i = (sub i 1)
                let T = (va@ i types...)
                let arg = (va@ i args...)
                let result... = (forward-imply arg T)
                if (va-empty? result...)
                    return
                        f-error
                            fn ()
                                .. "couldn't convert argument type from "
                                    repr (typeof arg)
                                    " to parameter type "
                                    repr T
                            get-types
                loop i result... outargs...

# formats a list of types to a string that can be used as readable function
    type signature
fn format-type-signature (types...)
    let typesz = (va-countof types...)
    let keys... = (va-keys types...)
    let loop (i s) = 0 ""
    if (icmp== i typesz)
        return s
    let T = (va@ i types...)
    let k = (va@ i keys...)
    loop (add i 1)
        .. s
            if (k == unnamed)
                repr T
            else
                .. (k as string) ":" (repr T)
            " "

# takes two type matchers that have been specialized up to the function target
    and returns a new type matcher that has also specialized up to the function
    target, which tries to match f1 first, then f2, and otherwise passes
    an error message to the error function, along with all previously attempted
    type signature constructors to the error function.
fn chain-fn-dispatch2 (f1 f2)
    fn "with-error-fn" (f-error)
        fn (args...)
            call
                f1
                    fn (msgf get-types1)
                        call
                            f2
                                fn (msgf get-types...)
                                    f-error
                                        fn ()
                                            .. "could not match arguments of types "
                                                format-type-signature (va-types args...)
                                                "to function"
                                        \ get-types1 get-types...
                            args...
                args...
# same as the previous function, but takes an arbitrary number of arguments
define chain-fn-dispatch (op2-rtl-multiop chain-fn-dispatch2)

# a default error handling function that prints all type signatures and
    produces a compiler error
fn fn-dispatch-error-handler (msgf get-types...)
    compiler-message "expected one of"
    let sz = (va-countof get-types...)
    let loop (i) = 0
    if (icmp== i sz)
        compiler-error! (msgf)
    else
        let get-types = (va@ i get-types...)
        compiler-message (format-type-signature (get-types))
        loop (add i 1)

# composes multiple target-bound type matchers into a single function
fn fn-dispatcher (args...)
    (chain-fn-dispatch args...) fn-dispatch-error-handler

# sugar for fn-dispatcher
define-macro fn...
    let sxname defs = (decons args)
    let name = (sxname as Syntax as Symbol)
    fn handle-argdef (argdef)
        let argdef = (argdef as Syntax as list)
        let loop (i indefs argtypes argnames) = (unconst 0) argdef (unconst '()) (unconst '())
        if (empty? indefs)
            return (list-reverse argtypes) (list-reverse argnames)
        else
            let indefs =
                do
                    if (i > 0)
                        let comma indefs = (decons indefs)
                        if ((comma as Syntax as Symbol) != ',)
                            syntax-error! comma "',' separator expected"
                        indefs
                    else indefs
            let argname sep argtype indefs = (decons indefs 3)
            argname as Syntax as Symbol # verify argname is a symbol
            if ((sep as Syntax as Symbol) != ':)
                syntax-error! sep "syntax: (name : type, ...)"
            loop (i + 1) indefs
                cons (list argname '= argtype) argtypes
                cons argname argnames
    fn handle-def (def)
        let argdef body = (decons def)
        let argtypes argnames = (handle-argdef argdef)
        list
            cons type-matcher argtypes
            cons fn argnames body
    let loop (indefs outdefs) = defs (unconst '())
    if (empty? indefs)
        let args = (Parameter (Syntax-anchor (sxname as Syntax)) 'args...)
        list fn name (list args)
            list
                cons fn-dispatcher
                    list-reverse outdefs
                args
    else
        let def indefs = (decons indefs)
        let def = (def as Syntax as list)
        loop indefs
            cons
                handle-def def
                outdefs

#-------------------------------------------------------------------------------
# references
#-------------------------------------------------------------------------------

define reference
    typename "reference"

fn pointer-type-imply? (src dest)
    or
        type== dest ('strip-storage src)
        type== dest ('immutable src)
        type== dest ('strip-storage ('immutable src))

do
    fn deref (val)
        let T = (typeof val)
        if (T <: reference)
            load (bitcast val (storageof T))
        else val

    fn passthru-overload (sym func)
        set-type-symbol! reference sym (fn (a b flipped) (func (deref a) (deref b)))
    passthru-overload '== ==; passthru-overload '!= !=
    passthru-overload '< <; passthru-overload '<= <=
    passthru-overload '> >; passthru-overload '>= >=
    passthru-overload '& &; passthru-overload '| |; passthru-overload '^ ^
    passthru-overload '+ +; passthru-overload '- -
    passthru-overload '/ /; passthru-overload '/ /
    passthru-overload '// //; passthru-overload '// //
    passthru-overload '% %
    passthru-overload '<< <<; passthru-overload '>> >>
    passthru-overload '.. ..; passthru-overload '.. ..
    set-type-symbol! reference 'getattr
        fn "reference-getattr" (self name)
            getattr (bitcast self (storageof (typeof self))) name

    set-type-symbol! reference '@
        fn "reference-@" (self key)
            let T = (storageof (typeof self))
            let ET = (element-type T 0)
            let op success = (type@ ET '@&)
            if success
                let result... = (op (bitcast self T) key)
                if (icmp== (va-countof result...) 0)
                else
                    return result...
            @ (load (bitcast self T)) key

    set-type-symbol! reference 'repr
        fn (self)
            forward-repr (load self)

    set-type-symbol! reference 'imply
        fn (self destT)
            let ptrtype = (storageof (typeof self))
            if (type== destT ptrtype)
                return (bitcast self ptrtype)
            if (pointer-type-imply? ptrtype destT)
                return (bitcast self destT)
            let ET = (element-type ptrtype 0)
            if (array-type? ET)
                let ET = (element-type ET 0)
                let aptrtype = (pointer ET)
                if (type== destT aptrtype)
                    return (bitcast self aptrtype)
            (load (bitcast self ptrtype)) as destT

    set-type-symbol! reference '=
        fn (self value)
            let ET = (element-type (storageof (typeof self)) 0)
            store (imply value ET) self
            true

    fn make-reference-type (PT)
        #let ET = (element-type PT 0)
        let T = (typename (.. "&" (type-name PT)))
        set-typename-super! T reference
        set-typename-storage! T PT
        T

    set-type-symbol! reference 'from-pointer-type
        fn "reference-apply-type" (cls PT)
            # due to auto-memoization, we'll always get the same type back
                provided the element type is a constant
            assert (constant? PT)
            assert-typeof PT type
            if (PT <: reference)
                compiler-error!
                    .. "cannot create reference type of reference type "
                        repr PT
            make-reference-type PT

    set-type-symbol! reference 'apply-type
        fn "reference-apply-type" (cls element)
            if (type== cls reference)
                compiler-error! "reference constructor deleted; use 'from-pointer-type"
            else
                let ET = (storageof cls)
                bitcast (imply element ET) cls

define-block-scope-macro var
    fn element-typeof (value)
        let T = (typeof value)
        if (T <: reference)
            element-type (storageof T) 0
        else T
    fn make-reference (dest value)
        let T = (typeof value)
        let PT =
            if (T <: reference)
                storageof T
            else (typeof dest)
        let ref = (('from-pointer-type reference PT) dest)
        = ref value
        ref

    let args = (list-next expr)
    let name token rest = (decons args 2)
    let token = (token as Syntax as Symbol)
    if (token == '=)
        let value rest = (decons rest)
        let tmp = (Parameter 'tmp)
        return
            cons
                list let tmp '= value
                list let name '= (list make-reference (list alloca (list element-typeof tmp)) tmp)
                next-expr
            syntax-scope
    elseif (token == '@)
        let size token rest = (decons rest 2)
        let token = (token as Syntax as Symbol)
        if (token == ':)
            let deftype rest = (decons rest)
            return
                cons
                    list let name '= (list alloca-array deftype (list usize size))
                    next-expr
                syntax-scope
        else
            syntax-error! (active-anchor)
                "syntax: var <name> @ <size> : <type>"
    else
        syntax-error! (active-anchor)
            "syntax: var <name> = <value> | <name> @ <size> : <type>"

define-macro global
    fn element-typeof (value)
        let T = (typeof value)
        if (T <: reference)
            element-type (storageof T) 0
        else T
    fn make-reference (dest value)
        let T = (typeof value)
        let PT =
            if (T <: reference)
                storageof T
            else (typeof dest)
        let ref = (('from-pointer-type reference PT) dest)
        = ref value
        ref

    let name token rest = (decons args 2)
    let token = (token as Syntax as Symbol)
    if (token == '=)
        let value rest = (decons rest)
        let tmp = (Parameter 'tmp)
        list define name
            list let tmp '= value
            list let name '= (list make-reference (list malloc (list element-typeof tmp)) tmp)
            # wrapping this prevents a crash on windows.
                right now, no idea what causes this, difficult to debug.
            list do name
            #name
    elseif (token == '@)
        let size token rest = (decons rest 2)
        let token = (token as Syntax as Symbol)
        if (token == ':)
            let deftype rest = (decons rest)
            list define name
                list let name '= (list malloc-array deftype (list usize size))
                name
        else
            syntax-error! (active-anchor)
                "syntax: global <name> @ <size> : <type>"
    else
        syntax-error! (active-anchor)
            "syntax: global <name> = <value> | <name> @ <size> : <type>"

# (typefn type 'symbol (params) body ...)
define-macro typefn
    let ty name params body = (decons args 3)
    list set-type-symbol! ty name
        cons fn params body

#-------------------------------------------------------------------------------
# compile time function chaining
#-------------------------------------------------------------------------------

define fnchain (typename "fnchain")
typefn fnchain 'apply-type (cls name)
    let T = (typename name)
    set-typename-super! T cls
    typefn T 'apply-type (cls args...)
    typefn T 'append (self f)
        assert (constant? f)
        assert (constant? self)
        let oldfn = self.apply-type
        typefn self 'apply-type (self args...)
            oldfn self args...
            f args...
        self
    typefn T 'prepend (self f)
        assert (constant? f)
        assert (constant? self)
        let oldfn = self.apply-type
        typefn self 'apply-type (self args...)
            f args...
            oldfn self args...
        self
    T

#-------------------------------------------------------------------------------
# module loading
#-------------------------------------------------------------------------------

define package
    let package = (Scope)
    set-scope-symbol! package 'path
        list
            .. compiler-dir "/lib/scopes/?.sc"
            .. compiler-dir "/lib/scopes/?/init.sc"
    set-scope-symbol! package 'modules (Scope)
    package

syntax-extend
    fn make-module-path (base-dir pattern name)
        let sz = (countof pattern)
        let loop (i start result) =
            unconst 0:usize
            unconst 0:usize
            unconst ""
        if (i == sz)
            return (.. result (slice pattern start))
        if ((@ pattern i) != (char "?"))
            loop (i + 1:usize) start result
        else
            loop (i + 1:usize) (i + 1:usize)
                .. result (slice pattern start i) name

    fn exec-module (expr eval-scope)
        let expr-anchor = (Syntax-anchor expr)
        let f = (compile (eval expr eval-scope))
        let rettype =
            element-type (element-type ('typeof f) 0) 0
        let ModuleFunctionType = (pointer (function (ReturnLabel (unknownof Any))))
        let fptr =
            if (rettype == Any)
                f as ModuleFunctionType
            else
                # build a wrapper
                let expr =
                    list
                        list let 'tmp '= (list f)
                        list unconst (list Any-new 'tmp)
                let expr = ((Syntax-wrap expr-anchor (Any expr) false) as Syntax)
                let f = (compile (eval expr (globals)))
                f as ModuleFunctionType
        fptr;

    fn dots-to-slashes (pattern)
        let sz = (countof pattern)
        let loop (i start result) =
            unconst 0:usize
            unconst 0:usize
            unconst ""
        if (i == sz)
            return (.. result (slice pattern start))
        let c = (@ pattern i)
        if (c == (char "/"))
            error!
                .. "no slashes permitted in module name: " pattern
        elseif (c == (char "\\"))
            error!
                .. "no slashes permitted in module name: " pattern
        elseif (c != (char "."))
            loop (i + 1:usize) start result
        elseif (icmp== (i + 1:usize) sz)
            error!
                .. "invalid dot at ending of module '" pattern "'"
        else
            if (icmp== i start)
                if (icmp>u start 0:usize)
                    loop (i + 1:usize) (i + 1:usize)
                        .. result (slice pattern start i) "../"
            loop (i + 1:usize) (i + 1:usize)
                .. result (slice pattern start i) "/"

    fn load-module (module-path main-module?)
        if (not (file? module-path))
            error!
                .. "no such module: " module-path
        let module-path = (realpath module-path)
        let module-dir = (dirname module-path)
        let expr = (list-load module-path)
        let eval-scope = (Scope (globals))
        set-scope-symbol! eval-scope 'main-module?
            if (none? main-module?) false
            else main-module?
        set-scope-symbol! eval-scope 'module-path module-path
        set-scope-symbol! eval-scope 'module-dir module-dir
        let content = (exec-module expr (Scope eval-scope))
        return content (unconst true)

    fn patterns-from-namestr (base-dir namestr)
        # if namestr starts with a slash (because it started with a dot),
            we only search base-dir
        if ((@ namestr 0) == (char "/"))
            unconst
                list
                    .. base-dir "?.sc"
                    .. base-dir "?/init.sc"
        else
            let package = (unconst package)
            package.path as list

    fn require-from (base-dir name)
        let name = (unconst name)
        let package = (unconst package)
        assert-typeof name Symbol
        let namestr = (Symbol->string name)
        let namestr = (dots-to-slashes namestr)
        fn load-module-from-symbol (name)
            let modules = (package.modules as Scope)
            let loop (patterns) = (patterns-from-namestr base-dir namestr)
            if (empty? patterns)
                return (unconst (Any none)) (unconst false)
            let pattern patterns = (decons patterns)
            let pattern = (pattern as string)
            let module-path = (realpath (make-module-path base-dir pattern namestr))
            if (empty? module-path)
                loop patterns
            let module-path-sym = (Symbol module-path)
            let content ok = (@ modules module-path-sym)
            if ok
                return content (unconst true)
            if (not (file? module-path))
                loop patterns
            let content ok = (load-module module-path)
            set-scope-symbol! modules module-path-sym content
            return content ok
        let content ok = (load-module-from-symbol name)
        if ok
            return content
        io-write! "no such module '"
        io-write! (Symbol->string name)
        io-write! "' in paths:\n"
        let loop (patterns) = (patterns-from-namestr base-dir namestr)
        if (empty? patterns)
            error! "failed to import module"
        let pattern patterns = (decons patterns)
        let pattern = (pattern as string)
        let module-path = (make-module-path base-dir pattern namestr)
        io-write! "    "
        io-write! module-path
        io-write! "\n"
        loop patterns

    set-scope-symbol! syntax-scope 'require-from require-from
    set-scope-symbol! syntax-scope 'load-module load-module
    syntax-scope

define-scope-macro locals
    # export locals as a chain of two scopes: a scope that contains
        all the constant symbols, and a scope that contains the dynamic
        ones.
    let constant-scope = (Scope)
    let tmp = (Parameter 'tmp)
    let loop (last-key result) = (unconst none) (unconst (list tmp))
    let key value =
        Scope-next syntax-scope (Any last-key)
    if (('typeof key) == Nothing)
        return
            cons do
                list let tmp '= (list Scope constant-scope)
                result
            syntax-scope
    else
        loop (unconst key)
            if (('typeof key) == Symbol)
                let key = (key as Symbol)
                let keyT = ('typeof value)
                if ((keyT == Parameter) or (keyT == Label))
                    cons
                        list set-scope-symbol! tmp (list quote key) value
                        result
                else
                    set-scope-symbol! constant-scope key value
                    result
            else
                # skip
                result

define-macro import
    fn resolve-scope (scope namestr start)
        let sz = (countof namestr)
        let loop (i start scope) = (unconst start) (unconst start) (unconst scope)
        if (i == sz)
            return scope (Symbol (slice namestr start i))
        if ((@ namestr i) == (char "."))
            if (i == start)
                loop (add i 1:usize) (add i 1:usize) scope
        loop (add i 1:usize) start scope

    let sxname rest = (decons args)
    let name = (sxname as Syntax as Symbol)
    let namestr = (name as string)
    list syntax-extend
        list let 'scope 'key '=
            list resolve-scope 'syntax-scope namestr 0:usize
        list set-scope-symbol! 'scope 'key
            list 'require-from 'module-dir
                list quote name
        'syntax-scope

#define llvm_eh_sjlj_setjmp
    extern 'llvm.eh.sjlj.setjmp (function i32 (pointer i8))

fn xpcall (f errorf)
    let pad = (alloca exception-pad-type)
    let old-pad =
        set-exception-pad pad
    if (== operating-system 'windows)
        if ((catch-exception pad (inttoptr 0 (pointer i8))) != 0)
            set-exception-pad old-pad
            errorf (exception-value pad)
        else
            let result... = (f)
            set-exception-pad old-pad
            result...
    else
        if ((catch-exception pad) != 0)
            set-exception-pad old-pad
            errorf (exception-value pad)
        else
            let result... = (f)
            set-exception-pad old-pad
            result...

fn format-exception (exc)
    if (('typeof exc) == Exception)
        let exc = (exc as Exception)
        format-message (Exception-anchor exc)
            .. (default-styler style-error "error:")
                \ " " (Exception-message exc)
    else
        .. "exception raised: " (repr exc) "\n"

fn prompt (prefix preload)
    __prompt prefix
        if (none? preload) ""
        else preload

#-------------------------------------------------------------------------------
# match
#-------------------------------------------------------------------------------

# earliest form of match macro - doesn't do elaborate patterns yet, just
    simple switch-case style comparisons
define-macro match
    let value rest = (decons args)
    let tmp = (Parameter 'tmp)

    fn vardef? (val)
        (('typeof val) == Symbol) and ((@ (val as Symbol as string) 0) == (char "$"))

    fn match-pattern (src sxkey)
        assert-typeof src Parameter
        assert-typeof sxkey Syntax
        let anchor = (sxkey as Anchor)
        let key = (sxkey as Any)
        let result =
            if (('typeof key) == list)
                let key = (key as list)
                if (empty? key)
                    list 'empty? src
                else
                    let head rest = (decons key)
                    let head = (head as Syntax as Symbol)
                    if (head == 'quote)
                        list '== src sxkey
                    # better support generic iterator
                    #elseif (head == 'list)
                        fn process-list-args (anchor src rest)
                            if (empty? rest)
                                list (list 'empty? src)
                            else
                                let tmp tmprest = (Parameter 'tmp) (Parameter 'rest)
                                let x rest = (decons rest)
                                cons
                                    list 'not (list 'empty? src)
                                    list do-in
                                        list let tmp tmprest '= (list 'decons src)
                                        match-pattern tmp (x as Syntax)
                                    process-list-args anchor tmprest rest
                        cons 'and
                            list '== (list typeof src) list
                            process-list-args anchor src rest
                    elseif (head == 'or)
                        if ((countof rest) < 2)
                            error! "'or' needs two arguments"
                        fn process-or-args (src rest)
                            let a rest = (decons rest)
                            if ((countof rest) <= 1)
                                let b = (decons rest)
                                list 'or
                                    match-pattern src (a as Syntax)
                                    match-pattern src (b as Syntax)
                            else
                                list 'or
                                    match-pattern src (a as Syntax)
                                    process-or-args src rest
                        process-or-args src rest
                    else
                        error!
                            .. "invalid pattern: " (repr key)
            elseif (vardef? key)
                let sym = (Symbol (slice (key as Symbol as string) 1))
                list do-in
                    list let sym '= src
                    true
            else
                # simple comparison
                list '== src key
        #print result
        Syntax-wrap anchor (Any result) false

    fn process (i src expr)
        if (empty? expr)
            error! "else expected"
        let pair rest = (decons expr)
        let key dst = (decons (pair as Syntax as list))
        let kkey = (key as Syntax as Any)
        let keytype = ('typeof kkey)
        if ((keytype == Symbol) and (kkey == 'else))
            cons (cons 'else dst) '()
        else
            cons
                cons
                    ? (i == 0) 'if 'elseif
                    match-pattern src (key as Syntax)
                    dst
                process (i + 1) src rest
    cons do
        list let tmp '= value
        process (unconst 0) tmp rest

#-------------------------------------------------------------------------------
# various C related sugar
#-------------------------------------------------------------------------------

# labels safecast to function pointers
typefn Closure 'imply (self destT)
    if (function-pointer-type? destT)
        let ET = (rawcall element-type destT 0)
        let sz = (itrunc (rawcall type-countof ET) i32)
        if (rawcall function-type-variadic? ET)
            compiler-error! "cannot typify to variadic function"
        let loop (i args...) = sz
        if (icmp== i 1)
            let result =
                compile (typify self args...)
            if (destT != ('typeof result))
                syntax-error! (Label-anchor (Closure-label self))
                    .. "function does not compile to type " (repr destT)
                        \ " but has type " (repr ('typeof result))
            return (imply result destT)
        else
            let i-1 = (sub i 1)
            loop i-1 (rawcall element-type ET i-1) args...

# a nullptr type that casts to whatever null pointer is required
syntax-extend
    let NullType = (typename "NullType")
    set-typename-storage! NullType (pointer void)
    set-type-symbol! NullType 'imply
        fn (self destT)
            if (pointer-type? destT)
                nullof destT
    set-type-symbol! NullType '==
        fn (a b flipped)
            if flipped
                if (pointer-type? (storageof (typeof a)))
                    icmp== (ptrtoint a usize) 0:usize
            else
                if (pointer-type? (storageof (typeof b)))
                    icmp== (ptrtoint b usize) 0:usize
    let null = (nullof NullType)
    set-scope-symbol! syntax-scope 'NullType NullType
    set-scope-symbol! syntax-scope 'null null
    syntax-scope

# support assignment syntax
typefn pointer '= (self value)
    store
        value as (element-type (typeof self) 0)
        self
    true

# pointer comparisons
typefn pointer '== (a b flipped)
    if flipped
        icmp== (ptrtoint (a as (typeof b)) usize) (ptrtoint b usize)
    else
        icmp== (ptrtoint a usize) (ptrtoint (b as (typeof a)) usize)

# pointer cast to element type executes load
typefn pointer 'as (self destT)
    if (type== destT (element-type (typeof self) 0))
        load self

# also supports mutable pointer safecast to immutable pointer
typefn pointer 'imply (self destT)
    if (pointer-type-imply? (typeof self) destT)
        bitcast self destT

# support getattr syntax
typefn pointer 'getattr (self name)
    let ET = (element-type (typeof self) 0)
    let op success = (type@ ET 'getattr&)
    if success
        let result... = (op self name)
        if (icmp== (va-countof result...) 0)
        else
            return result...
    getattr (load self) name

# support @
typefn pointer '@ (self index)
    let index =
        if (none? index) 0:usize # simple dereference
        else index
    ('from-pointer-type reference (typeof self)) (getelementptr self (usize index))

# extern cast to element type/pointer executes load/unconst
typefn extern 'imply (self destT)
    let ET = (element-type (typeof self) 0)
    if (type== destT ET)
        unconst self
    else
        forward-imply (load self) destT

typefn extern 'getattr (self name)
    let pET = (element-type (typeof self) 0)
    let ET = (element-type pET 0)
    let op success = (type@ ET 'getattr&)
    if success
        let result... = (op (unconst self) name)
        if (icmp== (va-countof result...) 0)
        else
            return result...
    getattr (load self) name

typefn extern 'as (self destT)
    forward-as (load self) destT

# support assignment syntax for extern
typefn extern '= (self value)
    let ET = (element-type (element-type (typeof self) 0) 0)
    store (imply value ET) self
    true

# support @ for extern
typefn extern '@ (self value)
    @ (unconst self) value

do
    fn unenum (val)
        let T = (typeof val)
        if (T <: CEnum)
            bitcast val (storageof T)
        else val

    # support for downcast
    typefn CEnum 'imply (self destT)
        let ST = (storageof (typeof self))
        if (type== destT ST)
            bitcast self ST
        elseif (type== destT i32)
            bitcast self i32

    typefn CEnum 'as (self destT)
        let ST = (storageof (typeof self))
        if (type== destT integer)
            bitcast self ST

    fn passthru-overload (sym func)
        set-type-symbol! CEnum sym (fn (a b flipped) (func (unenum a) (unenum b)))

    passthru-overload '!= ==; passthru-overload '== ==
    passthru-overload '< <; passthru-overload '<= <=
    passthru-overload '> >; passthru-overload '>= >=
    passthru-overload '+ +; passthru-overload '- -
    passthru-overload '* *; passthru-overload '/ /
    passthru-overload '// //; passthru-overload '% %
    passthru-overload '<< <<; passthru-overload '>> >>
    passthru-overload '| |
    passthru-overload '^ ^
    passthru-overload '& &

typefn CStruct 'structof (cls args...)
    let sz = (va-countof args...)
    if (icmp== sz 0)
        nullof cls
    else
        let T = (storageof cls)
        let keys... = (va-keys args...)
        let loop (i instance) = 0 (nullof cls)
        if (icmp<s i sz)
            let key = (va@ i keys...)
            let arg = (va@ i args...)
            let k =
                if (key == unnamed) i
                else
                    typename-field-index cls key
            let ET = (element-type T k)
            loop (add i 1)
                insertvalue instance (imply arg ET) k
        else
            instance

# support for C struct initializers
typefn CStruct 'apply-type (cls args...)
    if (cls == CStruct)
        let name fields... = args...
        let T = (typename name)
        set-typename-super! T CStruct
        set-typename-storage! T (tuple (va-values fields...))
        let types... = (va-keys fields...)
        if (not (va-empty? types...))
            set-typename-fields! T types...
        T
    else
        'structof cls args...

# access reference to struct element from pointer/reference
typefn CStruct 'getattr& (self name)
    let ET = (element-type (typeof self) 0)
    let idx = (typename-field-index ET name)
    if (icmp>=s idx 0)
        # cast result to reference
        let val = (getelementptr self 0 idx)
        ('from-pointer-type reference (typeof val)) val

typefn CStruct 'getattr (self name)
    let idx = (typename-field-index (typeof self) name)
    if (icmp>=s idx 0)
        extractvalue self idx

# support for basic C union initializer
typefn CUnion 'apply-type (cls)
    nullof cls

# access reference to union element from pointer/reference
typefn CUnion 'getattr& (self name)
    let ET = (element-type (typeof self) 0)
    let idx = (typename-field-index ET name)
    if (icmp>=s idx 0)
        let FT = (element-type ET idx)
        let newPT =
            'set-element-type (typeof self) FT
        # cast pointer to reference to alternative type
        ('from-pointer-type reference newPT)
            bitcast self newPT

# extern call attempts to cast arguments to correct type
typefn extern 'call (self ...)
    label docall (dest ET)
        let sz = (va-countof ...)
        let count = (itrunc (rawcall type-countof ET) i32)
        let variadic = (rawcall function-type-variadic? ET)
        let loop (i args...) = sz
        if (icmp== i 0)
            rawcall dest args...
        else
            let i-1 = (sub i 1)
            let arg = (va@ i-1 ...)
            if ((not variadic) or (icmp<s i count))
                let argtype = (rawcall element-type ET i)
                loop i-1 (imply arg argtype) args...
            else
                loop i-1 arg args...

    let pET = (rawcall element-type (typeof self) 0)
    let ET = (rawcall element-type pET 0)
    let ST = (rawcall superof ET)
    if (type== ST function)
        docall self ET
    elseif (function-pointer-type? ET) # can also call pointer to pointer to function
        docall (load self) (rawcall element-type ET 0)
    else
        (load self) ...

#-------------------------------------------------------------------------------
# using
#-------------------------------------------------------------------------------

fn merge-scope-symbols (source target filter)
    fn filter-contents (source target filter)
        let parent = (Scope-parent source)
        let target =
            if (parent == null) target
            else
                filter-contents parent target filter
        let loop (last-key) = (unconst none)
        let key value =
            Scope-next source (Any last-key)
        if (not (('typeof key) == Nothing))
            if (('typeof key) == Symbol)
                let key = (key as Symbol)
                if
                    or
                        none? filter
                        do
                            let keystr = (key as string)
                            string-match? filter keystr
                    set-scope-symbol! target key value
            loop key
        else
            target
    filter-contents (unconst source) (unconst target) filter

fn clone-scope-symbols (source target)
    fn clone-contents (source target)
        let parent = (Scope-parent source)
        let target =
            if (parent == null) target
            else
                clone-contents parent target
        Scope target source
    clone-contents (unconst source) (unconst target)

define-scope-macro using
    let name rest = (decons args)
    let nameval = (name as Syntax as Any)
    if ((('typeof nameval) == Symbol) and ((nameval as Symbol) == 'import))
        let module-dir ok = (@ syntax-scope 'module-dir)
        if (not ok)
            error! "using import requires module-dir symbol in scope"
        let module-dir = (module-dir as string)
        let name rest = (decons rest)
        let name = (name as Syntax as Symbol)
        let module = ((require-from module-dir name) as Scope)
        return (unconst (list do))
            clone-scope-symbols module syntax-scope
    let pattern =
        if (empty? rest) '()
        else
            let token pattern rest = (decons rest 2)
            let token = (token as Syntax as Symbol)
            if (token != 'filter)
                syntax-error! (active-anchor)
                    "syntax: using <scope> [filter <filter-string>]"
            let pattern = (pattern as Syntax as string)
            list pattern
    # attempt to import directly if possible
    label process (src)
        return (unconst (list do))
            if (empty? pattern)
                merge-scope-symbols src syntax-scope
            else
                merge-scope-symbols src syntax-scope ((@ pattern 0) as string)
    if (('typeof nameval) == Symbol)
        let sym = (nameval as Symbol)
        let src ok = (@ syntax-scope sym)
        if (ok and (('typeof src) == Scope))
            process (src as Scope)
    elseif (('typeof nameval) == Scope)
        process (nameval as Scope)
    return
        list syntax-extend
            cons merge-scope-symbols name 'syntax-scope pattern
        syntax-scope

#-------------------------------------------------------------------------------
# struct declaration
#-------------------------------------------------------------------------------

define-scope-macro struct
    fn begin-arg ()
    fn end-args (f) (f)
    fn append-arg (prevf x)
        fn (f)
            prevf
                fn ()
                    return x (f)

    define struct-dsl
        define-block-scope-macro :
            let args = (list-next expr)
            let lhs rhs = (decons args 2)
            let lhs = (lhs as Syntax as Symbol)
            return
                cons
                    list let 'field-names '=
                        list append-arg 'field-names (list quote lhs)
                    list let 'field-types '=
                        list append-arg 'field-types rhs
                    next-expr
                syntax-scope
        define-macro method
            let name params body = (decons args 2)
            list set-type-symbol! 'this-struct name
                cons fn params body

        define-infix> 70 :
        locals;

    fn finalize-struct (T field-names field-types)
        set-typename-storage! T (tuple (field-types begin-arg))
        set-typename-fields! T (field-names begin-arg)
        T

    let name body = (decons args)
    let name = (name as Syntax as Symbol)
    let T = (typename (name as string))
    set-typename-super! T CStruct
    set-scope-symbol! syntax-scope name T
    return
        cons do
            list using struct-dsl
            list define 'this-struct T
            list let 'field-names '= end-args
            list let 'field-types '= end-args
            ..
                cons do body
                list
                    list finalize-struct T 'field-names 'field-types
        syntax-scope

#-------------------------------------------------------------------------------
# tuples
#-------------------------------------------------------------------------------

typefn tuple 'countof (self)
    countof (typeof self)

typefn tuple '@ (self at)
    extractvalue self (usize at)

fn tupleof (...)
    let sz = (va-countof ...)
    # build tuple type
    let loop (i result...) = sz
    if (icmp>s i 0)
        let i = (sub i 1)
        let T = (va@ i ...)
        loop i (typeof T) result...
    else
        # build tuple
        let loop (i result) = 0 (nullof (tuple result...))
        if (icmp<s i sz)
            let T = (va@ i ...)
            loop (add i 1)
                insertvalue result T i
        else
            result

#-------------------------------------------------------------------------------
# arrays
#-------------------------------------------------------------------------------

typefn array 'countof (self)
    countof (typeof self)

typefn array '@ (self at)
    let val = (at as integer)
    if (constant? val)
        extractvalue self val
    else
        load (getelementptr (allocaof self) 0 val)

typefn array '@& (self at)
    let val = (at as integer)
    let newptr = (getelementptr self 0 val)
    ('from-pointer-type reference (typeof newptr)) newptr

fn arrayof (T ...)
    let count = (va-countof ...)
    let loop (i result) = 0 (nullof (array T (usize count)))
    if (i < count)
        let element = (va@ i ...)
        loop (i + 1)
            insertvalue result (imply element T) i
    else result

#-------------------------------------------------------------------------------
# iterators
#-------------------------------------------------------------------------------

define Generator
    typename "Generator"
set-typename-storage! Generator (storageof Closure)
typefn Generator 'apply-type (cls iter init)
    fn get-iter-init ()
        return iter init
    bitcast get-iter-init Generator
typefn Generator 'call (self)
    if (not (constant? self))
        compiler-error! "Generator must be constant"
    let f = (bitcast self Closure)
    call f

typefn Scope 'as (self destT)
    if (destT == Generator)
        Generator
            label (fret fdone key)
                let key value =
                    Scope-next self key
                if (('typeof key) == Nothing)
                    fdone;
                else
                    fret key key value
            unconst (Any none)

fn range (a b c)
    let num-type = (typeof a)
    let step =
        if (c == none)
            num-type 1
        else c
    let from =
        if (b == none)
            num-type 0
        else a
    let to =
        if (b == none) a
        else b
    Generator
        label (f fdone x)
            if (x < to)
                f (x + step) x
            else
                fdone;
        unconst from

fn zip (a b)
    let iter-a init-a = ((a as Generator))
    let iter-b init-b = ((b as Generator))
    Generator
        label (fret fdone t)
            let a = (@ t 0)
            let b = (@ t 1)
            iter-a
                label (next-a at-a...)
                    iter-b
                        label (next-b at-b...)
                            fret
                                tupleof next-a next-b
                                \ at-a... at-b...
                        \ fdone b
                \ fdone a
        tupleof init-a init-b

define-macro for
    let loop (it params) = args (unconst '())
    if (empty? it)
        error! "'in' expected"
    let sxat it = (decons it)
    let at = (sxat as Syntax as Symbol)
    if (at != 'in)
        loop it (cons sxat params)
    let generator-expr body = (decons it)
    let params = (list-reverse params)
    let iter = (Parameter 'iter)
    let start = (Parameter 'start)
    let next = (Parameter 'next)
    let loop = (Symbol "#loop")
    list do
        list let iter start '= (list (list (do as) generator-expr Generator))
        list label 'break '()
        list iter
            list label loop (cons next params)
                list label 'continue '()
                    list iter loop 'break next
                cons do body
                list 'continue
            \ 'break start

define-macro while
    let cond-expr body = (decons args)
    list do
        list label 'break '()
        list let 'continue '()
        list if cond-expr
            cons do body
            list 'continue

#-------------------------------------------------------------------------------
# vectors
#-------------------------------------------------------------------------------

fn vectorof (T ...)
    let count = (va-countof ...)
    let loop (i result) = 0 (nullof (vector T (usize count)))
    if (i < count)
        let element = (va@ i ...)
        loop (i + 1)
            insertelement result (imply element T) i
    else result

fn vector-signed-dispatch (fsigned funsigned)
    fn (a b)
        if (signed? (element-type (typeof a) 0))
            fsigned a b
        else
            funsigned a b

set-type-symbol! integer 'vector+ add
set-type-symbol! integer 'vector- sub
set-type-symbol! integer 'vector* mul
set-type-symbol! integer 'vector// (vector-signed-dispatch sdiv udiv)
set-type-symbol! integer 'vector% (vector-signed-dispatch srem urem)
set-type-symbol! integer 'vector& band
set-type-symbol! integer 'vector| bor
set-type-symbol! integer 'vector^ bxor
set-type-symbol! integer 'vector<< shl
set-type-symbol! integer 'vector>> (vector-signed-dispatch ashr lshr)
set-type-symbol! integer 'vector== icmp==
set-type-symbol! integer 'vector!= icmp!=
set-type-symbol! integer 'vector> (vector-signed-dispatch icmp>s icmp>u)
set-type-symbol! integer 'vector>= (vector-signed-dispatch icmp>s icmp>=u)
set-type-symbol! integer 'vector< (vector-signed-dispatch icmp<s icmp<u)
set-type-symbol! integer 'vector<= (vector-signed-dispatch icmp<=s icmp<=u)

set-type-symbol! real 'vector+ fadd
set-type-symbol! real 'vector- fsub
set-type-symbol! real 'vector* fmul
set-type-symbol! real 'vector/ fdiv
set-type-symbol! real 'vector% frem
set-type-symbol! real 'vector== fcmp==o
set-type-symbol! real 'vector!= fcmp!=u
set-type-symbol! real 'vector> fcmp>o
set-type-symbol! real 'vector>= fcmp>=o
set-type-symbol! real 'vector< fcmp<o
set-type-symbol! real 'vector<= fcmp<=o

fn vector-op2-dispatch (symbol)
    fn (a b flipped)
        if (type== (typeof a) (typeof b))
            let Ta = (element-type (typeof a) 0)
            let op success = (type@ Ta symbol)
            if success
                let result... = (op a b)
                if (icmp== (va-countof result...) 0)
                else
                    return result...

set-type-symbol! vector '+ (vector-op2-dispatch 'vector+)
set-type-symbol! vector '- (vector-op2-dispatch 'vector-)
set-type-symbol! vector '* (vector-op2-dispatch 'vector*)
set-type-symbol! vector '/ (vector-op2-dispatch 'vector/)
set-type-symbol! vector '// (vector-op2-dispatch 'vector//)
set-type-symbol! vector '% (vector-op2-dispatch 'vector%)
set-type-symbol! vector '& (vector-op2-dispatch 'vector&)
set-type-symbol! vector '| (vector-op2-dispatch 'vector|)
set-type-symbol! vector '^ (vector-op2-dispatch 'vector^)
set-type-symbol! vector '== (vector-op2-dispatch 'vector==)
set-type-symbol! vector '!= (vector-op2-dispatch 'vector!=)
set-type-symbol! vector '> (vector-op2-dispatch 'vector>)
set-type-symbol! vector '>= (vector-op2-dispatch 'vector>=)
set-type-symbol! vector '< (vector-op2-dispatch 'vector<)
set-type-symbol! vector '<= (vector-op2-dispatch 'vector<=)

typefn vector 'countof (self)
    type-countof (typeof self)

typefn vector 'unpack (v)
    let count = (type-countof (typeof v))
    let loop (i result...) = count
    if (i == 0:usize) result...
    else
        let i = (sub i 1:usize)
        loop i
            extractelement v i
            result...

typefn vector '@ (self x)
    if (integer? x)
        extractelement self x

typefn vector 'slice (self i0 i1)
    if ((constant? i0) and (constant? i1))
        let usz = (sub i1 i0)
        let loop (i mask) = i0 (nullof (vector i32 usz))
        if (icmp<u i i1)
            loop (add i 1:usize) (insertelement mask (i32 i) (sub i i0))
        else
            shufflevector self self mask
    else
        compiler-error! "slice indices must be constant"

fn vector-reduce (f v)
    let loop (v) = v
    let sz = (countof v)
    # special cases for low vector sizes
    if (sz == 1:usize)
        extractelement v 0
    elseif (sz == 2:usize)
        f
            extractelement v 0
            extractelement v 1
    elseif (sz == 3:usize)
        f
            f
                extractelement v 0
                extractelement v 1
            extractelement v 2
    elseif (sz == 4:usize)
        f
            f
                extractelement v 0
                extractelement v 1
            f
                extractelement v 2
                extractelement v 3
    else
        let hsz = (sz >> 1:usize)
        let fsz = (hsz << 1:usize)
        if (fsz != sz)
            compiler-error! "vector size must be a power of two"
        let a = (slice v 0 hsz)
        let b = (slice v hsz fsz)
        loop (f a b)

fn any? (v)
    vector-reduce bor v
fn all? (v)
    vector-reduce band v

typefn vector '.. (a b flipped)
    let Ta Tb = (typeof a) (typeof b)
    if (not (vector-type? Ta))
        return;
    if (not (vector-type? Tb))
        return;
    let ET = (element-type Ta 0)
    if (not (type== ET (element-type Tb 0)))
        return;
    if (type== Ta Tb)
        let usz = (mul (type-countof (typeof a)) 2:usize)
        let sz = (itrunc usz i32)
        let loop (i mask) = 0 (nullof (vector i32 usz))
        if (icmp<u i sz)
            loop (add i 1) (insertelement mask i i)
        else
            shufflevector a b mask
    else
        let asz = (type-countof (typeof a))
        let bsz = (type-countof (typeof b))
        let count = (add asz bsz)
        let loop (i result) = 0:usize (nullof (vector ET count))
        if (icmp<u i asz)
            loop (add i 1:usize)
                insertelement result (extractelement a i) i
        elseif (icmp<u i count)
            loop (add i 1:usize)
                insertelement result (extractelement b (sub i asz)) i
        else result

#-------------------------------------------------------------------------------
# apply locals as globals
#-------------------------------------------------------------------------------

set-globals!
    clone-scope-symbols (locals) (globals)

#-------------------------------------------------------------------------------
# REPL
#-------------------------------------------------------------------------------

fn compiler-version-string ()
    let vmin vmaj vpatch = (compiler-version)
    .. "Scopes " (string-repr vmin) "." (string-repr vmaj)
        if (vpatch == 0) ""
        else
            .. "." (string-repr vpatch)
        " ("
        if debug-build? "debug build, "
        else ""
        \ compiler-timestamp ")"

fn print-logo ()
    io-write! "  "; io-write! (default-styler style-string "\\\\\\"); io-write! "\n"
    io-write! "   "; io-write! (default-styler style-number "\\\\\\"); io-write! "\n"
    io-write! " "; io-write! (default-styler style-comment "///")
    io-write! (default-styler style-sfxfunction "\\\\\\"); io-write! "\n"
    io-write! (default-styler style-comment "///"); io-write! "  "
    io-write! (default-styler style-function "\\\\\\")

fn read-eval-print-loop ()
    fn repeat-string (n c)
        let loop (i s) =
            tie-const n (usize 0)
            tie-const n ""
        if (i == n)
            return s
        loop (i + (usize 1))
            .. s c

    fn leading-spaces (s)
        let len = (i32 (countof s))
        let loop (i) = (tie-const len 0)
        if (i == len)
            return s
        let c = (@ s i)
        if (c != (char " "))
            return (string-new (string->rawstring s) (usize i))
        loop (i + 1)

    fn blank? (s)
        let len = (i32 (countof s))
        let loop (i) =
            tie-const len 0
        if (i == len)
            return (unconst true)
        if ((@ s i) != (char " "))
            return (unconst false)
        loop (i + 1)

    let cwd =
        realpath "."

    print-logo;
    print " "
        compiler-version-string;

    let global-scope = (globals)
    let eval-scope = (Scope global-scope)
    set-scope-symbol! eval-scope 'module-dir cwd
    let loop (preload cmdlist counter) =
        unconst ""
        unconst ""
        unconst 0
    #dump "loop"
    fn make-idstr (counter)
        .. "$" (Any-string (Any counter))

    let idstr = (make-idstr counter)
    let promptstr =
        .. idstr " "
            default-styler style-comment "▶"
    let promptlen = ((countof idstr) + 2:usize)
    let cmd success =
        prompt
            ..
                if (empty? cmdlist) promptstr
                else
                    repeat-string promptlen "."
                " "
            preload
    if (not success)
        return;
    fn endswith-blank (s)
        let slen = (countof s)
        if (slen == 0:usize) (unconst false)
        else
            (@ s (slen - 1:usize)) == (char " ")
    let enter-multiline = (endswith-blank cmd)
    #dump "loop 1"
    let terminated? =
        (blank? cmd) or
            (empty? cmdlist) and (not enter-multiline)
    let cmdlist =
        .. cmdlist
            if enter-multiline
                slice cmd 0 -1
            else cmd
            "\n"
    let preload =
        if terminated? (unconst "")
        else (leading-spaces cmd)
    if (not terminated?)
        loop (unconst preload) cmdlist counter
    xpcall
        label ()
            let expr = (list-parse cmdlist)
            let expr-anchor = (Syntax-anchor expr)
            let f = (compile (eval expr eval-scope))
            let rettype =
                element-type (element-type ('typeof f) 0) 0
            let ModuleFunctionType = (pointer (function (ReturnLabel (unknownof Any))))
            let fptr =
                if (rettype == Any)
                    f as ModuleFunctionType
                else
                    # build a wrapper
                    let expr =
                        list
                            list let 'tmp '= (list f)
                            list unconst (list Any-new 'tmp)
                    let expr = ((Syntax-wrap expr-anchor (Any expr) false) as Syntax)
                    let f = (compile (eval expr global-scope))
                    f as ModuleFunctionType
            set-anchor! expr-anchor
            let result = (fptr)
            if (('typeof result) != Nothing)
                set-scope-symbol! eval-scope (Symbol idstr) result
                print idstr "=" result
                loop (unconst "") (unconst "") (counter + 1)
            else
                loop (unconst "") (unconst "") counter
        label (exc)
            io-write!
                format-exception exc
            loop (unconst "") (unconst "") counter

#-------------------------------------------------------------------------------
# main
#-------------------------------------------------------------------------------

fn print-help (exename)
    print "usage:" exename "[option [...]] [filename]
Options:
-h, --help                  print this text and exit.
-v, --version               print program version and exit.
-s, --signal-abort          raise SIGABRT when calling `abort!`.
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
    let loop (i sourcepath parse-options) = 1 none true
    if (i < argcount)
        let k = (i + 1)
        let arg = (va@ i args...)
        if (parse-options and ((@ arg 0) == (char "-")))
            if ((arg == "--help") or (arg == "-h"))
                print-help args...
            elseif ((== arg "--version") or (== arg "-v"))
                print-version;
            elseif ((== arg "--signal-abort") or (== arg "-s"))
                set-signal-abort! true
                loop k sourcepath parse-options
            elseif (== arg "--")
                loop k sourcepath false
            else
                print
                    .. "unrecognized option: " arg
                        \ ". Try --help for help."
                exit 1
                unreachable!;
        elseif (sourcepath == none)
            loop k arg parse-options
        else
            print
                .. "unrecognized argument: " arg
                    \ ". Try --help for help."
            exit 1
            unreachable!;

    if (sourcepath == none)
        read-eval-print-loop;
    else
        load-module sourcepath
            main-module? = true
        exit 0
        unreachable!;

run-main (args)
true

