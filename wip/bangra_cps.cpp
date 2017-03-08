/*
Bangra Interpreter
Copyright (c) 2017 Leonard Ritter

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
BEWARE: if you build this with anything else but clang, you will suffer.
*/

#ifndef BANGRA_CPP
#define BANGRA_CPP

//------------------------------------------------------------------------------
// C HEADER
//------------------------------------------------------------------------------

#if defined __cplusplus
extern "C" {
#endif

enum {
    // semver style versioning
    BANGRA_VERSION_MAJOR = 0,
    BANGRA_VERSION_MINOR = 6,
    BANGRA_VERSION_PATCH = 0,
};

extern int bangra_argc;
extern char **bangra_argv;
extern char *bangra_interpreter_path;
extern char *bangra_interpreter_dir;

int bangra_main(int argc, char ** argv);
int print_number(int value);

#if defined __cplusplus
}
#endif

#endif // BANGRA_CPP
#ifdef BANGRA_CPP_IMPL

//------------------------------------------------------------------------------
// SHARED LIBRARY IMPLEMENTATION
//------------------------------------------------------------------------------

// CFF form implemented after
// Leissa et al., Graph-Based Higher-Order Intermediate Representation
// http://compilers.cs.uni-saarland.de/papers/lkh15_cgo.pdf

// some parts of the paper use hindley-milner notation
// https://en.wikipedia.org/wiki/Hindley%E2%80%93Milner_type_system

// more reading material:
// Simple and Effective Type Check Removal through Lazy Basic Block Versioning
// https://arxiv.org/pdf/1411.0352v2.pdf
// Julia: A Fast Dynamic Language for Technical Computing
// http://arxiv.org/pdf/1209.5145v1.pdf


#undef NDEBUG
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#include "mman.h"
#include "stdlib_ex.h"
#include "dlfcn.h"
#include "external/linenoise-ng/include/linenoise.h"
#else
// for backtrace
#include <execinfo.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
//#include "external/linenoise/linenoise.h"
#include "external/linenoise-ng/include/linenoise.h"
#endif
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <libgen.h>
#include <setjmp.h>

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>

#include <ffi.h>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
/*
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/ErrorHandling.h>

#include "llvm/IR/LLVMContext.h"
*/
#include "llvm/IR/Module.h"
/*
#include "llvm/IR/IRBuilder.h"
//#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Casting.h"
*/

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/MultiplexConsumer.h"

#include "external/cityhash/city.cpp"

#include "external/coro/coro.h"

#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_DECORATE(name) stb_##name
#include "external/stb_sprintf.h"

// we're going to use extensions
#pragma GCC diagnostic ignored "-Wvla-extension"
#pragma GCC diagnostic ignored "-Wzero-length-array"
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma GCC diagnostic ignored "-Wembedded-directive"
#pragma GCC diagnostic ignored "-Wgnu-statement-expression"
#pragma GCC diagnostic ignored "-Wc99-extensions"
#pragma GCC diagnostic ignored "-Wmissing-braces"
// this one is only enabled for code cleanup
#pragma GCC diagnostic ignored "-Wunused-function"

typedef struct stb_printf_ctx {
    FILE *dest;
    char tmp[STB_SPRINTF_MIN];
} stb_printf_ctx;

static char *_printf_cb(char * buf, void * user, int len) {
    stb_printf_ctx *ctx = (stb_printf_ctx *)user;
    fwrite (buf, 1, len, ctx->dest);
    return ctx->tmp;
}
static int stb_vprintf(const char *fmt, va_list va) {
    stb_printf_ctx ctx;
    ctx.dest = stdout;
    return stb_vsprintfcb(_printf_cb, &ctx, ctx.tmp, fmt, va);
}
static int stb_printf(const char *fmt, ...) {
    stb_printf_ctx ctx;
    ctx.dest = stdout;
    va_list va;
    va_start(va, fmt);
    int c = stb_vsprintfcb(_printf_cb, &ctx, ctx.tmp, fmt, va);
    va_end(va);
    return c;
}

static int stb_fprintf(FILE *out, const char *fmt, ...) {
    stb_printf_ctx ctx;
    ctx.dest = out;
    va_list va;
    va_start(va, fmt);
    int c = stb_vsprintfcb(_printf_cb, &ctx, ctx.tmp, fmt, va);
    va_end(va);
    return c;
}

