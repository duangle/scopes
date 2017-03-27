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

    boot script
    the bangra executable looks for a boot file at path/to/executable.b and,
    if found, executes it.

syntax-extend def-quote-set (return env)
    set-scope-symbol! env
        symbol "quote"
        block-scope-macro
            fn/cc quote (return expr env)
                call
                    fn/cc (_ args)
                        return
                            cons
                                # stop compiler expansion
                                escape
                                    # stop macro expansion
                                    escape
                                        # keep wrapped in list if multiple
                                            arguments
                                        branch
                                            == (slice args 1) (list)
                                            fn/cc (_)
                                                _ (@ args 0)
                                            fn/cc (_)
                                                _ args
                                slice expr 1
                            \ env
                    slice (@ expr 0) 1
    set-scope-symbol! env
        symbol "set!"
        block-scope-macro
            fn/cc set! (return expr env)
                call
                    fn/cc (_ name)
                        call
                            fn/cc (_ param)
                                branch
                                    == (typeof param) parameter
                                    fn/cc (_) (_ none)
                                    fn/cc (_)
                                        error "set! requires parameter argument"
                                return
                                    cons
                                        syntax-cons
                                            datum->syntax bind! (active-anchor)
                                            syntax-cons
                                                # stop interpreter from expanding parameter
                                                escape
                                                    # stop compiler expansion
                                                    escape
                                                        # stop macro expansion
                                                        escape param
                                                slice (@ expr 0) 2
                                        slice expr 1
                                    \ env
                            branch
                                ==? (typeof (syntax->datum name)) parameter
                                fn/cc (_)
                                    _ (syntax->datum name)
                                fn/cc (_)
                                    _ (get-scope-symbol env name name)
                    @ (@ expr 0) 1
    return env

syntax-extend def-? (return env)
    set-scope-symbol! env
        quote symbol?
        fn/cc symbol? (return x)
            return
                ==? (typeof x) symbol
    set-scope-symbol! env
        quote list?
        fn/cc list? (return x)
            return
                ==? (typeof x) list
    set-scope-symbol! env
        quote none?
        fn/cc none? (return x)
            return
                ==? x none
    set-scope-symbol! env
        quote empty?
        fn/cc empty? (return x)
            return
                == (countof x) (u64 0)
    set-scope-symbol! env
        quote load
        fn/cc load (return path)
            return
                eval
                    list-load path
                    globals
                    \ path
    set-scope-symbol! env
        quote macro
        fn/cc macro (return f)
            return
                block-scope-macro
                    fn/cc (return expr env)
                        return
                            cons
                                f (@ expr 0) env
                                slice expr 1
                            \ env
    set-scope-symbol! env
        quote block-macro
        fn/cc block-macro (return f)
            return
                block-scope-macro
                    fn/cc (return expr env)
                        return
                            f expr env
                            \ env
    set-scope-symbol! env
        quote dump-syntax
        block-scope-macro
            fn/cc dump-syntax (return expr env)
                call
                    fn/cc (_ e env)
                        dump
                            @ e 0
                        return
                            cons
                                escape
                                    @ e 0
                                slice expr 1
                            \ env
                    expand
                        slice (@ expr 0) 1
                        \ env
    set-scope-symbol! env
        quote ?
        block-scope-macro
            fn/cc ? (return expr env)
                return
                    call
                        fn/cc (_ ret-true ret-false)
                            _
                                cons
                                    syntax-list
                                        datum->syntax branch
                                            syntax->anchor (@ expr 0)
                                        @ (@ expr 0) 1
                                        syntax-list
                                            datum->syntax fn/cc (active-anchor)
                                            syntax-list ret-true
                                            syntax-list ret-true
                                                @ (@ expr 0) 2
                                        syntax-list
                                            datum->syntax fn/cc (active-anchor)
                                            syntax-list ret-false
                                            syntax-list ret-false
                                                @ (@ expr 0) 3
                                    slice expr 1
                        datum->syntax
                            parameter (quote ret-true)
                        datum->syntax
                            parameter (quote ret-false)
                    \ env
    #set-scope-symbol! env
        quote :
        block-scope-macro
            fn/cc : (return expr env)
                return
                    cons
                        cons tupleof
                            cons
                                branch
                                    ==
                                        typeof
                                            @ (@ expr 0) 1
                                        \ symbol
                                    fn/cc ()
                                        list quote
                                            @ (@ expr 0) 1
                                    fn/cc ()
                                        @ (@ expr 0) 1
                                branch
                                    == (slice (@ expr 0) 2) (list)
                                    fn/cc ()
                                        list
                                            @ (@ expr 0) 1
                                    fn/cc ()
                                        slice (@ expr 0) 2
                        slice expr 1
                    \ env
    return env

