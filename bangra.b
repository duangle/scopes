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

syntax-extend
    fn/cc return-true (_) true
    fn/cc return-false (_) false
    fn/cc unordered-error (_ a b)
        error
            .. "illegal ordered comparison of values of types "
                .. (repr (typeof a))
                    .. " and "
                        repr (typeof b)

    set-scope-symbol! syntax-scope (symbol "==")
        fn/cc "==" (return a b)
            ordered-branch a b return-true
                fn/cc (_) (cc/call none unordered-error a b)
                \ return-false return-false
    set-scope-symbol! syntax-scope (symbol "!=")
        fn/cc "!=" (return a b)
            ordered-branch a b return-false
                fn/cc (_) (cc/call none unordered-error a b)
                \ return-true return-true
    set-scope-symbol! syntax-scope (symbol "<")
        fn/cc "<" (return a b)
            ordered-branch a b return-false
                fn/cc (_) (cc/call none unordered-error a b)
                \ return-true return-false
    set-scope-symbol! syntax-scope (symbol "<=")
        fn/cc "<=" (return a b)
            ordered-branch a b return-true
                fn/cc (_) (cc/call none unordered-error a b)
                \ return-true return-false
    set-scope-symbol! syntax-scope (symbol ">")
        fn/cc ">" (return a b)
            ordered-branch a b return-false
                fn/cc (_) (cc/call none unordered-error a b)
                \ return-false return-true
    set-scope-symbol! syntax-scope (symbol ">=")
        fn/cc ">=" (return a b)
            ordered-branch a b return-true
                fn/cc (_) (cc/call none unordered-error a b)
                \ return-false return-true
    set-scope-symbol! syntax-scope (symbol "==?")
        fn/cc "==?" (return a b)
            ordered-branch a b return-true return-false
                \ return-false return-false
    set-scope-symbol! syntax-scope (symbol "!=?")
        fn/cc "!=?" (return a b)
            ordered-branch a b return-false return-true
                \ return-true return-true
    set-scope-symbol! syntax-scope (symbol "<?")
        fn/cc "<?" (return a b)
            ordered-branch a b return-false return-false
                \ return-true return-false
    set-scope-symbol! syntax-scope (symbol "<=?")
        fn/cc "<=?" (return a b)
            ordered-branch a b return-true return-false
                \ return-true return-false
    set-scope-symbol! syntax-scope (symbol ">?")
        fn/cc ">?" (return a b)
            ordered-branch a b return-false return-false
                \ return-false return-true
    set-scope-symbol! syntax-scope (symbol ">=?")
        fn/cc ">=?" (return a b)
            ordered-branch a b return-true return-false
                \ return-false return-true

    set-scope-symbol! syntax-scope
        symbol "quote"
        block-scope-macro
            fn/cc "expand-quote" (return expr syntax-scope)
                call
                    fn/cc (_ args)
                        return
                            cons
                                syntax-list
                                    datum->syntax
                                        symbol "form-quote"
                                        syntax->anchor args
                                    syntax-quote
                                        # keep wrapped in list if multiple
                                            arguments
                                        ordered-branch (list) (slice args 1)
                                            fn/cc (_)
                                                _ (@ args 0)
                                            fn/cc (_) (_ args)
                                            fn/cc (_) (_ args)
                                            fn/cc (_) (_ args)
                                slice expr 1
                            \ syntax-scope
                    slice (@ expr 0) 1
    set-scope-symbol! syntax-scope
        symbol "quote-syntax"
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
                                    ordered-branch (list) (slice args 1)
                                        fn/cc (_)
                                            _ (@ args 0)
                                        fn/cc (_) (_ args)
                                        fn/cc (_) (_ args)
                                        fn/cc (_) (_ args)
                                    syntax->anchor args
                                slice expr 1
                            \ syntax-scope
                    slice (@ expr 0) 1
    \ syntax-scope

