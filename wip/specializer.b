# specializer: specializes and translates flow graph to LLVM

function ANSI-color (num bright)
    .. "\x1b["
        string num
        ? bright ";1m" "m"

let
    ANSI_RESET              = (ANSI-color 0  false)
    ANSI_COLOR_BLACK        = (ANSI-color 30 false)
    ANSI_COLOR_RED          = (ANSI-color 31 false)
    ANSI_COLOR_GREEN        = (ANSI-color 32 false)
    ANSI_COLOR_YELLOW       = (ANSI-color 33 false)
    ANSI_COLOR_BLUE         = (ANSI-color 34 false)
    ANSI_COLOR_MAGENTA      = (ANSI-color 35 false)
    ANSI_COLOR_CYAN         = (ANSI-color 36 false)
    ANSI_COLOR_GRAY60       = (ANSI-color 37 false)

    ANSI_COLOR_GRAY30       = (ANSI-color 30 true)
    ANSI_COLOR_XRED         = (ANSI-color 31 true)
    ANSI_COLOR_XGREEN       = (ANSI-color 32 true)
    ANSI_COLOR_XYELLOW      = (ANSI-color 33 true)
    ANSI_COLOR_XBLUE        = (ANSI-color 34 true)
    ANSI_COLOR_XMAGENTA     = (ANSI-color 35 true)
    ANSI_COLOR_XCYAN        = (ANSI-color 36 true)
    ANSI_COLOR_WHITE        = (ANSI-color 37 true)

    ANSI_STYLE_STRING       = ANSI_COLOR_XMAGENTA
    ANSI_STYLE_NUMBER       = ANSI_COLOR_XGREEN
    ANSI_STYLE_KEYWORD      = ANSI_COLOR_XBLUE
    ANSI_STYLE_OPERATOR     = ANSI_COLOR_XCYAN
    ANSI_STYLE_INSTRUCTION  = ANSI_COLOR_YELLOW
    ANSI_STYLE_TYPE         = ANSI_COLOR_XYELLOW
    ANSI_STYLE_COMMENT      = ANSI_COLOR_GRAY30
    ANSI_STYLE_ERROR        = ANSI_COLOR_XRED
    ANSI_STYLE_LOCATION     = ANSI_COLOR_XCYAN

    ANSI-wrapper =
        ? support-ANSI?
            function (code)
                function (content)
                    .. code content ANSI_RESET
            function (code)
                function (content) content

    style-string        = (ANSI-wrapper ANSI_COLOR_XMAGENTA)
    style-number        = (ANSI-wrapper ANSI_COLOR_XGREEN)
    style-keyword       = (ANSI-wrapper ANSI_COLOR_XBLUE)
    style-operator      = (ANSI-wrapper ANSI_COLOR_XCYAN)
    style-instruction   = (ANSI-wrapper ANSI_COLOR_YELLOW)
    style-type          = (ANSI-wrapper ANSI_COLOR_XYELLOW)
    style-comment       = (ANSI-wrapper ANSI_COLOR_GRAY30)
    style-error         = (ANSI-wrapper ANSI_COLOR_XRED)
    style-location      = (ANSI-wrapper ANSI_COLOR_XCYAN)

    LAMBDA_CHAR =
        style-keyword "λ"

function flow-label (aflow)
    .. LAMBDA_CHAR
        string
            flow-name aflow
        style-operator "$"
        string
            flow-id aflow

function closure-label (aclosure)
    ..
        style-comment "<"
        flow-label aclosure.entry
        style-comment ">"

function flow-iter-arguments (aflow aframe)
    let acount =
        flow-argument-count aflow
    tupleof
        function (i)
            if (i < acount)
                let arg =
                    flow-argument aflow i
                tupleof
                    structof
                        : index i
                        : argument
                            arg
                            /// ? ((typeof arg) == parameter)
                                frame-eval aframe i arg
                                arg

                    i + 1
            else none
        0

function param-label (aparam)
    .. (style-keyword "@")
        string aparam.name
        style-operator "$"
        string aparam.index

function flow-decl-label (aflow aframe)
    ..
        do
            let
                pcount =
                    flow-parameter-count aflow
                idx = 0
                s =
                    .. (flow-label aflow) " "
                        style-operator "("
            loop (idx s)
                if (idx < pcount)
                    let param =
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
        "\n    "
        do
            @
                fold (flow-iter-arguments aflow aframe) (tupleof "" 0)
                    function (out k)
                        let
                            arg = k.argument
                            argtype =
                                typeof arg
                            i =
                                out @ 1
                        tupleof
                            ..
                                out @ 0
                                ? (i == 1)
                                    style-operator " <- "
                                    " "
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
                            i + 1
                0

function dump-function (afunc)
    let visited = (table)
    function dump-closure (aclosure)
        function dump-flow (aflow aframe)
            if (none? (visited @ aflow))
                print
                    flow-decl-label aflow aframe
                set-key! visited aflow true
                fold (flow-iter-arguments aflow aframe) true
                    function (out k)
                        let arg = k.argument
                        let argtype =
                            typeof arg
                        if (argtype == closure)
                            dump-closure arg
                        elseif (argtype == flow)
                            dump-flow arg aframe
                        true
        dump-flow aclosure.entry aclosure.frame
    dump-closure afunc

#### test #####

function pow2 (x)
    * x x

function pow (x n)
    if (n == 0) 1
    elseif ((n % 2) == 0) (pow2 (pow x (n // 2)))
    else
        x * (pow x (n - 1))

assert
    (pow 2 5) == 32

dump-function pow

