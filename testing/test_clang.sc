

#fn testfunc (x y)
    x * y
let lib =
    import-c "lib.c" "
    int testfunc (int x, int y) {
        return x * y;
    }
    " '()
let testfunc = lib.testfunc
assert ((testfunc 2 3) == 6)
