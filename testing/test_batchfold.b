fn filter (pred)
    fn init (nextf)
        fn process (nextf values)
            let arity = (countof values)
            if (arity == 0) # init
                let nextff = (nextf)
                fn (value...)
                    process nextff (tupleof value...)
            elseif (arity == 2) # step: (result input)
                let result input = values
                if (pred input)
                    let nextff = (nextf result input)
                    fn (value...)
                        process nextff (tupleof value...)
                else
                    fn (value...)
                        process nextf (tupleof value...)
            elseif (arity == 1) # complete: (result)
                nextf (splice values)
        fn first (value...)
            process nextf (tupleof value...)

fn map (mapf)
    fn init (nextf)
        fn process (nextf values)
            let arity = (countof values)
            if (arity == 0) # init
                let nextff = (nextf)
                fn (value...)
                    process nextff (tupleof value...)
            elseif (arity == 2) # step: (result input)
                let result input = values
                let nextff = (nextf result (mapf input))
                fn (value...)
                    process nextff (tupleof value...)
            elseif (arity == 1) # complete: (result)
                nextf (splice values)
        fn first (value...)
            process nextf (tupleof value...)

fn limit (n)
    fn init (nextf)
        fn process (nextf i values)
            let arity = (countof values)
            if (arity == 0) # init
                let nextff = (nextf)
                fn (value...)
                    process nextff 0 (tupleof value...)
            elseif (arity == 2) # step: (result input)
                let result input = values
                if (i < n)
                    let nextff = (nextf result input)
                    fn (value...)
                        process nextff (i + 1) (tupleof value...)
                else
                    fn (value...)
                        process nextf i (tupleof value...)
            elseif (arity == 1) # complete: (result)
                nextf (splice values)
        fn first (value...)
            process nextf 0 (tupleof value...)

fn batchfold (xf f init l)
    fn finalize (state values)
        let arity = (countof values)
        if (arity == 0) # init
            fn (values...)
                finalize init (tupleof values...)
        elseif (arity == 2) # step: (result input)
            let result input = values
            let newresult =
                (f result input)
            fn (values...)
                finalize newresult (tupleof values...)
        elseif (arity == 1) # complete: (result)
            state
    fn ret-first (values...)
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

fn compose (funcs...)
    let funcs =
        list funcs...
    fn (values...)
        fn process (funcs values)
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
            fn (x)
                (x % 2) == 0
        map
            fn (x)
                x + 1
        limit 3

::@ print
batchfold
    pipeline
    fn (result input)
        print ">" input
        cons input result
    (list)
    list 1 2 3 4 5 6 7 8 9 10