syntax-extend
    set-scope-symbol! syntax-scope
        symbol "set!"
        block-scope-macro
            fn/cc "expand-set!" (return expr env)
                call
                    fn/cc (_ name expr-anchor)
                        call
                            fn/cc (_ param)
                                ordered-branch (typeof param) parameter
                                    fn/cc (_) (_ none)
                                    fn/cc (_)
                                        error "set! requires parameter argument"
                                    \ none none # never reached
                                return
                                    cons
                                        syntax-cons
                                            datum->syntax bind! expr-anchor
                                            syntax-cons
                                                # wrap parameter in closure to
                                                  prevent evaluation
                                                syntax-list
                                                    datum->syntax
                                                        fn/cc (_)
                                                            _ param
                                                        \ expr-anchor
                                                slice (@ expr 0) 2
                                        slice expr 1
                                    \ env
                            ordered-branch (typeof (syntax->datum name)) parameter
                                fn/cc (_) (_ (syntax->datum name))
                                fn/cc (_) (_ (get-scope-symbol env name name))
                                \ none none # never reached
                    @ (@ expr 0) 1
                    syntax->anchor (@ expr 0)
    \ syntax-scope

syntax-extend
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
    set-scope-symbol! syntax-scope
        quote symbol?
        fn/cc "symbol?" (return x)
            return
                ==? (typeof x) symbol
    set-scope-symbol! syntax-scope
        quote list?
        fn/cc "list?" (return x)
            return
                ==? (typeof x) list
    set-scope-symbol! syntax-scope
        quote type?
        fn/cc "type?" (return x)
            return
                ==? (typeof x) type
    set-scope-symbol! syntax-scope
        quote none?
        fn/cc "none?" (return x)
            return
                ==? x none
    set-scope-symbol! syntax-scope
        quote empty?
        fn/cc "empty?" (return x)
            return
                == (countof x) (u64 0)
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
            fn/cc "expand-?" (return expr env)
                return
                    call
                        fn/cc (_ ret-true ret-false expr-anchor)
                            _
                                cons
                                    syntax-list
                                        datum->syntax branch
                                            syntax->anchor (@ expr 0)
                                        @ (@ expr 0) 1
                                        syntax-cons
                                            datum->syntax fn/cc expr-anchor
                                            syntax-cons
                                                syntax-list ret-true
                                                syntax-list
                                                    @ (@ expr 0) 2
                                        syntax-cons
                                            datum->syntax fn/cc expr-anchor
                                            syntax-cons
                                                syntax-list ret-false
                                                syntax-list
                                                    @ (@ expr 0) 3
                                    slice expr 1
                        datum->syntax
                            parameter
                                datum->syntax
                                    quote ret-true
                                    syntax->anchor (@ expr 0)
                        datum->syntax
                            parameter
                                datum->syntax
                                    quote ret-false
                                    syntax->anchor (@ expr 0)
                        syntax->anchor
                            @ expr 0
                    \ env
    \ syntax-scope

syntax-extend
    set-scope-symbol! syntax-scope (quote syntax-head?)
        fn/cc "syntax-head?" (return expr name)
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
                ? (== (countof expr) (u64 1))
                    @ expr 0
                    syntax-cons (datum->syntax do (syntax->anchor expr)) expr
    # build a list terminator tagged with a desired anchor from an expression
    set-scope-symbol! syntax-scope (quote syntax-eol)
        fn/cc "syntax-eol" (return expr)
            return
                datum->syntax (list) (syntax->anchor expr)
    \ syntax-scope

syntax-extend
    fn/cc qquote-1 (_ x)
        ? (list-atom? x)
            syntax-list
                datum->syntax quote-syntax
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
    # quasiquote support
      (qquote expr [...])
    set-scope-symbol! syntax-scope (quote qquote)
        macro
            fn/cc "expand-qquote" (_ expr)
                ? (empty? (slice expr 2))
                    qquote-1 (@ expr 1)
                    qquote-1 (slice expr 1)
    \ syntax-scope

