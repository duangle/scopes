# boot script
# the bangra executable looks for a boot file at
# path/to/executable.b and, if found, executes it.

syntax-extend stage-0 (_ scope)
    set-key! scope
        symbol "quote"
        block-scope-macro
            continuation quote (_ expr scope)
                call
                    continuation (_ args)
                        tupleof
                            cons
                                # stop compiler expansion
                                escape
                                    # stop macro expansion
                                    escape
                                        # keep wrapped in list
                                        # if multiple arguments
                                        branch
                                            == (slice args 1) (list)
                                            continuation ()
                                                @ args 0
                                            continuation ()
                                                args
                                slice expr 1
                            scope
                    slice (@ expr 0) 1
    set-key! scope
        symbol "set!"
        block-scope-macro
            continuation set! (_ expr scope)
                call
                    continuation (_ name)
                        call
                            continuation (_ param)
                                branch
                                    == (typeof param) parameter
                                    continuation () none
                                    continuation ()
                                        error "set! requires parameter argument"
                                tupleof
                                    cons
                                        cons bind!
                                            cons
                                                # stop interpreter from expanding parameter
                                                escape
                                                    # stop compiler expansion
                                                    escape
                                                        # stop macro expansion
                                                        escape param
                                                slice (@ expr 0) 2
                                        slice expr 1
                                    scope
                            find-scope-symbol scope name name
                    @ (@ expr 0) 1
    scope

syntax-extend stage-1 (_ scope)
    .. scope
        tableof
            tupleof
                quote symbol?
                continuation symbol? (_ x)
                    == (typeof x) symbol
            tupleof
                quote list?
                continuation list? (_ x)
                    == (typeof x) list
            tupleof
                quote none?
                continuation none? (_ x)
                    == x none
            tupleof
                quote empty?
                continuation empty? (_ x)
                    == (countof x) 0
            tupleof
                quote key?
                continuation key? (_ x y)
                    != (@ x y) none
            tupleof
                quote load
                continuation load (_ path)
                    eval
                        list-load path
                        globals;
                        path
            tupleof
                quote API
                import-c
                    # todo: search path & embedded resources
                    .. interpreter-dir "/bangra.h"
                    tupleof;
            tupleof
                quote macro
                continuation macro (_ f)
                    block-scope-macro
                        continuation (_ expr scope)
                            tupleof
                                cons
                                    f (@ expr 0) scope
                                    slice expr 1
                                scope
            tupleof
                quote block-macro
                continuation macro (_ f)
                    block-scope-macro
                        continuation (_ expr scope)
                            tupleof
                                f expr scope
                                scope
            tupleof
                quote dump-syntax
                block-scope-macro
                    continuation dump-syntax (_ expr scope)
                        tupleof
                            call
                                continuation (_ e)
                                    dump
                                        @ (@ e 0) 0
                                    cons
                                        escape
                                            @ (@ e 0) 0
                                        slice e 1
                                @
                                    expand
                                        cons
                                            slice (@ expr 0) 1
                                            slice expr 1
                                        scope
                                    0
                            scope
            tupleof
                # a lofi version of let so we get some sugar early
                quote let
                block-scope-macro
                    continuation let (_ expr scope)
                        branch
                            == (typeof (@ (@ expr 0) 2)) symbol
                            continuation () none
                            continuation ()
                                error "syntax: let <var> = <expr>"
                        branch
                            == (@ (@ expr 0) 2) (quote =)
                            continuation () none
                            continuation ()
                                error "syntax: let <var> = <expr>"
                        tupleof
                            call
                                continuation (_ param-name rest)
                                    list
                                        cons
                                            cons continuation
                                                cons
                                                    list
                                                        parameter (quote _)
                                                        param-name
                                                    branch
                                                        == (countof rest) 0
                                                        continuation ()
                                                            list param-name
                                                        continuation ()
                                                            slice expr 1
                                            slice (@ expr 0) 3
                                @ (@ expr 0) 1
                                slice expr 1
                            scope
            tupleof
                quote ?
                block-scope-macro
                    continuation ? (_ expr scope)
                        tupleof
                            cons
                                list branch
                                    @ (@ expr 0) 1
                                    list continuation (list)
                                        @ (@ expr 0) 2
                                    list continuation (list)
                                        @ (@ expr 0) 3
                                slice expr 1
                            scope
            tupleof
                quote :
                block-scope-macro
                    continuation : (_ expr scope)
                        tupleof
                            cons
                                cons tupleof
                                    cons
                                        branch
                                            ==
                                                typeof
                                                    @ (@ expr 0) 1
                                                symbol
                                            continuation ()
                                                list quote
                                                    @ (@ expr 0) 1
                                            continuation ()
                                                @ (@ expr 0) 1
                                        branch
                                            == (slice (@ expr 0) 2) (list)
                                            continuation ()
                                                list
                                                    @ (@ expr 0) 1
                                            continuation ()
                                                slice (@ expr 0) 2
                                slice expr 1
                            scope

