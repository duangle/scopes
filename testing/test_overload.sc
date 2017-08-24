

# todo

#print
    tuple
        va-types 1 2 "hi" (tupleof 1 true)

#    set-type-symbol! Parameter 'apply-type
        fn (cls params...)
            let param1 param2 param3 = params...
            let TT = (tuple (typeof param1) (typeof param2) (typeof param3))
            if (type== TT (tuple Anchor Symbol type))
                Parameter-new param1 param2 param3
            elseif (type== TT (tuple Anchor Symbol Nothing))
                Parameter-new param1 param2 Unknown
            elseif (type== TT (tuple Symbol type Nothing))
                Parameter-new (active-anchor) param1 param2
            elseif (type== TT (tuple Symbol Nothing Nothing))
                Parameter-new (active-anchor) param1 Unknown
            else
                compiler-error! "usage: Parameter [anchor] symbol [type]"