syntax-extend
    set-scope-symbol! syntax-scope (quote define)
        block-macro
            fn/cc "expand-define" (return topexpr env)
                cons
                    qquote
                        syntax-extend
                            set-scope-symbol! syntax-scope
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
                ==? (@ (@ expr 0) 2) (quote =)
                fn/cc (_) (_)
                fn/cc (_)
                    syntax-error (@ expr 0) "syntax: let <var> = <expr>"
            return
                call
                    fn/cc (_ cont-param param-name rest)
                        list
                            qquote
                                ;
                                    fn/cc
                                        ;
                                            unquote cont-param
                                            unquote param-name
                                        unquote-splice
                                            datum->syntax rest
                                                syntax->anchor (@ expr 0)
                                    unquote
                                        syntax-do
                                            slice (@ expr 0) 3
                                    # force enclosing list to use anchor
                                      of original expression
                                    unquote-splice
                                        syntax-eol (@ expr 0)
                    datum->syntax
                        parameter (quote-syntax _)
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


define sizeof
    let sym = (symbol "size")
    fn/cc "sizeof" (return x)
        assert (type? x) "type expected"
        let size =
            @ x sym
        ? (none? size) (size_t 0) size

define alignof
    let sym = (symbol "alignment")
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
                qquote
                    @ (unquote (@ expr 1))
                        unquote
                            syntax-quote key
                        unquote-splice
                            syntax-eol key
                error "symbol expected"

define and
    macro
        fn and (expr)
            let tmp = (datum->syntax (parameter (quote-syntax tmp)))
            let ret = (datum->syntax (parameter (quote-syntax and-return)))
            let ret-true = (datum->syntax (parameter (quote-syntax ret-true)))
            let ret-false = (datum->syntax (parameter (quote-syntax ret-false)))
            qquote
                ;
                    fn/cc ((unquote ret) (unquote tmp))
                        branch (unquote tmp)
                            fn/cc ((unquote ret-true))
                                unquote (@ expr 2)
                            fn/cc ((unquote ret-false))
                                unquote tmp
                    unquote (@ expr 1)
                    unquote-splice
                        syntax-eol expr

define or
    macro
        fn or (expr)
            let tmp = (datum->syntax (parameter (quote-syntax tmp)))
            let ret = (datum->syntax (parameter (quote-syntax or-return)))
            let ret-true = (datum->syntax (parameter (quote-syntax ret-true)))
            let ret-false = (datum->syntax (parameter (quote-syntax ret-false)))
            qquote
                ;
                    fn/cc ((unquote ret) (unquote tmp))
                        branch (unquote tmp)
                            fn/cc ((unquote ret-true))
                                unquote tmp
                            fn/cc ((unquote ret-false))
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
            let ret-then = (datum->syntax (parameter (quote-syntax ret-then)))
            let ret-else = (datum->syntax (parameter (quote-syntax ret-else)))
            qquote
                branch (unquote cond)
                    fn/cc ((unquote ret-then))
                        unquote-splice then-exprlist
                    fn/cc ((unquote ret-else))
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

