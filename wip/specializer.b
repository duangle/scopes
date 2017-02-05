# specializer: specializes and translates flow graph to LLVM

let LAMBDA_CHAR "λ"

function flow-label (aflow)
    .. LAMBDA_CHAR
        string
            flow-name aflow
        string
            flow-id aflow

function closure-label (aclosure)
    .. "<" (flow-label aclosure.entry) ">"

function flow-iter-arguments (aflow aframe)
    let acount
        flow-argument-count aflow
    tupleof
        function (i)
            if (i < acount)
                tupleof
                    structof
                        : index i
                        : argument
                            frame-eval aframe i
                                flow-argument aflow i
                    i + 1
            else none
        0

function param-label (aparam)
    .. "%"
        string aparam.name
        string aparam.index

function flow-decl-label (aflow aframe)
    ..
        do
            let
                pcount
                    flow-parameter-count aflow
                idx 0
                s
                    .. (flow-label aflow) " ("
            loop (idx s)
                if (idx < pcount)
                    let param
                        flow-parameter aflow idx
                    # param.flow param.index param.type
                    repeat
                        idx + 1
                        .. s
                            ? (idx == 0) "" " "
                            param-label param
                else
                    s .. "):"
        do
            fold (flow-iter-arguments aflow aframe) ""
                function (out k)
                    let
                        arg k.argument
                    let argtype
                        typeof arg
                    .. out " "
                        if (argtype == parameter)
                            ..
                                ? (arg.flow == aflow) ""
                                    .. (flow-label arg.flow) "."
                                param-label arg
                        elseif (argtype == closure)
                            closure-label arg
                        elseif (argtype == flow)
                            flow-label arg
                        else
                            repr arg

function dump-function (aclosure)
    function dump-flow (aflow aframe)
        fold (flow-iter-arguments aflow aframe) true
            function (out k)
                let arg k.argument
                let argtype
                    typeof arg
                if (argtype == closure)
                    dump-function arg
                elseif (argtype == flow)
                    dump-flow arg aframe
                out
        print
            flow-decl-label aflow aframe
    dump-flow aclosure.entry aclosure.frame

#### test #####

let HELLO_WORLD
    .. "hello " "world"
function test1 (x)
    let
        k
            not x
        t true
    print
        ? k HELLO_WORLD ""

print
    dump-function test1
