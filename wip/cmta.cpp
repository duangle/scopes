// gcc -o - -P -E cmta.cpp

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wvarargs"

#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define DEC(x) PRIMITIVE_CAT(DEC_, x)
#define DEC_0 0
#define DEC_1 0
#define DEC_2 1
#define DEC_3 2
#define DEC_4 3
#define DEC_5 4
#define DEC_6 5
#define DEC_7 6
#define DEC_8 7
#define DEC_9 8

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

#define GETARG(N, ...) PRIMITIVE_CAT(GETARG_, N)(__VA_ARGS__)
#define GETARG_0(_0, ...) _0
#define GETARG_1(_0, _1, ...) _1
#define GETARG_2(_0, _1, _2, ...) _2
#define GETARG_3(_0, _1, _2, _3, ...) _3
#define GETARG_4(_0, _1, _2, _3, _4, ...) _4
#define GETARG_5(_0, _1, _2, _3, _4, _5, ...) _5
#define GETARG_6(_0, _1, _2, _3, _4, _5, _6, ...) _6
#define GETARG_7(_0, _1, _2, _3, _4, _5, _6, _7, ...) _7
#define GETARG_8(_0, _1, _2, _3, _4, _5, _6, _7, _8, ...) _8
#define GETARG_9(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, ...) _9
#define TAIL(...) \
    GETARG(DEC(COUNT_VARARGS(__VA_ARGS__)), __VA_ARGS__)
#define SEMITAIL(...) \
    GETARG(DEC(DEC(COUNT_VARARGS(__VA_ARGS__))), __VA_ARGS__)

#define CHECK_N(x, n, ...) n
#define CHECK(...) CHECK_N(__VA_ARGS__, 0,)
#define PROBE(x) x, 1,

#define IS_PAREN(x) CHECK(IS_PAREN_PROBE x)
#define IS_PAREN_PROBE(...) PROBE(~)

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

struct Closure;
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
Any wrap(Closure *x) { Any r; r.type = TYPE_Closure; r.closure = (Closure *)x; return r; }
const Any &wrap(const Any &x) { return x; }
Any &wrap(Any &x) { return x; }
template<typename T>
Any wrap(T &src) { return T::_wrap(src); }

struct Closure {
    Function f;
    size_t size;
    Any args[1];
};

template<typename ... Args>
inline Any _call(Closure *cl, Args ... args) {
    return cl->f(cl, sizeof...(args), wrap(args) ...);
}

template<typename ... Args>
inline Any _call(Any &cl, Args ... args) {
    assert(cl.type == TYPE_Closure);
    return cl.closure->f(cl.closure, sizeof...(args), wrap(args) ...);
}

template<typename ... Args>
inline Any _call(Args ... args) {
    return _call(wrap(args) ...);
}

/*
template<typename ... Args>
inline Any _call(Closure &cl, Args ... args) {
    return _call_wrapped(&cl, sizeof...(args), wrap(args) ...);
}*/

template<typename T>
struct _call_struct {
    template<typename ... Args>
    static Any call(Args ... args) {
        return T::run(nullptr, sizeof...(args), wrap(args) ...);
    }
    template<typename ... Args>
    static T capture(Args ... args) {
        return T::capture(wrap(args) ...);
    }
};

#if 1
#define CLOSURE_ASSERT(x) assert(x)
#else
#define CLOSURE_ASSERT(x)
#endif

#define _IS_VARARGS_VARARGS PROBE(~)
#define IS_VARARGS_KW(x) CHECK(CAT(_IS_VARARGS_, x))

#define DEF_EXTRACT_MEMBER(X, NAME) \
    Any &NAME = _self->args[X];
#define DEF_ANY_PARAM(NAME) , IF_ELSE(IS_VARARGS_KW(NAME))(...)(Any NAME)

#define CLOSURE_PARAMS(...) \
    MACRO_FOREACH(DEF_ANY_PARAM, __VA_ARGS__)
#define CLOSURE_UPVARS(...) \
    IF_ELSE(COUNT_VARARGS(__VA_ARGS__))( \
        CLOSURE_ASSERT(_self); \
        size_t numupvars = _self?_self->size:0; \
        CLOSURE_ASSERT(COUNT_VARARGS(__VA_ARGS__) <= numupvars); \
        MACRO_FOREACH_ENUM(DEF_EXTRACT_MEMBER, __VA_ARGS__) \
    )()