syntax-extend def-list-defs (return env)
    set-scope-symbol! env (quote syntax-head?)
        fn/cc syntax-head? (return expr name)
            return
                ? (list? (syntax->datum expr))
                    ? (empty? expr)
                        \ false
                        call
                            fn/cc (_ head)
                                ? (symbol? (syntax->datum head))
                                    _ (==? head name)
                                    _ false
                            @ expr 0
                    \ false
    set-scope-symbol! env (quote list-atom?)
        fn/cc list-atom? (return x)
            return
                ? (list? (syntax->datum x))
                    empty? x
                    \ true
    # unwrap single item from list or prepend 'do' clause to list
    set-scope-symbol! env (quote syntax-do)
        fn/cc syntax-do (return expr)
            return
                ? (== (countof expr) (u64 1))
                    @ expr 0
                    syntax-cons (datum->syntax do (syntax->anchor expr)) expr
    # build a list terminator tagged with a desired anchor from an expression
    set-scope-symbol! env (quote syntax-eol)
        fn/cc syntax-eol (return expr)
            return
                datum->syntax (list) (syntax->anchor expr)
    return env

syntax-extend def-qquote (return env)
    # quasiquote support
      (qquote expr [...])
    set-scope-symbol! env (quote qquote)
        call
            fn/cc (return qquote-1)
                set! qquote-1
                    fn/cc (return x)
                        return
                            ? (list-atom? x)
                                syntax-list
                                    datum->syntax quote
                                        syntax->anchor x
                                    \ x
                                ? (syntax-head? x (quote unquote))
                                    syntax-do (slice x 1)
                                    ? (syntax-head? x (quote qquote))
                                        qquote-1 (qquote-1 (@ x 1))
                                        ? (list-atom? (@ x 0))
                                            syntax-list
                                                datum->syntax syntax-cons
                                                    syntax->anchor (@ x 0)
                                                qquote-1 (@ x 0)
                                                qquote-1 (slice x 1)
                                            ? (syntax-head? (@ x 0) (quote unquote-splice))
                                                syntax-list
                                                    datum->syntax (do ..)
                                                        syntax->anchor (@ x 0)
                                                    syntax-do (slice (@ x 0) 1)
                                                    qquote-1 (slice x 1)
                                                syntax-list
                                                    datum->syntax syntax-cons
                                                        syntax->anchor (@ x 0)
                                                    qquote-1 (@ x 0)
                                                    qquote-1 (slice x 1)
                return
                    macro
                        fn/cc qquote (return expr)
                            return
                                ? (empty? (slice expr 2))
                                    qquote-1 (@ expr 1)
                                    qquote-1 (slice expr 1)
    return env

syntax-extend def-define (return env)
    set-scope-symbol! env (quote define)
        block-macro
            fn/cc expand-define (return topexpr env)
                return
                    cons
                        qquote
                            syntax-extend define (return local-scope)
                                set-scope-symbol! local-scope
                                    quote
                                        unquote (@ (@ topexpr 0) 1)
                                    unquote
                                        syntax-do (slice (@ topexpr 0) 2)
                                return local-scope
                        slice topexpr 1
    return env

# a lofi version of let so we get some sugar early
define let
    block-scope-macro
        fn/cc let (return expr env)
            branch
                == (typeof (@ (@ expr 0) 2)) syntax-symbol
                fn/cc (_) (_)
                fn/cc (_)
                    error "syntax: let <var> = <expr>"
            branch
                == (@ (@ expr 0) 2) (quote =)
                fn/cc (_) (_)
                fn/cc (_)
                    error "syntax: let <var> = <expr>"
            return
                call
                    fn/cc (_ cont-param param-name rest)
                        _
                            list
                                qquote
                                    ;
                                        fn/cc
                                            ;
                                                unquote cont-param
                                                unquote param-name
                                            ;
                                                unquote cont-param
                                                unquote
                                                    syntax-do
                                                        datum->syntax rest
                                                            syntax->anchor (@ expr 0)
                                                unquote-splice
                                                    syntax-eol (@ expr 0)
                                        unquote
                                            syntax-do
                                                slice (@ expr 0) 3
                                        # force enclosing list to use anchor
                                          of original expression
                                        unquote-splice
                                            syntax-eol (@ expr 0)
                    datum->syntax
                        parameter (quote _)
                    @ (@ expr 0) 1
                    slice expr 1
                \ env

# a standard function declaration that supports recursion
  (fn [name] (param ...) body ...)
