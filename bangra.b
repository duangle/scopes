# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.

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
                                        # keep wrapped in list
                                        # if multiple arguments
                                        branch
                                            == (slice args 1) (list)
                                            fn/cc (_)
                                                _ (@ args 0)
                                            fn/cc (_)
                                                _ args
                                slice expr 1
                            env
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
                                    env
                            get-scope-symbol env name name
                    @ (@ expr 0) 1
    return env

syntax-extend def-? (return env)
    set-scope-symbol! env
        quote symbol?
        fn/cc symbol? (return x)
            return
                == (typeof x) symbol
    set-scope-symbol! env
        quote list?
        fn/cc list? (return x)
            return
                == (typeof x) list
    set-scope-symbol! env
        quote none?
        fn/cc none? (return x)
            return
                == x none
    set-scope-symbol! env
        quote empty?
        fn/cc empty? (return x)
            return
                == (countof x) (u64 0)
    set-scope-symbol! env
        quote key?
        fn/cc key? (return x y)
            return
                != (@ x y) none
    set-scope-symbol! env
        quote load
        fn/cc load (return path)
            return
                eval
                    list-load path
                    globals;
                    path
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
                            env
    set-scope-symbol! env
        quote block-macro
        fn/cc block-macro (return f)
            return
                block-scope-macro
                    fn/cc (return expr env)
                        return
                            f expr env
                            env
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
                            env
                    expand
                        slice (@ expr 0) 1
                        env
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
                    env
    #~ set-scope-symbol! env
        #~ quote :
        #~ block-scope-macro
            #~ fn/cc : (return expr env)
                #~ return
                    #~ cons
                        #~ cons tupleof
                            #~ cons
                                #~ branch
                                    #~ ==
                                        #~ typeof
                                            #~ @ (@ expr 0) 1
                                        #~ symbol
                                    #~ fn/cc ()
                                        #~ list quote
                                            #~ @ (@ expr 0) 1
                                    #~ fn/cc ()
                                        #~ @ (@ expr 0) 1
                                #~ branch
                                    #~ == (slice (@ expr 0) 2) (list)
                                    #~ fn/cc ()
                                        #~ list
                                            #~ @ (@ expr 0) 1
                                    #~ fn/cc ()
                                        #~ slice (@ expr 0) 2
                        #~ slice expr 1
                    #~ env
    return env

syntax-extend def-list-defs (return env)
    set-scope-symbol! env (quote list-head?)
        fn/cc list-head? (return expr name)
            return
                ? (list? (syntax->datum expr))
                    ? (empty? expr)
                        false
                        call
                            fn/cc (_ head)
                                ? (symbol? (syntax->datum head))
                                    _ (== head name)
                                    _ false
                            @ expr 0
                    false
    set-scope-symbol! env (quote list-atom?)
        fn/cc list-atom? (return x)
            return
                ? (list? (syntax->datum x))
                    empty? x
                    true
    # unwrap single item from list or prepend 'do' clause to list
    set-scope-symbol! env (quote syntax-do)
        fn/cc syntax-do (return expr)
            return
                ? (empty? (slice expr 1))
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
    # (qquote expr [...])
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
                                    x
                                ? (list-head? x (quote unquote))
                                    syntax-do (slice x 1)
                                    ? (list-head? x (quote qquote))
                                        qquote-1 (qquote-1 (@ x 1))
                                        ? (list-atom? (@ x 0))
                                            syntax-list
                                                datum->syntax syntax-cons
                                                    syntax->anchor (@ x 0)
                                                qquote-1 (@ x 0)
                                                qquote-1 (slice x 1)
                                            ? (list-head? (@ x 0) (quote unquote-splice))
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
                                do
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
                                        unquote
                                            syntax-do
                                                slice (@ expr 0) 3
                                        # force enclosing list to use anchor
                                        # of original expression
                                        unquote-splice
                                            syntax-eol (@ expr 0)
                    datum->syntax
                        parameter (quote _)
                    @ (@ expr 0) 1
                    slice expr 1
                env

