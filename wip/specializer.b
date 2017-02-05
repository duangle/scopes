# specializer: specializes and translates flow graph to LLVM

let LAMBDA_CHAR "λ"

function flow-label (flow)
    .. LAMBDA_CHAR
        string
            flow-name flow
        string
            flow-id flow

function dump-flow (frame flow)
    print frame
    ..
        do
            let pcount
                flow-parameter-count flow
            let idx 0
            let s
                .. (flow-label flow) " ("
            loop (idx s)
                if (idx < pcount)
                    let param
                        flow-parameter flow idx
                    # param.flow param.index param.type
                    repeat
                        idx + 1
                        .. s
                            ? (idx == 0) "" " "
                            \ "%" (string param.index) #":" (string param.type)
                else
                    s .. "):"
        do
            let acount
                flow-argument-count flow
            let idx 0
            let s ""
            loop (idx s)
                if (idx < acount)
                    let arg
                        frame-eval frame idx
                            flow-argument flow idx
                    repeat
                        idx + 1
                        .. s " "
                            if ((typeof arg) == parameter)
                                ..
                                    ? (arg.flow == flow) ""
                                        .. (flow-label arg.flow) "."
                                    "%"
                                    string arg.index
                            else
                                repr arg
                else s

function dump-function (f)
    dump-flow f.frame f.entry

#### test #####

let HELLO_WORLD
    .. "hello " "world"
function test1 (x)
    print
        ? x HELLO_WORLD ""

print
    dump-function test1