define fn
    block-macro
        fn/cc fn (return topexpr)
            let expr =
                @ topexpr 0
            let decl =
                @ (@ topexpr 0) 1
            let decl-anchor =
                syntax->anchor decl
            let retparam =
                datum->syntax
                    syntax->datum
                        quote return
                    \ decl-anchor
            let wrap =
                fn/cc (return val)
                    return (datum->syntax val decl-anchor)
            let make-params-body =
                fn/cc (return param-idx)
                    return
                        syntax-cons
                            syntax-cons
                                \ retparam
                                @ expr param-idx
                            syntax-list
                                syntax-list retparam
                                    syntax-cons (wrap do)
                                        slice expr (+ param-idx 1)
            let rest =
                slice topexpr 1
            return
                ? (symbol? (syntax->datum decl))
                    cons
                        syntax-list
                            wrap let
                            \ decl (quote =)
                            wrap none
                        cons
                            syntax-list
                                wrap set!
                                \ decl
                                syntax-cons
                                    wrap fn/cc
                                    syntax-cons
                                        @ expr 1
                                        make-params-body 2
                            ? (empty? rest)
                                list decl
                                \ rest
                    cons
                        syntax-cons
                            wrap fn/cc
                            make-params-body 1
                        \ rest

define raise
    fn/cc raise (_ x)
        error x

define try
    block-macro
        fn expand-try (expr env)
            ? (not (syntax-head? (@ expr 1) (quote except)))
                error "except block missing"
                \ true
            cons
                qquote
                    xpcall
                        fn ()
                            unquote-splice
                                slice (@ expr 0) 1
                        fn
                            unquote (@ (@ expr 1) 1)
                            unquote-splice
                                slice (@ expr 1) 2
                slice expr 2

define empty-list
    escape (list)
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
            qquote
                ? (unquote (@ expr 1))
                    \ true
                    error
                        unquote
                            ? (empty? (slice expr 2))
                                datum->syntax
                                    .. "assertion failed: "
                                        string
                                            syntax->datum
                                                @ expr 1
                                    syntax->anchor expr
                                @ expr 2
                        unquote-splice
                            syntax-eol expr
                    unquote-splice
                        syntax-eol expr
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
            let key =
                syntax->datum (@ expr 2)
            ? (symbol? key)
                qquote
                    @ (unquote (@ expr 1)) (unquote (escape (escape key)))
                        unquote-splice
                            syntax-eol (@ expr 2)
                error "symbol expected"

define and
    macro
        fn and (expr)
            let tmp = (datum->syntax (parameter (quote tmp)))
            let ret = (datum->syntax (parameter (quote and-return)))
            let ret-true = (datum->syntax (parameter (quote ret-true)))
            let ret-false = (datum->syntax (parameter (quote ret-false)))
            qquote
                ;
                    fn/cc ((unquote ret) (unquote tmp))
                        branch (unquote tmp)
                            fn/cc ((unquote ret-true))
                                ;
                                    unquote ret
                                    unquote (@ expr 2)
                            fn/cc ((unquote ret-false))
                                ;
                                    unquote ret
                                    unquote tmp
                    unquote (@ expr 1)
                    unquote-splice
                        syntax-eol expr

define or
    macro
        fn or (expr)
            let tmp = (datum->syntax (parameter (quote tmp)))
            let ret = (datum->syntax (parameter (quote or-return)))
            let ret-true = (datum->syntax (parameter (quote ret-true)))
            let ret-false = (datum->syntax (parameter (quote ret-false)))
            qquote
                ;
                    fn/cc ((unquote ret) (unquote tmp))
                        branch (unquote tmp)
                            fn/cc ((unquote ret-true))
                                ;
                                    unquote ret
                                    unquote tmp
                            fn/cc ((unquote ret-false))
                                ;
                                    unquote ret
                                    unquote (@ expr 2)
                    unquote (@ expr 1)
                    unquote-splice
                        syntax-eol expr

define if
    fn if-rec (topexpr)
        let expr =
            @ topexpr 0
        let cond =
            @ expr 1
        let then-exprlist =
            slice expr 2
        fn make-branch (else-exprlist)
            let ret-then = (datum->syntax (parameter (quote ret-then)))
            let ret-else = (datum->syntax (parameter (quote ret-else)))
            qquote
                branch (unquote cond)
                    fn/cc ((unquote ret-then))
                        ;
                            unquote ret-then
                            unquote
                                syntax-do then-exprlist
                            unquote-splice
                                syntax-eol then-exprlist
                    fn/cc ((unquote ret-else))
                        ;
                            unquote ret-else
                            unquote
                                syntax-do else-exprlist
                            unquote-splice
                                syntax-eol else-exprlist
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
                    if-rec rest-expr
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
    block-macro if-rec