namespace bangra {

// maximum number of arguments to a function
#define BANGRA_MAX_FUNCARGS 256

// GC nursery size
#if 1
#define B_GC_NURSERY_SIZE 0x200000
#else
#define B_GC_NURSERY_SIZE 16384
#endif

//------------------------------------------------------------------------------
// UTILITY MACROS
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------
// ANSI COLOR DEFINES
//------------------------------------------------------------------------------

#define ANSI_RESET "\033[0m"
#define ANSI_COLOR_BLACK "\033[30m"
#define ANSI_COLOR_RED "\033[31m"
#define ANSI_COLOR_GREEN "\033[32m"
#define ANSI_COLOR_YELLOW "\033[33m"
#define ANSI_COLOR_BLUE "\033[34m"
#define ANSI_COLOR_MAGENTA "\033[35m"
#define ANSI_COLOR_CYAN "\033[36m"
#define ANSI_COLOR_GRAY60 "\033[37m"

#define ANSI_COLOR_GRAY30 "\033[30;1m"
#define ANSI_COLOR_XRED "\033[31;1m"
#define ANSI_COLOR_XGREEN "\033[32;1m"
#define ANSI_COLOR_XYELLOW "\033[33;1m"
#define ANSI_COLOR_XBLUE "\033[34;1m"
#define ANSI_COLOR_XMAGENTA "\033[35;1m"
#define ANSI_COLOR_XCYAN "\033[36;1m"
#define ANSI_COLOR_WHITE "\033[37;1m"

#define ANSI_STYLE_STRING ANSI_COLOR_XMAGENTA
#define ANSI_STYLE_NUMBER ANSI_COLOR_XGREEN
#define ANSI_STYLE_KEYWORD ANSI_COLOR_XBLUE
#define ANSI_STYLE_OPERATOR ANSI_COLOR_XCYAN
#define ANSI_STYLE_INSTRUCTION ANSI_COLOR_YELLOW
#define ANSI_STYLE_TYPE ANSI_COLOR_XYELLOW
#define ANSI_STYLE_COMMENT ANSI_COLOR_GRAY30
#define ANSI_STYLE_ERROR ANSI_COLOR_XRED
#define ANSI_STYLE_LOCATION ANSI_COLOR_XCYAN

//------------------------------------------------------------------------------
// ARITHMETIC UTILITIES
//------------------------------------------------------------------------------

static size_t align(size_t offset, size_t align) {
    return (offset + align - 1) & ~(align - 1);
}

//------------------------------------------------------------------------------
// TYPE SYSTEM
//------------------------------------------------------------------------------

typedef uint64_t Symbol;

#define B_MAP_TYPES() \
    T(Void) \
    T(Any) \
    \
    T(Type) \
    T(Array) \
    T(Vector) \
    T(Tuple) \
    T(Pointer) \
    T(CFunction) \
    T(Integer) \
    T(Real) \
    T(Struct) \
    T(Enum) \
    T(Tag) \
    T(Qualifier) \
    T(Splice) \
    T(Quote) \
    T(Macro) \
    \
    T(Bool) \
    T(I8) \
    T(I16) \
    T(I32) \
    T(I64) \
    T(U8) \
    T(U16) \
    T(U32) \
    T(U64) \
    \
    T(R16) \
    T(R32) \
    T(R64) \
    T(R80) \
    \
    T(String) \
    T(Symbol) \
    \
    T(AnchorRef) \
    \
    T(List) \
    T(Table) \
    T(Parameter) \
    T(BuiltinFlow) \
    T(Function) \
    T(Flow) \
    T(SpecialForm) \
    T(BuiltinMacro) \
    T(Frame) \
    T(Closure) \
    T(BuiltinClosure)

enum KnownType {
#define T(X) TYPE_ ## X,
    B_MAP_TYPES()
#undef T

    TYPE_BuiltinCount,
};

#define TYPE_SizeT TYPE_U64

typedef uint64_t TypeAttribute;
struct Type {
protected:
    static Type next_type_id;

    uint64_t _value;

    Type(uint64_t tid) :
        _value(tid) {
    }


    static Type wrap(uint64_t value) {
        return { value };
    }

public:
    Type() {}

    Type(KnownType tid) :
        _value(tid) {
    }

    bool is_builtin() const {
        return _value < TYPE_BuiltinCount;
    }

    KnownType builtin_value() const {
        assert(is_builtin());
        return (KnownType)_value;
    }

    // for std::map support
    bool operator < (Type b) const {
        return _value < b._value;
    }

