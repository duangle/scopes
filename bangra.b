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
    functions and macros, parses the command-line and then enters the REPL.

fn/cc expand-expr-list (_ topit env)
    fn/cc expand-fn/cc (_)
        fn/cc process-name (_ anchor it subenv)
            fn/cc expand-parameter (_ sxparam)
                call
                    fn/cc (_ param-anchor param-name)
                        call
                            fn/cc (_ param)
                                set-scope-symbol! subenv param-name param
                                \ param
                            Parameter-new param-anchor param-name Any
                    syntax->anchor sxparam
                    syntax->datum sxparam

            fn/cc process-params (_ func it)
                fn/cc process-body (_)
                    translate-label-body! func anchor
                        expand-expr-list (list-next it) subenv
                    # return result
                    list-cons
                        datum->quoted-syntax func anchor
                        list-next topit

                fn/cc process-param (_ params)
                    branch (list-empty? params)
                        \ process-body
                        fn/cc (_)
                            label-append-parameter! func
                                expand-parameter (list-at params)
                            process-param
                                list-next params
                call
                    fn/cc (_ sxplist)
                        call
                            fn/cc (_ params)
                                branch (list-empty? params)
                                    fn/cc (_)
                                        error (syntax->anchor params)
                                            \ "explicit continuation parameter missing"
                                    fn/cc (_)
                                        process-param params
                            syntax->datum sxplist
                    list-at it

            call
                fn/cc (_ tryfunc-name)
                    branch (type== (typeof tryfunc-name) Symbol)
                        fn/cc (_)
                            # named self-binding
                            call
                                fn/cc (_ func)
                                    set-scope-symbol! env tryfunc-name func
                                    process-params func
                                        list-next it
                                Label-new anchor tryfunc-name
                        fn/cc (_)
                            branch (type== (typeof tryfunc-name) string)
                                fn/cc (_)
                                    # named lambda
                                    process-params
                                        Label-new anchor (Symbol-new tryfunc-name)
                                        list-next it
                                fn/cc (_)
                                    # unnamed lambda
                                    process-params
                                        Label-new anchor (Symbol-new "")
                                        \ it
                syntax->datum (list-at it)

        call
            fn/cc (_ it)
                call process-name
                    syntax->anchor (list-at it)
                    list-next it
                    Scope-new env

            syntax->datum (list-at topit)

    fn/cc expand (_ topit env)
        fn/cc expand-list-or-symbol (_ expr anchor)

            fn/cc expand-call-list (_)
                call
                    fn/cc (return outlist outenv)
                        return
                            list-cons
                                datum->quoted-syntax outlist anchor
                                list-next topit
                            \ outenv
                    expand-expr-list expr env

            #fn/cc expand-wildcard-list (_)
                local default_handler = env:lookup(Symbol.ListWildcard)
                if default_handler then
                    return expand_wildcard("wildcard list",
                        env, default_handler, topit,
                        function (result)
                            if result ~= EOL then
                                return expand(env, result)
                            end
                            return expand-call-list()
                        end)
                end
                return expand-call-list()

            #fn/cc expand-macro-list (_ f)
                #return expand_macro(env, unmacro(head), topit,
                    function (result_list,result_env)
                        if (result_list ~= EOL) then
                            assert_scope(result_env)
                            assert(result_list ~= EOL)
                            return expand(result_env, result_list)
                        elseif result_env then
                            return cont(EOL, env)
                        else
                            return expand_wildcard_list()
                        end
                    end)

            fn/cc expand-list (return)
                branch
                    list-empty? expr
                    fn/cc (_)
                        error anchor "expression is empty"
                    fn/cc (_)
                call
                    fn/cc (_ head)
                        # expand-macro-list (macro->Label head)
                        # expand-wildcard-list
                        expand-call-list
                    call
                        fn/cc (_ head)
                            branch (type== (typeof head) Symbol)
                                fn/cc (_)
                                    branch (Symbol== head (Symbol-new "fn/cc"))
                                        fn/cc (_)
                                            return (expand-fn/cc)
                                        fn/cc (_)
                                            scope@ env head
                                fn/cc (_) head
                        syntax->datum
                            list-at expr

            fn/cc expand-symbol (_ value value-exists?)
                branch value-exists?
                    fn/cc (_)
                        branch (type== (typeof value) list)
                            fn/cc (return)
                                # quote lists
                                return
                                    list-cons
                                        list-cons
                                            datum->quoted-syntax
                                                \ form-quote anchor
                                            list-cons
                                                datum->quoted-syntax value anchor
                                                \ list-empty
                                        list-next topit
                                    \ env
                            fn/cc (return)
                                return
                                    list-cons
                                        datum->quoted-syntax value anchor
                                        list-next topit
                                    \ env
                    fn/cc (_)
                        fn/cc missing-symbol-error (_)
                            error anchor
                                string-join "no value bound to name "
                                    string-join (repr expr) " in scope"
                        #
                            local default_handler = env:lookup(Symbol.SymbolWildcard)
                            if default_handler then
                                return expand_wildcard("wildcard symbol",
                                    env, default_handler, topit, function(result)
                                    if result ~= EOL then
                                        return expand(env, result)
                                    end
                                    return missing_symbol_error()
                                end)
                            else
                                return missing_symbol_error()
                            end
                        missing-symbol-error

            call
                fn/cc (_ expr-type)
                    branch (type== expr-type list)
                        \ expand-list
                        fn/cc (_)
                            branch (type== expr-type Symbol)
                                fn/cc (_)
                                    expand-symbol (scope@ env expr)
                                fn/cc (return)
                                    return topit env
                typeof expr

        call
            fn/cc (_ expr)
                branch
                    syntax-quoted? expr
                    fn/cc (return)
                        return topit env
                    fn/cc (_)
                        expand-list-or-symbol
                            syntax->datum expr
                            syntax->anchor expr
            list-at topit

    print "expanding" topit
    branch (list-empty? topit)
        fn/cc (return)
            return topit env
        fn/cc (_)
            call
                fn/cc (_ nextlist nextscope)
                    call
                        fn/cc (return restlist restscope)
                            return
                                list-cons (list-at nextlist) restlist
                                \ restscope
                        expand-expr-list
                            list-next nextlist
                            \ nextscope
                expand topit env

# defer the rest of the source file to process()
syntax-apply-block
    fn/cc process (_ exprs env)
        expand-expr-list exprs env

# by the time we get here,
fn/cc somefunc (_ x)
    print
        branch false
            fn/cc (_)
                print "yes" x
                \ "!!"
            fn/cc (_)
                print "no" x
                \ "??"

somefunc "hello world 1"
somefunc "hello world 2"

