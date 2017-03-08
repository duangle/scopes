// gcc -o - -P -E cmta.cpp
// compile with -Os for smallest stack footprint

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <memory.h>
#include <setjmp.h>
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_DECORATE(name) stb_##name
#include "../external/stb_sprintf.h"

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

static char stb_printf_tmp[STB_SPRINTF_MIN];
static char *printf_cb(char * buf, void * user, int len) {
    fwrite (buf, 1, len, (FILE *)user );
    return stb_printf_tmp;
}
static int stb_printf(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    int c = stb_vsprintfcb(printf_cb, stdout, stb_printf_tmp, fmt, va);
    va_end(va);
    return c;
}

enum Type {
    TYPE_Void,
    TYPE_I32,
    TYPE_Function,
    TYPE_BuiltinClosure,
};

struct BuiltinClosure;
struct Any;

typedef void (*Function)(const BuiltinClosure *, size_t, const Any *);

struct Any {
    Type type;
    union {
        int32_t i32;
        Function func;
        const BuiltinClosure *builtin_closure;
        const char *ptr;
    };
};

inline static Any wrap(int32_t x) { Any r; r.type = TYPE_I32; r.i32 = x; return r; }
inline static Any wrap(Function x) { Any r; r.type = TYPE_Function; r.func = x; return r; }
inline static Any wrap(const BuiltinClosure *x) {
    Any r; r.type = TYPE_BuiltinClosure; r.builtin_closure = x; return r; }
inline static Any wrap(BuiltinClosure *x) {
    Any r; r.type = TYPE_BuiltinClosure; r.builtin_closure = x; return r; }
inline static const Any &wrap(const Any &x) { return x; }
inline static Any &wrap(Any &x) { return x; }
template<typename T>
inline static Any wrap(T &src) { return T::_wrap(src); }
template<typename T>
inline static Any wrap(const T &src) { return T::_wrap(src); }

struct BuiltinClosure {
    Function f;
    size_t size;
    Any args[1];
};

template<typename ... Args>
inline static void _call(const BuiltinClosure *cl, Args ... args) {
    Any wrapped_args[] = { wrap(args) ... };
    return cl->f(cl, sizeof...(args), wrapped_args);
}

template<typename ... Args>
inline static void _call(const Any &cl, Args ... args) {
    switch(cl.type) {
    case TYPE_BuiltinClosure: {
        Any wrapped_args[] = { wrap(args) ... };
        return cl.builtin_closure->f(cl.builtin_closure, sizeof...(args), wrapped_args);
    } break;
    case TYPE_Function: {
        Any wrapped_args[] = { wrap(args) ... };
        return cl.func(nullptr, sizeof...(args), wrapped_args);
    } break;
    default: {
        assert(false && "type not callable");
    } break;
    }
}

template<typename T, typename ... Args>
inline static void _call(const T &caller, Args ... args) {
    Any wrapped_args[] = { wrap(args) ... };
    Any cl = wrap(caller);
    assert(cl.type == TYPE_BuiltinClosure);
    return cl.builtin_closure->f(cl.builtin_closure, sizeof...(args), wrapped_args);
}

template<typename T, bool has_upvars>
struct _call_struct {};

template<typename T>
struct _call_struct<T, true> {
    template<typename ... Args>
    inline static T capture(Args ... args) {
        return T::capture(wrap(args) ...);
    }
};

template<typename T>
struct _call_struct<T, false> {
    template<typename ... Args>
    inline static void call(Args ... args) {
        Any wrapped_args[] = { wrap(args) ... };
        return T::run(nullptr, sizeof...(args), wrapped_args);
    }
    inline static Function capture() {
        return T::run;
    }
};

#if 0
#define MAX_STACK_SIZE 0x200000
#else
#define MAX_STACK_SIZE 16384
#endif

static size_t align(size_t offset, size_t align) {
    return (offset + align - 1) & ~(align - 1);
}

