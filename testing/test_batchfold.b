function filter (pred)
    function init (nextf)
        function process (nextf values)
            let arity = (countof values)
            if (arity == 0) # init
                let nextff = (nextf)
                function (value...)
                    process nextff (tupleof value...)
            elseif (arity == 2) # step: (result input)
                let result input = values
                if (pred input)
                    let nextff = (nextf result input)
                    function (value...)
                        process nextff (tupleof value...)
                else
                    function (value...)
                        process nextf (tupleof value...)
            elseif (arity == 1) # complete: (result)
                nextf (splice values)
        function first (value...)
            process nextf (tupleof value...)

function map (mapf)
    function init (nextf)
        function process (nextf values)
            let arity = (countof values)
            if (arity == 0) # init
                let nextff = (nextf)
                function (value...)
                    process nextff (tupleof value...)
            elseif (arity == 2) # step: (result input)
                let result input = values
                let nextff = (nextf result (mapf input))
                function (value...)
                    process nextff (tupleof value...)
            elseif (arity == 1) # complete: (result)
                nextf (splice values)
        function first (value...)
            process nextf (tupleof value...)

function limit (n)
    function init (nextf)
        function process (nextf i values)
            let arity = (countof values)
            if (arity == 0) # init
                let nextff = (nextf)
                function (value...)
                    process nextff 0 (tupleof value...)
            elseif (arity == 2) # step: (result input)
                let result input = values
                if (i < n)
                    let nextff = (nextf result input)
                    function (value...)
                        process nextff (i + 1) (tupleof value...)
                else
                    function (value...)
                        process nextf i (tupleof value...)
            elseif (arity == 1) # complete: (result)
                nextf (splice values)
        function first (value...)
            process nextf 0 (tupleof value...)

function batchfold (xf f init l)
    function finalize (state values)
        let arity = (countof values)
        if (arity == 0) # init
            function (values...)
                finalize init (tupleof values...)
        elseif (arity == 2) # step: (result input)
            let result input = values
            let newresult =
                (f result input)
            function (values...)
                finalize newresult (tupleof values...)
        elseif (arity == 1) # complete: (result)
            state
    function ret-first (values...)
        finalize init (tupleof values...)
    let
        init-process =
            xf ret-first
    let
        step-process =
            init-process;
    let state =
        step-process none
    loop (step-process state l)
        if (not (empty? l))
            let next-process =
                step-process state (@ l 0)
            repeat
                next-process
                next-process state
                slice l 1
        else state

function compose (funcs...)
    let funcs =
        list funcs...
    function (values...)
        function process (funcs values)
            let next-funcs =
                slice funcs 1
            if (empty? next-funcs)
                (@ funcs 0)
                    values...
            else
                (@ funcs 0)
                    process next-funcs values
        process funcs (tupleof values...)

let pipeline =
    compose
        filter
            function (x)
                (x % 2) == 0
        map
            function (x)
                x + 1
        limit 3

::@ print
batchfold
    pipeline
    function (result input)
        print ">" input
        cons input result
    (list)
    list 1 2 3 4 5 6 7 8 9 10