    bool operator ==(Type b) const {
        return _value == b._value;
    }

    bool operator !=(Type b) const {
        return _value != b._value;
    }

    bool operator ==(KnownType b) const {
        return _value == b;
    }

    bool operator !=(KnownType b) const {
        return _value != b;
    }

    std::size_t hash() const {
        return _value;
    }

    uint64_t value() const {
        return _value;
    }

    static Type create() {
        Type result = next_type_id;
        auto newvalue = next_type_id._value + 1;
        assert(newvalue <= 0xffffffffull);
        next_type_id = Type::wrap(newvalue);
        return result;
    }

    TypeAttribute attrib(Symbol sym) const {
        assert (sym <= 0xffffffffull);
        return (sym << 32) | _value;
    }

};

Type Type::next_type_id = { TYPE_BuiltinCount };

static const char *get_builtin_name(Type type) {
    if (!type.is_builtin()) return "???";
    switch(type.builtin_value()) {
    #define T(X) case TYPE_ ## X: return #X;
        B_MAP_TYPES()
    #undef T
        default:
            assert(false && "illegal builtin value");
    }
    return nullptr;
}

} // namespace bangra
namespace std {
template<>
struct hash<bangra::Type> {
    std::size_t operator()(const bangra::Type & s) const {
        return s.hash();
    }
};
} // namespace std
namespace bangra {

//------------------------------------------------------------------------------
// ANY
//------------------------------------------------------------------------------

static Type TYPE_Rawstring;
static Type TYPE_PVoid;
static Type TYPE_ListTableTuple;

struct Parameter;
struct Flow;
struct Any;
struct Hash;
struct List;
struct Table;
struct Closure;
struct Frame;
struct Cursor;
struct State;
struct BuiltinClosure;
struct Anchor;

template<typename T>
struct Slice {
    const T *ptr;
    size_t count;
};

typedef Slice<char> String;

#define B_FUNC(X) void X(bangra::State *S)
#define B_RCOUNT(S) ((S)->rcount)
#define B_GETFRAME(S) ((S)->frame)
#define B_GETCONT(S) ((S)->rargs[bangra::ARG_Cont])
#define B_GETFUNC(S) ((S)->rargs[bangra::ARG_Func])
#define B_ARGCOUNT(S) ((S)->rcount - bangra::ARG_Arg0)
#define B_GETARG(S, N) ((S)->rargs[bangra::ARG_Arg0 + (N)])
#define B_CALL(S, ...) { \
    const bangra::Any _args[] = { __VA_ARGS__ }; \
    const size_t _count = sizeof(_args) / sizeof(bangra::Any); \
    for (size_t i = 0; i < _count; ++i) { (S)->wargs[i] = _args[i]; } \
    (S)->wcount = _count; \
    }

typedef Any (*BuiltinFunction)(const Any *args, size_t argcount);
typedef B_FUNC((*BuiltinFlowFunction));
typedef Any (*SpecialFormFunction)(const List *, const Any &dest);
typedef void (*Function)(const BuiltinClosure *, size_t, const Any *);

struct Any {
    Type type;
    union {
        // embedded data can also be copied from/to this array
        uint8_t embedded[8];

        bool i1;

        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;

        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;

        size_t size;

        float r32;
        double r64;

        const void *ptr;
        const String *str;
        Symbol symbol;

        BuiltinFlowFunction builtin_flow;
        SpecialFormFunction special_form;
        Function function;
        const BuiltinClosure *builtin_closure;

        const char *c_str;
        const List *list;
        int64_t typeref;
        const Parameter *parameter;
        const Flow *flow;
        const Closure *closure;
        const Frame *frame;
        const Table *table;
        const Anchor *anchorref;
    };
};

static Any make_any(Type type) {
    Any any;
    any.type = type;
    return any;
}

struct Cursor {
    const List *list;
    const Table *scope;
};

struct BuiltinClosure {
    Function function;
    size_t size;
    Any args[1];
};

//------------------------------------------------------------------------------

static const Any none = { TYPE_Void, .ptr = nullptr };

static bool isnone(const Any &value) {
    return (value.type == TYPE_Void);
}

//------------------------------------------------------------------------------

struct State {
    const Any *rargs;
    Any *wargs;
    size_t rcount;
    size_t wcount;
    Frame *frame;
    Any exception_handler;
};

//------------------------------------------------------------------------------