syntax-extend def-let-xlet (return env)
    fn =? (x)
        and
            symbol? (syntax->datum x)
            ==? x (quote =)

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

    set-scope-symbol! env
        quote let
        # support for multiple declarations in one let env
        block-macro
            fn expand-let (topexpr env)
                let expr = (@ topexpr 0)
                let rest = (slice topexpr 1)
                let body = (slice expr 1)
                let argtype = (typeof (syntax->datum (@ body 0)))
                if (==? argtype list)
                    # prepare quotable values from declarations
                    fn handle-pairs (pairs)
                        if (empty? pairs)
                            \ rest
                        else
                            let pair =
                                @ pairs 0
                            assert (=? (@ pair 1))
                                "syntax: let (<name> = <expression>) ..."
                            cons
                                qquote let
                                    unquote-splice pair
                                handle-pairs
                                    slice pairs 1
                    handle-pairs body
                else
                    call
                        fn (args init)
                            let cont-param =
                                datum->syntax (parameter (quote let-return))
                            list
                                qquote
                                    ;
                                        fn/cc
                                            ;
                                                unquote cont-param
                                                unquote-splice
                                                    datum->syntax args
                                                        syntax->anchor body
                                            ;
                                                unquote cont-param
                                                unquote
                                                    syntax-do
                                                        datum->syntax rest
                                                            syntax->anchor expr
                                                unquote-splice
                                                    syntax-eol expr
                                            unquote-splice
                                                syntax-eol expr
                                        unquote-splice init
                                        # force enclosing list to use anchor
                                          of original expression
                                        unquote-splice
                                            syntax-eol expr
                        find= body

    set-scope-symbol! env
        quote xlet
        block-macro
            # a late-binding let with support for recursion and
              function-level cross dependencies. comparable to letrec.
            fn (topexpr env)
                let expr = (@ topexpr 0)
                let rest = (slice topexpr 1)
                let body = (slice expr 1)
                let argtype = (typeof (syntax->datum (@ body 0)))
                let xlet-return =
                    datum->syntax (parameter (quote xlet-return))
                if (==? argtype list)
                    # multiple variables with support for recursion
                      and circular dependencies

                    # prepare quotable values from declarations
                    fn handle-pairs (pairs)
                        if (empty? pairs)
                            return
                                syntax-eol expr
                                \ rest
                        else
                            call
                                fn (args rest)
                                    let pair =
                                        @ pairs 0
                                    assert
                                        and (=? (@ pair 1))
                                            == (countof pair) (u64 3)
                                        "syntax: xlet (<name> = <expression>) ..."
                                    let name = (@ pair 0)
                                    return
                                        syntax-cons name args
                                        cons
                                            qquote
                                                set!
                                                    unquote name
                                                    unquote
                                                        @ pair 2
                                            \ rest
                                handle-pairs
                                    slice pairs 1

                    call
                        fn (argnames xlet-rest)
                            list
                                qquote
                                    call
                                        fn/cc
                                            ;
                                                unquote xlet-return
                                                unquote-splice argnames
                                            ;
                                                unquote xlet-return
                                                unquote
                                                    syntax-do
                                                        datum->syntax xlet-rest
                                                            syntax->anchor expr
                                        unquote-splice
                                            syntax-eol expr
                        handle-pairs body
                else
                    assert (=? (@ expr 2))
                        "syntax: xlet <parameter> = <expression>"
                    let name = (@ expr 1)
                    let value = (@ expr 3)
                    # regular form with support for recursion
                    list
                        qquote
                            call
                                fn/cc
                                    ;
                                        unquote xlet-return
                                        unquote name
                                    ;
                                        unquote xlet-return
                                        unquote
                                            syntax-do
                                                datum->syntax
                                                    cons
                                                        qquote
                                                            set! (unquote name)
                                                                unquote value
                                                        \ rest
                                                    syntax->anchor expr
                                unquote-splice
                                    syntax-eol expr
    return env

syntax-extend stage-test1b (return env)
    #do
        xlet
            even? =
                fn (n)
                    ? (== n 0) true
                        odd? (- n one)
            odd? =
                fn (n)
                    ? (== n 0) false
                        even? (- n one)
            one = 1
        assert (even? 12)
        assert (odd? 11)

    #error "fuck"

    #do
        print
            try
                print "doing some stuff!"
                #raise "bang"
                print "no longer doing stuff"
                \ 606
            except (err)
                print "error raised:" err
                \ 303
        print "et voila"

    return env

define loop
    macro
        fn loop (expr)
            let expr-anchor = (syntax->anchor expr)
            let param-repeat =
                quote repeat
            let param-break =
                quote break
            qquote
                do
                    xlet (unquote param-repeat) =
                        fn/cc ((unquote param-break) (unquote-splice (@ expr 1)))
                            ;
                                unquote param-break
                                unquote
                                    syntax-do (slice expr 2)
                    ;
                        unquote param-repeat
                        unquote-splice (@ expr 1)

define syntax-infix-rules
    fn syntax-infix-rules (prec order name)
        fn spec (f)
            f prec order name

