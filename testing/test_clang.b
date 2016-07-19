IR

include "../macros.b"

defvalue sourcecode
    @str "

typedef _Bool bool;

typedef enum {
    A,
    B,C,D,
    F = 1 << 12,
} E;

typedef struct {
    int a[8];
    union {
        char *x;
        struct {
            int b;
            int c;
        };
        struct {
            float f;
        } K;
    };
} T;

typedef void (*testf)(int k, bool g, E value);

T test(int, float, void*);
"

run
    defvalue dest
        call ref
            null Value
    /// call import-c-module dest
        @str "C-Module"
        @str "../bangra.h"
        null (* rawstring)
        0
    call import-c-string dest
        @str "C-Module"
        sourcecode
        @str "memfile.c"
        null (* rawstring)
        0
    call dump-value dest