#define DEF_CAPTURE_PARAM(X, NAME) \
    IF_ELSE(X)(, Any NAME)(Any NAME)
#define DEF_CAPTURE_WRAP_ARG(NAME) , NAME
#define CLOSURE_CAPTURE_FN(...) \
    Function f; \
    size_t size; \
    Any args[ \
        IF_ELSE(COUNT_VARARGS(__VA_ARGS__))(COUNT_VARARGS(__VA_ARGS__))(1) \
        ]; \
    static this_struct capture( \
        MACRO_FOREACH_ENUM(DEF_CAPTURE_PARAM, __VA_ARGS__) \
        ) { \
        return { (Function)run, COUNT_VARARGS(__VA_ARGS__) \
            MACRO_FOREACH(DEF_CAPTURE_WRAP_ARG, __VA_ARGS__) }; }

#define DEF_GC_ARG(NAME) \
    IF_ELSE(IS_VARARGS_KW(NAME))( \
    )( gc_args[N++] = NAME; )
#define CLOSURE_GC_ARGS(...) \
    Any gc_args[numargs]; int N = 0; \
    MACRO_FOREACH(DEF_GC_ARG, __VA_ARGS__) \
    IF_ELSE(IS_VARARGS_KW(TAIL(__VA_ARGS__)))( \
        CVA_LIST va; \
        CVA_START(va); \
        while (N < numargs) { \
            gc_args[N++] = CVA_ARG(va); \
        } \
        CVA_END(va); \
    )() \
    return GC(_self, numargs, gc_args);
#define CLOSURE_VARARG_DEFS(...) \
    IF_ELSE(IS_VARARGS_KW(TAIL(__VA_ARGS__)))( \
        enum { VARARG_START = DEC(COUNT_VARARGS(__VA_ARGS__)) }; \
        CLOSURE_ASSERT(numargs >= VARARG_START); \
        auto &_va_begin = SEMITAIL(numargs, __VA_ARGS__); \
    )( \
        CLOSURE_ASSERT(numargs == COUNT_VARARGS(__VA_ARGS__)); \
    )
#define CLOSURE_2(NAME, PARAMS, UPVARS) \
    struct NAME { \
    typedef NAME this_struct; \
    CLOSURE_CAPTURE_FN UPVARS \
    static Any _wrap(this_struct &self) { return wrap((Closure *)&self); } \
    static Any run (Closure *_self, size_t numargs \
        CLOSURE_PARAMS PARAMS) { \
        char _stack_marker; char *_stack_addr = &_stack_marker; \
        CLOSURE_VARARG_DEFS PARAMS \
        if (_stack_addr <= g_stack_limit) { \
            CLOSURE_GC_ARGS PARAMS \
        } \
        CLOSURE_UPVARS UPVARS

#define DEF_FN(NAME, PARAMS, UPVARS, ...) \
    CLOSURE_2(NAME, PARAMS, UPVARS) __VA_ARGS__ }}

#define CLOSURE_CAPTURE_UPVARS(...) CAPTURE(_, __VA_ARGS__)
#define LAMBDA_FN(PARAMS, UPVARS, ...) \
    ({ CLOSURE_2(_, PARAMS, UPVARS) __VA_ARGS__ }}; CLOSURE_CAPTURE_UPVARS UPVARS; })

#define FN(ARG0, ...) \
    IF_ELSE(IS_PAREN(ARG0)) \
        (LAMBDA_FN(ARG0, __VA_ARGS__)) \
        (DEF_FN(ARG0, __VA_ARGS__))

typedef va_list CVA_LIST;
#define CVA_START(name) va_start(name, _va_begin)
#define CVA_ARG(name) va_arg(name, Any)
#define CVA_END(name) va_end(name)

#define RET(...) return _call(__VA_ARGS__)
#define CC(T, ...) return _call_struct<T>::call(__VA_ARGS__)
#define CAPTURE(T, ...) _call_struct<T>::capture(__VA_ARGS__)

