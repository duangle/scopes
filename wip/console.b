# a REPL console for bangra

let void* =
    pointer void
let NULL =
    bitcast void*
        uint64 0

function null? (x)
    (bitcast uint64 x) == 0

let C =
    import-c "mycoolfile.c"
        tupleof;
        "
        typedef unsigned long long size_t;
        //int printf ( const char * format, ... );
        void* malloc (size_t size);
        "

print
    external (quote readline) void*

#ssize_t getline(char **lineptr, size_t *n, FILE *stream);