static size_t sizeof_payload(const Any &from) {
    switch(from.type) {
        case TYPE_BuiltinClosure:
            if (!from.builtin_closure) return 0;
            return sizeof(BuiltinClosure)
                + sizeof(Any) * from.builtin_closure->size - sizeof(Any);
        default:
            return 0;
    }
}

static char *g_stack_start;
static char *g_stack_limit;
static jmp_buf g_retjmp;
static Any g_contobj;

struct GC_Context {
    char *_stack_addr;
    size_t numpages;
    Any *head;
    Any *head_end;
    char *heap;
    char *heap_end;
    uint64_t *bits;

    void mark_addr(const char *dstptr) {
        size_t offset = g_stack_start - dstptr;
        assert((offset % 8) == 0);
        size_t page = offset / 512;
        size_t bit = (offset / 8) % 64;
        assert (page < numpages);
        assert(!(bits[page] & (1<<bit)));
        bits[page] |= (1<<bit);
    }

    bool is_marked(const char *dstptr) {
        size_t offset = g_stack_start - dstptr;
        assert((offset % 8) == 0);
        size_t page = offset / 512;
        size_t bit = (offset / 8) % 64;
        assert (page < numpages);
        return bits[page] & (1<<bit);
    }

    bool is_on_stack(const char *ptr) {
        return (ptr >= g_stack_limit) && (ptr < g_stack_start);
    }

    void move_and_mark(size_t plsize, Any &from) {
        if (is_marked(from.ptr)) {
            char **dest = (char **)from.ptr;
            from.ptr = *dest;
        } else {
            mark_addr(from.ptr);
            // copy data to new heap
            memcpy(heap_end, from.ptr, plsize);
            // write pointer to old location
            char **dest = (char **)from.ptr;
            *dest = heap_end;
            from.ptr = heap_end;
            heap_end += align(plsize, 8);

            *head_end++ = from;
        }
    }

    void force_move(Any &from) {
        size_t plsize = sizeof_payload(from);
        if (plsize) {
            move_and_mark(plsize, from);
        }
    }

    void move(Any &from) {
        size_t plsize = sizeof_payload(from);
        if (plsize) {
            if (is_on_stack(from.ptr)) {
                move_and_mark(plsize, from);
            }
        }
    }

    GC_Context(char *stack_addr) {
        _stack_addr = stack_addr;
        head = (Any *)malloc(sizeof(Any) * 1024);
        head_end = head;
        heap = (char *)malloc(MAX_STACK_SIZE * 2);
        heap_end = heap;
        numpages = ((MAX_STACK_SIZE + 511) / 512) * 2;
        bits = (uint64_t *)malloc(numpages * sizeof(uint64_t));
        memset(bits, 0, numpages * sizeof(uint64_t));
    }

    ~GC_Context() {
        free(bits);
        free(head);
    }
};

static void resume_closure(const BuiltinClosure *_self, size_t numargs, const Any *_args) {
    assert(numargs == 0);
    assert(_self->size >= 3);
    const Any &f = _self->args[0];
    const Any &cl = _self->args[1];
    const Any &size = _self->args[2];
    const Any *args = _self->args + 3;
    assert (f.type == TYPE_Function);
    assert (cl.type == TYPE_BuiltinClosure);
    assert (size.type == TYPE_I32);
    return f.func(cl.builtin_closure, (size_t)size.i32, args);
}

static Any mark_and_sweep(Any ret) {
    uint64_t _stack_marker;
    GC_Context ctx((char *)&_stack_marker);

    stb_printf("GC!\n");
    ctx.force_move(ret);

    Any *headptr = ctx.head;
    while(headptr < ctx.head_end) {
        Any &val = *headptr;
        switch(val.type) {
        case TYPE_BuiltinClosure:
            if (val.builtin_closure) {
                auto &cl = *const_cast<BuiltinClosure *>(val.builtin_closure);
                for(size_t i = 0; i < cl.size; ++i) {
                    ctx.move(cl.args[i]);
                }
            } break;
        default: break;
        }
        headptr++;
    }

    stb_printf("%zd values / %zd bytes moved\n",
        ctx.head_end - ctx.head,
        ctx.heap_end - ctx.heap);
    return ret;
}