#define-infix-op or 100 > or
define define-infix-op
    macro
        fn expand-define-infix-op (expr)
            let name = (@ expr 1)
            let prec = (@ expr 2)
            let order = (@ expr 3)
            let dest-name = (@ expr 4)
            let infix-symbol =
                datum->syntax
                    symbol
                        .. "#ifx:" (string name)
                    syntax->anchor expr
            qquote
                syntax-extend define-infix-op (return local-scope)
                    set-scope-symbol! local-scope
                        quote
                            unquote infix-symbol
                        syntax-infix-rules
                            unquote prec
                            unquote order
                            quote
                                unquote dest-name
                    return local-scope

syntax-extend stage-3 (return env)

    fn fold (f init next it)
        let val it =
            next it
        let out = init
        loop (out val it)
            if (none? it)
                return out
            else
                repeat
                    f out val
                    next it

    fn iter (s)
        let ls =
            countof s
        return
            fn (i)
                if (< i ls)
                    return (@ s i) (+ i (u64 1))
                else none
            u64 0

    fn iter-r (s)
        return
            fn (i)
                if (> i (u64 0))
                    let k = (- i (u64 1))
                    return (@ s k) k
                else none
            countof s

    fn get-ifx-op (env op)
        let sym =
            syntax->datum op
        let key =
        ? (symbol? sym)
            get-scope-symbol env
                symbol
                    .. "#ifx:" (string sym)
            \ none

    fn has-infix-ops (infix-table expr)
        # any expression whose second argument matches an infix operator
          is treated as an infix expression.
        and
            not (empty? (slice expr 2))
            !=? (get-ifx-op infix-table (@ expr 1)) none

    fn infix-op (infix-table token prec pred)
        let op =
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        else
            op
                fn (op-prec)
                    ? (pred op-prec prec) op none

    fn rtl-infix-op (infix-table token prec pred)
        let op =
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        else
            op
                fn (op-prec op-order)
                    ? (and (== op-order <) (pred op-prec prec)) op none

    fn parse-infix-expr (infix-table lhs state mprec)
        loop (lhs state)
            if (empty? state)
                break lhs state
            let la = (@ state 0)
            let op = (infix-op infix-table la mprec >=)
            if (none? op)
                break lhs state
            let op-prec op-order op-name = (op _)
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
                            lop
                    if (none? nextop)
                        break rhs state
                    let nextop-prec = (nextop _)
                    repeat
                        parse-infix-expr infix-table rhs state
                            \ nextop-prec
            repeat
                datum->syntax
                    list op-name lhs next-rhs
                    syntax->anchor la
                \ next-state

    let bangra =
        scope
    set-scope-symbol! bangra (quote path)
        list "./?.b"
            .. interpreter-dir "/?.b"
    set-scope-symbol! bangra (quote modules)
        scope
    fn make-module-path (pattern name)
        fold
            fn (out val)
                .. out
                    ? (== val "?") name val
            \ ""
            iter pattern

    fn find-module (name)
        assert (symbol? name) "module name must be symbol"
        let content =
            @ (. bangra modules) name
        if (none? content)
            let namestr =
                string name
            let pattern =
                @ bangra
                    quote path
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
                            scope (globals)
                        set-scope-symbol! eval-scope
                            quote module-path
                            \ module-path
                        let fun =
                            eval expr eval-scope module-path
                        let content =
                            fun
                        set-scope-symbol! (. bangra modules) name content
                        \ content
                    else
                        repeat
                            slice pattern 1
                else
                    error
                        .. "module not found: " namestr
        else content

    fn make-expand-multi-op-ltr (op)
        # (op a b c ...) -> (op (op (op a b) c) ...)
        macro
            fn (expr)
                let wrapped-op =
                    datum->syntax op
                        syntax->anchor expr
                let tail =
                    slice expr 1
                loop (tail)
                    let rest =
                        slice tail 2
                    if (empty? rest)
                        syntax-cons wrapped-op tail
                    else
                        repeat
                            syntax-cons
                                syntax-list wrapped-op
                                    @ tail 0
                                    @ tail 1
                                \ rest

    set-scope-symbol! env (quote bangra) bangra
    set-scope-symbol! env (quote require) find-module
    set-scope-symbol! env (quote iterator)
        tag (quote iterator)
    set-scope-symbol! env (quote qualify)
        fn qualify (tag-type value)
            assert (== (typeof tag-type) type)
                error "type argument expected."
            cast
                tag-type (typeof value)
                \ value

    set-scope-symbol! env (quote disqualify)
        fn disqualify (tag-type value)
            assert (== (typeof tag-type) type)
                error "type argument expected."
            let t = (typeof value)
            if (not (< t tag-type))
                error
                    .. "can not unqualify value of type " (string t)
                        \ "; type not related to " (string tag-type) "."
            cast
                element-type t
                \ value

    set-scope-symbol! env (quote and)
        make-expand-multi-op-ltr and
    set-scope-symbol! env (quote or)
        make-expand-multi-op-ltr or
    set-scope-symbol! env (quote max)
        make-expand-multi-op-ltr
            fn (a b)
                ? (> b a) b a
    set-scope-symbol! env (quote min)
        make-expand-multi-op-ltr
            fn (a b)
                ? (< b a) b a
    set-scope-symbol! env (quote @)
        make-expand-multi-op-ltr @
    set-scope-symbol! env (quote ..)
        make-expand-multi-op-ltr ..
    set-scope-symbol! env (quote +)
        make-expand-multi-op-ltr +
    set-scope-symbol! env (quote *)
        make-expand-multi-op-ltr *

    set-scope-symbol! env scope-list-wildcard-symbol
        fn expand-any-list (topexpr env)
            let expr =
                @ topexpr 0
            let head =
                @ expr 0
            let headstr =
                string head
            # method call syntax
            if
                and
                    symbol? head
                    and
                        none? (get-scope-symbol env head)
                        == (slice headstr 0 1) "."

                let name =
                    datum->syntax
                        symbol
                            slice headstr 1
                        syntax->anchor head
                let self-arg =
                    @ expr 1
                let rest =
                    slice expr 2
                let self =
                    parameter
                        quote self
                cons
                    list
                        list fn/cc (list (parameter (quote _)) self)
                            cons
                                list (do @) self
                                    list quote name
                                cons self rest
                        \ self-arg
                    slice topexpr 1
            # infix operator support
            elseif (has-infix-ops env expr)
                cons
                    parse-infix-expr env (@ expr 0) (slice expr 1) 0
                    slice topexpr 1

    set-scope-symbol! env scope-symbol-wildcard-symbol
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
                        symbol
                            @ out 0
                        \ sym-anchor
                    slice out 1
            # return tokenized list if string contains a dot and it's not the
              concat operator
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
                    finalize-head
                        fold
                            fn (out k)
                                if (== k ".")
                                    cons ""
                                        cons
                                            quote .
                                            finalize-head out
                                else
                                    cons
                                        .. k (@ out 0)
                                        slice out 1
                            list ""
                            iter-r
                                string sym
                    slice topexpr 1
    return env