#define DEF_WRAP(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(CTYPE x) { return { TYPE_ ## BTYPE, .MEMBER = x }; }
#define DEF_WRAP_PTR_OR_NULL(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(const CTYPE *x) { return { TYPE_ ## BTYPE, .MEMBER = x}; } \
    inline static Any wrap(CTYPE *x) { return { TYPE_ ## BTYPE, .MEMBER = x}; }
#define DEF_WRAP_PTR(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(const CTYPE &x) { return { TYPE_ ## BTYPE, .MEMBER = &x }; }

DEF_WRAP(bool, Bool, i1);
DEF_WRAP(int8_t, I8, i8);
DEF_WRAP(int16_t, I16, i16);
DEF_WRAP(int32_t, I32, i32);
DEF_WRAP(int64_t, I64, i64);
DEF_WRAP(uint8_t, U8, u8);
DEF_WRAP(uint16_t, U16, u16);
DEF_WRAP(uint32_t, U32, u32);
DEF_WRAP(uint64_t, U64, u64);

DEF_WRAP(float, R32, r32);
DEF_WRAP(double, R64, r64);

DEF_WRAP(Function, Function, function);

DEF_WRAP_PTR(String, String, str);
DEF_WRAP_PTR_OR_NULL(BuiltinClosure, BuiltinClosure, builtin_closure);

inline static Any &wrap(Any &x) { return x; }
template<typename T>
inline static Any wrap(const T &src) { return T::_wrap(src); }
template<typename T>
inline static Any wrap(T &src) { return wrap(const_cast<const T &>(src)); }

//------------------------------------------------------------------------------

template<std::size_t n>
inline static String str(const char (&s)[n]) {
    return { s, n - 1 };
}

inline static String str(const char *s, size_t len) {
    return { s, len };
}

//------------------------------------------------------------------------------

static bool is_type(const Any &value, Type type) {
    return (value.type == type); }

template<typename T> struct extract {};

#define DEF_EXTRACT(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE> { \
        inline CTYPE operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return x.MEMBER; }}
#define DEF_EXTRACT_PTR(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE> { \
        inline const CTYPE &operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return *x.MEMBER; }}
#define DEF_EXTRACT_PTR_OR_NULL(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE *> { \
        inline const CTYPE *operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return x.MEMBER; }}

DEF_EXTRACT(bool, Bool, i1);
DEF_EXTRACT(int8_t, I8, i8);
DEF_EXTRACT(int16_t, I16, i16);
DEF_EXTRACT(int32_t, I32, i32);
DEF_EXTRACT(int64_t, I64, i64);
DEF_EXTRACT(uint8_t, U8, u8);
DEF_EXTRACT(uint16_t, U16, u16);
DEF_EXTRACT(uint32_t, U32, u32);
DEF_EXTRACT(uint64_t, U64, u64);

DEF_EXTRACT(float, R32, r32);
DEF_EXTRACT(double, R64, r64);

DEF_EXTRACT(Function, Function, function);

template<> struct extract<const char *> {
    const char *operator ()(const Any &x) {
        assert(is_type(x, TYPE_String)); assert(x.str);
        return x.str->ptr; }};

DEF_EXTRACT_PTR(String, String, str);
DEF_EXTRACT_PTR_OR_NULL(BuiltinClosure, BuiltinClosure, builtin_closure);

template<typename T>
inline static T unwrap(const Any &value) {
    return extract<T>()(value);
}

//------------------------------------------------------------------------------
// FUNCTION CALL TEMPLATES
//------------------------------------------------------------------------------

template<typename ... Args>
inline static void _call(const BuiltinClosure *cl, Args ... args) {
    Any wrapped_args[] = { wrap(args) ... };
    return cl->function(cl, sizeof...(args), wrapped_args);
}