static void GC(Function f, const BuiltinClosure *cl, size_t numargs, const Any *args) {
    // only minor GC, we just build a heap and forget it

    BuiltinClosure *retcl = (BuiltinClosure *)alloca(
        sizeof(BuiltinClosure) + sizeof(Any) * (numargs + 3) - sizeof(Any));
    retcl->size = numargs + 3;
    retcl->f = resume_closure;
    retcl->args[0] = wrap(f);
    retcl->args[1] = wrap(cl);
    retcl->args[2] = wrap((int)numargs);
    for (size_t i = 0; i < numargs; ++i) {
        retcl->args[3 + i] = args[i];
    }

    g_contobj = mark_and_sweep(wrap(retcl));
    longjmp(g_retjmp, 1);
}

static void exit_loop(int code) {
    longjmp(g_retjmp, -1);
}

#if 1
#define CLOSURE_ASSERT(x) assert(x)
#else
#define CLOSURE_ASSERT(x)
#endif

#define _IS_VARARGS_VARARGS PROBE(~)
#define IS_VARARGS_KW(x) CHECK(CAT(_IS_VARARGS_, x))

#define DEF_EXTRACT_MEMBER(X, NAME) \
    const Any &NAME = _self->args[X];
#define DEF_ANY_PARAM(N, NAME)  \
    IF_ELSE(IS_VARARGS_KW(NAME))( \
    )( \
        const Any &NAME = _args[N]; \
    )

#define CLOSURE_UNPACK_PARAMS(...) \
    MACRO_FOREACH_ENUM(DEF_ANY_PARAM, __VA_ARGS__)
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
    IF_ELSE(COUNT_VARARGS(__VA_ARGS__)) (\
    enum { has_upvars = true }; \
    Function f; \
    size_t size; \
    Any args[COUNT_VARARGS(__VA_ARGS__)]; \
    static this_struct capture( \
        MACRO_FOREACH_ENUM(DEF_CAPTURE_PARAM, __VA_ARGS__) \
        ) { \
        return { run, COUNT_VARARGS(__VA_ARGS__) \
            MACRO_FOREACH(DEF_CAPTURE_WRAP_ARG, __VA_ARGS__) }; } \
    ) ( \
    enum { has_upvars = false }; \
    )

#define CLOSURE_VARARG_DEFS(...) \
    IF_ELSE(IS_VARARGS_KW(TAIL(__VA_ARGS__)))( \
        enum { VARARG_START = DEC(COUNT_VARARGS(__VA_ARGS__)) }; \
        CLOSURE_ASSERT(numargs >= VARARG_START); \
    )( \
        CLOSURE_ASSERT(numargs == COUNT_VARARGS(__VA_ARGS__)); \
    )
