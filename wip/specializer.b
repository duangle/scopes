# specializer: specializes and translates flow graph to LLVM

function ANSI-color (num bright)
    .. "\x1b["
        string num
        ? bright ";1m" "m"

let
    ANSI_RESET              (ANSI-color 0  false)
    ANSI_COLOR_BLACK        (ANSI-color 30 false)
    ANSI_COLOR_RED          (ANSI-color 31 false)
    ANSI_COLOR_GREEN        (ANSI-color 32 false)
    ANSI_COLOR_YELLOW       (ANSI-color 33 false)
    ANSI_COLOR_BLUE         (ANSI-color 34 false)
    ANSI_COLOR_MAGENTA      (ANSI-color 35 false)
    ANSI_COLOR_CYAN         (ANSI-color 36 false)
    ANSI_COLOR_GRAY60       (ANSI-color 37 false)

    ANSI_COLOR_GRAY30       (ANSI-color 30 true)
    ANSI_COLOR_XRED         (ANSI-color 31 true)
    ANSI_COLOR_XGREEN       (ANSI-color 32 true)
    ANSI_COLOR_XYELLOW      (ANSI-color 33 true)
    ANSI_COLOR_XBLUE        (ANSI-color 34 true)
    ANSI_COLOR_XMAGENTA     (ANSI-color 35 true)
    ANSI_COLOR_XCYAN        (ANSI-color 36 true)
    ANSI_COLOR_WHITE        (ANSI-color 37 true)

let
    ANSI_STYLE_STRING       ANSI_COLOR_XMAGENTA
    ANSI_STYLE_NUMBER       ANSI_COLOR_XGREEN
    ANSI_STYLE_KEYWORD      ANSI_COLOR_XBLUE
    ANSI_STYLE_OPERATOR     ANSI_COLOR_XCYAN
    ANSI_STYLE_INSTRUCTION  ANSI_COLOR_YELLOW
    ANSI_STYLE_TYPE         ANSI_COLOR_XYELLOW
    ANSI_STYLE_COMMENT      ANSI_COLOR_GRAY30
    ANSI_STYLE_ERROR        ANSI_COLOR_XRED
    ANSI_STYLE_LOCATION     ANSI_COLOR_XCYAN

let ANSI-wrapper
    ? support-ANSI?
        function (code)
            function (content)
                .. code content ANSI_RESET
        function (code)
            function (content) content
let
    style-string        (ANSI-wrapper ANSI_COLOR_XMAGENTA)
    style-number        (ANSI-wrapper ANSI_COLOR_XGREEN)
    style-keyword       (ANSI-wrapper ANSI_COLOR_XBLUE)
    style-operator      (ANSI-wrapper ANSI_COLOR_XCYAN)
    style-instruction   (ANSI-wrapper ANSI_COLOR_YELLOW)
    style-type          (ANSI-wrapper ANSI_COLOR_XYELLOW)
    style-comment       (ANSI-wrapper ANSI_COLOR_GRAY30)
    style-error         (ANSI-wrapper ANSI_COLOR_XRED)
    style-location      (ANSI-wrapper ANSI_COLOR_XCYAN)

let
    LAMBDA_CHAR
        style-keyword "λ"

function flow-label (aflow)
    .. LAMBDA_CHAR
        string
            flow-name aflow
        string
            flow-id aflow

function closure-label (aclosure)
    ..
        style-comment "<"
        flow-label aclosure.entry
        style-comment ">"

function flow-iter-arguments (aflow aframe)
    let acount
        flow-argument-count aflow
    tupleof
        function (i)
            if (i < acount)
                let arg
                    flow-argument aflow i
                tupleof
                    structof
                        : index i
                        : argument
                            ? ((typeof arg) == parameter)
                                frame-eval aframe i arg
                                arg

                    i + 1
            else none
        0

function param-label (aparam)
    .. (style-keyword "@")
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
                    .. (flow-label aflow) " "
                        style-operator "("
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
                    .. s
                        style-operator "):"
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
                                    flow-label arg.flow
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
                true
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

print test1

print
    dump-function test1