syntax-extend stage-2 (_ scope)
    let list-head? =
        continuation list-head? (_ expr name)
            ? (list? expr)
                do
                    let head = (@ expr 0)
                    ? (symbol? head)
                        == head name
                        false
                false
    .. scope
        tableof
            : empty-list (list)
            : empty-tuple (tuple)

            : bool (integer 1 false)
            : int8 (integer 8 true)
            : int16 (integer 16 true)
            : int32 (integer 32 true)
            : int64 (integer 64 true)
            : uint8 (integer 8 false)
            : uint16 (integer 16 false)
            : uint32 (integer 32 false)
            : uint64 (integer 64 false)

            : int (integer 32 true)
            : uint (integer 32 false)

            : real16 (real 16)
            : real32 (real 32)
            : real64 (real 64)

            : half (real 16)
            : float (real 32)
            : double (real 64)

            : list-head?
            : list-atom?
                continuation list-atom? (_ x)
                    ? (list? x)
                        empty? x
                        true
            : assert # (assert bool-expr [error-message])
                macro
                    continuation assert (_ expr)
                        list ? (@ expr 1) true
                            list error
                                ? (empty? (slice expr 2))
                                    .. "assertion failed: " (string (@ expr 1))
                                    @ expr 2
            : ::@
                block-macro
                    continuation ::@ (_ expr)
                        cons
                            ..
                                slice (@ expr 0) 1
                                list
                                    @ expr 1
                            slice expr 2
            : ::*
                block-macro
                    continuation ::* (_ expr)
                        list
                            ..
                                slice (@ expr 0) 1
                                slice expr 1
            : .
                macro
                    continuation . (_ expr)
                        let key =
                            @ expr 2
                        ? (symbol? key)
                            list
                                (do @)
                                @ expr 1
                                list quote key
                            error "symbol expected"

            : function # (function [name] (param ...) body ...)
                block-macro
                    continuation function (_ topexpr)
                        let expr =
                            @ topexpr 0
                        let decl =
                            @ (@ topexpr 0) 1
                        let retparam =
                            quote return
                        let make-params-body =
                            continuation (_ param-idx)
                                cons
                                    cons
                                        retparam
                                        @ expr param-idx
                                    slice expr (+ param-idx 1)
                        let rest =
                            slice topexpr 1
                        ? (symbol? decl)
                            cons
                                list let decl (quote =) none
                                cons
                                    list set! decl
                                        cons continuation
                                            cons
                                                @ expr 1
                                                make-params-body 2
                                    ? (empty? rest)
                                        list decl
                                        rest
                            cons
                                cons continuation
                                    make-params-body 1
                                rest
            : and
                macro
                    continuation and (_ expr)
                        let tmp =
                            parameter
                                quote tmp
                        list
                            list continuation (list (parameter (quote _)) tmp)
                                list branch tmp
                                    list continuation (list)
                                        @ expr 2
                                    list continuation (list) tmp
                            @ expr 1
            : or
                macro
                    continuation or (_ expr)
                        let tmp =
                            parameter
                                quote tmp
                        list
                            list continuation (list (parameter (quote _)) tmp)
                                list branch tmp
                                    list continuation (list) tmp
                                    list continuation (list)
                                        @ expr 2
                            @ expr 1
            : loop # bootstrap loop, better version in stage 4
                macro
                    continuation loop (_ expr)
                        let param-repeat =
                            quote repeat
                        list do
                            list let param-repeat (quote =) none
                            list set! param-repeat
                                cons continuation
                                    cons
                                        cons
                                            parameter (quote _)
                                            @ expr 1
                                        slice expr 2
                            cons param-repeat
                                @ expr 1
            : if
                do
                    let if-rec = none
                    set! if-rec
                        continuation if (_ expr)
                            let cond =
                                @ (@ expr 0) 1
                            let then-exprlist =
                                slice (@ expr 0) 2
                            let make-branch =
                                continuation (_ else-exprlist)
                                    list branch
                                        cond
                                        cons continuation
                                            cons (list) then-exprlist
                                        cons continuation
                                            cons (list) else-exprlist
                            let rest-expr =
                                slice expr 1
                            let next-expr =
                                ? (empty? rest-expr)
                                    none
                                    @ rest-expr 0
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
                                        make-branch
                                            list;
                                        slice expr 1
                    block-macro if-rec
            : syntax-infix-rules
                continuation syntax-infix-rules (_ prec order name)
                    structof
                        tupleof (quote prec) prec
                        tupleof (quote order) order
                        tupleof (quote name) name
            : syntax-infix-op
                macro
                    continuation syntax-infix-op (_ expr)
                        list tupleof
                            list quote
                                symbol
                                    .. "#ifx:" (string (@ expr 1))
                            @ expr 2