template<typename ... Args>
inline static void _call(const Any &cl, Args ... args) {
    switch(cl.type.value()) {
    case TYPE_BuiltinClosure: {
        Any wrapped_args[] = { wrap(args) ... };
        return cl.builtin_closure->function(
            cl.builtin_closure, sizeof...(args), wrapped_args);
    } break;
    case TYPE_Function: {
        Any wrapped_args[] = { wrap(args) ... };
        return cl.function(nullptr, sizeof...(args), wrapped_args);
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
    return cl.builtin_closure->function(
        cl.builtin_closure, sizeof...(args), wrapped_args);
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

//------------------------------------------------------------------------------
// GARBAGE COLLECTOR
//------------------------------------------------------------------------------

static size_t sizeof_payload(const Any &from) {
    switch(from.type.value()) {
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
        if (is_marked((const char *)from.ptr)) {
            char **dest = (char **)from.ptr;
            from.ptr = *dest;
        } else {
            mark_addr((const char *)from.ptr);
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
            if (is_on_stack((const char *)from.ptr)) {
                move_and_mark(plsize, from);
            }
        }
    }

    GC_Context(char *stack_addr) {
        _stack_addr = stack_addr;
        head = (Any *)malloc(sizeof(Any) * 1024);
        head_end = head;
        heap = (char *)malloc(B_GC_NURSERY_SIZE * 2);
        heap_end = heap;
        numpages = ((B_GC_NURSERY_SIZE + 511) / 512) * 2;
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
    return f.function(cl.builtin_closure, (size_t)size.i32, args);
}

static Any mark_and_sweep(Any ret) {
    uint64_t _stack_marker;
    GC_Context ctx((char *)&_stack_marker);

    stb_printf("GC!\n");
    ctx.force_move(ret);

    Any *headptr = ctx.head;
    while(headptr < ctx.head_end) {
        Any &val = *headptr;
        switch(val.type.value()) {
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
    retcl->function = resume_closure;
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

static int run_gc_loop (Any entry) {
    uint64_t c = 0;
    g_stack_start = (char *)&c;
    g_stack_limit = g_stack_start - B_GC_NURSERY_SIZE;

    g_contobj = entry;

    switch(setjmp(g_retjmp)) {
    case 0: // first set
    case 1: _call(g_contobj); break; // loop
    default: break; // abort
    }
    return 0;
}

//------------------------------------------------------------------------------
// CLOSURE MACROS
//------------------------------------------------------------------------------

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
        size_t numupvars = (_self?(_self->size):0); \
        CLOSURE_ASSERT(COUNT_VARARGS(__VA_ARGS__) <= numupvars); \
        MACRO_FOREACH_ENUM(DEF_EXTRACT_MEMBER, __VA_ARGS__) \
    )()

#define DEF_CAPTURE_PARAM(X, NAME) \
    IF_ELSE(X)(, Any NAME)(Any NAME)
#define DEF_CAPTURE_WRAP_ARG(NAME) , NAME
#define CLOSURE_CAPTURE_FN(...) \
    enum { has_upvars = true }; \
    Function f; \
    size_t size; \
    Any args[COUNT_VARARGS(__VA_ARGS__)]; \
    static this_struct capture( \
        MACRO_FOREACH_ENUM(DEF_CAPTURE_PARAM, __VA_ARGS__) \
        ) { \
        return { run, COUNT_VARARGS(__VA_ARGS__) \
            MACRO_FOREACH(DEF_CAPTURE_WRAP_ARG, __VA_ARGS__) }; }

#define CLOSURE_VARARG_DEFS(...) \
    IF_ELSE(IS_VARARGS_KW(TAIL(__VA_ARGS__)))( \
        enum { VARARG_START = DEC(COUNT_VARARGS(__VA_ARGS__)) }; \
        CLOSURE_ASSERT(numargs >= VARARG_START); \
    )( \
        CLOSURE_ASSERT(numargs == COUNT_VARARGS(__VA_ARGS__)); \
    )
#define CLOSURE_2(NAME, PARAMS, UPVARS, ...) \
    struct NAME { \
        IF_ELSE(COUNT_VARARGS UPVARS) (\
        typedef NAME this_struct; \
        CLOSURE_CAPTURE_FN UPVARS \
        static Any _wrap(const this_struct &self) { return wrap((const BuiltinClosure *)&self); } \
        static void run (const BuiltinClosure *_self, size_t numargs, const Any *_args) { \
            char _stack_marker; char *_stack_addr = &_stack_marker; \
            if (_stack_addr <= g_stack_limit) { \
                return GC(run, _self, numargs, _args); \
            } \
        ) ( \
        enum { has_upvars = false }; \
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

#define RCLOSURE_2(NAME, PARAMS, ...) \
    struct NAME { \
        enum { has_upvars = false }; \
        static void run (const BuiltinClosure *, size_t numargs, const Any *_args) { \
            CLOSURE_UNPACK_PARAMS PARAMS \
            CLOSURE_VARARG_DEFS PARAMS \
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

#define RFN(ARG0, ...) \
    RCLOSURE_2(ARG0, __VA_ARGS__)

#define VARARG(i) _args[i]

#define RET(...) return _call(__VA_ARGS__)
#define CC(T, ...) return _call_struct<T, T::has_upvars>::call(__VA_ARGS__)
#define RCALL(T, ...) _call_struct<T, T::has_upvars>::call(__VA_ARGS__)
#define CAPTURE(T, ...) _call_struct<T, T::has_upvars>::capture(__VA_ARGS__)

//------------------------------------------------------------------------------
// UTILITY FUNCTIONS
//------------------------------------------------------------------------------

template <typename R, typename... Args>
static std::function<R (Args...)> memo(R (*fn)(Args...)) {
    std::map<std::tuple<Args...>, R> list;
    return [fn, list](Args... args) mutable -> R {
        auto argt = std::make_tuple(args...);
        auto memoized = list.find(argt);
        if(memoized == list.end()) {
            auto result = fn(args...);
            list[argt] = result;
            return result;
        } else {
            return memoized->second;
        }
    };
}

static char parse_hexchar(char c) {
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    } else if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    } else if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    return -1;
}

static char *extension(char *path) {
    char *dot = path + strlen(path);
    while (dot > path) {
        switch(*dot) {
            case '.': return dot; break;
            case '/': case '\\': {
                return NULL;
            } break;
            default: break;
        }
        dot--;
    }
    return NULL;
}

static size_t inplace_unescape(char *buf) {
    char *dst = buf;
    char *src = buf;
    while (*src) {
        if (*src == '\\') {
            src++;
            if (*src == 0) {
                break;
            } if (*src == 'n') {
                *dst = '\n';
            } else if (*src == 't') {
                *dst = '\t';
            } else if (*src == 'r') {
                *dst = '\r';
            } else if (*src == 'x') {
                char c0 = parse_hexchar(*(src + 1));
                char c1 = parse_hexchar(*(src + 2));
                if ((c0 >= 0) && (c1 >= 0)) {
                    *dst = (c0 << 4) | c1;
                    src += 2;
                } else {
                    src--;
                    *dst = *src;
                }
            } else {
                *dst = *src;
            }
        } else {
            *dst = *src;
        }
        src++;
        dst++;
    }
    // terminate
    *dst = 0;
    return dst - buf;
}


#define B_SNFORMAT 512 // how many characters per callback
typedef char *(*vsformatcb_t)(const char *buf, void *user, int len);

static int vsformatcb(vsformatcb_t cb, void *user, char *buf, const char *fmt,
    size_t numargs, const Any *args) {
    assert(buf);
    char *p = buf;
    const Any *arg = args;
#define VSFCB_CHECKWRITE(N) \
    if (((p - buf) + (N)) > B_SNFORMAT) { buf = p = cb(buf, user, p - buf); }
#define VSFCB_PRINT(FMT, ...) { \
        VSFCB_CHECKWRITE(B_SNFORMAT); \
        p += stb_snprintf(p, B_SNFORMAT - (p - buf), FMT, __VA_ARGS__); }
scan:
    for(;;) {
        if (!(*fmt)) goto done;
        if (*fmt == '{') {
            switch(*(fmt+1)) { // peek at next char
                case '}': // {}
                    // pop arg
                    if ((args - arg) < (long)numargs) { // got more args?
                        switch(arg->type.value()) {
                        case TYPE_Bool: VSFCB_PRINT("%s", (arg->i1?"true":"false")); goto success;
                        case TYPE_I8: VSFCB_PRINT("%" PRId8, arg->i8); goto success;
                        case TYPE_I16: VSFCB_PRINT("%" PRId16, arg->i16); goto success;
                        case TYPE_I32: VSFCB_PRINT("%" PRId32, arg->i32); goto success;
                        case TYPE_I64: VSFCB_PRINT("%" PRId64, arg->i64); goto success;
                        case TYPE_U8: VSFCB_PRINT("%" PRIu8, arg->u8); goto success;
                        case TYPE_U16: VSFCB_PRINT("%" PRIu16, arg->u16); goto success;
                        case TYPE_U32: VSFCB_PRINT("%" PRIu32, arg->u32); goto success;
                        case TYPE_U64: VSFCB_PRINT("%" PRIu64, arg->u64); goto success;
                        case TYPE_R32: VSFCB_PRINT("%f", arg->r32); goto success;
                        case TYPE_R64: VSFCB_PRINT("%f", arg->r64); goto success;
                        case TYPE_String: {
                            auto str = unwrap<String>(*arg);
                            int rem = str.count;
                            const char *src = str.ptr;
                            while (rem) {
                                int towrite = (rem > B_SNFORMAT)?B_SNFORMAT:rem;
                                VSFCB_CHECKWRITE(towrite);
                                const char *endptr = src + towrite;
                                while (src != endptr) {
                                    *p++ = *src++;
                                }
                                rem -= towrite;
                            }
                        } goto success;
                        default: {
                            auto tname = get_builtin_name(arg->type);
                            VSFCB_PRINT("<%s 0x%" PRIx64 ">", tname, arg->u64);
                        } goto success;
                        }
                        success: arg++; fmt += 2; goto scan;
                    } // otherwise fall through
                    //failed:
                case 0: // end of string
                case '{': // {{
                default: // no control character, do a regular write
                    break;
            }
        }
        VSFCB_CHECKWRITE(1);
        *p++ = *fmt++;
    }
done:
    VSFCB_CHECKWRITE(B_SNFORMAT); // force flush if non-empty
    return 0;
#undef VSFCB_CHECKWRITE
#undef VSFCB_PRINT
}

struct vsformat_cb_ctx {
    int count;
    char *dest;
    char tmp[B_SNFORMAT];
};

static char *vsformat_cb(const char *buf, void *user, int len) {
    vsformat_cb_ctx *ctx = (vsformat_cb_ctx *)user;
    if (buf != ctx->dest) {
        char *d = ctx->dest;
        char *e = d + len;
        while (d != e) {
            *d++ = *buf++;
        }
    }
    ctx->dest += len;
    return ctx->tmp;
}

static char *vsformat_cb_null(const char *buf, void *user, int len) {
    vsformat_cb_ctx *ctx = (vsformat_cb_ctx *)user;
    ctx->count += len;
    return ctx->tmp;
}

static int vsformat(char *buf, const char *fmt, size_t numargs, const Any *args) {
    vsformat_cb_ctx ctx;
    if (buf) {
        ctx.dest = buf;
        vsformatcb(vsformat_cb, &ctx, ctx.tmp, fmt, numargs, args);
        int l = ctx.dest - buf;
        buf[l] = 0;
        return l;
    } else {
        ctx.count = 0;
        vsformatcb(vsformat_cb_null, &ctx, ctx.tmp, fmt, numargs, args);
        return ctx.count;
    }
}

struct fvsprint_cb_ctx {
    FILE *out;
    char tmp[B_SNFORMAT];
};

static char *fvsprint_cb(const char *buf, void *user, int len) {
    fvsprint_cb_ctx *ctx = (fvsprint_cb_ctx *)user;
    fwrite(buf, 1, len, ctx->out);
    return ctx->tmp;
}

static void fvsprint(FILE *f, const char *fmt, size_t numargs, const Any *args) {
    fvsprint_cb_ctx ctx;
    ctx.out = f;
    vsformatcb(fvsprint_cb, &ctx, ctx.tmp, fmt, numargs, args);
}

static int escapestrcb(vsformatcb_t cb, void *user, char *buf, const String &str,
    const char *quote_chars = nullptr) {
    assert(buf);
    const char *fmt_start = str.ptr;
    const char *fmt = fmt_start;
    char *p = buf;
#define VSFCB_CHECKWRITE(N) \
    if (((p - buf) + (N)) > B_SNFORMAT) { buf = p = cb(buf, user, p - buf); }
#define VSFCB_PRINT(MAXCOUNT, FMT, SRC) { \
        VSFCB_CHECKWRITE(MAXCOUNT+1); \
        p += stb_snprintf(p, B_SNFORMAT - (p - buf), FMT, SRC); }
    for(;;) {
        char c = *fmt;
        switch(c) {
        case '\n': VSFCB_CHECKWRITE(2); *p++ = '\\'; *p++ = 'n'; break;
        case '\r': VSFCB_CHECKWRITE(2); *p++ = '\\'; *p++ = 'r'; break;
        case '\t': VSFCB_CHECKWRITE(2); *p++ = '\\'; *p++ = 't'; break;
        case 0: if ((fmt - fmt_start) == (long)str.count) goto done;
            // otherwise, fall through
        default:
            if ((c < 32) || (c >= 127)) {
                VSFCB_PRINT(4, "\\x%02x", (unsigned char)c);
            } else {
                if ((c == '\\') || (quote_chars && strchr(quote_chars, c))) {
                    VSFCB_CHECKWRITE(1);
                    *p++ = '\\';
                }
                *p++ = c;
            }
            break;
        }
        fmt++;
    }
done:
    VSFCB_CHECKWRITE(B_SNFORMAT); // force flush if non-empty
    return 0;
#undef VSFCB_CHECKWRITE
#undef VSFCB_PRINT
}

static int escapestr(char *buf, const String &str, const char *quote_chars = nullptr) {
    vsformat_cb_ctx ctx;
    if (buf) {
        ctx.dest = buf;
        escapestrcb(vsformat_cb, &ctx, ctx.tmp, str, quote_chars);
        int l = ctx.dest - buf;
        buf[l] = 0;
        return l;
    } else {
        ctx.count = 0;
        escapestrcb(vsformat_cb_null, &ctx, ctx.tmp, str, quote_chars);
        return ctx.count;
    }
}

RFN(print_format, (fmt, VARARGS),
    auto fmtstr = unwrap<const char *>(fmt);
    fvsprint(stdout, fmtstr, numargs - VARARG_START, &VARARG(VARARG_START));
);

RFN(print, (VARARGS),
    for (size_t i = VARARG_START; i < numargs; ++i) {
        if (i != VARARG_START) fputs(" ", stdout);
        fvsprint(stdout, "{}", 1, &VARARG(i));
    }
    fputs("\n", stdout);
);

FN(format, (cont, fmt, VARARGS), (),
    auto fmtstr = unwrap<const char *>(fmt);
    auto size = vsformat(nullptr,
        fmtstr, numargs - VARARG_START, &VARARG(VARARG_START));
    char dest[size + 1];
    vsformat(dest,
        fmtstr, numargs - VARARG_START, &VARARG(VARARG_START));
    RET(cont, str(dest, size)); );

FN(escape_string, (cont, s, VARARGS), (),
    auto sstr = unwrap<String>(s);
    const char *cquote_chars = nullptr;
    if (numargs >= 3) {
        cquote_chars = unwrap<const char *>(VARARG(VARARG_START)); }
    auto size = escapestr(nullptr, sstr, cquote_chars);
    char dest[size + 1];
    escapestr(dest, sstr, cquote_chars);
    RET(cont, str(dest, size)); );

FN(concat_strings, (cont, first, VARARGS), (),
    auto sfirst = unwrap<String>(first);
    // count
    size_t destsize = sfirst.count;
    for (size_t i = VARARG_START; i < numargs; ++i) {
        destsize += unwrap<String>(VARARG(i)).count; }
    // build buffer
    char dest[destsize + 1];
    destsize = sfirst.count;
    memcpy(dest, sfirst.ptr, sfirst.count);
    for (size_t i = VARARG_START; i < numargs; ++i) {
        auto selem = unwrap<String>(VARARG(i));
        memcpy(dest + destsize, selem.ptr, selem.count);
        destsize += selem.count; }
    dest[destsize] = 0;
    RET(cont, str(dest, destsize)); );

static bool support_ansi = false;
FN(color, (cont, code, content), (),
    if (support_ansi) {
        CC(concat_strings, cont, code, content, str(ANSI_RESET)); }
    else {
        RET(cont, content); } );

FN(repr_string, (cont, s), (),
    CC(escape_string,
        FN((s), (cont),
            auto q = str("\"");
            CC(concat_strings,
                FN((s), (cont),
                    CC(color, cont, str(ANSI_STYLE_STRING), s); ),
                q, s, q); ),
        s, str("\"")); );

FN(cmain, (), (),
    RCALL(print_format, str("hi {} {} {}!\n"), 1, 2, cmain::run);
    CC(color,
        FN((s), (),
            stb_printf("%s\n", unwrap<const char *>(s));
            CC(repr_string,
                FN((s), (),
                    stb_printf("'%s'\n", unwrap<const char *>(s)); ),
                str("this \n \r \\ is a \0 s\"t\"ring"));),
        str(ANSI_STYLE_STRING),
        str("the quick brown fox, guys!"));
);

} // namespace bangra

int main(int argc, char ** argv) {

#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
    // turn on ANSI processing
    auto hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    auto hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    GetConsoleMode(hStdOut, &mode);
    SetConsoleMode(hStdOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    GetConsoleMode(hStdErr, &mode);
    SetConsoleMode(hStdErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    // change codepage to UTF-8
    SetConsoleOutputCP(65001);
#endif

    bangra::run_gc_loop(bangra::wrap(bangra::cmain::run));
    printf("exited.\n");

    return 0;
}


#endif // BANGRA_CPP_IMPL