# a standard function declaration that supports recursion
# (fn [name] (param ...) body ...)
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
                quote return
            let wrap =
                fn/cc (return val)
                    return (datum->syntax val decl-anchor)
            let make-params-body =
                fn/cc (return param-idx)
                    return
                        syntax-cons
                            syntax-cons
                                retparam
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
                                decl
                                syntax-cons
                                    wrap fn/cc
                                    syntax-cons
                                        @ expr 1
                                        make-params-body 2
                            ? (empty? rest)
                                list decl
                                rest
                    cons
                        syntax-cons
                            wrap fn/cc
                            make-params-body 1
                        rest

define empty-list
    escape (list)
define int i32
define uint i32
define float r32
define double r64
# (assert bool-expr [error-message])
define assert
    macro
        fn assert (expr)
            qquote
                ? (unquote (@ expr 1))
                    true
                    error
                        unquote
                            ? (empty? (slice expr 2))
                                .. "assertion failed: " (string (@ expr 1))
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

syntax-extend stage-2 (return env)
    set-scope-symbol! env (quote and)
        macro
            fn/cc and (return expr)
                let tmp =
                    parameter
                        quote tmp
                return
                    list
                        list fn/cc (list (parameter (quote _)) tmp)
                            list branch tmp
                                list fn/cc (list)
                                    @ expr 2
                                list fn/cc (list) tmp
                        @ expr 1
    set-scope-symbol! env (quote or)
        macro
            fn/cc or (return expr)
                let tmp =
                    parameter
                        quote tmp
                return
                    list
                        list fn/cc (list (parameter (quote _)) tmp)
                            list branch tmp
                                list fn/cc (list) tmp
                                list fn/cc (list)
                                    @ expr 2
                        @ expr 1
    set-scope-symbol! env (quote loop) # bootstrap loop, better version later
        macro
            fn/cc loop (return expr)
                let expr-anchor = (syntax->anchor expr)
                let param-repeat =
                    quote repeat
                let param-return =
                    datum->syntax (parameter (quote return))
                return
                    syntax-list
                        datum->syntax do expr-anchor
                        syntax-list
                            datum->syntax let expr-anchor
                            param-repeat
                            quote =
                            datum->syntax none expr-anchor
                        syntax-list
                            datum->syntax set! expr-anchor
                            param-repeat
                            syntax-cons
                                datum->syntax fn/cc expr-anchor
                                syntax-cons
                                    syntax-cons param-return
                                        @ expr 1
                                    syntax-list
                                        syntax-list param-return
                                            syntax-cons
                                                datum->syntax do expr-anchor
                                                slice expr 2
                        syntax-cons param-repeat
                            @ expr 1
    let if-rec = none
    set! if-rec
        fn/cc if (return expr)
            let cond =
                @ (@ expr 0) 1
            let then-exprlist =
                slice (@ expr 0) 2
            let make-branch =
                fn/cc make-branch (return else-exprlist)
                    let ret-then = (datum->syntax (parameter (quote ret-then)))
                    let ret-else = (datum->syntax (parameter (quote ret-else)))
                    return
                        syntax-list
                            branch
                            cond
                            syntax-cons fn/cc
                                syntax-cons (syntax-list ret-then)
                                    syntax-list ret-then
                                        syntax-cons do
                                            then-exprlist
                            syntax-cons fn/cc
                                syntax-cons (syntax-list ret-else)
                                    syntax-list ret-else
                                        syntax-cons do
                                            else-exprlist
            let rest-expr =
                slice expr 1
            let next-expr =
                ? (empty? rest-expr)
                    none
                    @ rest-expr 0
            return
                ? (list-head? next-expr (quote elseif))
                    do
                        let nextif =
                            if-rec
                                slice expr 1
                        cons
                            make-branch
                                list (@ nextif 0)
                            slice nextif 1
                    ? (list-head? next-expr (quote else))
                        cons
                            make-branch
                                slice (@ expr 1) 1
                            slice expr 2
                        cons
                            make-branch (list)
                            slice expr 1
    set-scope-symbol! env (quote if) (block-macro if-rec)
    set-scope-symbol! env (quote syntax-infix-rules)
        fn/cc syntax-infix-rules (return prec order name)
            let spec = (scope)
            set-scope-symbol! spec (quote prec) prec
            set-scope-symbol! spec (quote order) order
            set-scope-symbol! spec (quote name) name
            return spec
    set-scope-symbol! env (quote set-syntax-infix-op!)
        fn/cc set-syntax-infix-op! (return env name expr)
            set-scope-symbol! env
                symbol
                    .. "#ifx:" name
                expr
            return;
    return env

