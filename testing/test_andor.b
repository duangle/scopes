IR

include "../macros.b"

run
    or?
        and?
            and? true
                splice
                    print
                        quote yes\ 1
                    true
            splice
                print
                    quote yes\ 2
                true
        splice
            print
                quote no
            true
