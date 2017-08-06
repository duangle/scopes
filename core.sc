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

fn type? (T)
    rawcall icmp== (rawcall ptrtoint type usize) (rawcall ptrtoint (rawcall typeof T) usize)

fn type== (a b)
    fn assert-type (T)
        if (type? T)
        else
            rawcall compiler-error!
                rawcall string-join "type expected, not " (rawcall Any-repr (rawcall Any-wrap T))
    assert-type a
    assert-type b
    rawcall icmp== (rawcall ptrtoint a usize) (rawcall ptrtoint b usize)

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
fn extern-type? (T)
    icmp== (type-kind T) type-kind-extern
fn function-pointer-type? (T)
    if (pointer-type? T)
        function-type? (element-type T 0)
    else false
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

fn cons (...)
    let i = (va-countof ...)
    if (icmp<s i 2)
        compiler-error! "at least two parameters expected"
    let i = (sub i 2)
    let [loop] i at tail = i (va@ i ...)
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
fn cast
fn forward-cast
fn softcast
fn forward-softcast

fn not (x)
    bxor (softcast bool x) true

fn gen-type-op2 (f)
    fn (a b flipped)
        if (type== (typeof a) (typeof b))
            f a b
        elseif flipped
            let result... = (forward-softcast (typeof b) a)
            if (va-empty? result...)
            else
                f result... b
        else
            let result... = (forward-softcast (typeof a) b)
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
    set-type-symbol! Any 'apply-type 
        fn (cls value)
            Any-new value
    set-type-symbol! Symbol 'apply-type 
        fn (cls value)
            string->Symbol value
    set-type-symbol! Scope 'apply-type
        fn (cls parent)
            if (type== (typeof parent) Nothing)
                Scope-new;
            else
                Scope-new-subscope parent

    set-type-symbol! type '== (gen-type-op2 type==)
    set-type-symbol! Any '== (gen-type-op2 Any==)
    set-type-symbol! string '.. (gen-type-op2 string-join)
    set-type-symbol! list '.. (gen-type-op2 list-join)

    set-type-symbol! type 'getattr
        fn (cls name)
            let val ok = (type@ cls name)
            if ok 
                return val
            else
                return;

    set-type-symbol! Symbol 'cast
        fn (destT self)
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
        set-type-symbol! T 'cast
            fn hardcast (destT val)
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
                        trunc val destT
                elseif (real-type? destST)
                    if (signed? vT)
                        sitofp val destT
                    else
                        uitofp val destT

        # only perform safe casts i.e. integer / usize conversions that expand width
        # unless the value is constant
        set-type-symbol! T 'softcast
            fn (destT val)
                if (constant? val)
                    hardcast destT val
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
                cast destT val

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
        set-type-symbol! T 'softcast
            fn (destT val)
                let vT = (typeof val)
                if (real-type? destT)
                    let valw destw = (bitcountof vT) (bitcountof destT)
                    if (icmp== destw valw)
                        bitcast val destT
                    elseif (icmp>s destw valw)
                        fpext val destT

        set-type-symbol! T 'apply-type
            fn (destT val)
                cast destT val

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
        let [loop] i result... = 0 (f a b)
        if (icmp<s i (va-countof ...))
            let x = (va@ i ...)
            loop (add i 1) (f result... x)
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
fn @ (...)
    fn at (obj key)
        (op2-dispatch '@) obj
            if (integer? key)
                if (signed? (typeof key))
                    if (icmp<s key 0)
                        add (i64 (countof obj)) (i64 key)
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
        if (type== CT i32) false
        elseif (type== CT bool) false
        elseif (type== CT Nothing) false
        elseif (type== CT f32) false
        elseif (type== CT string) false
        elseif (type== CT list) false
        elseif (type== CT Symbol) false
        elseif (type== CT type) false
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

fn forward-cast (dest-type value)
    let T = (typeof value)
    if (type== T dest-type)
        return value
    let f ok = (type@ T 'softcast)
    if ok
        let result... = (f dest-type value)
        if (icmp!= (va-countof result...) 0)
            return result...
    let f ok = (type@ T 'cast)
    if ok
        let result... = (f dest-type value)
        if (icmp!= (va-countof result...) 0)
            return result...

fn cast (dest-type value)
    let T = (typeof value)
    if (type== T dest-type)
        return value
    let f ok = (type@ T 'softcast)
    if ok
        let result... = (f dest-type value)
        if (icmp!= (va-countof result...) 0)
            return result...
    let f ok = (type@ T 'cast)
    if ok
        let result... = (f dest-type value)
        if (icmp!= (va-countof result...) 0)
            return result...
    compiler-error!
        string-join "cannot cast value of type " 
            string-join (Any-repr (Any-wrap T))
                string-join " to "
                    Any-repr (Any-wrap dest-type)

fn forward-softcast (dest-type value)
    let T = (typeof value)
    if (type== T dest-type)
        return value
    let f ok = (type@ T 'softcast)
    if ok
        let result... = (f dest-type value)
        if (icmp!= (va-countof result...) 0)
            return result...

fn softcast (dest-type value)
    let T = (typeof value)
    if (type== T dest-type)
        return value
    let f ok = (type@ T 'softcast)
    if ok
        let result... = (f dest-type value)
        if (icmp!= (va-countof result...) 0)
            return result...
    compiler-error!
        string-join "cannot softcast value of type " 
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
    if (list-empty? l) 0:usize
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
        bitcast (getelementptr a 0 1 0) (pointer-type i8)
        bitcast (getelementptr b 0 1 0) (pointer-type i8)
    let [loop] i =
        tie-const cc 0:usize
    if (== i cc)
        if (< ca cb)
            return -1
        elseif (> ca cb)
            return 1
        else
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

fn list-reverse (l tail)
    assert-typeof l list
    let tail =
        if (type== (typeof tail) Nothing) eol
        else tail
    assert-typeof tail list
    let [loop] l next = l (tie-const l tail)
    if (list-empty? l) next
    else
        loop (list-next l) (list-cons (list-at l) next)

fn set-scope-symbol! (scope sym value)
    __set-scope-symbol! scope sym (Any value)

syntax-extend
    set-type-symbol! integer 'apply-type 
        fn (cls ...)
            integer-type ...
    #set-type-symbol! real 'apply-type 
        fn (cls ...)
            real-type ...
    set-type-symbol! pointer 'apply-type 
        fn (cls ...)
            if (va-empty? ...)
                nullof cls
            else
                pointer-type ...
    set-type-symbol! array 'apply-type 
        fn (cls ...)
            array-type ...
    #set-type-symbol! vector 'apply-type 
        fn (cls ...)
            vector-type ...
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

    set-type-symbol! Any 'softcast
        fn (destT src)
            Any-extract src destT

    set-type-symbol! Syntax 'softcast
        fn (destT src)
            Any-extract (Syntax->datum src) destT

    set-type-symbol! type '@
        fn (self key)
            let keyT = (typeof key)
            if (type== keyT Symbol)
                type@ self key
            elseif (type== keyT i32)
                element-type self key
    set-type-symbol! type 'countof type-countof

    let empty-symbol = (Symbol "")

    set-type-symbol! Parameter 'apply-type
        fn (cls params...)
            let param1 param2 param3 = params...
            let TT = (tuple (typeof param1) (typeof param2) (typeof param3))
            if (type== TT (tuple Anchor Symbol type))
                Parameter-new param1 param2 param3
            elseif (type== TT (tuple Anchor Symbol Nothing))
                Parameter-new param1 param2 unknown
            elseif (type== TT (tuple Symbol type Nothing))
                Parameter-new (active-anchor) param1 param2
            elseif (type== TT (tuple Symbol Nothing Nothing))
                Parameter-new (active-anchor) param1 unknown
            else
                compiler-error! "usage: Parameter [anchor] symbol [type]"

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
            let [loop] x i = (tie-const i self) (i32 i)
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
            let [skip-head] l i =
                tie-const i0 self
                tie-const i0 (i64 0)
            if (< i i0)
                skip-head (list-next l) (+ i (i64 1))
            let count = (i64 (list-countof l))
            if (== (- i1 i0) count)
                return l
            let [build-slice] l next i = 
                tie-const i1 l
                tie-const i1 eol
                tie-const i1 i
            if (== i i1)
                list-reverse next
            else
                build-slice (list-next l) (list-cons (list-at l) next) (+ i 1:i64)

    fn list== (a b)
        if (icmp!= (list-countof a) (list-countof b))
            return false
        let [loop] a b = (tie-const b a) (tie-const a b)
        if (list-empty? a)
            return true
        let u v = (list-at a) (list-at b)
        let uT vT = ('typeof u) ('typeof v)
        if (not (type== uT vT))
            return false
        let un vn = (list-next a) (list-next b)
        if (type== uT list)
            if (list== (softcast list u) (softcast list v))
                loop un vn
            else
                return false
        elseif (Any== u v)
            loop un vn
        else
            return false
                
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
    set-type-symbol! string 'softcast
        fn (destT self)
            if (type== destT rawstring)
                getelementptr self 0 1 0

    set-type-symbol! string 'countof string-countof
    set-type-symbol! string '@
        fn string-at (s i)
            assert-typeof s string
            let i = (i64 i)
            let len = (i64 (string-countof s))
            if (< i 0:i64)
                return 0:i8
            elseif (>= i len)
                return 0:i8
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

fn <: (T superT)
    let [loop] T = T
    let value = (superof T)
    if (type== value superT) true
    elseif (type== value typename) false
    else
        loop value

fn Any-list? (val)
    assert-typeof val Any
    type== ('typeof val) list

fn maybe-unsyntax (val)
    if (type== ('typeof val) Syntax)
        extractvalue (load (cast Syntax val)) 1
    else val

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
    fn print-element (val)
        let T = (typeof val)
        if (== T string)
            io-write! val
        else
            io-write! (repr val)

    let [loop] i = 0
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
    let [loop] l = l
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
                cast list value
                add depth 1
        else
            on-leaf value depth
            true
        loop next

fn typify (f types...)
    let vacount = (va-countof types...)
    let atype = (array-type type (usize vacount))
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
            elseif (== flag 'dump-function) compile-flag-dump-function
            elseif (== flag 'dump-time) compile-flag-dump-time
            elseif (== flag 'no-opts) compile-flag-no-opts
            else
                compiler-error!
                    .. "illegal flag: " (repr flag)

fn syntax-error! (anchor msg)
    let T = (typeof anchor)
    if (== T string)
        if (none? msg)
            __error! anchor
            unreachable!;
    set-anchor!
        if (== T Any)
            let T = ('typeof anchor)
            if (== T Syntax)
                Syntax-anchor (cast Syntax anchor)
            else
                cast Anchor anchor
        elseif (== T Syntax)
            Syntax-anchor anchor
        else anchor
    __error! msg
    unreachable!;

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
            function (tuple list Scope) list list Scope
    set-typename-storage! Macro BlockScopeFunction
    set-type-symbol! Macro 'apply-type
        fn (cls f)
            assert-typeof f BlockScopeFunction
            bitcast f Macro
    set-type-symbol! Macro 'cast
        fn (destT self)
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
            cast BlockScopeFunction
                compile (typify f list list Scope)
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
      --------------------------------------------------------------------------

    fn dotted-symbol? (env head)
        let s = (Symbol->string head)
        let sz = (countof s)
        let [loop] i = (unconst 0:usize)
        if (== i sz)
            return false
        elseif (== (@ s i) (char "."))
            return true
        loop (+ i 1:usize)

    fn split-dotted-symbol (head start end tail)
        let s = (Symbol->string head)
        let [loop] i = (unconst start)
        if (== i end)
            # did not find a dot
            if (== start 0:usize)
                return (cons head tail)
            else
                return (cons (Symbol (slice s start)) tail)
        if (== (@ s i) (char "."))
            let dot = '.
            let tail =
                # no remainder after dot
                if (== i (- end 1:usize)) tail
                else # remainder after dot, split the rest first
                    split-dotted-symbol head (+ i 1:usize) end tail
            if (== i 0:usize)
                # no prefix before dot
                return (cons dot tail)
            else
                # prefix before dot
                return
                    cons (Symbol (slice s start i)) dot tail
        loop (+ i 1:usize)
    
    # infix notation support
      --------------------------------------------------------------------------

    fn get-ifx-symbol (name)
        Symbol (.. "#ifx:" (Symbol->string name))

    fn make-expand-define-infix (order)
        fn expand-define-infix (args scope)
            let prec token func = (decons args 3)
            let prec = 
                cast i32 (cast Syntax prec)
            let token = 
                cast Symbol (cast Syntax token)
            let func = 
                if (== ('typeof func) Nothing) token
                else
                    cast Symbol (cast Syntax func)
            set-scope-symbol! scope (get-ifx-symbol token)
                list prec order func
            return none scope

    fn get-ifx-op (env op)
        let sym = (Syntax->datum (cast Syntax op))
        if (== ('typeof sym) Symbol)
            @ env (get-ifx-symbol (cast Symbol sym))
        else
            return (Any none) false

    fn has-infix-ops? (infix-table expr)
        # any expression of which one odd argument matches an infix operator
          has infix operations.
        let [loop] expr = expr
        if (< (countof expr) 3:usize)
            return false
        let expr = (list-next expr)
        let at next = (decons expr)
        let result ok = (get-ifx-op infix-table at)
        if ok
            return true
        loop expr

    fn unpack-infix-op (op)
        let op-prec op-order op-func = (decons (cast list op) 3)
        return
            cast i32 op-prec
            cast Symbol op-order
            cast Symbol op-func

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
                Any none
        else
            syntax-error! token
                "unexpected token in infix expression"

    fn parse-infix-expr (infix-table lhs state mprec)
        assert-typeof infix-table Scope
        assert-typeof lhs Any
        assert-typeof state list
        assert-typeof mprec i32
        let [loop] lhs state = lhs state
        if (empty? state)
            return lhs state
        let la next-state = (decons state)
        let op = (infix-op infix-table la mprec >=)
        if (== ('typeof op) Nothing)
            return lhs state
        let op-prec op-order op-name = (unpack-infix-op op)
        let [rhs-loop] rhs state = (decons next-state)
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
        let sxexpr = (cast Syntax (list-at topexpr))
        let expr expr-anchor = (Syntax->datum sxexpr) (Syntax-anchor sxexpr)
        if (!= ('typeof expr) list)
            return topexpr env
        let expr = (cast list expr)
        let head-key = (Syntax->datum (cast Syntax (list-at expr)))
        let head =
            if (== ('typeof head-key) Symbol)
                let head success = (@ env (cast Symbol head-key))
                if success head
                else head-key
            else head-key
        if (== ('typeof head) Macro)
            let head = (cast Macro head)
            let next = (list-next topexpr)
            let expr env = (head expr next env)
            let expr = (Syntax-wrap expr-anchor (Any expr) false)
            return (cast list (cast Syntax expr)) env
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
        let sxname = (cast Syntax at)
        let name name-anchor = (cast Symbol sxname) (Syntax-anchor sxname)
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
            let [loop] result head = (decons expr)
            if (list-empty? head)
                return result
            let tmp =
                Parameter-new
                    Syntax-anchor (cast Syntax (list-at head))
                    \ 'tmp unknown
            loop
                Any
                    list do
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
    let sxcond = (cast Syntax cond)
    let anchor = (Syntax-anchor sxcond)
    let tmp =
        Parameter-new anchor 'tmp unknown
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
        let sym = (cast Symbol (cast Syntax b))
        list getattr a (list quote sym)
    let a b rest = (decons args 2)
    let [loop] rest result = rest (op a b)
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
define-infix> 800 .
define-infix> 800 @
#define-infix> 800 .=
#define-infix> 800 @=
#define-infix> 800 =@

#-------------------------------------------------------------------------------
# references
#-------------------------------------------------------------------------------

define reference 
    typename "reference"

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
        fn (self name)
            getattr (bitcast self (storageof (typeof self))) name

    set-type-symbol! reference 'repr
        fn (self)
            forward-repr (load self)

    set-type-symbol! reference 'softcast
        fn (destT self)
            let ptrtype = (storageof (typeof self))
            if (type== destT ptrtype)
                bitcast self ptrtype
            else
                cast destT (load (bitcast self ptrtype))

    set-type-symbol! reference '=
        fn (self value)
            let ET = (element-type (storageof (typeof self)) 0)
            store (softcast ET value) self
            true

    set-type-symbol! reference 'apply-type
        fn (cls element)
            if (type== cls reference)
                fn make-reference-type (ET)
                    let T = (typename (.. "&" (type-name ET)))
                    set-typename-super! T reference
                    set-typename-storage! T (pointer ET)
                    T

                # due to auto-memoization, we'll always get the same type back
                    provided the element type is a constant
                assert (constant? element)
                assert-typeof element type
                if (element <: reference)
                    compiler-error! 
                        .. "cannot create reference type of reference type " 
                            repr element
                make-reference-type element
            else
                let ET = (storageof cls)
                bitcast (softcast ET element) cls

define-block-scope-macro var
    fn element-typeof (value)
        let T = (typeof value)
        if (T <: reference)
            element-type (storageof T) 0
        else T

    let args = (list-next expr)
    let name token rest = (decons args 2)
    let token = (cast Symbol (cast Syntax token))
    if (token == '=)
        let value rest = (decons rest)
        let tmp = (Parameter 'tmp)
        let T = (Parameter 'T)
        return 
            cons
                list let tmp '= value
                list let T '= (list element-typeof tmp)
                list let name '= (list (list reference T) (list alloca T))
                #list let name '= (list alloca T)
                list (do =) name tmp
                next-expr
            syntax-scope
    elseif (token == '@)
        let size token rest = (decons rest 2)
        let token = (cast Symbol (cast Syntax token))
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

    let name token rest = (decons args 2)
    let token = (cast Symbol (cast Syntax token))
    if (token == '=)
        let value rest = (decons rest)
        let tmp = (Parameter 'tmp)
        let T = (Parameter 'T)
        list define name
            list let tmp '= value
            list let T '= (list element-typeof tmp)
            list let name '= (list (list reference T) (list malloc T))
            list (do =) name tmp
            name
    elseif (token == '@)
        let size token rest = (decons rest 2)
        let token = (cast Symbol (cast Syntax token))
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

#-------------------------------------------------------------------------------
# module loading
#-------------------------------------------------------------------------------

define package
    let package = (Scope)
    set-scope-symbol! package 'path
        list "./?.sc"
            .. compiler-dir "/?.sc"
    set-scope-symbol! package 'modules (Scope)
    package

syntax-extend
    fn make-module-path (pattern name)
        let sz = (countof pattern)
        let [loop] i start result = 
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
        let ModuleFunctionType = (pointer (function Any))
        let fptr =
            if (rettype == Any)
                cast ModuleFunctionType f
            else
                # build a wrapper
                let expr =
                    list
                        list let 'tmp '= (list f)
                        list Any-new 'tmp
                let expr = (cast Syntax (Syntax-wrap expr-anchor (Any expr) false))
                let f = (compile (eval expr (globals)))
                cast ModuleFunctionType f
        fptr;

    fn require (name)
        let package = (unconst package)
        assert-typeof name Symbol
        let namestr = (Symbol->string name)
        fn load-module (name)
            let modules = (cast Scope package.modules)
            let content ok = (@ modules name)
            if ok
                return content true
            let [loop] patterns = (cast list package.path)
            if (empty? patterns)
                return (Any none) false
            let pattern patterns = (decons patterns)
            let pattern = (cast string pattern)
            let module-path = (make-module-path pattern namestr)
            if (not (file? module-path))
                loop patterns
            let expr = (list-load module-path)
            let eval-scope = (Scope (globals))
            set-scope-symbol! eval-scope 'module-path module-path
            let content = (exec-module expr eval-scope)
            set-scope-symbol! modules name content
            return content true
        let content ok = (load-module name)
        if ok
            return content
        io-write! "no such module '"
        io-write! (Symbol->string name)
        io-write! "' in paths:\n"
        let [loop] patterns = (cast list package.path)
        if (empty? patterns)
            abort!;
            unreachable!;
        let pattern patterns = (decons patterns)
        let pattern = (cast string pattern)
        let module-path = (make-module-path pattern namestr)
        io-write! "    "
        io-write! module-path
        io-write! "\n"
        loop patterns
        
    set-scope-symbol! syntax-scope 'require require
    syntax-scope

define-scope-macro locals
    let newscope = (Scope)
    let [loop] last-key = (unconst none)
    let key value =
        Scope-next syntax-scope (Any last-key)
    if (not (('typeof key) == Nothing))
        if (('typeof key) == Symbol)
            let key = (cast Symbol key)
            set-scope-symbol! newscope key value
        loop key
    return newscope syntax-scope

define-macro import
    let name = (decons args)
    let name = (cast Symbol (cast Syntax name))
    list define name
        list require
            list quote name

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
        let exc = (cast Exception exc)
        format-message (Exception-anchor exc)
            .. (default-styler style-error "error:")
                \ " " (Exception-message exc)
    else
        .. "exception raised: " (repr exc) "\n"

fn prompt (prefix preload)
    __prompt prefix
        if (none? preload) "" 
        else preload

# earliest form of match macro - doesn't do elaborate patterns yet, just
  simple switch-case style comparisons
define-macro match
    let value rest = (decons args)
    let tmp = (Parameter 'tmp)
    fn process (i src expr)
        if (empty? expr)
            return '()        
        let pair rest = (decons expr)
        let key dst = (decons (cast list (cast Syntax pair)))
        let kkey = (cast Symbol (cast Syntax key))
        cons
            if (kkey == 'else)
                cons 'else dst
            else        
                cons (? (i == 0) 'if 'elseif) (list '== src key) dst
            process (i + 1) src rest
    cons do
        list let tmp '= value
        process (unconst 0) tmp rest

#-------------------------------------------------------------------------------
# using
#-------------------------------------------------------------------------------

fn merge-scope-symbols (source target filter)
    let [loop] last-key = (unconst none)
    let key value =
        Scope-next source (Any last-key)
    if (not (('typeof key) == Nothing))
        if (('typeof key) == Symbol)
            let key = (cast Symbol key)
            if 
                or (none? filter)
                    do
                        let keystr = (cast string key)                
                        string-match? filter keystr
                set-scope-symbol! target key value
        loop key

define-macro using
    let name rest = (decons args)
    list syntax-extend
        cons merge-scope-symbols name 'syntax-scope 
            if (empty? rest) '()
            else
                let token pattern rest = (decons rest 2)
                let token = (cast Symbol (cast Syntax token))
                if (token != 'filter)
                    syntax-error! (active-anchor)
                        "syntax: using <scope> [filter <filter-string>]"
                let pattern = (cast string (cast Syntax pattern))
                list pattern
        'syntax-scope

#-------------------------------------------------------------------------------
# various C related sugar
#-------------------------------------------------------------------------------

# labels softcast to function pointers
set-type-symbol! Label 'softcast
    fn (destT self)
        if (function-pointer-type? destT)
            let ET = (rawcall element-type destT 0)
            let sz = (trunc (rawcall type-countof ET) i32)
            if (rawcall function-type-variadic? ET)
                compiler-error! "cannot typify to variadic function"
            let [loop] i args... = sz
            if (icmp== i 1)
                let result =
                    compile (typify self args...)
                if (destT != ('typeof result))
                    syntax-error! (Label-anchor self)
                        .. "function does not compile to type " (repr destT)
                            \ " but has type " (repr ('typeof result))
                return (softcast destT result)
            else
                let i-1 = (sub i 1)
                loop i-1 (rawcall element-type ET i-1) args...

# a nullptr type that casts to whatever null pointer is required
syntax-extend
    let NullType = (typename "NullType")
    set-typename-storage! NullType (pointer void)
    set-type-symbol! NullType 'softcast
        fn (destT self)
            if (pointer-type? destT)
                nullof destT
    let null = (nullof NullType)
    set-scope-symbol! syntax-scope 'NullType NullType
    set-scope-symbol! syntax-scope 'null null
    syntax-scope

# support assignment syntax
set-type-symbol! pointer '=
    fn (self value)
        store
            cast (element-type (typeof self) 0) value
            self
        true

# pointer comparisons
set-type-symbol! pointer '==
    fn (a b flipped)
        if flipped
            icmp== (ptrtoint (softcast (typeof b) a) usize) (ptrtoint b usize)
        else
            icmp== (ptrtoint a usize) (ptrtoint (softcast (typeof a) b) usize)


# pointer cast to element type executes load
set-type-symbol! pointer 'cast
    fn (destT self)
        if (type== destT (element-type (typeof self) 0))
            load self

# support getattr syntax
set-type-symbol! pointer 'getattr
    fn (self name)
        let ET = (element-type (typeof self) 0)
        let op success = (type@ ET 'getattr&)
        if success
            let result... = (op self name)
            if (icmp== (va-countof result...) 0)
            else                
                return result...
        getattr (load self) name

# support @
set-type-symbol! pointer '@
    fn (self index)
        let index =
            if (none? index) 0:usize # simple dereference
            else index
        let ET = (element-type (typeof self) 0)
        (reference ET) (getelementptr self (usize index))

# extern cast to element type/pointer executes load/unconst
set-type-symbol! extern 'softcast
    fn (destT self)
        let ET = (element-type (typeof self) 0)
        if (type== destT ET)
            load (unconst self)
        elseif (type== destT (pointer ET))
            unconst self

# support assignment syntax for extern
set-type-symbol! extern '=
    fn (self value)
        (unconst self) = value
        true

# support @ for extern
set-type-symbol! extern '@
    fn (self value)
        @ (unconst self) value

do
    fn unenum (val)
        let T = (typeof val)
        if (T <: CEnum)
            bitcast val (storageof T)
        else val

    # support for downcast
    set-type-symbol! CEnum 'softcast
        fn (destT self)
            let ST = (storageof (typeof self))
            if (type== destT ST)
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

# support for C struct initializers
set-type-symbol! CStruct 'apply-type
    fn (cls args...)
        let sz = (va-countof args...)
        if (icmp== sz 0)
            nullof cls
        else
            let T = (storageof cls)
            let keys... = (va-keys args...)
            let [loop] i instance = 0 (nullof cls)
            if (icmp<s i sz)
                let key = (va@ i keys...) 
                let arg = (va@ i args...)
                let k =
                    if (key == unnamed) i
                    else
                        typename-field-index cls key
                let ET = (element-type T k)
                loop (add i 1)
                    insertvalue instance (softcast ET arg) k
            else
                instance

# access reference to struct element from pointer/reference
set-type-symbol! CStruct 'getattr&
    fn (self name)
        let ET = (element-type (typeof self) 0)
        let idx = (typename-field-index ET name)
        if (icmp>=s idx 0)
            # cast result to reference
            let val = (getelementptr self 0 idx)
            (reference (element-type (typeof val) 0)) val

# support for basic C union initializer
set-type-symbol! CUnion 'apply-type
    fn (cls)
        nullof cls

# access reference to union element from pointer/reference
set-type-symbol! CUnion 'getattr&
    fn (self name)
        let ET = (element-type (typeof self) 0)
        let idx = (typename-field-index ET name)
        if (icmp>=s idx 0)
            let FT = (element-type ET idx)
            # cast pointer to reference to alternative type
            (reference FT) (bitcast self (pointer FT))

fn pointer-call (self ...)
    fn docall (dest ET)
        let sz = (va-countof ...)
        let count = (trunc (rawcall type-countof ET) i32)
        let variadic = (rawcall function-type-variadic? ET)
        let [loop] i args... = sz
        if (icmp== i 0)
            rawcall dest args...
        else
            let i-1 = (sub i 1)
            let arg = (va@ i-1 ...)
            if ((not variadic) or (icmp<s i count))
                let argtype = (rawcall element-type ET i)
                loop i-1 (softcast argtype arg) args...
            else
                loop i-1 arg args...

    let ET = (rawcall element-type (typeof self) 0)
    let ST = (rawcall superof ET)
    if (type== ST function)
        docall self ET
    elseif (function-pointer-type? ET) # can also call pointer to pointer to function
        docall (load self) (rawcall element-type ET 0)
    else
        (load self) ...

# extern call attempts to cast arguments to correct type
set-type-symbol! extern 'call
    fn (self ...)
        pointer-call (unconst self) ...

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
    io-write! "  "; io-write! (default-styler style-number "\\\\\\"); io-write! "\n"
    io-write! "   "; io-write! (default-styler style-type "\\\\\\"); io-write! "\n"
    io-write! " "; io-write! (default-styler style-comment "///")
    io-write! (default-styler style-sfxfunction "\\\\\\"); io-write! "\n"
    io-write! (default-styler style-comment "///"); io-write! "  "
    io-write! (default-styler style-keyword "\\\\\\")

fn read-eval-print-loop ()
    fn repeat-string (n c)
        let [loop] i s =
            tie-const n (usize 0)
            tie-const n ""
        if (i == n)
            return s
        loop (i + (usize 1))
            .. s c

    fn leading-spaces (s)
        let len = (i32 (countof s))
        let [loop] i = (tie-const len 0)
        if (i == len)
            return s
        let c = (@ s i)
        if (c != (char " "))
            return (string-new (string->rawstring s) (usize i))
        loop (i + 1)

    fn blank? (s)
        let len = (i32 (countof s))
        let [loop] i =
            tie-const len 0
        if (i == len)
            return true
        if ((@ s i) != (char " "))
            return false
        loop (i + 1)

    print-logo;
    print " "
        compiler-version-string;

    let global-scope = (globals)
    let eval-scope = (Scope global-scope)
    let [loop] preload cmdlist counter =
        unconst ""
        unconst ""
        unconst 0
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
        if (slen == 0:usize) false
        else
            (@ s (slen - 1:usize)) == (char " ")
    let enter-multiline = (endswith-blank cmd)
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
        if terminated? ""
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
            let ModuleFunctionType = (pointer (function Any))
            let fptr =
                if (rettype == Any)
                    cast ModuleFunctionType f
                else
                    # build a wrapper
                    let expr =
                        list
                            list let 'tmp '= (list f)
                            list Any-new 'tmp
                    let expr = (cast Syntax (Syntax-wrap expr-anchor (Any expr) false))
                    let f = (compile (eval expr global-scope))
                    cast ModuleFunctionType f
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
    let [loop] i sourcepath parse-options = 1 none true
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
        let expr = (list-load sourcepath)
        let eval-scope = (Scope (globals))
        set-scope-symbol! eval-scope 'module-path sourcepath
        let f = (compile (eval expr eval-scope))
        let ModuleFunctionType = (pointer (function void))
        if (function-pointer-type? ('typeof f))
            call (inttoptr (Any-payload f) ModuleFunctionType)
        else
            error! "function pointer expected"
        exit 0
        unreachable!;

run-main (args)
true