syntax-extend stage-3 (return env)

    fn fold (it init f)
        let next =
            @ it 0
        let st =
            next (@ it 1)
        let out = init
        loop (out st)
            if (none? st) out
            else
                repeat
                    f out (@ st 0)
                    next (@ st 1)

    fn iter (s)
        let ls =
            countof s
        tupleof
            fn (i)
                if (< i ls)
                    tupleof (@ s i) (+ i (u64 1))
                else none
            u64 0

    fn iter-r (s)
        tupleof
            fn (i)
                if (> i (u64 0))
                    let k = (- i (u64 1))
                    tupleof (@ s k) k
                else none
            countof s

    fn get-ifx-op (env op)
        let key =
            symbol
                .. "#ifx:" (string op)
        ? (symbol? op)
            get-scope-symbol env key
            none

    fn has-infix-ops (infix-table expr)
        # any expression whose second argument matches an infix operator
        # is treated as an infix expression.
        and
            not (empty? (slice expr 2))
            != (get-ifx-op infix-table (@ expr 1)) none

    fn infix-op (infix-table token prec pred)
        let op =
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif (pred (. op prec) prec)
            op
        else none

    fn rtl-infix-op (infix-table token prec pred)
        let op =
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif
            and
                == (. op order) <
                pred (. op prec) prec
            op
        else none

    fn parse-infix-expr (infix-table lhs state mprec)
        loop (lhs state)
            if (empty? state)
                tupleof lhs state
            else
                let la = (@ state 0)
                let op =
                    infix-op infix-table la mprec >=
                if (none? op)
                    tupleof lhs state
                else
                    let next-state = (slice state 1)
                    let rhs = (@ next-state 0)
                    let state = (slice next-state 1)
                    let rhs-state =
                        loop (rhs state)
                            if (empty? state)
                                tupleof rhs state
                            else
                                let ra =
                                    @ state 0
                                let lop =
                                    infix-op infix-table ra (. op prec) >
                                let nextop =
                                    ? (none? lop)
                                        rtl-infix-op infix-table ra (. op prec) ==
                                        lop
                                if (none? nextop)
                                    tupleof rhs state
                                else
                                    let rhs-state =
                                        parse-infix-expr
                                            infix-table
                                            rhs
                                            state
                                            . nextop prec
                                    repeat
                                        @ rhs-state 0
                                        @ rhs-state 1
                    repeat
                        list (. op name) lhs
                            @ rhs-state 0
                        @ rhs-state 1

    let bangra =
        tableof
            : path
                list
                    "./?.b"
                    .. interpreter-dir "/?.b"
            : modules
                tableof;
    fn make-module-path (pattern name)
        fold (iter pattern) ""
            fn (out val)
                .. out
                    ? (== val "?") name val

    fn find-module (name)
        assert (symbol? name)
            "module name must be symbol"
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
                            namestr
                    let expr =
                        list-load module-path
                    if (not (none? expr))
                        let fn =
                            eval expr
                                tableof
                                    tupleof scope-parent-symbol
                                        globals;
                                    : module-path
                                module-path
                        let content =
                            fn;
                        set-scope-symbol! (. bangra modules)
                            tupleof name content
                        content
                    else
                        repeat
                            slice pattern 1
                else
                    error
                        .. "module not found: " namestr
        else
            content

    fn make-expand-multi-op-ltr (op)
        # (op a b c ...) -> (op (op (op a b) c) ...)
        macro
            fn (expr)
                let tail =
                    slice expr 1
                loop (tail)
                    let rest =
                        slice tail 2
                    if (empty? rest)
                        cons op tail
                    else
                        repeat
                            cons
                                list op
                                    @ tail 0
                                    @ tail 1
                                rest

    fn xpcall (func xfunc)
        let old_handler =
            get-exception-handler;
        fn cleanup ()
            set-exception-handler! old_handler
        call
            fn/cc try (finally)
                fn except (exc aframe args)
                    cleanup;
                    finally
                        xfunc exc
                set-exception-handler! except
                let result =
                    func;
                cleanup;
                result

    set-scope-symbol! env (quote xpcall) xpcall
    set-scope-symbol! env (quote bangra) bangra
    set-scope-symbol! env (quote require) find-module
    set-scope-symbol! env (quote iterator)
        tag (quote iterator)
    set-scope-symbol! env (quote qualify)
        fn qualify (tag-type value)
            assert (== (typeof tag-type) type)
                error "type argument expected."
            bitcast
                tag-type (typeof value)
                value

    set-scope-symbol! env (quote disqualify)
        fn disqualify (tag-type value)
            assert (== (typeof tag-type) type)
                error "type argument expected."
            let t = (typeof value)
            if (not (< t tag-type))
                error
                    .. "can not unqualify value of type " (string t)
                        \ "; type not related to " (string tag-type) "."
            bitcast
                element-type t
                value

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
    set-scope-symbol! env (quote try)
        block-macro
            fn (expr env)
                if (not (list-head? (@ expr 1) (quote except)))
                    error "except block missing"
                cons
                    list xpcall
                        cons fn/cc
                            cons (list)
                                slice (@ expr 0) 1
                        cons fn/cc
                            cons
                                cons (parameter (quote _))
                                    @ (@ expr 1) 1
                                slice (@ expr 1) 2
                    slice expr 2

    set-scope-symbol! env (quote define)
        block-macro
            fn (expr env)
                let name =
                    @ (@ expr 0) 1
                let exprlist =
                    slice (@ expr 0) 2
                let subscope =
                    parameter (quote env)
                cons
                    list syntax-extend (list (parameter (quote _)) subscope)
                        list let (quote name) (quote =)
                            cons do exprlist
                        list set-scope-symbol! subscope (list quote name) (quote name)
                        subscope
                    slice expr 1

    set-scope-symbol! env scope-list-wildcard-symbol
        fn (topexpr env)
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
                    symbol
                        slice headstr 1
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
                        self-arg
                    slice topexpr 1
            # infix operator support
            elseif (has-infix-ops env expr)
                cons
                    @
                        parse-infix-expr env
                            \ (@ expr 0) (slice expr 1) 0
                        0
                    slice topexpr 1

    set-scope-symbol! env scope-symbol-wildcard-symbol
        fn (topexpr env)
            let sym =
                @ topexpr 0
            let it =
                iter-r
                    string sym
            fn finalize-head (out)
                cons
                    symbol
                        @ out 0
                    slice out 1
            # return tokenized list if string contains a dot
            # and it's not the concat operator
            if
                and
                    none? (get-scope-symbol env sym)
                    fold it false
                        fn (out k)
                            if (== k ".") true
                            else out
                cons
                    finalize-head
                        fold it (list "")
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
                    slice topexpr 1

    set-syntax-infix-op! env ":" (syntax-infix-rules 70 > :)
    set-syntax-infix-op! env "or" (syntax-infix-rules 100 > or)
    set-syntax-infix-op! env "and" (syntax-infix-rules 200 > and)
    set-syntax-infix-op! env "|" (syntax-infix-rules 240 > |)
    set-syntax-infix-op! env "^" (syntax-infix-rules 250 > ^)
    set-syntax-infix-op! env "&" (syntax-infix-rules 260 > &)
    set-syntax-infix-op! env "<" (syntax-infix-rules 300 > <)
    set-syntax-infix-op! env ">" (syntax-infix-rules 300 > >)
    set-syntax-infix-op! env "<=" (syntax-infix-rules 300 > <=)
    set-syntax-infix-op! env ">=" (syntax-infix-rules 300 > >=)
    set-syntax-infix-op! env "!=" (syntax-infix-rules 300 > !=)
    set-syntax-infix-op! env "==" (syntax-infix-rules 300 > ==)
    #set-syntax-infix-op! env "is" (syntax-infix-rules 300 > is)
    set-syntax-infix-op! env ".." (syntax-infix-rules 400 < ..)
    set-syntax-infix-op! env "<<" (syntax-infix-rules 450 > <<)
    set-syntax-infix-op! env ">>" (syntax-infix-rules 450 > >>)
    set-syntax-infix-op! env "-" (syntax-infix-rules 500 > -)
    set-syntax-infix-op! env "+" (syntax-infix-rules 500 > +)
    set-syntax-infix-op! env "%" (syntax-infix-rules 600 > %)
    set-syntax-infix-op! env "/" (syntax-infix-rules 600 > /)
    set-syntax-infix-op! env "//" (syntax-infix-rules 600 > //)
    set-syntax-infix-op! env "*" (syntax-infix-rules 600 > *)
    set-syntax-infix-op! env "**" (syntax-infix-rules 700 < **)
    set-syntax-infix-op! env "." (syntax-infix-rules 800 > .)
    set-syntax-infix-op! env "@" (syntax-infix-rules 800 > @)
    #set-syntax-infix-op! env ".=" (syntax-infix-rules 800 > .=)
    #set-syntax-infix-op! env "@=" (syntax-infix-rules 800 > @=)
    #set-syntax-infix-op! env "=@" (syntax-infix-rules 800 > =@)
    return env

syntax-extend stage-4 (_ env)
    fn =? (x)
        and
            == (typeof x) symbol
            == x (quote =)

    .. env
        tableof
            : let
                # support for multiple declarations in one let env
                block-macro
                    fn (expr env)
                        let args = (slice (@ expr 0) 1)
                        let argtype = (typeof (@ args 0))
                        if (== argtype symbol)
                            if (=? (@ args 1))
                                # regular form
                                cons
                                    cons let args
                                    slice expr 1
                            else
                                # unpacking multiple variables

                                # iterate until we hit the = symbol, after which
                                # the body follows
                                fn find-body (expr)

                                    if (empty? expr)
                                        error "syntax: let <name> ... = <expression>"
                                    elseif (=? (@ expr 0))
                                        tupleof (list)
                                            slice expr 1
                                    else
                                        let out =
                                            find-body (slice expr 1)
                                        tupleof
                                            cons (@ expr 0) (@ out 0)
                                            @ out 1
                                let cells =
                                    find-body args
                                list
                                    list
                                        cons fn/cc
                                            cons
                                                cons (parameter (quote _)) (@ cells 0)
                                                slice expr 1
                                        list splice
                                            cons do
                                                @ cells 1

                        elseif (== argtype parameter)
                            assert (=? (@ args 1))
                                "syntax: let <parameter> = <expression>"
                            # regular form, hidden parameter
                            list
                                cons
                                    cons fn/cc
                                        cons
                                            list
                                                parameter (quote _)
                                                @ args 0
                                            slice expr 1
                                    slice args 2
                        else
                            # multiple variables

                            # prepare quotable values from declarations
                            fn handle-pairs (pairs)
                                if (empty? pairs)
                                    slice expr 1
                                else
                                    let pair =
                                        @ pairs 0
                                    assert (=? (@ pair 1))
                                        "syntax: let (<name> = <expression>) ..."
                                    cons
                                        cons let pair
                                        handle-pairs
                                            slice pairs 1

                            handle-pairs args
            : xlet
                block-macro
                    # a late-binding let with support for recursion and
                    # function-level cross dependencies. comparable to letrec.
                    fn (expr env)
                        let args = (slice (@ expr 0) 1)
                        let name = (@ args 0)
                        let argtype = (typeof name)
                        if (or (== argtype symbol) (== argtype parameter))
                            assert (=? (@ args 1))
                                "syntax: xlet <parameter> = <expression>"
                            # regular form with support for recursion
                            cons
                                list let name (quote =) none
                                cons
                                    list set! name
                                        cons do
                                            slice args 2
                                    slice expr 1
                        else
                            assert (== argtype list)
                                "syntax: xlet (<name> = <expression>) ..."
                            # multiple variables with support for recursion
                            # and circular dependencies

                            # prepare quotable values from declarations
                            fn handle-pairs (pairs)
                                if (empty? pairs)
                                    tupleof
                                        list;
                                        slice expr 1
                                else
                                    let result =
                                        handle-pairs
                                            slice pairs 1
                                    let pair =
                                        @ pairs 0
                                    assert (=? (@ pair 1))
                                        "syntax: xlet (<name> = <expression>) ..."
                                    let name = (@ pair 0)
                                    tupleof
                                        cons name (@ result 0)
                                        cons
                                            list set! name
                                                cons do
                                                    slice pair 2
                                            (@ result 1)

                            let entries =
                                handle-pairs args

                            list
                                list
                                    cons fn/cc
                                        cons
                                            cons (parameter (quote _)) (@ entries 0)
                                            @ entries 1

syntax-extend stage-5 (_ env)

    fn iterator? (x)
        (typeof x) < iterator

    fn countable-rslice-iter (l)
        if ((countof l) != 0)
            tupleof (@ l 0) (slice l 1)

    fn countable-iter (x)
        let c i = x
        if (i < (countof c))
            tupleof (@ c i) (tupleof c (i + 1))

    fn table-iter (x)
        let t = (@ x 0)
        let key-value =
            next-key t
                @ x 1
        if (not (none? key-value))
            tupleof key-value
                tupleof t (@ key-value 0)

    fn gen-yield-iter (callee)
        let caller-return = none
        fn yield-iter (ret)
            # store caller continuation in state
            set! caller-return return
            if (none? ret) # first invocation
                # invoke callee with yield function as first argument
                callee
                    fn/cc (ret value)
                        # continue caller
                        caller-return
                            tupleof value ret
                # callee has returned for good
                # resume caller - we're done here.
                cc/call none caller-return none
            else # continue callee
                cc/call none ret

        qualify iterator
            tupleof yield-iter none

    fn iter (x)
        if (iterator? x) x
        else
            let t = (typeof x)
            if (<= t list)
                qualify iterator
                    tupleof countable-rslice-iter x
            elseif (< t tuple)
                qualify iterator
                    tupleof countable-iter (tupleof x 0)
            elseif (<= t table)
                qualify iterator
                    tupleof table-iter (tupleof x none)
            elseif (<= t string)
                qualify iterator
                    tupleof countable-iter (tupleof x 0)
            elseif (<= t closure)
                gen-yield-iter x
            else
                error
                    .. "don't know how to iterate " (string x)

    fn range (a b c)
        let step = (? (none? c) 1 c)
        let from = (? (none? b) 0 a)
        let to = (? (none? b) a b)
        qualify iterator
            tupleof
                fn (x)
                    if (< x to)
                        tupleof x (+ x step)
                from

    fn zip (a b)
        let iter-a init-a = (disqualify iterator (iter a))
        let iter-b init-b = (disqualify iterator (iter b))
        qualify iterator
            tupleof
                fn (x)
                    let state-a = (iter-a (@ x 0))
                    let state-b = (iter-b (@ x 1))
                    if (not (or (none? state-a) (none? state-b)))
                        let at-a next-a = state-a
                        let at-b next-b = state-b
                        tupleof
                            tupleof at-a at-b
                            tupleof next-a next-b
                tupleof init-a init-b

    fn infrange (a b)
        let step = (? (none? b) 1 b)
        let from = (? (none? a) 0 a)
        qualify iterator
            tupleof
                fn (x)
                    tupleof x (+ x step)
                from

    fn enumerate (x from step)
        zip (infrange from step) (iter x)

    fn =? (x)
        and
            == (typeof x) symbol
            == x (quote =)

    fn parse-loop-args (fullexpr)
        let expr =
            ? (list-head? fullexpr (quote with))
                slice fullexpr 1
                fullexpr
        loop (expr)
            if (empty? expr)
                tupleof
                    list;
                    list;
            else
                let args names =
                    repeat (slice expr 1)
                let elem = (@ expr 0)
                if (symbol? elem)
                    tupleof
                        cons elem args
                        cons elem names
                else
                    # initializer
                    assert
                        and
                            list? elem
                            (countof elem) >= (u64 3)
                            =? (@ elem 1)
                        error "illegal initializer"
                    tupleof
                        cons (cons do (slice elem 2)) args
                        cons (@ elem 0) names
    .. env
        tableof
            : iter
            : iterator?
            : range
            : zip
            : enumerate
            : loop # better loop with support for initializers
                macro
                    fn loop (expr)
                        let param-repeat = (quote repeat)
                        let args names =
                            parse-loop-args (@ expr 1)
                        list do
                            list let param-repeat (quote =) none
                            list set! param-repeat
                                cons fn/cc
                                    cons
                                        cons
                                            parameter (quote _)
                                            names
                                        slice expr 2
                            cons param-repeat
                                args
            : for
                block-macro
                    fn (block-expr)
                        fn iter-expr (expr)
                            assert (not (empty? expr))
                                "syntax: (for let-name ... in iter-expr body-expr ...)"
                            if (list-head? expr (quote in))
                                tupleof
                                    list;
                                    slice expr 1
                            else
                                let names rest =
                                    iter-expr
                                        slice expr 1
                                tupleof
                                    cons
                                        @ expr 0
                                        names
                                    rest

                        let expr = (@ block-expr 0)
                        let dest-names rest =
                            iter-expr (slice expr 1)
                        let src-expr = (@ rest 0)
                        let block-rest else-block =
                            let remainder =
                                (slice block-expr 1)
                            if
                                and
                                    not (empty? remainder)
                                    list-head? (@ remainder 0) (quote else)
                                tupleof (slice remainder 1) (slice (@ remainder 0) 1)
                            else
                                tupleof remainder (list none)

                        fn generate-template (body extra-args extra-names)
                            let param-iter = (parameter (quote iter))
                            let param-state = (parameter (quote state))
                            let param-for = (parameter (quote for-loop))
                            let param-at-next = (parameter (quote at-next))
                            cons
                                qquote
                                    do
                                        let (unquote param-for) = none
                                        set! (unquote param-for)
                                            fn/cc (
                                                (unquote (parameter (quote _)))
                                                (unquote param-iter)
                                                (unquote param-state)
                                                (unquote-splice extra-names))
                                                let (unquote param-at-next) =
                                                    (unquote param-iter) (unquote param-state)
                                                ? (== (unquote param-at-next) none)
                                                    do
                                                        unquote-splice else-block
                                                    do
                                                        let repeat = none
                                                        set! repeat
                                                            fn/cc (
                                                                (unquote (parameter (quote _)))
                                                                (unquote-splice extra-names))
                                                                (unquote param-for)
                                                                    unquote param-iter
                                                                    @ (unquote param-at-next) 1
                                                                    unquote-splice extra-names
                                                        let (unquote-splice dest-names) =
                                                            @ (unquote param-at-next) 0
                                                        unquote-splice body
                                        (unquote param-for)
                                            splice (disqualify iterator (iter (unquote src-expr)))
                                            unquote-splice extra-args
                                block-rest

                        let body = (slice rest 1)
                        if (list-head? (@ body 0) (quote with))
                            let args names =
                                parse-loop-args (@ body 0)
                            # read extra state params
                            generate-template
                                slice body 1
                                \ args names
                        else
                            # no extra state params
                            generate-template body (list) (list)

            # an extended version of function that permits chaining
            # sequential cross-dependent declarations with a `with` keyword
            : fn # (fn [name] (param ...) body ...) with (fn ...) ...
                block-macro
                    do
                        fn parse-funcdef (topexpr k head)
                            let expr =
                                @ topexpr 0
                            assert (list-head? expr head)
                                "function definition expected after 'with'"
                            let decl =
                                @ (@ topexpr 0) 1
                            let retparam =
                                quote return
                            let make-params-body =
                                fn (param-idx)
                                    cons
                                        cons
                                            retparam
                                            @ expr param-idx
                                        slice expr (+ param-idx 1)
                            let rest =
                                slice topexpr 1
                            if (symbol? decl)
                                # build single xlet assignment
                                let func-expr =
                                    list decl (quote =)
                                        cons fn/cc
                                            cons
                                                @ expr 1
                                                make-params-body 2
                                let result =
                                    if (empty? rest)
                                        tupleof (list func-expr)
                                            list decl
                                    elseif (list-head? rest (quote with))
                                        let defs defs-rest =
                                            parse-funcdef
                                                slice rest 1
                                                k + 1
                                                head
                                        tupleof
                                            cons func-expr defs
                                            defs-rest
                                    else
                                        tupleof (list func-expr) rest
                                if (k == 0)
                                    cons
                                        cons xlet (@ result 0)
                                        @ result 1
                                else result
                            else
                                assert (k == 0)
                                    "unnamed function can not be chained"
                                # regular, unchained form
                                cons
                                    cons fn/cc
                                        make-params-body 1
                                    rest

                        fn (topexpr)
                            parse-funcdef topexpr 0 (@ (@ topexpr 0) 0)

syntax-extend stage-6 (_ env)
    let void* =
        pointer void
    let null =
        bitcast void*
            uint64 0
    fn null? (x)
        (bitcast uint64 x) == 0
    fn extern-library (cdefs)
        let lib = (tableof)
        for k v in cdefs
            set-scope-symbol! lib k
                ? ((typeof v) < tuple)
                    external (splice v)
                    v
            repeat;
        lib

    .. env
        tableof
            : void*
            : null
            : null?
            : extern-library


syntax-extend stage-7 (_ env)
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
        print "Bangra"
            .. (string (@ bangra-version 0)) "." (string (@ bangra-version 1))
                ? ((@ bangra-version 2) == 0) ""
                    .. "." (string (@ bangra-version 2))
        let state = (tableof)
        fn reset-state ()
            set-scope-symbol! state
                env :
                    .. (globals)
                        tableof
                            reset : reset-state
            set-scope-symbol! state (counter : 1)
        reset-state;
        fn capture-scope (env)
            set-scope-symbol! state (: env)
        # appending this to an expression before evaluation captures the env
        # table so it can be used for the next expression.
        let expression-suffix =
            list
                qquote
                    syntax-extend (_ env)
                        (unquote capture-scope) env
                        env
        loop
            with
                preload = ""
                cmdlist = ""
            let idstr = (.. "$" (string state.counter))
            let id = (symbol idstr)
            let promptstr = (.. idstr ">")
            let cmd =
                prompt
                    ..
                        ? (empty? cmdlist) promptstr
                            repeat-string (countof promptstr) "."
                        " "
                    preload

            if ((typeof cmd) != void)
                let terminated? =
                    or (not (has-chars cmd))
                        (empty? cmdlist) and ((slice cmd -1) == ";")
                let cmdlist = (.. cmdlist cmd "\n")
                let preload =
                    if terminated? ""
                    else (get-leading-spaces cmd)
                let cmdlist =
                    if terminated?
                        try
                            let expr = (list-parse cmdlist)
                            if (none? expr)
                                error "parsing failed"
                            let f =
                                eval
                                    .. (list (cons do expr)) expression-suffix
                                    state.scope
                            let result = (f)
                            if ((typeof result) != void)
                                print (.. idstr "= " (repr result))
                                set-scope-symbol! state.scope id result
                                set-scope-symbol! state
                                    counter : (state.counter + 1)
                        except (e)
                            print "error:" e
                        ""
                    else
                        cmdlist
                repeat preload cmdlist
    set-scope-symbol! env
        : read-eval-print-loop
    env

syntax-extend stage-final (_ env)
    set-globals! env
    env

none