#define-infix-op : 70 > :
define-infix-op or 100 > or
define-infix-op and 200 > and
#define-infix-op | 240 > |
#define-infix-op ^ 250 > ^
#define-infix-op & 260 > &

define-infix-op < 300 > <
define-infix-op > 300 > >
define-infix-op <= 300 > <=
define-infix-op >= 300 > >=
define-infix-op != 300 > !=
define-infix-op == 300 > ==

define-infix-op <? 300 > <?
define-infix-op >? 300 > >?
define-infix-op <=? 300 > <=?
define-infix-op >=? 300 > >=?
define-infix-op !=? 300 > !=?
define-infix-op ==? 300 > ==?

#define-infix-op is 300 > is
define-infix-op .. 400 < ..
#define-infix-op << 450 > <<
#define-infix-op >> 450 > >>
define-infix-op - 500 > -
define-infix-op + 500 > +
#define-infix-op % 600 > %
define-infix-op / 600 > /
#define-infix-op // 600 > //
define-infix-op * 600 > *
#define-infix-op ** 700 < **
define-infix-op . 800 > .
define-infix-op @ 800 > @
#define-infix-op .= 800 > .=
#define-infix-op @= 800 > @=
#define-infix-op =@ 800 > =@

syntax-extend stage-5 (return env)

    fn iterator? (x)
        (typeof x) <? iterator

    fn new-iterator (next start)
        qualify iterator
            fn iterator-tuple ()
                return next start

    fn countable-rslice-iter (l)
        if ((countof l) != 0)
            return (@ l 0) (slice l 1)

    fn countable-iter (f)
        let c i = (f)
        if (i < (countof c))
            return (@ c i)
                fn ()
                    return c (i + (size_t 1))

    fn scope-iter (f)
        let t k = (f)
        let key value =
            next-scope-symbol t k
        if (not (none? key))
            return key value
                fn ()
                    return t key

    fn gen-yield-iter (callee)
        let caller-return = none
        let yield-iter =
            fn/cc yield-iter (return ret)
                # store caller continuation in state
                set! caller-return return
                branch (none? ret) # first invocation
                    fn/cc (_)
                        # invoke callee with yield function as first argument
                        callee
                            fn/cc (ret value)
                                # continue caller
                                caller-return value ret
                        # callee has returned for good
                          resume caller - we're done here.
                        cc/call none caller-return
                    fn/cc (_)
                        # continue callee
                        cc/call none ret

        new-iterator yield-iter none

    fn iter (x)
        if (iterator? x) x
        else
            let t = (typeof x)
            if (<=? t list)
                new-iterator countable-rslice-iter x
            elseif (<=? t scope)
                new-iterator scope-iter
                    fn () (return x none)
            elseif (<=? t string)
                new-iterator countable-iter
                    fn () (return x (size_t 0))
            elseif (<=? t closure)
                gen-yield-iter x
            else
                error
                    .. "don't know how to iterate " (string x)

    fn range (a b c)
        let num-type = (typeof a)
        let step = (? (none? c) (num-type 1) c)
        let from = (? (none? b) (num-type 0) a)
        let to = (? (none? b) a b)
        new-iterator
            fn (x)
                if (< x to)
                    return x (+ x step)
            \ from

    fn zip (a b)
        let iter-a init-a = ((disqualify iterator (iter a)))
        let iter-b init-b = ((disqualify iterator (iter b)))
        new-iterator
            fn (f)
                let a b = (f)
                let at-a... next-a = (iter-a a)
                let at-b... next-b = (iter-b b)
                if (not (or (none? next-a) (none? next-b)))
                    return at-a... at-b...
                        fn ()
                            return next-a next-b
            fn ()
                return init-a init-b

    fn infrange (a b)
        let num-type =
            ? (none? a)
                ? (none? b) int (typeof b)
                \ (typeof a)
        let step = (? (none? b) (num-type 1) b)
        let from = (? (none? a) (num-type 0) a)
        new-iterator
            fn (x)
                return x (+ x step)
            \ from

    fn enumerate (x from step)
        zip (infrange from step) (iter x)

    fn =? (x)
        and
            symbol? (syntax->datum x)
            ==? x (quote =)

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
                    repeat (slice expr 1)
                let elem = (@ expr 0)
                if (symbol? elem)
                    break
                        syntax-cons elem args
                        syntax-cons elem names
                else
                    # initializer
                    let nelem = (syntax->datum elem)
                    assert
                        and
                            list? nelem
                            (countof nelem) >= (u64 3)
                            =? (@ nelem 1)
                        syntax-error elem "illegal initializer"
                    break
                        syntax-cons (@ elem 2) args
                        syntax-cons (@ elem 0) names

    set-scope-symbol! env (quote iter) iter
    set-scope-symbol! env (quote iterator?) iterator?
    set-scope-symbol! env (quote range) range
    set-scope-symbol! env (quote zip) zip
    set-scope-symbol! env (quote enumerate) enumerate
    set-scope-symbol! env (quote loop)
        # better loop with support for initializers
        macro
            fn loop (expr)
                let expr-anchor = (syntax->anchor expr)
                let param-repeat =
                    quote repeat
                let param-break =
                    quote break
                let args names =
                    parse-loop-args (@ expr 1)
                qquote
                    do
                        xlet (unquote param-repeat) =
                            fn/cc ((unquote param-break) (unquote-splice names))
                                ;
                                    unquote param-break
                                    unquote
                                        syntax-do (slice expr 2)
                        ;
                            unquote param-repeat
                            unquote-splice args

    set-scope-symbol! env (quote for)
        block-macro
            fn (block-expr)
                fn iter-expr (expr)
                    assert (not (empty? expr))
                        \ "syntax: (for let-name ... in iter-expr body-expr ...)"
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
                            syntax-list
                                datum->syntax none
                                    syntax->anchor expr

                fn generate-template (body extra-args extra-names)
                    let param-ret =
                        datum->syntax (parameter (quote return))
                    let param-inner-ret =
                        datum->syntax (parameter (quote return))
                    let param-iter =
                        datum->syntax (parameter (quote iter))
                    let param-state =
                        datum->syntax (parameter (quote state))
                    let param-for =
                        datum->syntax (parameter (quote for-loop))
                    let param-at =
                        datum->syntax (parameter (quote at...))
                    let param-next =
                        datum->syntax (parameter (quote next))
                    cons
                        qquote
                            do
                                xlet (unquote param-for) =
                                    fn/cc (
                                        (unquote param-ret)
                                        (unquote param-iter)
                                        (unquote param-state)
                                        (unquote-splice extra-names))
                                        let (unquote param-at) (unquote param-next) =
                                            (unquote param-iter) (unquote param-state)
                                        ;
                                            unquote param-ret
                                            ? (==? (unquote param-next) none)
                                                unquote
                                                    syntax-do else-block
                                                do
                                                    xlet repeat =
                                                        fn/cc (
                                                            (unquote param-inner-ret)
                                                            (unquote-splice extra-names))
                                                            ;
                                                                unquote param-inner-ret
                                                                ;
                                                                    unquote param-for
                                                                    unquote param-iter
                                                                    unquote param-next
                                                                    unquote-splice extra-names
                                                    let (unquote-splice dest-names) =
                                                        unquote param-at
                                                    unquote-splice body
                                    unquote-splice
                                        syntax-eol expr
                                let iter-val state-val =
                                    (disqualify iterator (iter (unquote src-expr)))
                                ;
                                    unquote param-for
                                    \ iter-val state-val
                                    unquote-splice extra-args
                        \ block-rest

                let body = (slice rest 1)
                if (syntax-head? (@ body 0) (quote with))
                    # read extra state params
                    generate-template
                        slice body 1
                        parse-loop-args (@ body 0)
                else
                    # no extra state params
                    generate-template body expr-eol expr-eol

    set-scope-symbol! env (quote fn)
        # an extended version of function that permits chaining
          sequential cross-dependent declarations with a `with` keyword
          (fn [name] (param ...) body ...) with (fn ...) ...
        block-macro
            do
                fn parse-funcdef (topexpr k head)
                    let expr =
                        @ topexpr 0
                    assert (syntax-head? expr head)
                        "function definition expected after 'with'"
                    let decl =
                        @ (@ topexpr 0) 1
                    let retparam =
                        quote return
                    let make-params-body =
                        fn (param-idx)
                            syntax-cons
                                syntax-cons retparam
                                    @ expr param-idx
                                syntax-list
                                    syntax-list retparam
                                        syntax-do
                                            slice expr (+ param-idx 1)
                    let rest =
                        slice topexpr 1
                    if (symbol? (syntax->datum decl))
                        # build single xlet assignment
                        let func-expr =
                            qquote
                                (unquote decl) =
                                    fn/cc (unquote (@ expr 1))
                                        unquote-splice
                                            make-params-body 2
                        let result-body result-rest =
                            if (empty? rest)
                                _
                                    syntax-list func-expr
                                    syntax-list decl
                            elseif (syntax-head? rest (quote with))
                                let defs defs-rest =
                                    parse-funcdef
                                        slice rest 1
                                        k + 1
                                        head
                                _
                                    syntax-cons func-expr defs
                                    \ defs-rest
                            else
                                _ (syntax-list func-expr) rest
                        if (k == 0)
                            cons
                                syntax-cons
                                    quote xlet
                                    \ result-body
                                \ result-rest
                        else
                            _ result-body result-rest
                    else
                        assert (k == 0)
                            "unnamed function can not be chained"
                        # regular, unchained form
                        cons
                            syntax-cons
                                quote fn/cc
                                make-params-body 1
                            \ rest

                fn (topexpr)
                    parse-funcdef topexpr 0 (@ (@ topexpr 0) 0)

    return env

