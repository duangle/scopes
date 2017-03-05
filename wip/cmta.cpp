
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define INC(x) PRIMITIVE_CAT(INC_, x)
#define INC_0 1
#define INC_1 2
#define INC_2 3
#define INC_3 4
#define INC_4 5
#define INC_5 6
#define INC_6 7
#define INC_7 8
#define INC_8 9
#define INC_9 10

#define CHECK_N(x, n, ...) n
#define CHECK(...) CHECK_N(__VA_ARGS__, 0,)
#define PROBE(x) x, 1,

#define NOT(x) CHECK(PRIMITIVE_CAT(NOT_, x))
#define NOT_0 PROBE(~)

#define IF_ELSE(condition) CAT(_IF_, NOT(NOT(condition)))
#define _IF_1(...) __VA_ARGS__ _IF_1_ELSE
#define _IF_0(...)             _IF_0_ELSE
#define _IF_1_ELSE(...)
#define _IF_0_ELSE(...) __VA_ARGS__

#define _GET_10TH_ARG(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, N, ...) N
#define COUNT_VARARGS(...) \
    _GET_10TH_ARG("ignored", ##__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define _FE_0(_call, ...)
#define _FE_1(_call, x) _call(x)
#define _FE_2(_call, x, ...) _call(x) _FE_1(_call, __VA_ARGS__)
#define _FE_3(_call, x, ...) _call(x) _FE_2(_call, __VA_ARGS__)
#define _FE_4(_call, x, ...) _call(x) _FE_3(_call, __VA_ARGS__)
#define _FE_5(_call, x, ...) _call(x) _FE_4(_call, __VA_ARGS__)
#define _FE_6(_call, x, ...) _call(x) _FE_5(_call, __VA_ARGS__)
#define _FE_7(_call, x, ...) _call(x) _FE_6(_call, __VA_ARGS__)
#define _FE_8(_call, x, ...) _call(x) _FE_7(_call, __VA_ARGS__)
#define _FE_9(_call, x, ...) _call(x) _FE_8(_call, __VA_ARGS__)
#define MACRO_FOREACH(x, ...) \
    CAT(_FE_, COUNT_VARARGS(__VA_ARGS__))(x, ##__VA_ARGS__)
#define _FEN_0(n, _call, ...)
#define _FEN_1(n, _call, x) _call(n,x)
#define _FEN_2(n, _call, x, ...) _call(n,x) _FEN_1(INC(n), _call, __VA_ARGS__)
#define _FEN_3(n, _call, x, ...) _call(n,x) _FEN_2(INC(n), _call, __VA_ARGS__)
#define _FEN_4(n, _call, x, ...) _call(n,x) _FEN_3(INC(n), _call, __VA_ARGS__)
#define _FEN_5(n, _call, x, ...) _call(n,x) _FEN_4(INC(n), _call, __VA_ARGS__)
#define _FEN_6(n, _call, x, ...) _call(n,x) _FEN_5(INC(n), _call, __VA_ARGS__)
#define _FEN_7(n, _call, x, ...) _call(n,x) _FEN_6(INC(n), _call, __VA_ARGS__)
#define _FEN_8(n, _call, x, ...) _call(n,x) _FEN_7(INC(n), _call, __VA_ARGS__)
#define _FEN_9(n, _call, x, ...) _call(n,x) _FEN_8(INC(n), _call, __VA_ARGS__)
#define MACRO_FOREACH_ENUM(x, ...) \
    CAT(_FEN_, COUNT_VARARGS(__VA_ARGS__))(0, x, ##__VA_ARGS__)

enum Type {
    TYPE_Void,
    TYPE_I32,
    TYPE_Function,
    TYPE_Closure,
};


template<size_t count> struct ClosureN;
typedef ClosureN<1> Closure;

struct Any;
typedef Any (*Function)(Closure *, size_t, ...);

struct Any {
    union {
        int32_t i32;
        Function func;
        Closure *closure;
    };
    Type type;
};

Any wrap(int32_t x) { Any r; r.type = TYPE_I32; r.i32 = x; return r; }
Any wrap(Function x) { Any r; r.type = TYPE_Function; r.func = x; return r; }
template<size_t count>
Any wrap(ClosureN<count> &x) { Any r; r.type = TYPE_Closure; r.closure = (Closure *)&x; return r; }
const Any &wrap(const Any &x) { return x; }

template<size_t count>
struct ClosureN {
    size_t size;
    Function f;
    Any args[count];
};

typedef ClosureN<1> Closure;

template<typename F>
static ClosureN<1> closure(F f) {
    return { 0, (Function)f };
}

template<typename F, typename ... Args>
static ClosureN<sizeof ... (Args)> closure(F f, Args ... args) {
    return { sizeof ... (args), (Function)f, wrap(args) ... };
}

template<typename ... Args>
inline Any _call_wrapped(Closure *cl, Args ... args) {
    return cl->f(cl, args ...);
}

template<size_t count, typename ... Args>
inline Any _call(ClosureN<count> *cl, Args ... args) {
    return _call_wrapped((Closure *)cl, sizeof...(args), wrap(args) ...);
}

template<size_t count, typename ... Args>
inline Any _call(ClosureN<count> &cl, Args ... args) {
    return _call_wrapped((Closure *)&cl, sizeof...(args), wrap(args) ...);
}

template<typename ... FArgs, typename ... Args>
inline Any _call(Any (*f)(Closure *, size_t, FArgs ...), Args ... args) {
    return f(nullptr, sizeof...(args), wrap(args) ...);
}

#define _IS_VARARGS_VARARGS PROBE(~)
#define IS_VARARGS_KW(x) CHECK(_IS_VARARGS_ ## x)

#define DEF_EXTRACT_MEMBER(X, NAME) \
    Any &NAME = _self->args[X];
#define DEF_ANY_PARAM(NAME) , IF_ELSE(IS_VARARGS_KW(NAME))(...)(Any NAME)

#define CLOSURE_PARAMS(...) \
    MACRO_FOREACH(DEF_ANY_PARAM, __VA_ARGS__)
#define CLOSURE_UPVARS(...) \
    IF_ELSE(COUNT_VARARGS(__VA_ARGS__))( \
        assert(_self); \
        size_t numupvars = _self?_self->size:0; \
        assert(COUNT_VARARGS(__VA_ARGS__) <= numupvars); \
        MACRO_FOREACH_ENUM(DEF_EXTRACT_MEMBER, __VA_ARGS__) \
    )()
#define CLOSURE_2(NAME, PARAMS, UPVARS) \
    static Any NAME (Closure *_self, size_t numargs \
        CLOSURE_PARAMS PARAMS) { \
        char _stack_marker; char *_stack_addr = &_stack_marker; \
        if (_stack_addr <= g_stack_limit) { \
            printf("stack overflow\n"); \
        } \
        CLOSURE_UPVARS UPVARS

#define CLOSURE_1(NAME, PARAMS) \
    CLOSURE_2(NAME, PARAMS, ())
#define CLOSURE_0(NAME, ...) \
    CLOSURE_2(NAME, (), ())

#define EXPECT_BODY(...) __VA_ARGS__ }
#define FN(NAME, ...) \
    CAT(CLOSURE_, COUNT_VARARGS(__VA_ARGS__))(NAME, __VA_ARGS__) EXPECT_BODY

typedef va_list CVA_LIST;
#define CVA_START_0(x, ...) va_start(x, numargs)
#define CVA_START_1(x, y) va_start(x, y)
#define CVA_START(name, ...) CAT(CVA_START_, COUNT_VARARGS(__VA_ARGS__))(name, __VA_ARGS__)
#define CVA_ARG(name) va_arg(name, Any)
#define CVA_END(name) va_end(name)

#define CC(...) return _call(__VA_ARGS__)

void walk(const Any &value) {
    if (value.type == TYPE_Closure) {
        auto cl = value.closure;
        printf("closure: %p\n", (void *)cl);
        for (size_t i = 0; i < cl->size; ++i) {
            walk(cl->args[i]);
        }
    }
}

static char *g_stack_limit;

FN(func, (VARARGS), (a1, a2, a3, a4)) (
    printf("c=%zu: %i %i %i %i |",
        numupvars,
        a1.i32, a2.i32, a3.i32, a4.i32);
    printf(" a=%zu:", numargs);
    CVA_LIST ap;
    CVA_START(ap);
    for (size_t i = 0; i < numargs; ++i) {
        Any v = CVA_ARG(ap);
        printf(" %i", v.i32);
    }
    CVA_END(ap);
    printf("\n");
)

/*
fn pow2 (x)
    * x x

fn pow (x n)
    if (n == 0) 1
    elseif ((n % 2) == 0) (pow2 (pow x (n // 2)))
    else (x * (pow x (n - 1)))
*/

FN(print, (x)) (
    printf("%i\n", x.i32);
)

FN(pow2, (cont, x)) (

    CC(cont.closure, x.i32 * x.i32);
)

FN(pow_even, (x2), (cont)) (

    CC(pow2, cont, x2);
)

FN(pow_odd, (x2), (cont, x)) (
    CC(cont.closure, x.i32 * x2.i32);
)

FN(pow, (cont, x, n)) (
    if (n.i32 == 0) {
        CC(cont.closure, 1);
    } else if ((n.i32 % 2) == 0) {
        CC(pow, closure(pow_even, cont), x, n.i32 / 2);
    } else {
        CC(pow, closure(pow_odd, cont, x), x, n.i32 - 1);
    }
)

FN(cmain, ()) (
    CC(pow, closure(print), 2, 16);
)

int main(int argc, char ** argv) {
    char c = 0; g_stack_limit = &c - 0x200000;
    printf("limit at %p\n", g_stack_limit);

    cmain(nullptr, 0);

    return 0;
}
