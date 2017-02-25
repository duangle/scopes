# a REPL console for bangra

let C =
    import-c "mycoolfile.c"
        tupleof
            \ "-I../clang/lib/clang/3.9.1/include"
        "
        #include <stdio.h>
        #include <dlfcn.h>
        void* malloc (size_t size);
        "

let void* =
    pointer void
let NULL =
    bitcast void*
        uint64 0

let
    RTLD_LAZY     = 0x0001
    RTLD_NOW      = 0x0002
    RTLD_GLOBAL   = 0x0100
    RTLD_LOCAL    = 0x0000
    RTLD_DEFAULT  = NULL
    RTLD_NEXT     = (bitcast void* (uint64 -1))

function getsym (atype name)
    bitcast atype
        C.dlsym RTLD_DEFAULT (rawstring name)

C.printf
    rawstring "yo! %p\n"
    C.getline

#ssize_t getline(char **lineptr, size_t *n, FILE *stream);

let line =
    bitcast
        pointer rawstring
        C.malloc (sizeof rawstring)


print
    C.getline line (bitcast (pointer size_t) NULL) C.stdin