syntax-extend
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

    set-scope-symbol! syntax-scope
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
                                datum->syntax (parameter (quote-syntax let-return))
                            list
                                qquote
                                    ;
                                        fn/cc
                                            ;
                                                unquote cont-param
                                                unquote-splice
                                                    datum->syntax args
                                                        syntax->anchor body
                                            unquote-splice
                                                datum->syntax rest
                                                    syntax->anchor expr
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
            qquote
                do
                    fn/cc (unquote param-continue) (
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
            let order =
                syntax->datum (@ expr 3)
            let dest-name = (@ expr 4)
            let infix-symbol =
                symbol
                    .. "#ifx:" (string name)
            set-scope-symbol! env infix-symbol
                syntax-infix-rules prec (@ env order) dest-name
            return rest env

syntax-extend

    fn fold (f init next it)
        let val it =
            next it
        let out = init
        loop (out val it)
            if (none? it)
                return out
            else
                continue
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
            ? (and (==? op-order <) (pred op-prec prec)) op none

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
                    syntax->anchor la
                \ next-state

    let bangra =
        scope
    set-scope-symbol! bangra (quote path)
        list "./?.b"
            .. interpreter-dir "/?.b"
    set-scope-symbol! bangra (quote modules)
        scope
    set-scope-symbol! syntax-scope (quote bangra) bangra

    fn make-module-path (pattern name)
        fold
            fn (out val)
                .. out
                    ? (== val "?") name val
            \ ""
            iter pattern

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
                            scope (globals)
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

    set-scope-symbol! syntax-scope scope-list-wildcard-symbol
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
                error "todo"
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

    set-scope-symbol! syntax-scope scope-symbol-wildcard-symbol
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

#define-infix-op : 70 > :
define-infix-op or 100 > or
define-infix-op and 200 > and
define-infix-op | 240 > |
define-infix-op ^ 250 > ^
define-infix-op & 260 > &

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
define-infix-op << 450 > <<
define-infix-op >> 450 > >>
define-infix-op - 500 > -
define-infix-op + 500 > +
define-infix-op % 600 > %
define-infix-op / 600 > /
#define-infix-op // 600 > //
define-infix-op * 600 > *
define-infix-op ** 700 < **
define-infix-op . 800 > .
define-infix-op @ 800 > @
#define-infix-op .= 800 > .=
#define-infix-op @= 800 > @=
#define-infix-op =@ 800 > =@

syntax-extend
    let qualifier =
        type
            quote qualifier

    set-type-symbol! qualifier (quote apply-type)
        fn new-qualifier (name)
            assert (symbol? name) "symbol expected"
            let aqualifier =
                type
                    symbol
                        .. "qualifier[" (string name) "]"

            set-type-symbol! aqualifier (quote super) qualifier
            set-type-symbol! aqualifier (quote cast)
                fn cast-qualifier (fromtype totype value)
                    if (fromtype <? aqualifier)
                        let fromelemtype = fromtype.element-type
                        if (fromelemtype ==? totype)
                            bitcast totype value
                    elseif (totype <? aqualifier)
                        let toelemtype = totype.element-type
                        if (toelemtype ==? fromtype)
                            bitcast totype value
            set-type-symbol! aqualifier (quote apply-type)
                fn new-typed-qualifier (element-type)
                    let typed-qualifier =
                        type
                            symbol
                                .. (string name) "[" (string element-type) "]"
                    if (none? (. typed-qualifier complete))
                        set-type-symbol! typed-qualifier (quote super) aqualifier
                        set-type-symbol! typed-qualifier (quote element-type) element-type
                        set-type-symbol! typed-qualifier (quote complete) true

                    return typed-qualifier
            set-type-symbol! aqualifier (quote complete) true

            return aqualifier

    set-scope-symbol! syntax-scope (quote qualifier) qualifier
    set-scope-symbol! syntax-scope (quote iterator)
        qualifier (quote iterator)
    set-scope-symbol! syntax-scope (quote qualify)
        fn qualify (qualifier-type value)
            assert
                qualifier-type <? qualifier
                error
                    .. "qualifier type expected, got "
                        repr qualifier-type
            cast
                qualifier-type (typeof value)
                \ value

    set-scope-symbol! syntax-scope (quote disqualify)
        fn disqualify (qualifier-type value)
            assert
                qualifier-type <? qualifier
                error
                    .. "qualifier type expected, got "
                        repr qualifier-type
            let t = (typeof value)
            if (not (t <? qualifier-type))
                error
                    .. "can not unqualify value of type " (string t)
                        \ "; type not related to " (string qualifier-type) "."
            cast t.element-type
                \ value

    \ syntax-scope

syntax-extend

    fn iterator? (x)
        (typeof x) <? iterator

    # `start` is a private token passed to `next`, which is an iterator
      function of the format (next token) -> next-token value ... | none
    fn new-iterator (next start)
        qualify iterator
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

    fn gen-yield-iter (callee)
        let caller-return = none
        let yield-iter =
            fn/cc "yield-iter" (return ret)
                # store caller continuation in state
                set! caller-return return
                branch (none? ret) # first invocation
                    fn/cc (_)
                        # invoke callee with yield function as first argument
                        callee
                            fn/cc (ret value)
                                # continue caller
                                caller-return ret value
                        # callee has returned for good
                          resume caller - we're done here.
                        cc/call none caller-return
                    fn/cc (_)
                        # continue callee
                        cc/call none ret

        new-iterator yield-iter none

    # TODO: allow types to overload iterator generation
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
                    .. "don't know how to iterate value of type " (string t)

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

    # repeats a sequence n times or indefinitely
    fn repeat (x n)
        let nextf init = ((disqualify iterator (iter x)))
        new-iterator
            fn repeat-next (f)
                let state i = (f)
                if (i < n)
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
                                return next-state  i
                            \ at...
            fn ()
                return init 0

    # returns the cartesian product of two sequences
    fn product (a b)
        let a-iter a-init = ((disqualify iterator (iter a)))
        let b-iter b-init = ((disqualify iterator (iter b)))
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
        let iter-a init-a = ((disqualify iterator (iter a)))
        let iter-b init-b = ((disqualify iterator (iter b)))
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
        let iter-a init-a = ((disqualify iterator (iter a)))
        let iter-b init-b = ((disqualify iterator (iter b)))
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

    fn infrange (a b)
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
                    continue (slice expr 1)
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

    set-scope-symbol! syntax-scope (quote iter) iter
    set-scope-symbol! syntax-scope (quote va-iter) va-iter
    set-scope-symbol! syntax-scope (quote iterator?) iterator?
    set-scope-symbol! syntax-scope (quote range) range
    set-scope-symbol! syntax-scope (quote repeat) repeat
    set-scope-symbol! syntax-scope (quote product) product
    set-scope-symbol! syntax-scope (quote concat) concat
    set-scope-symbol! syntax-scope (quote zip) zip
    set-scope-symbol! syntax-scope (quote enumerate) enumerate
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
                qquote
                    do
                        fn/cc (unquote param-continue) (
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
                            syntax-list
                                datum->syntax none
                                    syntax->anchor expr

                fn generate-template (body extra-args extra-names)
                    let param-ret =
                        datum->syntax (parameter (quote-syntax return))
                    let param-inner-ret =
                        datum->syntax (parameter (quote-syntax return))
                    let param-iter =
                        datum->syntax (parameter (quote-syntax iter))
                    let param-state =
                        datum->syntax (parameter (quote-syntax state))
                    let param-for =
                        datum->syntax (symbol "#loop-for")
                            syntax->anchor expr
                    let param-at =
                        datum->syntax (parameter (quote-syntax at...))
                    let param-next =
                        datum->syntax (parameter (quote-syntax next))
                    cons
                        qquote
                            do
                                fn/cc (unquote param-for) (
                                    (unquote param-ret)
                                    (unquote param-iter)
                                    (unquote param-state)
                                    (unquote-splice extra-names))
                                    let (unquote param-next) (unquote param-at) =
                                        (unquote param-iter) (unquote param-state)
                                    ? (==? (unquote param-next) none)
                                        unquote
                                            syntax-do else-block
                                        do
                                            fn/cc continue (
                                                (unquote param-inner-ret)
                                                (unquote-splice extra-names))
                                                ;
                                                    unquote param-for
                                                    unquote param-iter
                                                    unquote param-next
                                                    unquote-splice extra-names
                                            let (unquote-splice dest-names) =
                                                unquote param-at
                                            unquote-splice body
                                let iter-val state-val =
                                    (disqualify iterator (iter (unquote src-expr)))
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
    # extended function body macro expander that binds the function itself
      to `recur` and a list of all parameters to `parameters` for further
      macro processing.
    fn make-function-body (env decl paramdef body expr-anchor nextfunc)
        if (not (list? (syntax->datum paramdef)))
            syntax-error paramdef "parameter list expected"
        let retparam =
            quote-syntax return
        let func =
            flow
                ? (none? decl)
                    datum->syntax
                        symbol ""
                        \ expr-anchor
                    \ decl
        let retparam = (parameter retparam)
        flow-append-parameter! func retparam
        if (not (none? decl))
            set-scope-symbol! env decl func
        let subenv = (scope env)
        set-scope-symbol! subenv (quote recur) func
        set-scope-symbol! subenv (quote return) retparam
        loop-for param-name in (syntax->datum paramdef)
            let param = (parameter param-name)
            set-scope-symbol! subenv (syntax->datum param-name) param
            flow-append-parameter! func param
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
                            \ expr-anchor nextfunc
                    if (not (empty? rest))
                        parse-funcdef rest complete-fn-decl
                    else
                        return complete-fn-decl
                call
                    parse-funcdef (slice (@ topexpr 0) 1)
                        fn (rest) rest
                    slice topexpr 1

    # an extended version of fn
      (fn [name] (param ...) body ...) with (fn ...) ...
    set-scope-symbol! syntax-scope (quote fn)
        block-macro
            fn (topexpr env)
                let expr =
                    @ topexpr 0
                let expr-anchor =
                    syntax->anchor expr
                let decl =
                    @ expr 1
                let retparam =
                    quote-syntax return
                let rest =
                    slice topexpr 1
                fn make-params-body (name body)
                    fn tailf (rest) rest
                    call
                        make-function-body env name (@ body 0) (slice body 1)
                            \ expr-anchor tailf
                        \ rest

                if (symbol? (syntax->datum decl))
                    make-params-body decl
                        slice expr 2
                else
                    # regular, unchained form
                    make-params-body none
                        slice expr 1
    \ syntax-scope

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
                syntax-extend
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
            let id = (symbol idstr)
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
                            loop-for i in (range (va-countof result...))
                                let idstr = (make-idstr)
                                let value = (va-arg i result...)
                                print (.. idstr "= " (repr value))
                                set-scope-symbol! eval-env id value
                                set-scope-symbol! state (quote counter)
                                    (get-scope-symbol state (quote counter)) + 1
                                continue
                    except (e)
                        print
                            traceback slevel 2
                        print "error:" e
                    \ ""
                else cmdlist
            continue preload cmdlist

syntax-extend
    set-globals! syntax-scope
    \ syntax-scope

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
    let sourcepath = none
    let parse-options = true
    loop
        with
            i = 1
        let arg = (va-arg i args...)
        if (not (none? arg))
            if (parse-options and ((@ arg 0) == "-"))
                if (arg == "--help" or arg == "-h")
                    print-help (va-arg 0 args...)
                elseif (arg == "--version" or arg == "-v")
                    print-version
                elseif (arg == "--")
                    set! parse-options false
                else
                    print
                        .. "unrecognized option: " arg
                            \ ". Try --help for help."
                    exit 1
            elseif (none? sourcepath)
                set! sourcepath arg
            else
                print
                    .. "unrecognized argument: " arg
                        \ ". Try --help for help."
                exit 1
            continue (i + 1)

    if (none? sourcepath)
        read-eval-print-loop
    else
        let expr =
            list-load sourcepath
        let eval-scope =
            scope (globals)
        set-scope-symbol! eval-scope (quote module-path) sourcepath
        clear-traceback
        let fun =
            eval expr eval-scope sourcepath
        clear-traceback
        fun
        exit 0

run-main (args)