syntax-extend stage-3 (_ scope)

    function unwrap-single (expr)
        # unwrap single item from list or prepend 'do' clause to list
        ? (empty? (slice expr 1))
            @ expr 0
            cons do expr

    function fold (it init f)
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

    function iter (s)
        let ls =
            countof s
        tupleof
            function (i)
                if (< i ls)
                    tupleof (@ s i) (+ i 1)
                else none
            0

    function iter-r (s)
        tupleof
            function (i)
                if (> i 0)
                    let k = (- i 1)
                    tupleof (@ s k) k
                else none
            countof s

    function get-ifx-op (scope op)
        let key =
            symbol
                .. "#ifx:" (string op)
        ? (symbol? op)
            find-scope-symbol scope key
            none

    function has-infix-ops (infix-table expr)
        # any expression whose second argument matches an infix operator
        # is treated as an infix expression.
        and
            not (empty? (slice expr 2))
            != (get-ifx-op infix-table (@ expr 1)) none

    function infix-op (infix-table token prec pred)
        let op =
            get-ifx-op infix-table token
        if (none? op)
            error
                .. (string token)
                    " is not an infix operator, but embedded in an infix expression"
        elseif (pred (. op prec) prec)
            op
        else none

    function rtl-infix-op (infix-table token prec pred)
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

    function parse-infix-expr (infix-table lhs state mprec)
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
    function make-module-path (pattern name)
        fold (iter pattern) ""
            function (out val)
                list out val
                    .. out val
                .. out
                    ? (== val "?") name val

    function find-module (name)
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
                        set-key! (. bangra modules)
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

    function make-expand-multi-op-ltr (op)
        # (op a b c ...) -> (op (op (op a b) c) ...)
        macro
            function (expr)
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

    function xpcall (func xfunc)
        let old_handler =
            get-exception-handler;
        function cleanup ()
            set-exception-handler! old_handler
        call
            continuation try (finally)
                function except (exc aframe args)
                    cleanup;
                    finally
                        xfunc exc
                set-exception-handler! except
                let result =
                    func;
                cleanup;
                result

    .. scope
        tableof
            : xpcall
            : bangra
            : require find-module
            : iterator
                tag (quote iterator)
            : qualify
                function qualify (tag-type value)
                    assert (== (typeof tag-type) type)
                        error "type argument expected."
                    bitcast
                        tag-type (typeof value)
                        value

            : disqualify
                function disqualify (tag-type value)
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

            : and
                make-expand-multi-op-ltr and
            : or
                make-expand-multi-op-ltr or
            : max
                make-expand-multi-op-ltr
                    function (a b)
                        ? (> b a) b a
            : min
                make-expand-multi-op-ltr
                    function (a b)
                        ? (< b a) b a
            : @
                make-expand-multi-op-ltr @
            ///
                # a mid-fi version of let that avoids creating continuations when
                # all values are constants
                : let
                    block-scope-macro
                        function (topexpr scope)
                            let expr = (@ topexpr 0)
                            let rest = (slice topexpr 1)
                            if (!= (typeof (@ expr 2)) symbol)
                                error "syntax: let <var> = <expr>"
                            if (!= (@ expr 2) (quote =))
                                error "syntax: let <var> = <expr>"
                            let param-name = (@ expr 1)
                            let value-expr = (slice expr 3)
                            let param = (parameter param-name)
                            set-key! scope param-name param
                            tupleof
                                do
                                    list
                                        cons
                                            cons continuation
                                                cons (list (parameter (quote _)) param)
                                                    rest
                                            value-expr
                                @ expanded 1
            : try
                block-macro
                    function (expr scope)
                        if (not (list-head? (@ expr 1) (quote except)))
                            error "except block missing"
                        cons
                            list xpcall
                                cons continuation
                                    cons (list)
                                        slice (@ expr 0) 1
                                cons continuation
                                    cons
                                        cons (parameter (quote _))
                                            @ (@ expr 1) 1
                                        slice (@ expr 1) 2
                            slice expr 2

            # quasiquote support
            # (qquote expr [...])
            : qquote
                do
                    function qquote-1 (x)
                        if (list-atom? x)
                            list quote x
                        elseif (list-head? x (quote unquote))
                            unwrap-single (slice x 1)
                        elseif (list-head? x (quote qquote))
                            qquote-1 (qquote-1 (@ x 1))
                        elseif (list-atom? (@ x 0))
                            list cons
                                qquote-1 (@ x 0)
                                qquote-1 (slice x 1)
                        elseif (list-head? (@ x 0) (quote unquote-splice))
                            list (do ..)
                                unwrap-single (slice (@ x 0) 1)
                                qquote-1 (slice x 1)
                        else
                            list cons
                                qquote-1 (@ x 0)
                                qquote-1 (slice x 1)
                    block-macro
                        function (expr)
                            cons
                                ? (empty? (slice (@ expr 0) 2))
                                    qquote-1 (@ (@ expr 0) 1)
                                    qquote-1 (slice (@ expr 0) 1)
                                slice expr 1

            : define
                block-macro
                    function (expr scope)
                        let name =
                            @ (@ expr 0) 1
                        let exprlist =
                            slice (@ expr 0) 2
                        let subscope =
                            parameter (quote scope)
                        cons
                            list syntax-extend (list (parameter (quote _)) subscope)
                                list let (quote name) (quote =)
                                    cons do exprlist
                                list set-key! subscope (list quote name) (quote name)
                                subscope
                            slice expr 1

            tupleof scope-list-wildcard-symbol
                function (topexpr scope)
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
                                none? (find-scope-symbol scope head)
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
                                list continuation (list (parameter (quote _)) self)
                                    cons
                                        list (do @) self
                                            list quote name
                                        cons self rest
                                self-arg
                            slice topexpr 1
                    # infix operator support
                    elseif (has-infix-ops scope expr)
                        cons
                            @
                                parse-infix-expr scope
                                    \ (@ expr 0) (slice expr 1) 0
                                0
                            slice topexpr 1
            tupleof scope-symbol-wildcard-symbol
                function (topexpr scope)
                    let sym =
                        @ topexpr 0
                    let it =
                        iter-r
                            string sym
                    function finalize-head (out)
                        cons
                            symbol
                                @ out 0
                            slice out 1
                    # return tokenized list if string contains a dot
                    # and it's not the concat operator
                    if
                        and
                            none? (find-scope-symbol scope sym)
                            fold it false
                                function (out k)
                                    if (== k ".") true
                                    else out
                        cons
                            finalize-head
                                fold it (list "")
                                    function (out k)
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

            syntax-infix-op : (syntax-infix-rules 70 > :)
            syntax-infix-op or (syntax-infix-rules 100 > or)
            syntax-infix-op and (syntax-infix-rules 200 > and)
            syntax-infix-op | (syntax-infix-rules 240 > |)
            syntax-infix-op ^ (syntax-infix-rules 250 > ^)
            syntax-infix-op & (syntax-infix-rules 260 > &)
            syntax-infix-op < (syntax-infix-rules 300 > <)
            syntax-infix-op > (syntax-infix-rules 300 > >)
            syntax-infix-op <= (syntax-infix-rules 300 > <=)
            syntax-infix-op >= (syntax-infix-rules 300 > >=)
            syntax-infix-op != (syntax-infix-rules 300 > !=)
            syntax-infix-op == (syntax-infix-rules 300 > ==)
            #syntax-infix-op is (syntax-infix-rules 300 > is)
            syntax-infix-op .. (syntax-infix-rules 400 < ..)
            syntax-infix-op << (syntax-infix-rules 450 > <<)
            syntax-infix-op >> (syntax-infix-rules 450 > >>)
            syntax-infix-op - (syntax-infix-rules 500 > -)
            syntax-infix-op + (syntax-infix-rules 500 > +)
            syntax-infix-op % (syntax-infix-rules 600 > %)
            syntax-infix-op / (syntax-infix-rules 600 > /)
            syntax-infix-op // (syntax-infix-rules 600 > //)
            syntax-infix-op * (syntax-infix-rules 600 > *)
            syntax-infix-op ** (syntax-infix-rules 700 < **)
            syntax-infix-op . (syntax-infix-rules 800 > .)
            syntax-infix-op @ (syntax-infix-rules 800 > @)
            #syntax-infix-op .= (syntax-infix-rules 800 > .=)
            #syntax-infix-op @= (syntax-infix-rules 800 > @=)
            #syntax-infix-op =@ (syntax-infix-rules 800 > =@)