syntax-extend stage-7 (return env)
    fn repeat-string (n c)
        for i in (range n)
            with (s = "")
            repeat (s .. c)
        else s
    fn get-leading-spaces (s)
        for c in s
            with (out = "")
            if (c == " ")
                repeat (out .. c)
            else out
        else out

    fn has-chars (s)
        for i in s
            if (i != " ") true
            else (repeat)
        else false

    fn read-eval-print-loop ()
        let vmin vmaj vpatch = (interpreter-version)
        print "Bangra"
            .. (string vmin) "." (string vmaj)
                ? (vpatch == 0) ""
                    .. "." (string vpatch)
                \ " (" interpreter-timestamp ")"
        let state = (scope)
        fn reset-state ()
            let repl-env = (scope (globals))
            set-scope-symbol! repl-env (quote reset) reset-state
            set-scope-symbol! state (quote env) repl-env
            set-scope-symbol! state (quote counter) 1
        reset-state;
        fn capture-scope (env)
            set-scope-symbol! state (quote env) env
        # appending this to an expression before evaluation captures the env
          table so it can be used for the next expression.
        let expression-suffix =
            qquote
                syntax-extend (return env)
                    ;
                        unquote
                            datum->syntax capture-scope
                                active-anchor
                        \ env
                    return env
        fn make-idstr ()
            .. "$"
                string (get-scope-symbol state (quote counter))
        loop
            with
                preload = ""
                cmdlist = ""
            let idstr = (make-idstr)
            let id = (symbol idstr)
            let promptstr = (.. idstr " ▶")
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
            let slevel = (stack-level)
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
                        set! slevel (stack-level)
                        let f = (eval code eval-env)
                        set! slevel (stack-level)
                        let result... = (f)
                        if (not (none? result...))
                            for i in (range (va-countof result...))
                                let idstr = (make-idstr)
                                let value = (va-arg i result...)
                                print (.. idstr "= " (repr value))
                                set-scope-symbol! eval-env id value
                                set-scope-symbol! state (quote counter)
                                    (get-scope-symbol state (quote counter)) + 1
                                repeat
                    except (e)
                        print
                            traceback slevel 2
                        print "error:" e
                    \ ""
                else cmdlist
            repeat preload cmdlist
    set-scope-symbol! env (quote read-eval-print-loop) read-eval-print-loop
    return env

syntax-extend stage-final (return env)
    set-globals! env
    return env

read-eval-print-loop
