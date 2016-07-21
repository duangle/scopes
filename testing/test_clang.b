IR

include "../macros.b"
include "../libc.b"

defvalue sourcecode
    &str "#line 7 \"test_clang.b\"
#include <stdbool.h>
#include <stdint.h>

typedef struct _Q1 Q1;
typedef struct _Q2 Q2;

class X {
public:
    void method();
};

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

enum XX { XA, XB };
XX test4();

extern struct { float v[3]; } somevar;

extern \"C\" {

int embedded_func() {
    return 303;
}

} // extern C

"

declare embedded_func
    function i32

run
    defvalue dest
        call ref
            null Value
    defvalue argc 5
    defvalue opts
        alloca rawstring argc
    store
        &str "-I/usr/lib/gcc/x86_64-linux-gnu/5/include"
        getelementptr opts 0
    store
        &str "-I/usr/local/include"
        getelementptr opts 1
    store
        &str "-I/usr/lib/gcc/x86_64-linux-gnu/5/include-fixed"
        getelementptr opts 2
    store
        &str "-I/usr/include/x86_64-linux-gnu"
        getelementptr opts 3
    store
        &str "-I/usr/include"
        getelementptr opts 4
    defvalue MI
        call import-c-string dest
            sourcecode
            &str "memfile.cpp"
            opts #bitcast opts (* (* i8))
            argc
    call link-llvm-module env MI
    call dump-value dest
    defvalue dest2
        call ref
            null Value
    defvalue M
        call import-c-module dest2
            &str "../bangra.h"
            #@str "../clang/include/llvm-c/Core.h"
            null (pointer rawstring)
            0
    call dump-value dest2

run
    call printf
        &str "embedded_func() == %i\n"
        call embedded_func