syntax-extend stage-4 (_ scope)
    .. scope
        tableof
            : let
                function =? (x)
                    and
                        == (typeof x) symbol
                        == x (quote =)

                # support for multiple declarations in one let scope
                block-macro
                    function (expr scope)
                        let args = (slice (@ expr 0) 1)
                        let argtype = (typeof (@ args 0))
                        if (== argtype symbol)
                            if (=? (@ args 1))
                                # regular form with support for recursion
                                cons
                                    cons let args
                                    slice expr 1
                            else
                                # unpacking multiple variables, without recursion

                                # iterate until we hit the = symbol, after which
                                # the body follows
                                function find-body (expr)

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
                                        cons continuation
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
                                    cons continuation
                                        cons
                                            list
                                                parameter (quote _)
                                                @ args 0
                                            slice expr 1
                                    slice args 2
                        else
                            # multiple variables with support for recursion,
                            # and later variables can depend on earlier ones

                            # prepare quotable values from declarations
                            function handle-pairs (pairs)
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

syntax-extend stage-5 (_ scope)

    function iterator? (x)
        (typeof x) < iterator

    function countable-rslice-iter (l)
        if ((countof l) != 0)
            tupleof (@ l 0) (slice l 1)

    function countable-iter (x)
        let c i = x
        if (i < (countof c))
            tupleof (@ c i) (tupleof c (i + 1))

    function table-iter (x)
        let t = (@ x 0)
        let key-value =
            next-key t
                @ x 1
        if (not (none? key-value))
            tupleof key-value
                tupleof t (@ key-value 0)

    function gen-yield-iter (callee)
        let caller-return = none
        function yield-iter (ret)
            # store caller continuation in state
            set! caller-return return
            if (none? ret) # first invocation
                # invoke callee with yield function as first argument
                callee
                    continuation (ret value)
                        # continue caller
                        caller-return
                            tupleof value ret
                # callee has returned for good
                # resume caller - we're done here.
                contcall none caller-return none
            else # continue callee
                contcall none ret

        qualify iterator
            tupleof yield-iter none

    function iter (x)
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

    function range (a b c)
        let step = (? (none? c) 1 c)
        let from = (? (none? b) 0 a)
        let to = (? (none? b) a b)
        qualify iterator
            tupleof
                function (x)
                    if (< x to)
                        tupleof x (+ x step)
                from

    function zip (a b)
        let iter-a init-a = (disqualify iterator (iter a))
        let iter-b init-b = (disqualify iterator (iter b))
        qualify iterator
            tupleof
                function (x)
                    let state-a = (iter-a (@ x 0))
                    let state-b = (iter-b (@ x 1))
                    if (not (or (none? state-a) (none? state-b)))
                        let at-a next-a = state-a
                        let at-b next-b = state-b
                        tupleof
                            tupleof at-a at-b
                            tupleof next-a next-b
                tupleof init-a init-b

    function infrange (a b)
        let step = (? (none? b) 1 b)
        let from = (? (none? a) 0 a)
        qualify iterator
            tupleof
                function (x)
                    tupleof x (+ x step)
                from

    function enumerate (x from step)
        zip (infrange from step) (iter x)

    function =? (x)
        and
            == (typeof x) symbol
            == x (quote =)

    function parse-loop-args (fullexpr)
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
                            (countof elem) >= 3
                            =? (@ elem 1)
                        error "illegal initializer"
                    tupleof
                        cons (cons do (slice elem 2)) args
                        cons (@ elem 0) names
    .. scope
        tableof
            : iter
            : iterator?
            : range
            : zip
            : enumerate
            : loop # better loop with support for initializers
                macro
                    function loop (expr)
                        let param-repeat = (quote repeat)
                        let args names =
                            parse-loop-args (@ expr 1)
                        list do
                            list let param-repeat (quote =) none
                            list set! param-repeat
                                cons continuation
                                    cons
                                        cons
                                            parameter (quote _)
                                            names
                                        slice expr 2
                            cons param-repeat
                                args
            : for
                block-macro
                    function (block-expr)
                        function iter-expr (expr)
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

                        function generate-template (body extra-args extra-names)
                            let param-iter = (parameter (quote iter))
                            let param-state = (parameter (quote state))
                            let param-for = (parameter (quote for-loop))
                            let param-at-next = (parameter (quote at-next))
                            cons
                                qquote
                                    do
                                        let (unquote param-for) = none
                                        set! (unquote param-for)
                                            continuation (
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
                                                            continuation (
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

syntax-extend stage-final (_ scope)
    set-globals! scope
    scope

none