//#define MAX_STACK_SIZE 0x200000
#define MAX_STACK_SIZE 16384

void dump(Any &value) {
    switch(value.type) {
        case TYPE_I32: {
            printf("int: %i\n", value.i32);
        } break;
        case TYPE_Function: {
            printf("function: %p\n", value.func);
        } break;
        case TYPE_Closure: {
            printf("closure: %p\n", (void *)value.closure);
        } break;
        case TYPE_Void: {
            printf("none\n");
        } break;
        default: break;
    }
}

void walk(Any &value) {
    switch(value.type) {
        case TYPE_Closure: {
            auto cl = value.closure;
            printf("function: %p\n", cl->f);
            printf("%i arguments\n", (int)cl->size);
            for (size_t i = 0; i < cl->size; ++i) {
                printf("  #%i: ", (int)i); dump(cl->args[i]);
            }
            for (size_t i = 0; i < cl->size; ++i) {
                walk(cl->args[i]);
            }
        } break;
        default: break;
    }
}

Any GC(Closure *cl, size_t numargs, Any *args) {
    printf("walk! %p %zu\n", cl, numargs);
    if (cl) {
        Any clany = wrap(cl);
        walk(clany);
    }
    for (size_t i = 0; i < numargs; ++i) {
        printf("#%i: ", (int)i); dump(args[i]);
    }
    for (size_t i = 0; i < numargs; ++i) {
        walk(args[i]);
    }
    return wrap(0);
}

static char *g_stack_limit;

FN(func, (x, y, VARARGS), (a1, a2, a3, a4),
    printf("c=%zu: %i %i %i %i |",
        numupvars,
        a1.i32, a2.i32, a3.i32, a4.i32);
    printf(" a=%zu:", numargs);
    printf(" %i %i >>", x.i32, y.i32);
    CVA_LIST ap;
    CVA_START(ap);
    for (size_t i = VARARG_START; i < numargs; ++i) {
        Any v = CVA_ARG(ap);
        printf(" %i", v.i32);
    }
    CVA_END(ap);
    printf("\n");
);

/*
fn pow2 (x)
    * x x

fn pow (x n)
    if (n == 0) 1
    elseif ((n % 2) == 0) (pow2 (pow x (n // 2)))
    else (x * (pow x (n - 1)))
*/

FN(pow, (x, n, cont), (),
    if (n.i32 == 0) {
        RET(cont.closure, 1); }
    else if ((n.i32 % 2) == 0) {
        CC(pow, x, n.i32 / 2,
            FN((x2), (cont),
                RET(cont.closure, x2.i32 * x2.i32);));}
    else {
        CC(pow, x, n.i32 - 1,
            FN((x2), (x, cont),
                RET(cont.closure, x.i32 * x2.i32);));});

FN(cmain, (), (),
#if 1
    FN(loop, (i, cont), (n),
        auto loop_self = wrap(_self);
        if (i.i32 <= n.i32) {
            CC(pow, 2, i,
                FN((x), (i, cont, loop_self),
                    printf("%i\n", x.i32);
            printf("stack size: %zu bytes\n",
                MAX_STACK_SIZE - (size_t)(_stack_addr - g_stack_limit));
                    RET(loop_self, i.i32 + 1, cont);)); }
        else {
            RET(cont, 0); });

    int n = 30;
    RET(CAPTURE(loop, n), 0,
        FN((x), (),
            printf("stack size: %zu bytes\n",
                MAX_STACK_SIZE - (size_t)(_stack_addr - g_stack_limit));
            exit(0);));
#elif 0
    auto cl = CAPTURE(func, 1, 2, 3, 4);
    RET(cl, 23, 42, 303, 606, 909);
#else
    CC(pow, 2, 16,
        FN((VARARGS), (),
            CVA_LIST ap;
            CVA_START(ap);
            for (size_t i = VARARG_START; i < numargs; ++i) {
                Any v = CVA_ARG(ap);
                printf(" %i", v.i32);
            }
            CVA_END(ap);
            //printf("%i\n", x.i32);
            exit(0);));
#endif
);

int main(int argc, char ** argv) {
    char c = 0; g_stack_limit = &c - MAX_STACK_SIZE;
    cmain::run(nullptr, 0);

    return 0;
}
