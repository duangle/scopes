IR

include "../macros.b"

defvalue sourcecode
    @str "

typedef _Bool bool;

typedef struct _Q1 Q1;
typedef struct _Q2 Q2;

typedef enum {
    A,
    B,C,D,
    F = 1 << 12,
} E;

typedef enum _EE {
    EA, EB, EC
} EE;

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
typedef struct _TT TT;
TT test2();
struct _TT {
    char *s;
};
struct _TT test3(enum _EE, EE);

enum XX { XA, XB } test4();

extern struct { float v[3]; } somevar;

"

run
    defvalue dest
        call ref
            null Value
    call import-c-string dest
        @str "C-Module"
        sourcecode
        @str "memfile.c"
        null (* rawstring)
        0
    call dump-value dest
    defvalue dest2
        call ref
            null Value
    call import-c-module dest2
        @str "C-Module"
        @str "../bangra.h"
        null (* rawstring)
        0
    call dump-value dest2