#define CLOSURE_2(NAME, PARAMS, UPVARS, ...) \
    struct NAME { \
        typedef NAME this_struct; \
        CLOSURE_CAPTURE_FN UPVARS \
        IF_ELSE(COUNT_VARARGS UPVARS) (\
        static Any _wrap(const this_struct &self) { return wrap((const BuiltinClosure *)&self); } \
        static void run (const BuiltinClosure *_self, size_t numargs, const Any *_args) { \
            char _stack_marker; char *_stack_addr = &_stack_marker; \
            if (_stack_addr <= g_stack_limit) { \
                return GC(run, _self, numargs, _args); \
            } \
        ) ( \
        static void run (const BuiltinClosure *, size_t numargs, const Any *_args) { \
            char _stack_marker; char *_stack_addr = &_stack_marker; \
            if (_stack_addr <= g_stack_limit) { \
                return GC(run, nullptr, numargs, _args); \
            } \
        ) \
            CLOSURE_UNPACK_PARAMS PARAMS \
            CLOSURE_VARARG_DEFS PARAMS \
            CLOSURE_UPVARS UPVARS \
            __VA_ARGS__ \
        } \
    }

#define DEF_FN(NAME, PARAMS, UPVARS, ...) \
    CLOSURE_2(NAME, PARAMS, UPVARS, __VA_ARGS__)

#define CLOSURE_CAPTURE_UPVARS(...) CAPTURE(_, __VA_ARGS__)
#define LAMBDA_FN(PARAMS, UPVARS, ...) \
    ({ CLOSURE_2(_, PARAMS, UPVARS, __VA_ARGS__); CLOSURE_CAPTURE_UPVARS UPVARS; })

#define FN(ARG0, ...) \
    IF_ELSE(IS_PAREN(ARG0)) \
        (LAMBDA_FN(ARG0, __VA_ARGS__)) \
        (DEF_FN(ARG0, __VA_ARGS__))

#define VARARG(i) _args[i]

#define RET(...) return _call(__VA_ARGS__)
#define CC(T, ...) return _call_struct<T, T::has_upvars>::call(__VA_ARGS__)
#define CAPTURE(T, ...) _call_struct<T, T::has_upvars>::capture(__VA_ARGS__)


FN(func, (x, y, VARARGS), (a1, a2, a3, a4),
    stb_printf("c=%zu: %i %i %i %i |",
        numupvars,
        a1.i32, a2.i32, a3.i32, a4.i32);
    stb_printf(" a=%zu:", numargs);
    stb_printf(" %i %i >>", x.i32, y.i32);
    for (size_t i = VARARG_START; i < numargs; ++i) {
        Any v = VARARG(i);
        stb_printf(" %i", v.i32);
    }
    stb_printf("\n");
    exit(0);
);

/*
fn pow2 (x)
    * x x

fn pow (x n)
    if (n == 0) 1
    elseif ((n % 2) == 0) (pow2 (pow x (n // 2)))
    else (x * (pow x (n - 1)))
*/

// needlessly continuation-based version of pow so we can stress the stack
FN(pow, (x, n, cont), (),
    if (n.i32 == 0) {
        RET(cont, 1); }
    else if ((n.i32 % 2) == 0) {
        CC(pow, x, n.i32 / 2,
            FN((x2), (cont),
                RET(cont, x2.i32 * x2.i32);));}
    else {
        CC(pow, x, n.i32 - 1,
            FN((x2), (x, cont),
                RET(cont, x.i32 * x2.i32);));});

FN(cmain, (), (),
#if 1
    // print all powers of 2 up to 30 bits
    int n = 30;
    RET(
        FN((i, cont), (n),
            assert(_self);
            auto loop_self = wrap(_self);
            if (i.i32 <= n.i32) {
                CC(pow, 2, i,
                    FN((x), (i, cont, loop_self),
                        assert(_self);
                        stb_printf("%i\n", x.i32);
                        RET(loop_self, i.i32 + 1, cont);)); }
            else {
                RET(cont, 0); }),
        0,
        FN((x), (),
            stb_printf("stack size: %zd bytes\n",
                MAX_STACK_SIZE - (size_t)(_stack_addr - g_stack_limit));
            exit_loop(0);));
#elif 0
    auto cl = CAPTURE(func, 1, 2, 3, 4);
    RET(cl, 23, 42, 303, 606, 909);
#else
    CC(pow, 2, 16,
        FN((VARARGS), (),
            for (size_t i = VARARG_START; i < numargs; ++i) {
                Any v = VARARG(i);
                stb_printf("%i\n", v.i32);
            }
            //stb_printf("%i\n", x.i32);
            exit_loop(0);));
#endif
);

static int run_gc_loop (Any entry) {
    uint64_t c = 0;
    g_stack_start = (char *)&c;
    g_stack_limit = g_stack_start - MAX_STACK_SIZE;

    g_contobj = entry;

    switch(setjmp(g_retjmp)) {
    case 0: // first set
    case 1: _call(g_contobj); break; // loop
    default: break; // abort
    }
    return 0;
}

int main(int argc, char ** argv) {
    stb_printf("%zu\n", sizeof(va_list));

    run_gc_loop(wrap(cmain::run));
    stb_printf("exited.\n");

    return 0;
}