#
    # this is a test block
    syntax-extend

        fn/cc make-param (_ name)
            parameter-new (datum->syntax (symbol-new name) (active-anchor))
        fn/cc make-typed-param (_ name atype)
            parameter-new (datum->syntax (symbol-new name) (active-anchor)) atype

        fn/cc testboot (_)
            #fn/cc test-pow (_)
                fn/cc pow2 (ret x)
                    i32* x x

                fn/cc pow (ret x n)
                    branch (i32== n 0)
                        fn/cc (_)
                            ret 1
                        fn/cc (_)
                            branch (i32== (i32% n 2) 0)
                                fn/cc (_)
                                    ret
                                        pow2
                                            pow x (i32/ n 2)
                                fn/cc (_)
                                    ret
                                        i32* x (pow x (i32- n 1))

                fn/cc pow7 (ret x)
                    pow x 7

                call
                    fn/cc (_ f)
                        print f
                        print
                            f 5
                    mangle pow7
                        make-param "ret"
                        make-typed-param "x" i32

            fn/cc test-polymorph-return (_)
                # example: a div that converts arguments to r64 if division isn't integer
                fn/cc polydiv (_ x y)
                    branch (i32== (i32% x y) 0)
                        # divisible
                        fn/cc (_)
                            i32/ x y
                        # fractional
                        fn/cc (_)
                            r64/ (r64-new x) (r64-new y)
                print "test-polymorph-return"
                call
                    fn/cc (_ polydiv-i32)
                        print "polydiv template:"
                        dump-label polydiv
                        print "polydiv typed to i32 i32:"
                        dump-label polydiv-i32
                        #print "polydiv 1/2"
                            r64== (polydiv-i32 1 2) (r64-new 0.5)
                        #print "polydiv 4/2"
                            i32== (polydiv-i32 4 2) 2
                    typify polydiv i32 i32

            fn/cc test-print-n (_)
                fn/cc print-n (ret f n)
                    cc/call
                        fn/cc rec (_ i)
                            cc/call branch none (i32== i n)
                                fn/cc (_)
                                    ret
                                fn/cc (_)
                                    f i
                                    cc/call rec none (i32+ i 1)
                        \ none 0

                fn/cc print-n-main (_ n)
                    print-n
                        call
                            fn/cc (_ x y)
                                fn/cc (_ i)
                                    print i n
                                        i32+ x y
                            \ 300 3
                        \ n

                print "test-print-n"
                print "print-n template:"
                dump-label print-n-main
                call
                    fn/cc (_ print-n-main-typed)
                        print "print-n typed:"
                        dump-label print-n-main-typed
                    typify print-n-main i32

            fn/cc test-print-args (_)
                fn/cc print-args (_)
                    call
                        fn/cc (_ x extra)
                            print
                                branch
                                    type== (typeof extra) string
                                    fn/cc (_)
                                        print "branch 1"
                                        string== extra "yes"
                                    fn/cc (_)
                                        print "branch 2"
                                        \ "???"
                        \ "test" "yes"
                call
                    fn/cc (_ print-args-typed)
                        print "print-args template:"
                        dump-label print-args
                        print "print-args typed:"
                        dump-label print-args-typed
                    typify print-args

            test-polymorph-return
            test-print-n
            test-print-args

            exit 0

        call
            fn/cc (_ self extra)
                branch
                    type== (typeof extra) string
                    fn/cc (_)
                        branch
                            string== extra "testboot"
                            \ testboot
                            fn/cc (_) none
                    fn/cc (_) none
            args

        \ syntax-scope

    print "running main"

    syntax-extend
        print "stage 1"
        set-type-symbol! Symbol (symbol-new "apply-type") symbol-new
        \ syntax-scope

    syntax-extend
        print "stage 2"
        fn/cc no-op (_)
        fn/cc unreachable (_)
            error "unreachable branch"

        fn/cc forward (_ name errmsg)
            fn/cc (_ x values...)
                call
                    fn/cc (_ x-func)
                        branch
                            type== (typeof x-func) Nothing
                            fn/cc (_)
                                error
                                    string-join "can not "
                                        string-join errmsg
                                            string-join " value of type "
                                                string-new (typeof x)
                            fn/cc (_)
                                x-func x values...
                    type-at (typeof x) name

        fn/cc forward-op2 (_ name errmsg)
            fn/cc (_ a b)
                fn/cc forward-op2-error (_)
                    error
                        string-join "can not "
                            string-join errmsg
                                string-join " values of type "
                                    string-join
                                        string-new (typeof a)
                                        string-join " and " (string-new (typeof b))
                fn/cc alt-forward-op2 (_)
                    call
                        fn/cc (_ b-func)
                            branch
                                type== (typeof b-func) Nothing
                                \ forward-op2-error
                                fn/cc (_)
                                    call
                                        fn/cc (_ result...)
                                            branch
                                                u64== (va-countof result...) (u64-new 0)
                                                \ forward-op2-error
                                                fn/cc (_) result...
                                        b-func a b true
                        type-at (typeof b) name
                call
                    fn/cc (_ a-func)
                        branch
                            type== (typeof a-func) Nothing
                            \ alt-forward-op2
                            fn/cc (_)
                                call
                                    fn/cc (_ result...)
                                        branch
                                            u64== (va-countof result...) (u64-new 0)
                                            \ alt-forward-op2
                                            fn/cc (_) result...
                                    a-func a b false
                    type-at (typeof a) name

        call
            fn/cc (_ forward-slice forward-at forward-countof forward-join)
                fn/cc syntax-slice (_ self values...)
                    call
                        fn/cc (_ value loc)
                            datum->syntax (forward-slice value values...) loc
                        syntax->datum self
                        syntax->anchor self

                fn/cc syntax-at (_ self values...)
                    forward-at (syntax->datum self) values...

                fn/cc syntax-countof (_ self)
                    forward-countof (syntax->datum self)

                fn/cc syntax-join (_ a b)
                    call
                        fn/cc (_ a b a-loc)
                            datum->syntax (forward-join a b) a-loc
                        syntax->datum a
                        syntax->datum b
                        syntax->anchor a

                fn/cc slice (_ obj start-index end-index)
                    call
                        fn/cc (_ zero count i0 i1)
                            call
                                fn/cc (_ i0 i1)
                                    call
                                        fn/cc (_ i1)
                                            forward-slice obj (u64-new i0) (u64-new i1)
                                        branch
                                            i64>= i1 zero
                                            fn/cc (_) i1
                                            fn/cc (_)
                                                call
                                                    fn/cc (_ i1)
                                                        branch
                                                            i64>= i1 i0
                                                            fn/cc (_) i1
                                                            fn/cc (_) i0
                                                    i64+ i1 count
                                branch
                                    i64< i0 zero
                                    fn/cc (_)
                                        branch
                                            i64<= i0 (i64- zero count)
                                            fn/cc (_) zero
                                            fn/cc (_) (i64+ i0 count)
                                    fn/cc (_)
                                        branch
                                            i64<= i0 count
                                            fn/cc (_) i0
                                            fn/cc (_) count
                                branch
                                    type== (typeof i1) Nothing
                                    fn/cc (_) count
                                    fn/cc (_) (i64-new i1)
                        i64-new 0
                        i64-new
                            forward-countof obj
                        i64-new start-index
                        \ end-index

                set-scope-symbol! syntax-scope (Symbol "slice") slice
                set-scope-symbol! syntax-scope (Symbol "@") forward-at
                set-scope-symbol! syntax-scope (Symbol "countof") forward-countof
                set-scope-symbol! syntax-scope (Symbol "..") forward-join
                set-type-symbol! Syntax (Symbol "slice") syntax-slice
                set-type-symbol! Syntax (Symbol "@") syntax-at
                set-type-symbol! Syntax (Symbol "countof") syntax-countof
                set-type-symbol! Syntax (Symbol "..") syntax-join

            forward (Symbol "slice") "slice"
            forward (Symbol "@") "index"
            forward (Symbol "countof") "count"
            forward-op2 (Symbol "..") "join"

        fn/cc set-forward-op2 (_ name errmsg)
            set-scope-symbol! syntax-scope name (forward-op2 name errmsg)
        set-forward-op2 (Symbol "+") "add"
        set-forward-op2 (Symbol "-") "subtract"
        set-forward-op2 (Symbol "*") "multiply"
        set-forward-op2 (Symbol "/") "divide"
        set-forward-op2 (Symbol "%") "modulate"
        set-forward-op2 (Symbol "&") "and-combine"
        set-forward-op2 (Symbol "|") "or-combine"
        set-forward-op2 (Symbol "^") "xor"
        set-forward-op2 (Symbol "**") "exponentiate"
        fn/cc set-forward (_ name errmsg)
            set-scope-symbol! syntax-scope name (forward name errmsg)
        set-forward (Symbol "~") "bitwise-negate"
        set-forward (Symbol "<<") "left-shift"
        set-forward (Symbol ">>") "right-shift"

        set-type-symbol! list (Symbol "apply-type") list-new
        #set-type-symbol! list (Symbol "compare") list-compare
        set-type-symbol! list (Symbol "@") list@
        set-type-symbol! list (Symbol "countof") list-countof
        set-type-symbol! list (Symbol "slice") list-slice
        #set-type-symbol! Syntax (Symbol "compare") syntax-compare
        \ syntax-scope

    syntax-extend
        print "stage 2.1"
        call
            fn/cc (_ name)
                fn/cc ordered-branch (_ a b cc== cc!= cc< cc>)
                    fn/cc forward-compare-error (_ a b)
                        error
                            string-join "types "
                                string-join (repr (typeof a))
                                    string-join " and "
                                        string-join (repr (typeof b)) " are incomparable"

                    fn/cc alt-forward-compare (_)
                        call
                            fn/cc (_ b-func)
                                branch
                                    type== (typeof b-func) Nothing
                                    fn/cc (_)
                                        forward-compare-error a b
                                    fn/cc (_)
                                        b-func b a cc== cc!= cc> cc<
                            type-at (typeof b) name
                    call
                        fn/cc (_ a-func)
                            branch
                                type== (typeof a-func) Nothing
                                \ alt-forward-compare
                                fn/cc (_)
                                    a-func a b cc== alt-forward-compare cc< cc>
                        type-at (typeof a) name
                set-scope-symbol! syntax-scope (Symbol "ordered-branch") ordered-branch
            Symbol "compare"
        \ syntax-scope

    syntax-extend
        print "stage 2.2"
        set-scope-symbol! syntax-scope
            Symbol "quote"
            block-scope-macro
                fn/cc "expand-quote" (return expr syntax-scope)
                    call
                        fn/cc (_ args)
                            return
                                cons
                                    syntax-list
                                        datum->syntax
                                            Symbol "form-quote"
                                            \ args
                                        syntax-quote
                                            # keep wrapped in list if multiple
                                                arguments
                                            branch
                                                u64== (countof args) (u64-new 1)
                                                fn/cc (_) (@ args 0)
                                                fn/cc (_) args
                                    slice expr 1
                                \ syntax-scope
                        slice (@ expr 0) 1
        set-scope-symbol! syntax-scope
            Symbol "quote-syntax"
            block-scope-macro
                fn/cc "expand-quote-syntax" (return expr syntax-scope)
                    call
                        fn/cc (_ args)
                            return
                                cons
                                    # wrap syntax object in another syntax object
                                    datum->syntax
                                        # keep wrapped in list if multiple
                                            arguments
                                        branch
                                            u64== (countof args) (u64-new 1)
                                            fn/cc (_) (@ args 0)
                                            fn/cc (_) args
                                        \ args
                                    slice expr 1
                                \ syntax-scope
                        slice (@ expr 0) 1
        \ syntax-scope

    syntax-extend
        print "stage 3"
        fn/cc unordered-error (_ a b)
            error
                .. "illegal ordered comparison of values of types "
                    .. (repr (typeof a))
                        .. " and "
                            repr (typeof b)

        fn/cc return-true (_) true
        fn/cc return-false (_) false

        set-scope-symbol! syntax-scope (quote ==)
            fn/cc "==" (return a b)
                ordered-branch a b return-true
                    \ return-false return-false return-false
        set-scope-symbol! syntax-scope (quote !=)
            fn/cc "!=" (return a b)
                ordered-branch a b return-false
                    \ return-true return-true return-true
        set-scope-symbol! syntax-scope (quote <)
            fn/cc "<" (return a b)
                ordered-branch a b return-false
                    fn/cc (_) (cc/call unordered-error none a b)
                    \ return-true return-false
        set-scope-symbol! syntax-scope (quote <=)
            fn/cc "<=" (return a b)
                ordered-branch a b return-true
                    fn/cc (_) (cc/call unordered-error none a b)
                    \ return-true return-false
        set-scope-symbol! syntax-scope (quote >)
            fn/cc ">" (return a b)
                ordered-branch a b return-false
                    fn/cc (_) (cc/call unordered-error none a b)
                    \ return-false return-true
        set-scope-symbol! syntax-scope (quote >=)
            fn/cc ">=" (return a b)
                ordered-branch a b return-true
                    fn/cc (_) (cc/call unordered-error none a b)
                    \ return-false return-true
        set-scope-symbol! syntax-scope (quote <>)
            fn/cc "<>" (return a b)
                ordered-branch a b return-false
                    fn/cc (_) (cc/call unordered-error none a b)
                    \ return-true return-true
        set-scope-symbol! syntax-scope (quote <:)
            fn/cc "<:" (return a b)
                ordered-branch a b return-false return-false
                    \ return-true return-false

        \ syntax-scope

    syntax-extend
        print "stage 4"
        #---
        set-type-symbol! string (quote apply-type) string-new
        set-type-symbol! type (quote apply-type) type-new
        set-type-symbol! Label (quote apply-type) label-new
        set-type-symbol! Parameter (quote apply-type) parameter-new
        set-type-symbol! Scope (quote apply-type) scope-new
        set-type-symbol! ref (quote apply-type) ref-new

        set-type-symbol! i8 (quote apply-type) i8-new
        set-type-symbol! i16 (quote apply-type) i16-new
        set-type-symbol! i32 (quote apply-type) i32-new
        set-type-symbol! i64 (quote apply-type) i64-new

        set-type-symbol! u8 (quote apply-type) u8-new
        set-type-symbol! u16 (quote apply-type) u16-new
        set-type-symbol! u32 (quote apply-type) u32-new
        set-type-symbol! u64 (quote apply-type) u64-new

        set-type-symbol! r32 (quote apply-type) r32-new
        set-type-symbol! r64 (quote apply-type) r64-new

        #---
        fn/cc unordered-compare-op (_ T T==)
            set-type-symbol! T (quote compare)
                fn/cc (_ a b cc== cc!=)
                    branch
                        type== (typeof a) T
                        fn/cc (_)
                            branch
                                type== (typeof b) T
                                fn/cc (_)
                                    branch
                                        T== a b
                                        \ cc== cc!=
                                \ cc!=
                        \ cc!=

        fn/cc ordered-compare-op (_ T T== T<)
            set-type-symbol! T (quote compare)
                fn/cc (_ a b cc== cc!= cc< cc>)
                    branch
                        type== (typeof a) T
                        fn/cc (_)
                            branch
                                type== (typeof b) T
                                fn/cc (_)
                                    branch
                                        T== a b
                                        \ cc==
                                        fn/cc (_)
                                            branch
                                                T< a b
                                                \ cc< cc>
                                \ cc!=
                        \ cc!=

        fn/cc partially-ordered-compare-op (_ T T== T< T>)
            set-type-symbol! T (quote compare)
                fn/cc (_ a b cc== cc!= cc< cc>)
                    branch
                        type== (typeof a) T
                        fn/cc (_)
                            branch
                                type== (typeof b) T
                                fn/cc (_)
                                    branch
                                        T== a b
                                        \ cc==
                                        fn/cc (_)
                                            branch
                                                T< a b
                                                \ cc<
                                                fn/cc (_)
                                                    branch
                                                        T> a b
                                                        \ cc> cc!=
                                \ cc!=
                        \ cc!=

        set-type-symbol! Nothing (quote compare)
            fn/cc nothing-compare (_ a b cc== cc!=)
                branch
                    type== (typeof a) (typeof b)
                    \ cc== cc!=
        unordered-compare-op bool bool==
        unordered-compare-op Symbol Symbol==
        unordered-compare-op Parameter Parameter==
        unordered-compare-op Label Label==
        unordered-compare-op Scope Scope==

        ordered-compare-op string string== string<
        ordered-compare-op type type== type<

        ordered-compare-op i8 i8== i8<
        ordered-compare-op i16 i16== i16<
        ordered-compare-op i32 i32== i32<
        ordered-compare-op i64 i64== i64<

        ordered-compare-op u8 u8== u8<
        ordered-compare-op u16 u16== u16<
        ordered-compare-op u32 u32== u32<
        ordered-compare-op u64 u64== u64<

        partially-ordered-compare-op r32 r32== r32< r32>
        partially-ordered-compare-op r64 r64== r64< r64>

        set-type-symbol! list (quote compare)
            fn/cc list-compare (_ a b cc== cc!=)
                fn/cc list-loop (_ a b)
                    call
                        fn/cc (_ a-len)
                            branch
                                u64== a-len (list-countof b)
                                fn/cc (_)
                                    branch
                                        u64== a-len (u64-new 0)
                                        \ cc==
                                        fn/cc (_)
                                            branch
                                                == (list-at a) (list-at b)
                                                fn/cc (_)
                                                    list-loop
                                                        list-next a
                                                        list-next b
                                                \ cc!=
                                \ cc!=
                        list-countof a
                branch
                    type== (typeof a) list
                    fn/cc (_)
                        branch
                            type== (typeof b) list
                            fn/cc (_)
                                list-loop a b
                            \ cc!=
                    \ cc!=

        set-type-symbol! Syntax (quote compare)
            fn/cc syntax-compare (_ a b cc== cc!= cc< cc>)
                ordered-branch
                    syntax->datum a
                    syntax->datum b
                    \ cc== cc!= cc< cc>

        #---
        set-type-symbol! string (quote ..) string-join
        set-type-symbol! list (quote ..) list-join

        #---
        set-type-symbol! string (quote countof) string-countof

        #---
        set-type-symbol! Scope (quote @) scope-at
        set-type-symbol! type (quote @) type-at
        set-type-symbol! string (quote @) string-at
        set-type-symbol! ref (quote @) ref-at

        #---
        set-type-symbol! string (quote slice) string-slice
        #---
        set-type-symbol! i8 (quote ~) i8~; set-type-symbol! i16 (quote ~) i16~; set-type-symbol! i32 (quote ~) i32~; set-type-symbol! i64 (quote ~) i64~
        set-type-symbol! u8 (quote ~) u8~; set-type-symbol! u16 (quote ~) u16~; set-type-symbol! u32 (quote ~) u32~; set-type-symbol! u64 (quote ~) u64~

        set-type-symbol! i8 (quote +) i8+; set-type-symbol! i16 (quote +) i16+; set-type-symbol! i32 (quote +) i32+; set-type-symbol! i64 (quote +) i64+
        set-type-symbol! u8 (quote +) u8+; set-type-symbol! u16 (quote +) u16+; set-type-symbol! u32 (quote +) u32+; set-type-symbol! u64 (quote +) u64+
        set-type-symbol! i8 (quote -) i8-; set-type-symbol! i16 (quote -) i16-; set-type-symbol! i32 (quote -) i32-; set-type-symbol! i64 (quote -) i64-
        set-type-symbol! u8 (quote -) u8-; set-type-symbol! u16 (quote -) u16-; set-type-symbol! u32 (quote -) u32-; set-type-symbol! u64 (quote -) u64-
        set-type-symbol! i8 (quote *) i8*; set-type-symbol! i16 (quote *) i16*; set-type-symbol! i32 (quote *) i32*; set-type-symbol! i64 (quote *) i64*
        set-type-symbol! u8 (quote *) u8*; set-type-symbol! u16 (quote *) u16*; set-type-symbol! u32 (quote *) u32*; set-type-symbol! u64 (quote *) u64*
        set-type-symbol! i8 (quote /) i8/; set-type-symbol! i16 (quote /) i16/; set-type-symbol! i32 (quote /) i32/; set-type-symbol! i64 (quote /) i64/
        set-type-symbol! u8 (quote /) u8/; set-type-symbol! u16 (quote /) u16/; set-type-symbol! u32 (quote /) u32/; set-type-symbol! u64 (quote /) u64/
        set-type-symbol! i8 (quote %) i8%; set-type-symbol! i16 (quote %) i16%; set-type-symbol! i32 (quote %) i32%; set-type-symbol! i64 (quote %) i64%
        set-type-symbol! u8 (quote %) u8%; set-type-symbol! u16 (quote %) u16%; set-type-symbol! u32 (quote %) u32%; set-type-symbol! u64 (quote %) u64%
        set-type-symbol! i8 (quote **) i8**; set-type-symbol! i16 (quote **) i16**; set-type-symbol! i32 (quote **) i32**; set-type-symbol! i64 (quote **) i64**
        set-type-symbol! u8 (quote **) u8**; set-type-symbol! u16 (quote **) u16**; set-type-symbol! u32 (quote **) u32**; set-type-symbol! u64 (quote **) u64**
        set-type-symbol! i8 (quote &) i8&; set-type-symbol! i16 (quote &) i16&; set-type-symbol! i32 (quote &) i32&; set-type-symbol! i64 (quote &) i64&
        set-type-symbol! u8 (quote &) u8&; set-type-symbol! u16 (quote &) u16&; set-type-symbol! u32 (quote &) u32&; set-type-symbol! u64 (quote &) u64&
        set-type-symbol! i8 (quote |) i8|; set-type-symbol! i16 (quote |) i16|; set-type-symbol! i32 (quote |) i32|; set-type-symbol! i64 (quote |) i64|
        set-type-symbol! u8 (quote |) u8|; set-type-symbol! u16 (quote |) u16|; set-type-symbol! u32 (quote |) u32|; set-type-symbol! u64 (quote |) u64|
        set-type-symbol! i8 (quote ^) i8^; set-type-symbol! i16 (quote ^) i16^; set-type-symbol! i32 (quote ^) i32^; set-type-symbol! i64 (quote ^) i64^
        set-type-symbol! u8 (quote ^) u8^; set-type-symbol! u16 (quote ^) u16^; set-type-symbol! u32 (quote ^) u32^; set-type-symbol! u64 (quote ^) u64^
        set-type-symbol! i8 (quote <<) i8<<; set-type-symbol! i16 (quote <<) i16<<; set-type-symbol! i32 (quote <<) i32<<; set-type-symbol! i64 (quote <<) i64<<
        set-type-symbol! u8 (quote <<) u8<<; set-type-symbol! u16 (quote <<) u16<<; set-type-symbol! u32 (quote <<) u32<<; set-type-symbol! u64 (quote <<) u64<<
        set-type-symbol! i8 (quote >>) i8>>; set-type-symbol! i16 (quote >>) i16>>; set-type-symbol! i32 (quote >>) i32>>; set-type-symbol! i64 (quote >>) i64>>
        set-type-symbol! u8 (quote >>) u8>>; set-type-symbol! u16 (quote >>) u16>>; set-type-symbol! u32 (quote >>) u32>>; set-type-symbol! u64 (quote >>) u64>>

        set-type-symbol! r32 (quote +) r32+; set-type-symbol! r64 (quote +) r64+
        set-type-symbol! r32 (quote -) r32-; set-type-symbol! r64 (quote -) r64-
        set-type-symbol! r32 (quote *) r32*; set-type-symbol! r64 (quote *) r64*
        set-type-symbol! r32 (quote /) r32/; set-type-symbol! r64 (quote /) r64/
        set-type-symbol! r32 (quote %) r32%; set-type-symbol! r64 (quote %) r64%
        set-type-symbol! r32 (quote **) r32**; set-type-symbol! r64 (quote **) r64**

        \ syntax-scope

    syntax-extend
        print "stage 5"
        set-scope-symbol! syntax-scope
            quote dump-syntax
            block-scope-macro
                fn/cc "expand-dump-syntax" (return expr env)
                    call
                        fn/cc (_ e env)
                            dump
                                @ e 0
                            return
                                cons
                                    @ e 0
                                    slice expr 1
                                \ env
                        expand
                            slice (@ expr 0) 1
                            \ env
        \ syntax-scope

    syntax-extend
        print "stage 6"
        set-scope-symbol! syntax-scope
            quote callable?
            fn/cc "callable?" (return x)
                return
                    <: (typeof x) Callable
        set-scope-symbol! syntax-scope
            quote integer?
            fn/cc "integer?" (return x)
                return
                    <: (typeof x) Integer
        set-scope-symbol! syntax-scope
            quote label?
            fn/cc "label?" (return x)
                return
                    == (typeof x) Label
        set-scope-symbol! syntax-scope
            quote real?
            fn/cc "real?" (return x)
                return
                    <: (typeof x) Real
        set-scope-symbol! syntax-scope
            quote symbol?
            fn/cc "symbol?" (return x)
                return
                    == (typeof x) Symbol
        set-scope-symbol! syntax-scope
            quote syntax?
            fn/cc "syntax?" (return x)
                return
                    == (typeof x) Syntax
        set-scope-symbol! syntax-scope
            quote list?
            fn/cc "list?" (return x)
                return
                    == (typeof x) list
        set-scope-symbol! syntax-scope
            quote type?
            fn/cc "type?" (return x)
                return
                    == (typeof x) type
        set-scope-symbol! syntax-scope
            quote none?
            fn/cc "none?" (return x)
                return
                    == x none
        set-scope-symbol! syntax-scope
            quote empty?
            fn/cc "empty?" (return x)
                return
                    not (> (countof x) (size_t 0))
        set-scope-symbol! syntax-scope
            quote load
            fn/cc "load" (return path)
                return
                    eval
                        list-load path
                        globals
                        \ path
        set-scope-symbol! syntax-scope
            quote macro
            fn/cc "macro" (return f)
                return
                    block-scope-macro
                        fn/cc (return expr env)
                            return
                                cons
                                    f (@ expr 0) env
                                    slice expr 1
                                \ env
        set-scope-symbol! syntax-scope
            quote block-macro
            fn/cc "block-macro" (return f)
                return
                    block-scope-macro
                        fn/cc (return expr env)
                            return
                                f expr env
                                \ env
        set-scope-symbol! syntax-scope
            quote ?
            block-scope-macro
                fn/cc "expand-?" (return topexpr env)
                    return
                        call
                            fn/cc (_ expr rest)
                                call
                                    fn/cc (_ ret-true ret-false expr-anchor)
                                        cons
                                            syntax-list
                                                datum->syntax branch expr
                                                @ expr 1
                                                syntax-cons
                                                    datum->syntax fn/cc expr-anchor
                                                    syntax-cons
                                                        syntax-list ret-true
                                                        syntax-list
                                                            @ expr 2
                                                syntax-cons
                                                    datum->syntax fn/cc expr-anchor
                                                    syntax-cons
                                                        syntax-list ret-false
                                                        syntax-list
                                                            @ expr 3
                                            \ rest
                                    datum->syntax
                                        Parameter (datum->syntax (quote _) expr)
                                    datum->syntax
                                        Parameter (datum->syntax (quote _) expr)
                                    syntax->anchor expr
                            @ topexpr 0
                            slice topexpr 1
                        \ env
        \ syntax-scope

    syntax-extend
        print "stage 7"
        set-scope-symbol! syntax-scope (quote do)
            macro
                fn/cc "expand-do" (return expr env)
                    syntax-list
                        syntax-cons
                            datum->syntax fn/cc expr
                            syntax-cons
                                syntax-list
                                    datum->syntax
                                        Parameter
                                            datum->syntax (quote _) expr
                                slice expr 1
        \ syntax-scope

    syntax-extend
        print "stage 8"
        set-scope-symbol! syntax-scope (quote syntax-head?)
            fn/cc "syntax-head?" (return expr name)
                return
                    ? (list? (syntax->datum expr))
                        ? (empty? expr)
                            \ false
                            call
                                fn/cc (_ head)
                                    ? (symbol? (syntax->datum head))
                                        _ (== head name)
                                        _ false
                                @ expr 0
                        \ false
        set-scope-symbol! syntax-scope (quote list-atom?)
            fn/cc "list-atom?" (return x)
                return
                    ? (list? (syntax->datum x))
                        empty? x
                        \ true
        # unwrap single item from list or prepend 'do' clause to list
        set-scope-symbol! syntax-scope (quote syntax-do)
            fn/cc "syntax-do" (return expr)
                return
                    ? (== (countof expr) (size_t 1))
                        @ expr 0
                        syntax-cons (datum->syntax do expr) expr
        # build a list terminator tagged with a desired anchor from an expression
        set-scope-symbol! syntax-scope (quote syntax-eol)
            fn/cc "syntax-eol" (return expr)
                return
                    datum->syntax (list) expr
        \ syntax-scope

    syntax-extend
        print "stage 9"
        fn/cc syntax-wrap (_ loc x)
            ? (syntax? x) x
                datum->syntax x loc

        fn/cc syntax-qquote-1 (_ x)
            ? (list-atom? x)
                syntax-list
                    datum->syntax quote-syntax x
                    \ x
                ?
                    ? (syntax-head? x (quote unquote)) true
                        syntax-head? x (quote curly-list)
                    syntax-list (datum->syntax syntax-wrap x)
                        datum->syntax  (syntax->anchor x) x
                        syntax-do (slice x 1)
                    ? (syntax-head? x (quote qquote-syntax))
                        syntax-qquote-1 (syntax-qquote-1 (@ x 1))
                        ? (list-atom? (@ x 0))
                            syntax-list
                                datum->syntax syntax-cons (@ x 0)
                                syntax-qquote-1 (@ x 0)
                                syntax-qquote-1 (slice x 1)
                            ? (syntax-head? (@ x 0) (quote unquote-splice))
                                syntax-list
                                    datum->syntax (do ..) (@ x 0)
                                    syntax-do (slice (@ x 0) 1)
                                    syntax-qquote-1 (slice x 1)
                                syntax-list
                                    datum->syntax syntax-cons (@ x 0)
                                    syntax-qquote-1 (@ x 0)
                                    syntax-qquote-1 (slice x 1)
        # quasiquote support
          (qquote expr [...])
        set-scope-symbol! syntax-scope (quote qquote-syntax)
            macro
                fn/cc "expand-qquote" (_ expr env)
                    ? (empty? (slice expr 2))
                        syntax-qquote-1 (@ expr 1)
                        syntax-qquote-1 (slice expr 1)
        \ syntax-scope

    syntax-extend
        print "stage 10"
        set-scope-symbol! syntax-scope (quote define)
            block-macro
                fn/cc "expand-define" (return topexpr env)
                    cons
                        qquote-syntax
                            {syntax-extend}
                                {set-scope-symbol!} syntax-scope
                                    quote
                                        unquote (@ (@ topexpr 0) 1)
                                    unquote
                                        syntax-do (slice (@ topexpr 0) 2)
                                \ syntax-scope
                        slice topexpr 1
        \ syntax-scope

    # a lofi version of let so we get some sugar early
    define let
        block-scope-macro
            fn/cc "expand-let" (return expr env)
                branch
                    symbol? (syntax->datum (@ (@ expr 0) 2))
                    fn/cc (_) (_)
                    fn/cc (_)
                        syntax-error (@ expr 0) "syntax: let <var> = <expr>"
                branch
                    == (@ (@ expr 0) 2) (quote =)
                    fn/cc (_) (_)
                    fn/cc (_)
                        syntax-error (@ expr 0) "syntax: let <var> = <expr>"
                return
                    call
                        fn/cc (_ cont-param param-name rest)
                            list
                                qquote-syntax
                                    ;
                                        {fn/cc}
                                            ;
                                                unquote cont-param
                                                unquote param-name
                                            unquote-splice
                                                datum->syntax rest (@ expr 0)
                                        unquote
                                            syntax-do
                                                slice (@ expr 0) 3
                                        # force enclosing list to use anchor
                                          of original expression
                                        unquote-splice
                                            syntax-eol (@ expr 0)
                        datum->syntax
                            Parameter (quote-syntax _)
                        @ (@ expr 0) 1
                        slice expr 1
                    \ env

    # a standard function declaration with implicit continuation argument
      (fn [name] (param ...) body ...)
      an extended implementation follows further down
    define fn
        block-macro
            fn/cc "expand-fn" (return topexpr)
                let expr =
                    @ topexpr 0
                let decl =
                    @ expr 1
                let decl-anchor =
                    syntax->anchor decl
                let retparam =
                    datum->syntax
                        syntax->datum
                            quote return
                        \ decl-anchor
                fn/cc wrap (_ val)
                    datum->syntax val decl-anchor
                fn/cc make-params-body (_ param-idx)
                    syntax-cons
                        syntax-cons
                            \ retparam
                            @ expr param-idx
                        slice expr (+ param-idx 1)
                let rest =
                    slice topexpr 1
                ? (symbol? (syntax->datum decl))
                    cons
                        syntax-cons
                            wrap fn/cc
                            syntax-cons
                                \ decl
                                make-params-body 2
                        \ rest
                    cons
                        syntax-cons
                            wrap fn/cc
                            make-params-body 1
                        \ rest

    define raise
        fn/cc "raise" (_ x)
            error x

    define try
        block-macro
            fn expand-try (expr env)
                ? (not (syntax-head? (@ expr 1) (quote except)))
                    error "except block missing"
                    \ true
                cons
                    qquote-syntax
                        {xpcall}
                            {fn} ()
                                unquote-splice
                                    slice (@ expr 0) 1
                            {fn}
                                unquote (@ (@ expr 1) 1)
                                unquote-splice
                                    slice (@ expr 1) 2
                    slice expr 2

    define int i32
    define uint i32
    define float r32
    define double r64

    define _
        fn forward-multiargs (args...) args...

    # (assert bool-expr [error-message])
    define assert
        macro
            fn assert (expr)
                qquote-syntax
                    {?} (unquote (@ expr 1))
                        \ {true}
                        {error}
                            unquote
                                ? (empty? (slice expr 2))
                                    datum->syntax
                                        .. "assertion failed: "
                                            string
                                                syntax->datum
                                                    @ expr 1
                                        \ expr
                                    @ expr 2
                            unquote-splice
                                syntax-eol expr
                        unquote-splice
                            syntax-eol expr


    define sizeof
        let sym = (Symbol "size")
        fn/cc "sizeof" (return x)
            assert (type? x) "type expected"
            let size =
                @ x sym
            ? (none? size) (size_t 0) size

    define alignof
        let sym = (Symbol "alignment")
        fn/cc "alignof" (return x)
            assert (type? x) "type expected"
            return
                @ x sym

    define ::@
        block-macro
            fn ::@ (expr)
                cons
                    ..
                        slice (@ expr 0) 1
                        syntax-list
                            @ expr 1
                    slice expr 2
    define ::*
        block-macro
            fn ::* (expr)
                list
                    ..
                        slice (@ expr 0) 1
                        slice expr 1

    define .
        macro
            fn . (expr)
                let key = (@ expr 2)
                ? (symbol? (syntax->datum key))
                    qquote-syntax
                        {@} (unquote (@ expr 1))
                            unquote
                                syntax-quote key
                            unquote-splice
                                syntax-eol key
                    error "symbol expected"

    define and
        macro
            fn and (expr)
                let tmp = (datum->syntax (Parameter (quote-syntax tmp)))
                let ret = (datum->syntax (Parameter (quote-syntax and-return)))
                let ret-true = (datum->syntax (Parameter (quote-syntax ret-true)))
                let ret-false = (datum->syntax (Parameter (quote-syntax ret-false)))
                qquote-syntax
                    ;
                        {fn/cc} ((unquote ret) (unquote tmp))
                            {branch} (unquote tmp)
                                {fn/cc} ((unquote ret-true))
                                    unquote (@ expr 2)
                                {fn/cc} ((unquote ret-false))
                                    unquote tmp
                        unquote (@ expr 1)
                        unquote-splice
                            syntax-eol expr

    define or
        macro
            fn or (expr)
                let tmp = (datum->syntax (Parameter (quote-syntax tmp)))
                let ret = (datum->syntax (Parameter (quote-syntax or-return)))
                let ret-true = (datum->syntax (Parameter (quote-syntax ret-true)))
                let ret-false = (datum->syntax (Parameter (quote-syntax ret-false)))
                qquote-syntax
                    ;
                        {fn/cc} ((unquote ret) (unquote tmp))
                            {branch} (unquote tmp)
                                {fn/cc} ((unquote ret-true))
                                    unquote tmp
                                {fn/cc} ((unquote ret-false))
                                    unquote (@ expr 2)
                        unquote (@ expr 1)
                        unquote-splice
                            syntax-eol expr

    define if
        block-macro
            fn if-rec (topexpr env)
                let expr =
                    @ topexpr 0
                let cond =
                    @ expr 1
                let then-exprlist =
                    slice expr 2
                fn make-branch (else-exprlist)
                    let ret-then = (datum->syntax (Parameter (quote-syntax ret-then)))
                    let ret-else = (datum->syntax (Parameter (quote-syntax ret-else)))
                    qquote-syntax
                        {branch} (unquote cond)
                            {fn/cc} ((unquote ret-then))
                                unquote-splice then-exprlist
                            {fn/cc} ((unquote ret-else))
                                unquote-splice else-exprlist
                            unquote-splice
                                syntax-eol expr
                let rest-expr =
                    slice topexpr 1
                let next-expr =
                    ? (empty? rest-expr)
                        \ rest-expr
                        @ rest-expr 0
                ? (syntax-head? next-expr (quote elseif))
                    do
                        let nextif =
                            if-rec rest-expr env
                        cons
                            make-branch
                                syntax-list (@ nextif 0)
                            slice nextif 1
                    ? (syntax-head? next-expr (quote else))
                        cons
                            make-branch
                                slice next-expr 1
                            slice topexpr 2
                        cons
                            make-branch
                                syntax-eol expr
                            \ rest-expr

    syntax-extend
        print "stage 11"
        fn =? (x)
            and
                symbol? (syntax->datum x)
                == x (quote =)

        # iterate until we hit the = symbol, after which
          the body follows
        fn find= (expr)
            if (empty? expr)
                error "syntax: let <name> ... = <expression> ..."
            elseif (=? (@ expr 0))
                return (list)
                    slice expr 1
            else
                call
                    fn (args rest)
                        return
                            cons (@ expr 0) args
                            \ rest
                    find=
                        slice expr 1

        set-scope-symbol! syntax-scope
            quote let
            # support for multiple declarations in one let env
            block-macro
                fn expand-let (topexpr env)
                    let expr = (@ topexpr 0)
                    let rest = (slice topexpr 1)
                    let body = (slice expr 1)
                    let argtype = (typeof (syntax->datum (@ body 0)))
                    if (== argtype list)
                        # prepare quotable values from declarations
                        fn handle-pairs (pairs)
                            if (empty? pairs)
                                \ rest
                            else
                                let pair =
                                    @ pairs 0
                                if (not (=? (@ pair 1)))
                                    syntax-error (@ pair 1) "syntax: let (<name> = <expression>) ..."
                                cons
                                    qquote-syntax let
                                        unquote-splice pair
                                    handle-pairs
                                        slice pairs 1
                        handle-pairs body
                    else
                        call
                            fn (args init)
                                let cont-param =
                                    datum->syntax (Parameter (quote-syntax let-return))
                                list
                                    qquote-syntax
                                        ;
                                            {fn/cc}
                                                ;
                                                    unquote cont-param
                                                    unquote-splice
                                                        datum->syntax args body
                                                unquote-splice
                                                    datum->syntax rest expr
                                            unquote-splice init
                                            # force enclosing list to use anchor
                                              of original expression
                                            unquote-splice
                                                syntax-eol expr
                            find= body

        \ syntax-scope

    define loop
        macro
            fn loop (expr)
                let expr-anchor = (syntax->anchor expr)
                let param-continue =
                    quote-syntax continue
                let param-break =
                    quote-syntax break
                qquote-syntax
                    {do}
                        {fn/cc} (unquote param-continue) (
                            (unquote param-break)
                            (unquote-splice (@ expr 1)))
                            unquote-splice
                                slice expr 2
                        ;
                            unquote param-continue
                            unquote-splice (@ expr 1)

    define syntax-infix-rules
        fn syntax-infix-rules (prec order name)
            fn spec ()
                return prec order name

    define define-infix-op
        block-macro
            fn expand-define-infix-op (topexpr env)
                let expr rest =
                    @ topexpr 0
                    slice topexpr 1
                let name =
                    syntax->datum (@ expr 1)
                let prec =
                    syntax->datum (@ expr 2)
                let dest-name = (@ expr 3)
                let order =
                    syntax->datum (@ expr 4)
                let infix-symbol =
                    Symbol
                        .. "#ifx:" (string name)
                set-scope-symbol! env infix-symbol
                    syntax-infix-rules prec (@ env order) dest-name
                return rest env

    syntax-extend
        print "stage 12"
        fn get-ifx-op (env op)
            let sym =
                syntax->datum op
            let key =
            ? (symbol? sym)
                get-scope-symbol env
                    Symbol
                        .. "#ifx:" (string sym)
                \ none

        fn has-infix-ops? (infix-table expr)
            # any expression of which one odd argument matches an infix operator
              has infix operations.
            loop (expr)
                if (< (countof expr) (size_t 3)) false
                elseif (none? (get-ifx-op infix-table (@ expr 1)))
                    continue (slice expr 1)
                else true

        fn infix-op (infix-table token prec pred)
            let op =
                get-ifx-op infix-table token
            if (none? op)
                error
                    .. (string token)
                        " is not an infix operator, but embedded in an infix expression"
            else
                let op-prec = (op)
                ? (pred op-prec prec) op none

        fn rtl-infix-op (infix-table token prec pred)
            let op =
                get-ifx-op infix-table token
            if (none? op)
                error
                    .. (string token)
                        " is not an infix operator, but embedded in an infix expression"
            else
                let op-prec op-order = (op)
                ? (and (== op-order <) (pred op-prec prec)) op none

        fn parse-infix-expr (infix-table lhs state mprec)
            loop (lhs state)
                if (empty? state)
                    break lhs state
                let la = (@ state 0)
                let op = (infix-op infix-table la mprec >=)
                if (none? op)
                    break lhs state
                let op-prec op-order op-name = (op)
                let next-state = (slice state 1)
                let rhs = (@ next-state 0)
                let state = (slice next-state 1)
                let next-rhs next-state =
                    loop (rhs state)
                        if (empty? state)
                            break rhs state
                        let ra = (@ state 0)
                        let lop = (infix-op infix-table ra op-prec >)
                        let nextop =
                            ? (none? lop)
                                rtl-infix-op infix-table ra op-prec ==
                                \ lop
                        if (none? nextop)
                            break rhs state
                        let nextop-prec = (nextop)
                        continue
                            parse-infix-expr infix-table rhs state
                                \ nextop-prec
                continue
                    datum->syntax
                        list op-name lhs next-rhs
                        \ la
                    \ next-state

        # expand infix operators in impure list of arguments
        fn parse-partial-infix-expr (infix-table expr)
            fn split (x)
                return (@ x 0) (slice x 1)
            # Collect tokens in a sequence as long as every second token is
              an infix operator.
              When a sequence is complete and longer than 1, expand that sequence
              as an infix expression list.
              Lastly, expand the last ongoing sequence if it's longer than 1,
              and only wrap in list if any previous sequences have been expanded.
            let k = 0
            loop (k expr)
                if (< (countof expr) (size_t 3)) expr
                else
                    let infix-expr rest =
                        loop (expr)
                            let lhs expr1 = (split expr)
                            let op expr2 = (split expr1)
                            if (or (none? (get-ifx-op infix-table op))
                                (empty? expr2))
                                break (list lhs) expr1
                            elseif (> (countof expr2) (size_t 1))
                                let result rest = (continue expr2)
                                break (cons lhs (cons op result)) rest
                            else
                                break (list lhs op (@ expr2 0)) (slice expr2 1)
                    if (>= (countof infix-expr) (size_t 3))
                        let expanded-expr =
                            parse-infix-expr infix-table (@ infix-expr 0) (slice infix-expr 1) 0
                        if (and (empty? rest) (== k 0)) expanded-expr
                        else
                            syntax-cons expanded-expr (continue (+ k 1) rest)
                    else
                        syntax-cons (@ infix-expr 0) (continue (+ k 1) rest)

        fn make-expand-multi-op-ltr (op)
            # (op a b c ...) -> (op (op (op a b) c) ...)
            macro
                fn (expr)
                    let wrapped-op =
                        datum->syntax op expr
                    let tail =
                        slice expr 1
                    loop (tail)
                        let rest =
                            slice tail 2
                        if (empty? rest)
                            syntax-cons wrapped-op tail
                        else
                            continue
                                syntax-cons
                                    syntax-list wrapped-op
                                        @ tail 0
                                        @ tail 1
                                    \ rest

        set-scope-symbol! syntax-scope (quote and)
            make-expand-multi-op-ltr and
        set-scope-symbol! syntax-scope (quote or)
            make-expand-multi-op-ltr or
        set-scope-symbol! syntax-scope (quote max)
            make-expand-multi-op-ltr
                fn (a b)
                    ? (> b a) b a
        set-scope-symbol! syntax-scope (quote min)
            make-expand-multi-op-ltr
                fn (a b)
                    ? (< b a) b a
        set-scope-symbol! syntax-scope (quote @)
            make-expand-multi-op-ltr @
        set-scope-symbol! syntax-scope (quote ..)
            make-expand-multi-op-ltr ..
        set-scope-symbol! syntax-scope (quote +)
            make-expand-multi-op-ltr +
        set-scope-symbol! syntax-scope (quote *)
            make-expand-multi-op-ltr *
        set-scope-symbol! syntax-scope (quote |)
            make-expand-multi-op-ltr |

        fn/cc selfcall (cont self name args...)
            (@ self name) self args...

        fn dotted-symbol? (env head)
            if (none? (get-scope-symbol env head))
                let s = (string head)
                let sz = (countof s)
                let i = (size_t 0)
                loop (i)
                    if (== i sz) false
                    elseif (== (@ s i) ".") true
                    else (continue (+ i (size_t 1)))
            else false

        fn has-dotted-symbols? (env expr)
            let expr = (syntax->datum expr)
            loop (expr)
                if (empty? expr)
                    break false
                fn next ()
                    continue (slice expr 1)
                let head = (syntax->datum (@ expr 0))
                if (symbol? head)
                    if (dotted-symbol? env head) true
                    else (next)
                else (next)

        fn expand-dotted-symbols (env expr)
            loop (expr)
                if (empty? expr)
                    break expr
                fn next ()
                    continue (slice expr 1)
                let sxhead = (@ expr 0)
                let head = (syntax->datum sxhead)
                if (symbol? head)
                    if (none? (get-scope-symbol env head))
                        let s = (string head)
                        let sz = (countof s)
                        let i = (size_t 0)
                        loop (i)
                            if (== i sz)
                                syntax-cons sxhead (next)
                            elseif (== (@ s i) ".")
                                let dot = (datum->syntax (quote .) sxhead)
                                fn head ()
                                    datum->syntax (Symbol (slice s 0 i)) sxhead
                                fn rest ()
                                    syntax-cons
                                        datum->syntax
                                            Symbol (slice s (+ i (size_t 1)))
                                            \ sxhead
                                        next
                                if (== i (size_t 0))
                                    syntax-cons dot (rest)
                                elseif (== i (- sz (size_t 1)))
                                    syntax-cons (head) (syntax-cons dot (next))
                                else
                                    syntax-cons (head) (syntax-cons dot (rest))
                            else (continue (+ i (size_t 1)))
                    else (syntax-cons sxhead (next))
                else (syntax-cons sxhead (next))

        fn named-argument-tuple? (entry)
            and (list? entry)
                and (== (countof entry) (size_t 3))
                    and (symbol? (syntax->datum (@ entry 0)))
                        == (@ entry 1) (quote =)

        fn has-named-arguments? (env expr)
            let expr = (syntax->datum expr)
            if (empty? expr)
                return false
            let head = (syntax->datum (@ expr 0))
            let head =
                ? (symbol? head)
                    get-scope-symbol env head
                    \ head
            let no-named-arguments? = (!= (typeof head) Label)
            let expr = (slice expr 1)
            loop (expr)
                if (empty? expr)
                    break false
                let entry = (@ expr 0)
                if (named-argument-tuple? (syntax->datum entry))
                    if no-named-arguments?
                        syntax-error entry "invalid use of named arguments"
                    # rest must also be named
                    let expr = (slice expr 1)
                    loop (expr)
                        if (empty? expr)
                            break true
                        let entry = (@ expr 0)
                        if (not (named-argument-tuple? (syntax->datum entry)))
                            syntax-error entry "named argument expected"
                        continue (slice expr 1)
                else
                    continue (slice expr 1)

        fn expand-named-arguments (env expr)
            let eol = (syntax-eol expr)
            let expr = (syntax->datum expr)
            let sxhead = (@ expr 0)
            let head = (syntax->datum sxhead)
            let head =
                ? (symbol? head)
                    get-scope-symbol env head
                    \ head
            let expr = (slice expr 1)
            let params... = (label-parameters head)
            let pcount = (int (va-countof params...))

            let idx = 1
            syntax-cons
                datum->syntax head sxhead
                loop (idx expr)
                    if (empty? expr) expr
                    let sxentry = (@ expr 0)
                    let entry = (syntax->datum sxentry)
                    if (named-argument-tuple? entry)
                        # create a mapping
                        let map = (Scope)
                        loop (expr)
                            if (empty? expr)
                                break
                            let sxentry = (@ expr 0)
                            let entry = (syntax->datum sxentry)
                            let sxkey = (@ entry 0)
                            let sxvalue = (@ entry 2)
                            let key = (syntax->datum sxkey)
                            if (none? (get-scope-symbol map key))
                                set-scope-symbol! map key
                                    fn () (return sxkey sxvalue)
                                continue (slice expr 1)
                            else
                                syntax-error sxentry "duplicate definition for named parameter"
                        # map remaining parameters
                        loop (idx)
                            let param = (va@ idx params...)
                            if (none? param)
                                # complain about unused scope entries
                                let key value = (next-scope-symbol map none)
                                if (none? key)
                                    break eol
                                else
                                    syntax-error (value) "no such named parameter"
                            let param-name = (parameter-name param)
                            let sxvalue = (get-scope-symbol map param-name)
                            del-scope-symbol! map param-name
                            syntax-cons
                                ? (none? sxvalue)
                                    datum->syntax none eol
                                    va@ 1 (sxvalue)
                                continue (+ idx 1)
                    else
                        # regular argument
                        syntax-cons sxentry
                            continue (+ idx 1) (slice expr 1)

        set-scope-symbol! syntax-scope scope-list-wildcard-symbol
            fn expand-any-list (topexpr env)
                let expr =
                    @ topexpr 0
                let head =
                    @ expr 0
                let headstr =
                    string head
                fn finalize (expr)
                    cons expr (slice topexpr 1)
                # method call syntax
                if
                    and
                        symbol? (syntax->datum head)
                        and
                            none? (get-scope-symbol env head)
                            == (slice headstr 0 1) "."

                    let name =
                        datum->syntax
                            Symbol
                                slice headstr 1
                            \ head
                    let self-arg =
                        @ expr 1
                    let rest =
                        slice expr 2
                    let self =
                        Parameter
                            quote-syntax self
                    finalize
                        qquote-syntax
                            unquote
                                datum->syntax selfcall expr
                            unquote self-arg
                            {quote}
                                unquote name
                            unquote-splice rest
                # expand unbound symbols that contain dots
                elseif (has-dotted-symbols? env expr)
                    finalize
                        expand-dotted-symbols env expr
                # infix operator support
                elseif (has-infix-ops? env expr)
                    finalize
                        #parse-infix-expr env (@ expr 0) (slice expr 1) 0
                        parse-partial-infix-expr env expr
                # named arguments
                elseif (has-named-arguments? env expr)
                    finalize
                        expand-named-arguments env expr

        #set-scope-symbol! syntax-scope scope-symbol-wildcard-symbol
            fn expand-any-symbol (topexpr env)
                let sym =
                    @ topexpr 0
                let sym-anchor =
                    syntax->anchor sym
                let it =
                    iter-r
                        string sym
                fn finalize-head (out)
                    cons
                        datum->syntax
                            Symbol
                                @ out 0
                            \ sym-anchor
                        slice out 1
                # return tokenized list if string contains a dot
                if
                    and
                        none? (get-scope-symbol env sym)
                        fold
                            fn (out k)
                                if (== k ".") true
                                else out
                            \ false
                            iter-r
                                string sym
                    cons
                        datum->syntax
                            finalize-head
                                fold
                                    fn (out k)
                                        if (== k ".")
                                            cons ""
                                                cons
                                                    datum->syntax
                                                        quote .
                                                        \ sym-anchor
                                                    finalize-head out
                                        else
                                            cons
                                                .. k (@ out 0)
                                                slice out 1
                                    list ""
                                    iter-r
                                        string sym
                            \ sym-anchor
                        slice topexpr 1
        \ syntax-scope

    #define-infix-op : 70 : >
    define-infix-op or 100 or >
    define-infix-op and 200 and >
    define-infix-op | 240 | >
    define-infix-op ^ 250 ^ >
    define-infix-op & 260 & >

    define-infix-op < 300 < >
    define-infix-op > 300 > >
    define-infix-op <= 300 <= >
    define-infix-op >= 300 >= >
    define-infix-op != 300 != >
    define-infix-op == 300 == >

    define-infix-op <: 300 <: >
    define-infix-op <> 300 <> >

    #define-infix-op is 300 is >
    define-infix-op .. 400 .. <
    define-infix-op << 450 << >
    define-infix-op >> 450 >> >
    define-infix-op - 500 - >
    define-infix-op + 500 + >
    define-infix-op % 600 % >
    define-infix-op / 600 / >
    #define-infix-op // 600 // >
    define-infix-op * 600 * >
    define-infix-op ** 700 ** <
    define-infix-op . 800 . >
    define-infix-op @ 800 @ >
    #define-infix-op .= 800 .= >
    #define-infix-op @= 800 @= >
    #define-infix-op =@ 800 =@ >

    syntax-extend
        print "stage 13"
        let Qualifier =
            type
                quote Qualifier

        set-type-symbol! Qualifier (quote apply-type)
            fn new-qualifier (name)
                assert (symbol? name) "symbol expected"
                let aqualifier =
                    type
                        Symbol
                            .. "Qualifier[" (string name) "]"

                set-type-symbol! aqualifier (quote super) Qualifier
                set-type-symbol! aqualifier (quote cast)
                    fn cast-qualifier (fromtype totype value)
                        if (fromtype <: aqualifier)
                            let fromelemtype = fromtype.element-type
                            if (fromelemtype == totype)
                                bitcast totype value
                        elseif (totype <: aqualifier)
                            let toelemtype = totype.element-type
                            if (toelemtype == fromtype)
                                bitcast totype value
                set-type-symbol! aqualifier (quote apply-type)
                    fn new-typed-qualifier (element-type)
                        let typed-qualifier =
                            type
                                Symbol
                                    .. (string name) "[" (string element-type) "]"
                        if (none? (. typed-qualifier complete))
                            set-type-symbol! typed-qualifier (quote super) aqualifier
                            set-type-symbol! typed-qualifier (quote element-type) element-type
                            set-type-symbol! typed-qualifier (quote complete) true

                        return typed-qualifier
                set-type-symbol! aqualifier (quote complete) true

                return aqualifier

        set-scope-symbol! syntax-scope (quote Qualifier) Qualifier
        set-scope-symbol! syntax-scope (quote Iterator)
            Qualifier (quote Iterator)
        set-scope-symbol! syntax-scope (quote qualify)
            fn qualify (qualifier-type value)
                assert
                    qualifier-type <: Qualifier
                    error
                        .. "qualifier type expected, got "
                            repr qualifier-type
                cast
                    qualifier-type (typeof value)
                    \ value

        set-scope-symbol! syntax-scope (quote disqualify)
            fn disqualify (qualifier-type value)
                assert
                    qualifier-type <: Qualifier
                    error
                        .. "qualifier type expected, got "
                            repr qualifier-type
                let t = (typeof value)
                if (not (t <: qualifier-type))
                    error
                        .. "can not unqualify value of type " (string t)
                            \ "; type not related to " (string qualifier-type) "."
                cast t.element-type
                    \ value

        \ syntax-scope

    syntax-extend
        print "stage 14"

        fn iterator? (x)
            (typeof x) <: Iterator

        # `start` is a private token passed to `next`, which is an iterator
          function of the format (next token) -> next-token value ... | none
        fn new-iterator (next start)
            qualify Iterator
                fn iterator-tuple ()
                    return next start

        fn countable-rslice-iter (l)
            if (not (empty? l))
                return (slice l 1) (@ l 0)

        fn countable-iter (f)
            let c i = (f)
            if (i < (countof c))
                return
                    fn ()
                        return c (i + (size_t 1))
                    @ c i

        fn scope-iter (f)
            let t k = (f)
            let key value =
                next-scope-symbol t k
            if (not (none? key))
                return
                    fn ()
                        return t key
                    \ key value

        fn iter (x)
            if (iterator? x) x
            else
                let t = (typeof x)
                let make-iter = t.iter
                if (none? make-iter)
                    error
                        .. "don't know how to iterate value of type " (string t)
                else
                    new-iterator
                        make-iter x

        set-type-symbol! list (quote iter)
            fn list-iter (self)
                return countable-rslice-iter self
        set-type-symbol! Scope (quote iter)
            fn scope-iter (self)
                return scope-iter
                    fn () (return self none)
        set-type-symbol! string (quote iter)
            fn string-iter (self)
                return countable-iter
                    fn () (return self (size_t 0))
        set-type-symbol! Label (quote iter)
            fn gen-yield-iter (callee)
                let caller-return = (ref none)
                fn/cc yield-iter (return cont-callee)
                    # store caller continuation in state
                    ref-set! caller-return return
                    branch (none? cont-callee) # first invocation
                        fn/cc (_)
                            # invoke callee with yield function as first argument
                            callee
                                fn/cc (cont-callee values...)
                                    # continue caller
                                    cc/call (@ caller-return) none cont-callee values...
                            # callee has returned for good
                              resume caller - we're done here.
                            cc/call (@ caller-return) none
                        fn/cc (_)
                            # continue callee
                            cc/call cont-callee none

                return yield-iter none

        fn varargs-iter (f)
            let args... = (f)
            if (> (va-countof args...) (size_t 0))
                let first rest... = args...
                return
                    fn () rest...
                    \ first

        fn va-iter (args...)
            new-iterator varargs-iter
                fn () args...

        fn range (a b c)
            let num-type = (typeof a)
            let step = (? (none? c) (num-type 1) c)
            let from = (? (none? b) (num-type 0) a)
            let to = (? (none? b) a b)
            new-iterator
                fn (x)
                    if (< x to)
                        return (+ x step) x
                \ from

        fn inf-range (a b)
            let num-type =
                ? (none? a)
                    ? (none? b) int (typeof b)
                    \ (typeof a)
            let step = (? (none? b) (num-type 1) b)
            let from = (? (none? a) (num-type 0) a)
            new-iterator
                fn (x)
                    return (+ x step) x
                \ from

        # repeats a sequence n times or indefinitely
        fn repeat (x n)
            let nextf init = ((disqualify Iterator (iter x)))
            let pred =
                ? (none? n)
                    fn (i n) true
                    fn (i n) (i < n)
            new-iterator
                fn repeat-next (f)
                    let state i = (f)
                    if (pred i n)
                        let next-state at... = (nextf state)
                        if (none? next-state) # iterator depleted?
                            # restart
                            return
                                repeat-next
                                    fn ()
                                        return init (i + 1)
                        else
                            return
                                fn ()
                                    return next-state i
                                \ at...
                fn ()
                    return init 0

        # returns the cartesian product of two sequences
        fn product (a b)
            let a-iter a-init = ((disqualify Iterator (iter a)))
            let b-iter b-init = ((disqualify Iterator (iter b)))
            new-iterator
                fn repeat-next (f)
                    let b-state a-next-state a-at... = (f)
                    if (not (none? a-next-state)) # first iterator not depleted?
                        let b-next-state b-at... = (b-iter b-state)
                        if (none? b-next-state) # second iterator depleted?
                            let a-next-state a-at... = (a-iter a-next-state)
                            # restart with next first iterator
                            return
                                repeat-next
                                    fn ()
                                        return b-init a-next-state a-at...
                        else
                            return
                                fn ()
                                    return b-next-state a-next-state a-at...
                                \ a-at... b-at...
                do
                    let a-next-state a-at... = (a-iter a-init)
                    fn ()
                        return b-init a-next-state a-at...

        fn concat (a b)
            let iter-a init-a = ((disqualify Iterator (iter a)))
            let iter-b init-b = ((disqualify Iterator (iter b)))
            new-iterator
                fn (f)
                    let next-iter next-init iter-f iter-state = (f)
                    let next-state at... = (iter-f iter-state)
                    if (none? next-state) # iterator depleted?
                        if (none? next-iter) # no more iterators to walk?
                            return
                        else # start second iterator
                            let next-state at... = (next-iter next-init)
                            return
                                fn ()
                                    return none none next-iter next-state
                                \ at...
                    else # iterator not depleted yet
                        return
                            fn ()
                                return next-iter next-init iter-f next-state
                            \ at...
                fn ()
                    return iter-b init-b iter-a init-a

        fn zip (a b)
            let iter-a init-a = ((disqualify Iterator (iter a)))
            let iter-b init-b = ((disqualify Iterator (iter b)))
            new-iterator
                fn (f)
                    let a b = (f)
                    let next-a at-a... = (iter-a a)
                    let next-b at-b... = (iter-b b)
                    if (not (or (none? next-a) (none? next-b)))
                        return
                            fn ()
                                return next-a next-b
                            \ at-a... at-b...
                fn ()
                    return init-a init-b

        fn zip-fill (a b a-fill b-fill...)
            let b-fill... =
                ? (none? b-fill...) a-fill b-fill...
            new-iterator
                fn (f)
                    let iter-a iter-b a b = (f)
                    let next-a at-a... =
                        if (none? iter-a)
                            _ none a-fill
                        else
                            iter-a a
                    let next-b at-b... =
                        if (none? iter-b)
                            _ none b-fill...
                        else
                            iter-b b
                    let k iter-a at-a... =
                        if (none? next-a)
                            _ 0 none a-fill
                        else
                            _ 1 iter-a at-a...
                    let k iter-b at-b... =
                        if (none? next-b)
                            _ k none b-fill...
                        else
                            _ (k + 1) iter-b at-b...
                    if (k > 0)
                        return
                            fn ()
                                return iter-a iter-b next-a next-b
                            \ at-a... at-b...
                do
                    let iter-a init-a = ((disqualify Iterator (iter a)))
                    let iter-b init-b = ((disqualify Iterator (iter b)))
                    fn ()
                        return iter-a iter-b init-a init-b

        fn enumerate (x from step)
            zip (inf-range from step) (iter x)

        fn =? (x)
            and
                symbol? (syntax->datum x)
                == x (quote =)

        fn parse-loop-args (fullexpr)
            let expr =
                ? (syntax-head? fullexpr (quote with))
                    slice fullexpr 1
                    \ fullexpr
            let expr-eol =
                syntax-eol expr
            loop (expr)
                if (empty? expr)
                    break expr-eol expr-eol
                else
                    let args names =
                        continue (slice expr 1)
                    let elem = (@ expr 0)
                    if (symbol? (syntax->datum elem))
                        break
                            syntax-cons elem args
                            syntax-cons elem names
                    else
                        # initializer
                        let nelem = (syntax->datum elem)
                        assert
                            and
                                list? nelem
                                (countof nelem) >= (size_t 3)
                                =? (@ nelem 1)
                            syntax-error elem "illegal initializer"
                        break
                            syntax-cons (@ elem 2) args
                            syntax-cons (@ elem 0) names

        set-scope-symbol! syntax-scope (quote iter) iter
        set-scope-symbol! syntax-scope (quote va-iter) va-iter
        set-scope-symbol! syntax-scope (quote iterator?) iterator?
        set-scope-symbol! syntax-scope (quote range) range
        set-scope-symbol! syntax-scope (quote repeat) repeat
        set-scope-symbol! syntax-scope (quote product) product
        set-scope-symbol! syntax-scope (quote concat) concat
        set-scope-symbol! syntax-scope (quote zip) zip
        set-scope-symbol! syntax-scope (quote zip-fill) zip-fill
        set-scope-symbol! syntax-scope (quote enumerate) enumerate
        set-scope-symbol! syntax-scope (quote inf-range) inf-range
        set-scope-symbol! syntax-scope (quote zip-fill) zip-fill
        set-scope-symbol! syntax-scope (quote loop)
            # better loop with support for initializers
            macro
                fn loop (expr)
                    let expr-anchor = (syntax->anchor expr)
                    let param-continue =
                        quote-syntax continue
                    let param-break =
                        quote-syntax break
                    let args names =
                        parse-loop-args (@ expr 1)
                    qquote-syntax
                        {do}
                            {fn/cc} (unquote param-continue) (
                                (unquote param-break)
                                (unquote-splice names))
                                unquote-splice
                                    slice expr 2
                            ;
                                unquote param-continue
                                unquote-splice args

        set-scope-symbol! syntax-scope (quote loop-for)
            block-macro
                fn (block-expr)
                    fn iter-expr (expr)
                        assert (not (empty? expr))
                            \ "syntax: (loop-for let-name ... in iter-expr body-expr ...)"
                        if (syntax-head? expr (quote in))
                            return
                                syntax-list
                                slice expr 1
                        else
                            let names rest =
                                iter-expr
                                    slice expr 1
                            return
                                syntax-cons
                                    @ expr 0
                                    \ names
                                \ rest

                    let expr = (@ block-expr 0)
                    let expr-eol =
                        syntax-eol expr
                    let dest-names rest =
                        iter-expr (slice expr 1)
                    let src-expr = (@ rest 0)
                    let block-rest else-block =
                        let remainder =
                            slice block-expr 1
                        if
                            and
                                not (empty? remainder)
                                syntax-head? (@ remainder 0) (quote else)
                            _ (slice remainder 1) (slice (@ remainder 0) 1)
                        else
                            _ remainder
                                syntax-eol expr

                    fn generate-template (body extra-args extra-names)
                        let param-ret =
                            quote-syntax break
                        let param-inner-ret =
                            datum->syntax (Parameter (quote-syntax return))
                        let param-iter =
                            datum->syntax (Parameter (quote-syntax iter))
                        let param-state =
                            datum->syntax (Parameter (quote-syntax state))
                        let param-for =
                            datum->syntax (Symbol "#loop-for") expr
                        let param-at =
                            datum->syntax (Parameter (quote-syntax at...))
                        let param-next =
                            datum->syntax (Parameter (quote-syntax next))
                        cons
                            qquote-syntax
                                {do}
                                    {fn/cc} (unquote param-for) (
                                        (unquote param-ret)
                                        (unquote param-iter)
                                        (unquote param-state)
                                        (unquote-splice extra-names))
                                        {let} (unquote param-next) (unquote param-at) =
                                            (unquote param-iter) (unquote param-state)
                                        {?} ({none?} (unquote param-next))
                                            unquote
                                                syntax-do else-block
                                            {do}
                                                {fn/cc} continue (
                                                    (unquote param-inner-ret)
                                                    (unquote-splice extra-names))
                                                    ;
                                                        unquote param-for
                                                        unquote param-iter
                                                        unquote param-next
                                                        unquote-splice extra-names
                                                {let} (unquote-splice dest-names) =
                                                    unquote param-at
                                                unquote-splice body
                                    {let} iter-val state-val =
                                        (disqualify Iterator (iter (unquote src-expr)))
                                    ;
                                        unquote param-for
                                        \ iter-val state-val
                                        unquote-splice extra-args
                            \ block-rest

                    let body = (slice rest 1)
                    if
                        and
                            not (empty? body)
                            syntax-head? (@ body 0) (quote with)
                        # read extra state params
                        generate-template
                            slice body 1
                            parse-loop-args (@ body 0)
                    else
                        # no extra state params
                        generate-template body expr-eol expr-eol
        \ syntax-scope

    syntax-extend
        print "stage 15"
        fn paramdef-has-types? (expr)
            loop-for token in (syntax->datum expr)
                if (token == (quote ,))
                    break true
                elseif (token == (quote :))
                    break true
                else
                    continue
            else false

        fn tokenize-name-type-tuples (expr)
            fn split (expr)
                return (@ expr 0) (slice expr 1)
            fn (yield)
                loop (expr)
                    if (empty? expr)
                        break
                    let k expr = (split expr)
                    let eq expr = (split expr)
                    if (eq != (quote :))
                        syntax-error eq "type separator expected"
                    let v expr = (split expr)
                    yield k v
                    if (not (empty? expr))
                        let token expr = (split expr)
                        if (token != (quote ,))
                            syntax-error token "comma separator expected"
                        continue expr

        # extended function body macro expander that binds the function itself
          to `recur` and a list of all parameters to `parameters` for further
          macro processing.
        fn make-function-body (env decl paramdef body expr-anchor nextfunc expanded-fn?)
            if (not (list? (syntax->datum paramdef)))
                syntax-error paramdef "parameter list expected"
            let func =
                Label
                    ? (none? decl)
                        datum->syntax
                            Symbol ""
                            \ expr-anchor
                        \ decl
            if (not (none? decl))
                set-scope-symbol! env decl func
            let subenv = (Scope env)
            fn append-param (param-name param-type)
                let param-name-datum = (syntax->datum param-name)
                label-append-parameter! func
                    if ((typeof param-name-datum) == Parameter) param-name-datum
                    else
                        let param = (Parameter param-name param-type)
                        set-scope-symbol! subenv param-name-datum param
                        \ param
            if expanded-fn?
                set-scope-symbol! subenv (quote recur) func
                append-param (quote-syntax return) Any
            if (paramdef-has-types? paramdef)
                loop-for param-name param-type-expr in (tokenize-name-type-tuples paramdef)
                    let param-sym = (syntax->datum param-type-expr)
                    if (not (symbol? param-sym))
                        syntax-error param-type-expr
                            .. "symbol expected, not value of type " (repr (typeof param-sym))
                    let param-type = (@ env param-sym)
                    if ((typeof param-type) != type)
                        syntax-error param-type-expr
                            .. "type expected, not value of type " (repr (typeof param-type))
                    append-param param-name param-type
                    continue
            else
                loop-for param-name in (syntax->datum paramdef)
                    append-param param-name Any
                    continue
            fn (rest)
                cons
                    @
                        expand
                            list
                                syntax-cons
                                    quote-syntax form-fn-body
                                    syntax-cons
                                        datum->syntax func expr-anchor
                                        \ body
                            \ subenv
                        \ 0
                    nextfunc rest

        # chains cross-dependent function declarations.
          (xfn (name (param ...) body ...) ...)
        set-scope-symbol! syntax-scope (quote xfn)
            block-macro
                fn (topexpr env)
                    fn parse-funcdef (topexpr nextfunc)
                        let expr =
                            @ topexpr 0
                        let decl =
                            @ expr 0
                        if (not (symbol? (syntax->datum decl)))
                            syntax-error decl "symbol expected"
                        let paramdef =
                            @ expr 1
                        let rest =
                            slice topexpr 1
                        let expr-anchor =
                            syntax->anchor expr
                        let complete-fn-decl =
                            make-function-body env decl paramdef (slice expr 2)
                                \ expr-anchor nextfunc true
                        if (not (empty? rest))
                            parse-funcdef rest complete-fn-decl
                        else
                            return complete-fn-decl
                    call
                        parse-funcdef (slice (@ topexpr 0) 1)
                            fn (rest) rest
                        slice topexpr 1

        fn make-fn-macro (expanded-fn?)
            block-macro
                fn (topexpr env)
                    let expr =
                        @ topexpr 0
                    let expr-anchor =
                        syntax->anchor expr
                    let decl =
                        @ expr 1
                    let rest =
                        slice topexpr 1
                    fn make-params-body (name body)
                        fn tailf (rest) rest
                        call
                            make-function-body env name (@ body 0) (slice body 1)
                                \ expr-anchor tailf expanded-fn?
                            \ rest

                    if (symbol? (syntax->datum decl))
                        make-params-body decl
                            slice expr 2
                    else
                        # regular, unchained form
                        make-params-body none
                            slice expr 1

        # an extended version of fn
          (fn [name] (param ...) body ...)
        set-scope-symbol! syntax-scope (quote fn)
            make-fn-macro true

        # an extended version of fn
          (fn/cc [name] (param ...) body ...)
        set-scope-symbol! syntax-scope (quote fn/cc)
            make-fn-macro false
        \ syntax-scope

    # (fn-types type ...)
    define fn-types
        fn assert-type (param-name atype value...)
            if (not (none? atype))
                if (callable? atype)
                    assert (atype value...)
                        .. param-name ": value of type " (string (typeof value...)) " failed predicate"
                else
                    let valuetype = (typeof value...)
                    assert (valuetype == atype)
                        .. param-name ": "
                            \ (string atype) " expected, not " (string valuetype)
        macro
            fn (expr env)
                let assert-type-fn =
                    datum->syntax assert-type expr
                syntax-do
                    loop-for i param expected-type-arg in
                        enumerate
                            zip-fill
                                va-iter
                                    label-parameters env.recur
                                syntax->datum expr
                                \ none
                        if (i == 0)
                            continue
                        else
                            if (none? param)
                                syntax-error expr "parameter count mismatch"
                            let param-label =
                                .. "parameter #" (string i) " '"
                                    \ (string (parameter-name param)) "'"
                            syntax-cons
                                qquote-syntax
                                    (unquote assert-type-fn)
                                        unquote
                                            datum->syntax param-label
                                                parameter-anchor param
                                        unquote expected-type-arg
                                        unquote
                                            datum->syntax param
                                                parameter-anchor param
                                        unquote-splice
                                            syntax-eol expr
                                continue
                    else (syntax-eol expr)

    #-------------------------------------------------------------------------------
    # scopeof
    #-------------------------------------------------------------------------------

    syntax-extend
        print "stage 16"
        fn build-scope (names...)
            fn (values...)
                let table = (Scope)
                loop-for key value in (zip (va-iter names...) (va-iter values...))
                    set-scope-symbol! table key value
                    continue
                \ table

        # (scopeof (key = value) ...)
        set-scope-symbol! syntax-scope (quote scopeof)
            macro
                fn expand-scopeof (expr env)
                    qquote-syntax
                        {call}
                            (unquote (datum->syntax build-scope expr))
                                unquote-splice
                                    loop-for entry in (syntax->datum (slice expr 1))
                                        if
                                            not
                                                and
                                                    (countof entry) == (size_t 3)
                                                    (@ entry 1) == (quote =)
                                            syntax-error entry "syntax: (scopeof (key = value) ...)"
                                        let key = (@ entry 0)
                                        let value = (slice entry 2)
                                        syntax-cons
                                            qquote-syntax
                                                {quote}
                                                    unquote key
                                            continue
                                    else
                                        syntax-eol expr
                            unquote-splice
                                loop-for entry in (syntax->datum (slice expr 1))
                                    let value = (slice entry 2)
                                    syntax-cons
                                        syntax-do value
                                        continue
                                else
                                    syntax-eol expr
        \ syntax-scope

    #-------------------------------------------------------------------------------
    # module loading
    #-------------------------------------------------------------------------------

    syntax-extend
        print "stage 17"
        let bangra = (Scope)
        set-scope-symbol! bangra (quote path)
            list "./?.b"
                .. interpreter-dir "/?.b"
        set-scope-symbol! bangra (quote modules) (Scope)
        set-scope-symbol! syntax-scope (quote bangra) bangra

        fn make-module-path (pattern name)
            let sz = (countof pattern)
            let i = (size_t 0)
            loop (i)
                if (i == sz)
                    break ""
                let idx =
                    loop (i)
                        if ((@ pattern i) == "?")
                            break i
                        elseif (i < sz)
                            continue (i + (size_t 1))
                if (none? idx)
                    slice pattern i
                else
                    .. (slice pattern i idx) name
                        continue (idx + (size_t 1))

        fn find-module (name)
            let name = (syntax->datum name)
            assert (symbol? name) "module name must be symbol"
            let content =
                get-scope-symbol
                    get-scope-symbol bangra (quote modules)
                    \ name
            if (none? content)
                let namestr =
                    string name
                let pattern =
                    get-scope-symbol bangra (quote path)
                loop (pattern)
                    if (not (empty? pattern))
                        let module-path =
                            make-module-path
                                @ pattern 0
                                \ namestr
                        let expr =
                            list-load module-path
                        if (not (none? expr))
                            let eval-scope =
                                Scope (globals)
                            set-scope-symbol! eval-scope
                                quote module-path
                                \ module-path
                            let fun =
                                eval expr eval-scope module-path
                            let content =
                                fun
                            set-scope-symbol!
                                get-scope-symbol bangra (quote modules)
                                \ name content
                            \ content
                        else
                            continue
                                slice pattern 1
                    else
                        error
                            .. "module not found: " namestr
            else content
        set-scope-symbol! syntax-scope (quote require) find-module
        \ syntax-scope

    #-------------------------------------------------------------------------------
    # C types
    #-------------------------------------------------------------------------------

    define array
        type (quote array)

    define vector
        type (quote vector)

    define tuple
        type (quote tuple)

    define struct
        type (quote struct)

    define pointer
        type (quote pointer)

    fn arrayof (atype values...)
        call
            array atype
                va-countof values...
            \ values...

    fn vectorof (atype values...)
        call
            vector atype
                va-countof values...
            \ values...

    fn tupleof (values...)
        call
            tuple
                loop-for value in (va-iter values...)
                    break (typeof value) (continue)
                else
                    break
            \ values...

    fn pointerof (value)
        (pointer (typeof value)) value

    fn align (offset align)
        let T = (typeof offset)
        fn-types integer? T
        (offset + align - (T 1)) & (~ (align - (T 1)))

    # pointer implementation
      ----------------------

    set-type-symbol! pointer (quote size) (sizeof Boxed)
    set-type-symbol! pointer (quote alignment) (alignof Boxed)

    set-type-symbol! pointer (quote apply-type)
        fn apply-pointer-type (element)
            fn-types type
            let etype =
                type (Symbol (.. (repr element) "*"))
            if (none? (. etype complete))
                fn addressof (self)
                    @
                        bitcast
                            tuple u64
                            tupleof self
                        \ 0
                fn getvalue (self)
                    ? ((addressof self) == (u64 0)) none
                        unbox element
                            bitcast Boxed self
                let methods =
                    scopeof
                        getvalue = getvalue
                        getaddress = addressof

                set-type-symbol! etype (quote super) pointer
                set-type-symbol! etype (quote apply-type)
                    fn apply-typed-pointer-type (value)
                        bitcast etype (box value)
                set-type-symbol! etype (quote @)
                    fn pointer-at (self name)
                        @ methods name
                set-type-symbol! etype (quote repr)
                    fn pointer-type-repr (self styler)
                        ..
                            styler Style.Operator "&"
                            repr
                                getvalue self

                set-type-symbol! etype (quote complete) true
            \ etype

    # array implementation
      --------------------

    set-type-symbol! array (quote apply-type)
        fn apply-array-type (element count)
            fn-types type size_t
            let etype =
                type (Symbol (.. "[" (repr count) " x " (repr element) "]"))
            if (none? (. etype complete))
                let sz = (sizeof element)
                let alignment = (alignof element)
                let stride = (align sz alignment)
                set-type-symbol! etype (quote super) array
                set-type-symbol! etype (quote size) (stride * count)
                set-type-symbol! etype (quote alignment) alignment
                set-type-symbol! etype (quote apply-type)
                    fn apply-typed-array-type (args...)
                        let self =
                            alloc etype
                        let argcount = (va-countof args...)
                        assert (argcount == count)
                            .. (string (int count)) " elements expected, got "
                                string (int argcount)
                        loop-for i arg in (enumerate (va-iter args...))
                            let offset = (stride * (size_t i))
                            copy-memory! self offset arg (size_t 0) sz
                            continue
                        \ self
                set-type-symbol! etype (quote @)
                    fn array-type-at (self at)
                        fn-types none integer?
                        let offset = (stride * (size_t at))
                        extract-memory element self offset
                set-type-symbol! etype (quote countof)
                    fn array-countof (self) count
                set-type-symbol! etype (quote slice)
                    fn array-type-at (self i0 i1)
                        fn-types none size_t size_t
                        let num = (i1 - i0)
                        let offset = (stride * (size_t i0))
                        let T = (array element num)
                        extract-memory T self offset
                set-type-symbol! etype (quote repr)
                    fn array-type-repr (self styler)
                        loop
                            with
                                i = (size_t 0)
                                s = (styler Style.Operator "[")
                            if (i < count)
                                continue
                                    i + (size_t 1)
                                    .. s
                                        ? (i > (size_t 0)) " " ""
                                        repr (@ self i) styler
                            else
                                .. s (styler Style.Operator "]")
                set-type-symbol! etype (quote iter)
                    fn gen-array-type-iter (self)
                        return
                            fn countable-iter (i)
                                if (i < count)
                                    return
                                        i + (size_t 1)
                                        @ self i
                            size_t 0

                set-type-symbol! etype (quote complete) true
            \ etype

    # vector implementation
      ---------------------

    set-type-symbol! vector (quote apply-type)
        fn apply-vector-type (element count)
            fn-types type size_t
            let etype =
                type (Symbol (.. "<" (repr count) " x " (repr element) ">"))
            if (none? (. etype complete))
                let sz = (sizeof element)
                let alignment = (alignof element)
                let stride = (align sz alignment)
                set-type-symbol! etype (quote super) vector
                set-type-symbol! etype (quote size) (stride * count)
                set-type-symbol! etype (quote alignment) alignment
                set-type-symbol! etype (quote apply-type)
                    fn apply-typed-vector-type (args...)
                        let self =
                            alloc etype
                        let argcount = (va-countof args...)
                        assert (argcount == count)
                            .. (string (int count)) " elements expected, got "
                                string (int argcount)
                        loop-for i arg in (enumerate (va-iter args...))
                            let offset = (stride * (size_t i))
                            copy-memory! self offset arg (size_t 0) sz
                            continue
                        \ self
                set-type-symbol! etype (quote @)
                    fn vector-type-at (self at)
                        fn-types none integer?
                        let offset = (stride * (size_t at))
                        extract-memory element self offset
                set-type-symbol! etype (quote countof)
                    fn vector-countof (self) count
                set-type-symbol! etype (quote slice)
                    fn vector-type-at (self i0 i1)
                        fn-types none size_t size_t
                        let num = (i1 - i0)
                        let offset = (stride * (size_t i0))
                        let T = (vector element num)
                        extract-memory T self offset
                set-type-symbol! etype (quote repr)
                    fn vector-type-repr (self styler)
                        loop
                            with
                                i = (size_t 0)
                                s = (styler Style.Operator "<")
                            if (i < count)
                                continue
                                    i + (size_t 1)
                                    .. s
                                        ? (i > (size_t 0)) " " ""
                                        repr (@ self i) styler
                            else
                                .. s (styler Style.Operator ">")
                set-type-symbol! etype (quote iter)
                    fn gen-vector-type-iter (self)
                        return
                            fn countable-iter (i)
                                if (i < count)
                                    return
                                        i + (size_t 1)
                                        @ self i
                            size_t 0

                set-type-symbol! etype (quote complete) true
            \ etype

    # tuple implementation
      --------------------

    set-type-symbol! tuple (quote apply-type)
        fn apply-tuple-type (element-types...)
            let count = (va-countof element-types...)
            let etypes = (va-iter element-types...)
            let etype =
                loop-for i elemtype in (enumerate etypes)
                    with (s = "{")
                    assert ((typeof elemtype) == type) "type expected"
                    continue
                        .. s
                            ? (i == 0) "" " "
                            repr elemtype
                else
                    type
                        Symbol
                            .. s "}"
            if (none? (. etype complete))
                let sz alignment offsets... =
                    loop-for elemtype in etypes
                        with
                            offset = (size_t 0)
                            max-alignment = (size_t 1)
                        let sz = (sizeof elemtype)
                        let alignment = (alignof elemtype)
                        let aligned-offset = (align offset alignment)
                        let total-size sum-alignment offsets... =
                            continue
                                aligned-offset + sz
                                max max-alignment alignment
                        break total-size sum-alignment aligned-offset offsets...
                    else
                        break offset max-alignment

                set-type-symbol! etype (quote super) tuple
                set-type-symbol! etype (quote size) sz
                set-type-symbol! etype (quote alignment) alignment

                set-type-symbol! etype (quote apply-type)
                    fn apply-typed-tuple-type (args...)
                        let self =
                            alloc etype
                        let argcount = (va-countof args...)
                        assert (argcount == count)
                            .. (string (int count)) " elements expected, got "
                                string (int argcount)
                        loop-for i arg in (enumerate (va-iter args...))
                            let offset = (va@ i offsets...)
                            let elemtype = (va@ i element-types...)
                            assert ((typeof arg) == elemtype)
                                .. "type " (string elemtype) " expected, got "
                                    string (typeof arg)
                            copy-memory! self offset arg (size_t 0) (sizeof elemtype)
                            continue
                        \ self
                    set-type-symbol! etype (quote @)
                        fn tuple-at (self at)
                            fn-types none integer?
                            let offset = (va@ at offsets...)
                            let elemtype = (va@ at element-types...)
                            extract-memory elemtype self offset
                    set-type-symbol! etype (quote countof)
                        fn tuple-countof (self) count
                    set-type-symbol! etype (quote slice)
                        fn vector-type-at (self i0 i1)
                            fn-types none size_t size_t
                            let num = (i1 - i0)
                            let offset = (va@ i0 offsets...)
                            let sliced-types... =
                                loop-for i elemtype in (enumerate etypes (size_t 0))
                                    if (i >= i1)
                                        break
                                    elseif (i >= i0)
                                        break elemtype (continue)
                                    else
                                        continue
                                else
                                    break
                            let T =
                                tuple sliced-types...
                            extract-memory T self offset
                    set-type-symbol! etype (quote repr)
                        fn vector-type-repr (self styler)
                            loop
                                with
                                    i = (size_t 0)
                                    s = (styler Style.Operator "{")
                                if (i < count)
                                    continue
                                        i + (size_t 1)
                                        .. s
                                            ? (i > (size_t 0)) " " ""
                                            repr (@ self i) styler
                                else
                                    .. s (styler Style.Operator "}")
                    set-type-symbol! etype (quote iter)
                        fn gen-vector-type-iter (self)
                            return
                                fn countable-iter (i)
                                    if (i < count)
                                        return
                                            i + (size_t 1)
                                            @ self i
                                size_t 0

                    set-type-symbol! etype (quote complete) true
            \ etype

    # struct implementation
      ---------------------

    fn field (field-name field-type)
        fn-types symbol? type?
        fn ()
            return field-name field-type

    set-type-symbol! struct (quote apply-type)
        fn apply-struct-type (fields...)
            let count = (va-countof fields...)
            let fields = (va-iter fields...)
            let etype =
                loop-for i f in (enumerate fields)
                    with (s = "{")
                    assert (label? f) "field expected"
                    let name elemtype = (f)
                    assert (symbol? name) "name expected"
                    assert (type? elemtype) "type expected"
                    continue
                        .. s
                            ? (i == 0) "" " "
                            \ (repr name) "=" (repr elemtype)
                else
                    type
                        Symbol
                            .. s "}"
            if (none? (. etype complete))
                let etypes... =
                    loop-for f in fields
                        let name elemtype = (f)
                        break elemtype (continue)
                let names... =
                    loop-for f in fields
                        let name elemtype = (f)
                        break name (continue)
                let ttype = (tuple etypes...)
                set-type-symbol! etype (quote super) struct
                set-type-symbol! etype (quote size) ttype.size
                set-type-symbol! etype (quote alignment) ttype.alignment
                set-type-symbol! etype (quote apply-type)
                    fn struct-apply-type (values...)
                        let stype = (ttype.apply-type values...)
                        bitcast etype stype
                set-type-symbol! etype (quote @)
                    fn struct-at (self at)
                        if (symbol? at)
                            loop-for i name in (enumerate (va-iter names...))
                                if (name == at)
                                    break
                                        ttype.@ self i
                                else
                                    continue
                            else
                                error
                                    .. "no field named " (repr at) " in struct"
                        else
                            ttype.@ self at
                set-type-symbol! etype (quote countof) ttype.countof
                set-type-symbol! etype (quote repr)
                    fn struct-repr (self styler)
                        loop-for i name in (enumerate (va-iter names...))
                            with
                                s = (styler Style.Operator "{")
                            continue
                                .. s
                                    ? (i > 0) " " ""
                                    repr name
                                    styler Style.Operator "="
                                    repr (@ self i) styler
                        else
                            .. s (styler Style.Operator "}")

            \ etype

    syntax-extend
        fn build-struct (names...)
            fn (values...)
                call
                    struct
                        loop-for key value in (zip (va-iter names...) (va-iter values...))
                            let value-type = (typeof value)
                            break
                                field key value-type
                                continue
                    \ values...

        # (structof (key = value) ...)
        set-scope-symbol! syntax-scope (quote structof)
            macro
                fn expand-structof (expr env)
                    qquote-syntax
                        {call}
                            (unquote (datum->syntax build-struct expr))
                                unquote-splice
                                    loop-for entry in (syntax->datum (slice expr 1))
                                        if
                                            not
                                                and
                                                    (countof entry) == (size_t 3)
                                                    (@ entry 1) == (quote =)
                                            syntax-error entry "syntax: (structof (key = value) ...)"
                                        let key = (@ entry 0)
                                        let value = (slice entry 2)
                                        syntax-cons
                                            qquote-syntax
                                                {quote}
                                                    unquote key
                                            continue
                                    else
                                        syntax-eol expr
                            unquote-splice
                                loop-for entry in (syntax->datum (slice expr 1))
                                    let value = (slice entry 2)
                                    syntax-cons
                                        syntax-do value
                                        continue
                                else
                                    syntax-eol expr


        \ syntax-scope

    #-------------------------------------------------------------------------------
    # REPL
    #-------------------------------------------------------------------------------

    define read-eval-print-loop
        fn repeat-string (n c)
            loop-for i in (range n)
                with (s = "")
                continue (s .. c)
            else s
        fn get-leading-spaces (s)
            loop-for c in s
                with (out = "")
                if (c == " ")
                    continue (out .. c)
                else out
            else out

        fn has-chars (s)
            loop-for i in s
                if (i != " ") true
                else (continue)
            else false

        fn ()
            let vmin vmaj vpatch = (interpreter-version)
            print "Bangra"
                .. (string vmin) "." (string vmaj)
                    ? (vpatch == 0) ""
                        .. "." (string vpatch)
                    \ " ("
                    ? debug-build? "debug build, " ""
                    \ interpreter-timestamp ")"
            let state = (Scope)
            fn reset-state ()
                let repl-env = (Scope (globals))
                set-scope-symbol! repl-env (quote reset) reset-state
                set-scope-symbol! state (quote env) repl-env
                set-scope-symbol! state (quote counter) 1
            reset-state;
            fn capture-scope (env)
                set-scope-symbol! state (quote env) env
            # appending this to an expression before evaluation captures the env
              table so it can be used for the next expression.
            let expression-suffix =
                qquote-syntax
                    {syntax-extend}
                        ;
                            unquote
                                datum->syntax capture-scope
                                    active-anchor
                            \ syntax-scope
                        \ syntax-scope
            fn make-idstr ()
                .. "$"
                    string (get-scope-symbol state (quote counter))
            loop
                with
                    preload = ""
                    cmdlist = ""
                let idstr = (make-idstr)
                let id = (Symbol idstr)
                let styler = default-styler
                let promptstr =
                    .. idstr " "
                        styler Style.Comment "▶"
                let promptlen = ((countof idstr) + (size_t 2))
                let cmd =
                    prompt
                        ..
                            ? (empty? cmdlist) promptstr
                                repeat-string promptlen "."
                            \ " "
                        \ preload
                if (none? cmd)
                    break
                let terminated? =
                    or (not (has-chars cmd))
                        (empty? cmdlist) and ((slice cmd 0 1) == "\\")
                let cmdlist = (.. cmdlist cmd "\n")
                let preload =
                    if terminated? ""
                    else (get-leading-spaces cmd)
                let slevel = (ref (stack-level))
                let cmdlist =
                    if terminated?
                        try
                            let expr = (list-parse cmdlist)
                            if (none? expr)
                                error "parsing failed"
                            let eval-env = (get-scope-symbol state (quote env))
                            let code =
                                .. expr
                                    syntax-list expression-suffix
                            ref-set! slevel (stack-level)
                            let f =
                                eval code eval-env
                            ref-set! slevel (stack-level)
                            let result... = (f)
                            if (not (none? result...))
                                loop-for i in (range (va-countof result...))
                                    let idstr = (make-idstr)
                                    let value = (va@ i result...)
                                    print
                                        .. idstr "= "
                                            repr value default-styler
                                            default-styler Style.Operator ":"
                                            repr (typeof value) default-styler
                                    set-scope-symbol! eval-env id value
                                    set-scope-symbol! state (quote counter)
                                        (get-scope-symbol state (quote counter)) + 1
                                    continue
                        except (e)
                            print
                                traceback (@ slevel) 2
                            print "error:" e
                        \ ""
                    else cmdlist
                continue preload cmdlist

    syntax-extend
        print "stage 18"
        set-globals! syntax-scope
        \ syntax-scope

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

    fn print-version (total)
        let vmin vmaj vpatch = (interpreter-version)
        print "Bangra"
            .. (string vmin) "." (string vmaj)
                ? (vpatch == 0) ""
                    .. "." (string vpatch)
                \ " ("
                ? debug-build? "debug build, " ""
                \ interpreter-timestamp ")"
        print "Executable path:" interpreter-path
        exit 0

    fn run-main (args...)
        #set-debug-trace! true
        # running in interpreter mode
        let sourcepath = (ref none)
        let parse-options = (ref true)
        loop
            with
                i = 1
            let arg = (va@ i args...)
            if (not (none? arg))
                if ((@ parse-options) and ((@ arg 0) == "-"))
                    if (arg == "--help" or arg == "-h")
                        print-help (va@ 0 args...)
                    elseif (arg == "--version" or arg == "-v")
                        print-version
                    elseif (arg == "--")
                        ref-set! parse-options false
                    else
                        print
                            .. "unrecognized option: " arg
                                \ ". Try --help for help."
                        exit 1
                elseif (none? (@ sourcepath))
                    ref-set! sourcepath arg
                else
                    print
                        .. "unrecognized argument: " arg
                            \ ". Try --help for help."
                    exit 1
                continue (i + 1)

        let sourcepath = (@ sourcepath)
        if (none? sourcepath)
            read-eval-print-loop
        else
            let expr =
                list-load sourcepath
            let eval-scope =
                Scope (globals)
            set-scope-symbol! eval-scope (quote module-path) sourcepath
            clear-traceback
            let fun =
                eval expr eval-scope sourcepath
            clear-traceback
            fun
            exit 0

    print "run-main"
    run-main (args)
