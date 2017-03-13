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

typedef float real32_t;
typedef double real64_t;

typedef struct stb_printf_ctx {
    FILE *dest;
    char tmp[STB_SPRINTF_MIN];
} stb_printf_ctx;

static char *_printf_cb(char * buf, void * user, int len) {
    stb_printf_ctx *ctx = (stb_printf_ctx *)user;
    fwrite (buf, 1, len, ctx->dest);
    fflush(ctx->dest);
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

// have GC announce when it does something
#define BANGRA_NOISY_GC 1

// GC nursery size
#define B_GC_NURSERY_SIZE 0x200000
#if 1
#define B_GC_NURSERY_LIMIT B_GC_NURSERY_SIZE
#else
#define B_GC_NURSERY_LIMIT 1024
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
#define DEC_10 9
#define DEC_11 10
#define DEC_12 11
#define DEC_13 12
#define DEC_14 13
#define DEC_15 14
#define DEC_16 15
#define DEC_17 16
#define DEC_18 17
#define DEC_19 18

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
#define INC_10 11
#define INC_11 12
#define INC_12 13
#define INC_13 14
#define INC_14 15
#define INC_15 16
#define INC_16 17
#define INC_17 18
#define INC_18 19
#define INC_19 20

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
#define GETARG_10(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, ...) _10
#define GETARG_11(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, ...) _11
#define GETARG_12(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, ...) _12
#define GETARG_13(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, _13, ...) _13
#define GETARG_14(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, _13, _14, ...) _14
#define GETARG_15(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, _13, _14, _15, ...) _15
#define GETARG_16(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, _13, _14, _15, _16, ...) _16
#define GETARG_17(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, _13, _14, _15, _16, _17, ...) _17
#define GETARG_18(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, _13, _14, _15, _16, _17, _18, ...) _18
#define GETARG_19(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                 _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, ...) _19
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

#define _GET_20TH_ARG(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
                     _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, \
                     N, ...) N
#define COUNT_VARARGS(...) \
    _GET_20TH_ARG("ignored", ##__VA_ARGS__, \
    19, 18, 17, 16, 15, 14, 13, 12, 11, 10, \
    9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
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
#define _FE_10(_call, x, ...) _call(x) _FE_9(_call, __VA_ARGS__)
#define _FE_11(_call, x, ...) _call(x) _FE_10(_call, __VA_ARGS__)
#define _FE_12(_call, x, ...) _call(x) _FE_11(_call, __VA_ARGS__)
#define _FE_13(_call, x, ...) _call(x) _FE_12(_call, __VA_ARGS__)
#define _FE_14(_call, x, ...) _call(x) _FE_13(_call, __VA_ARGS__)
#define _FE_15(_call, x, ...) _call(x) _FE_14(_call, __VA_ARGS__)
#define _FE_16(_call, x, ...) _call(x) _FE_15(_call, __VA_ARGS__)
#define _FE_17(_call, x, ...) _call(x) _FE_16(_call, __VA_ARGS__)
#define _FE_18(_call, x, ...) _call(x) _FE_17(_call, __VA_ARGS__)
#define _FE_19(_call, x, ...) _call(x) _FE_18(_call, __VA_ARGS__)
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
#define _FEN_10(n, _call, x, ...) _call(n,x) _FEN_9(INC(n), _call, __VA_ARGS__)
#define _FEN_11(n, _call, x, ...) _call(n,x) _FEN_10(INC(n), _call, __VA_ARGS__)
#define _FEN_12(n, _call, x, ...) _call(n,x) _FEN_11(INC(n), _call, __VA_ARGS__)
#define _FEN_13(n, _call, x, ...) _call(n,x) _FEN_12(INC(n), _call, __VA_ARGS__)
#define _FEN_14(n, _call, x, ...) _call(n,x) _FEN_13(INC(n), _call, __VA_ARGS__)
#define _FEN_15(n, _call, x, ...) _call(n,x) _FEN_14(INC(n), _call, __VA_ARGS__)
#define _FEN_16(n, _call, x, ...) _call(n,x) _FEN_15(INC(n), _call, __VA_ARGS__)
#define _FEN_17(n, _call, x, ...) _call(n,x) _FEN_16(INC(n), _call, __VA_ARGS__)
#define _FEN_18(n, _call, x, ...) _call(n,x) _FEN_17(INC(n), _call, __VA_ARGS__)
#define _FEN_19(n, _call, x, ...) _call(n,x) _FEN_18(INC(n), _call, __VA_ARGS__)
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

#define ANSI(X) (support_ansi?(ANSI_ ## X):"")

static bool support_ansi = false;

//------------------------------------------------------------------------------
// ARITHMETIC UTILITIES
//------------------------------------------------------------------------------

static size_t align(size_t offset, size_t align) {
    return (offset + align - 1) & ~(align - 1);
}

//------------------------------------------------------------------------------
// TYPE TAGGED INTEGERS
//------------------------------------------------------------------------------

template<typename EnumT, EnumT end_value>
struct TypedInt {
protected:
    uint64_t _value;

    TypedInt(uint64_t tid) :
        _value(tid) {
    }

public:
    static TypedInt wrap(uint64_t value) {
        return { value };
    }

    TypedInt() {}

    TypedInt(EnumT id) :
        _value(id) {
    }

    bool is_known() const {
        return _value < end_value;
    }

    EnumT known_value() const {
        assert(is_known());
        return (EnumT)_value;
    }

    // for std::map support
    bool operator < (TypedInt b) const {
        return _value < b._value;
    }

    bool operator ==(TypedInt b) const {
        return _value == b._value;
    }

    bool operator !=(TypedInt b) const {
        return _value != b._value;
    }

    bool operator ==(EnumT b) const {
        return _value == b;
    }

    bool operator !=(EnumT b) const {
        return _value != b;
    }

    std::size_t hash() const {
        return _value;
    }

    uint64_t value() const {
        return _value;
    }
};

} namespace std {
template<typename EnumT, EnumT end_value>
struct hash< bangra::TypedInt<EnumT, end_value> > {
    std::size_t operator()(const bangra::TypedInt<EnumT, end_value> & s) const {
        return s.hash();
    }
};
} namespace bangra {

//------------------------------------------------------------------------------
// SYMBOL TYPE
//------------------------------------------------------------------------------

#define B_MAP_SYMBOLS() \
    T(SYM_Unnamed, "") \
    T(SYM_Name, "name") \
    \
    T(SYM_SquareList, "[") \
    T(SYM_CurlyList, "{") \
    \
    T(SYM_Cmp, "compare") \
    T(SYM_CmpType, "compare-type") \
    T(SYM_ToString, "tostring") \
    T(SYM_Hash, "hash") \
    T(SYM_Slice, "slice") \
    T(SYM_ApplyType, "apply-type") \
    T(SYM_CountOf, "countof") \
    \
    T(SYM_OP1_Neg, "neg") \
    T(SYM_OP1_Not, "bitnot") /* bit not (~) */ \
    T(SYM_OP1_BoolNot, "not") \
    T(SYM_OP1_Rcp, "rcp") \
    \
    T(SYM_OP2_Add, "add") \
    T(SYM_OP2_Sub, "sub") \
    T(SYM_OP2_Mul, "mul") \
    T(SYM_OP2_Div, "div") \
    T(SYM_OP2_FloorDiv, "floordiv") \
    T(SYM_OP2_Mod, "mod") \
    T(SYM_OP2_And, "and") \
    T(SYM_OP2_Or, "or") \
    T(SYM_OP2_Xor, "xor") \
    T(SYM_OP2_Join, "join") \
    T(SYM_OP2_At, "at") \
    T(SYM_OP2_LShift, "shiftl") \
    T(SYM_OP2_RShift, "shiftr") \
    T(SYM_OP2_Pow, "pow") \
    \
    T(SYM_OP2_RAdd, "radd") \
    T(SYM_OP2_RSub, "rsub") \
    T(SYM_OP2_RMul, "rmul") \
    T(SYM_OP2_RDiv, "rdiv") \
    T(SYM_OP2_RFloorDiv, "rfloordiv") \
    T(SYM_OP2_RMod, "rmod") \
    T(SYM_OP2_RAnd, "rand") \
    T(SYM_OP2_ROr, "ror") \
    T(SYM_OP2_RXor, "rxor") \
    T(SYM_OP2_RJoin, "rjoin") \
    T(SYM_OP2_RAt, "rat") \
    T(SYM_OP2_RLShift, "rshiftl") \
    T(SYM_OP2_RRShift, "rshiftr") \
    T(SYM_OP2_RPow, "rpow") \
    \
    T(SYM_SuperType, "super") \
    T(SYM_Size, "size") \
    T(SYM_Alignment, "alignment") \
    T(SYM_IsEmbedded, "embedded?") \
    T(SYM_IsSigned, "signed?") \
    T(SYM_IsVarArg, "vararg?") \
    T(SYM_Width, "width") \
    T(SYM_ElementCount, "count") \
    T(SYM_ElementType, "element-type") \
    T(SYM_TagType, "tag-type") \
    T(SYM_ReturnType, "return-type") \
    T(SYM_FieldTypes, "field-types") \
    T(SYM_FieldOffsets, "field-offsets") \
    T(SYM_FieldNames, "field-names") \
    T(SYM_TagValues, "tag-values") \
    T(SYM_ParameterTypes, "parameter-types") \
    T(SYM_PayloadTypes, "payload-types") \
    \
    T(SYM_Do, "do") \
    T(SYM_DoForm, "form:do") \
    T(SYM_Parent, "#parent") \
    T(SYM_VarArgs, "...") \
    T(SYM_Escape, "escape") \
    T(SYM_ContinuationForm, "form:fn/cc") \
    T(SYM_QuoteForm, "form:quote") \
    T(SYM_ListWC, "#list") \
    T(SYM_SymbolWC, "#symbol") \
    T(SYM_ScriptSize, "script-size") \
    T(SYM_FunctionFlow, "function") \
    T(SYM_ReturnFlow, "return") \
    T(SYM_ExitFlow, "exit") \
    T(SYM_Return, "return") \
    T(SYM_Result, "result") \
    T(SYM_Continuation, "cont") \
    T(SYM_ReadEvalPrintLoop, "read-eval-print-loop")

enum KnownSymbol {
#define T(sym, name) sym,
    B_MAP_SYMBOLS()
#undef T
    SYM_Count,
};

typedef TypedInt<KnownSymbol, SYM_Count> Symbol;

//------------------------------------------------------------------------------
// TYPE SYSTEM
//------------------------------------------------------------------------------

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
    T(Anchor) \
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
    T(BuiltinClosure) \
    T(SourceFile)

enum KnownType {
#define T(X) TYPE_ ## X,
    B_MAP_TYPES()
#undef T

    TYPE_BuiltinCount,
};

#define TYPE_SizeT TYPE_U64

typedef uint64_t TypeAttribute;

typedef TypedInt<KnownType, TYPE_BuiltinCount> Type;

static uint64_t next_type_id = TYPE_BuiltinCount;

static Type new_type() {
    Type result = Type::wrap(next_type_id);
    auto newvalue = next_type_id + 1;
    assert(newvalue <= 0xffffffffull);
    next_type_id = newvalue;
    return result;
}

static TypeAttribute type_attrib(Type type, Symbol sym) {
    assert (sym.value() <= 0xffffffffull);
    return (sym.value() << 32) | type.value();
}

static const char *get_builtin_name(Type type) {
    if (!type.is_known()) return "???";
    switch(type.known_value()) {
    #define T(X) case TYPE_ ## X: return #X;
        B_MAP_TYPES()
    #undef T
        default:
            assert(false && "illegal builtin value");
    }
    return nullptr;
}

} namespace std {
template<>
struct hash<bangra::Type> {
    std::size_t operator()(const bangra::Type & s) const {
        return s.hash();
    }
};
} namespace bangra {

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
struct SourceFile;

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
typedef void (*Function)(BuiltinClosure *, size_t, const Any *);

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

        real32_t r32;
        real64_t r64;

        void *ptr;
        String *str;
        uint64_t symbol;

        BuiltinFlowFunction builtin_flow;
        SpecialFormFunction special_form;
        Function function;
        BuiltinClosure *builtin_closure;

        const char *c_str;
        List *list;
        uint64_t typeref;
        Parameter *parameter;
        Flow *flow;
        Closure *closure;
        Frame *frame;
        Table *table;
        Anchor *anchor;

        SourceFile *source_file;
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

#define ALLOC_CLOSURE(NARGS) \
    ((BuiltinClosure *)alloca(sizeof(BuiltinClosure) - sizeof(Any) + (NARGS) * sizeof(Any)))

//------------------------------------------------------------------------------

static const Any none = { TYPE_Void, .ptr = nullptr };

static bool is_none(const Any &value) {
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

static char *g_stack_start;
static char *g_stack_end;

static bool is_stackptr(const char *ptr) {
    return (ptr >= g_stack_end) && (ptr < g_stack_start);
}

#define CHECKALIGN(x) \
    assert(!is_stackptr((const char *)(x)) || !(((uintptr_t)(x))&3))

#define DEF_WRAP(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(CTYPE x) { return { TYPE_ ## BTYPE, .MEMBER = x }; }

#define DEF_WRAP_TAG(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(CTYPE x) { return { TYPE_ ## BTYPE, .MEMBER = x.value() }; }

#define DEF_WRAP_PTR_OR_NULL(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(const CTYPE *x) { CHECKALIGN(x); return { TYPE_ ## BTYPE, .MEMBER = x}; } \
    inline static Any wrap(CTYPE *x) { CHECKALIGN(x); return { TYPE_ ## BTYPE, .MEMBER = x}; }

#define DEF_WRAP_MUTABLE_PTR_OR_NULL(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(CTYPE *x) { CHECKALIGN(x); return { TYPE_ ## BTYPE, .MEMBER = x}; }

#define DEF_WRAP_PTR(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(CTYPE &x) { return { TYPE_ ## BTYPE, .MEMBER = &x }; }

#define DEF_WRAP_MUTABLE_PTR(CTYPE, BTYPE, MEMBER) \
    inline static Any wrap(CTYPE &x) { CHECKALIGN(&x); return { TYPE_ ## BTYPE, .MEMBER = &x }; }

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

DEF_WRAP_TAG(Symbol, Symbol, symbol);
DEF_WRAP_TAG(Type, Type, typeref);

DEF_WRAP(Function, Function, function);

DEF_WRAP_PTR(String, String, str);
DEF_WRAP_PTR(Anchor, Anchor, anchor);
DEF_WRAP_MUTABLE_PTR(SourceFile, SourceFile, source_file);
DEF_WRAP_MUTABLE_PTR_OR_NULL(BuiltinClosure, BuiltinClosure, builtin_closure);
DEF_WRAP_MUTABLE_PTR_OR_NULL(List, List, list);

inline static Any &wrap(Any &x) { return x; }
inline static const Any &wrap(const Any &x) { return x; }
//template<typename T> inline static Any wrap(T &src) { return T::_wrap(src); }

//------------------------------------------------------------------------------

template<std::size_t n>
inline static String str(const char (&s)[n]) {
    return { s, n - 1 };
}

inline static String str(const char *s, size_t len) {
    CHECKALIGN(s);
    return { s, len };
}

inline static String strc(const char *s) {
    CHECKALIGN(s);
    return { s, strlen(s) };
}

//------------------------------------------------------------------------------

static bool is_type(const Any &value, Type type) {
    return (value.type == type); }

template<typename T> struct extract {};

#define DEF_EXTRACT(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE> { \
        typedef CTYPE return_type; \
        inline return_type operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return x.MEMBER; }}
#define DEF_EXTRACT_TAG(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE> { \
        typedef CTYPE return_type; \
        inline return_type operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return CTYPE::wrap(x.MEMBER); }}
#define DEF_EXTRACT_PTR(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE> { \
        typedef const CTYPE &return_type; \
        inline return_type operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return *x.MEMBER; }}
#define DEF_EXTRACT_MUTABLE_PTR(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE> { \
        typedef CTYPE &return_type; \
        inline return_type operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return *x.MEMBER; }}
#define DEF_EXTRACT_PTR_OR_NULL(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE *> { \
        typedef const CTYPE *return_type; \
        inline return_type operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return x.MEMBER; }}
#define DEF_EXTRACT_MUTABLE_PTR_OR_NULL(CTYPE, BTYPE, MEMBER) \
    template<> struct extract<CTYPE *> { \
        typedef CTYPE *return_type; \
        inline return_type operator ()(const Any &x) { \
            assert(is_type(x, TYPE_ ## BTYPE));  \
            return x.MEMBER; }}
#define DEF_EXTRACT_CLASS_PTR(CTYPE) \
    template<> struct extract<CTYPE> { \
        typedef CTYPE &return_type; \
        inline return_type operator ()(const Any &x) { \
            assert(is_type(x, TYPE_BuiltinClosure)); \
            assert(x.builtin_closure->function == CTYPE::run); \
            return *(CTYPE *)x.builtin_closure; }}

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

DEF_EXTRACT_TAG(Symbol, Symbol, symbol);
DEF_EXTRACT_TAG(Type, Type, typeref);

DEF_EXTRACT(Function, Function, function);

template<> struct extract<const char *> {
    typedef const char *return_type;
    return_type operator ()(const Any &x) {
        assert(is_type(x, TYPE_String)); assert(x.str);
        return x.str->ptr; }};

DEF_EXTRACT_PTR(String, String, str);
DEF_EXTRACT_PTR(Anchor, Anchor, anchor);
DEF_EXTRACT_MUTABLE_PTR(SourceFile, SourceFile, source_file);
DEF_EXTRACT_PTR_OR_NULL(BuiltinClosure, BuiltinClosure, builtin_closure);
DEF_EXTRACT_MUTABLE_PTR_OR_NULL(List, List, list);

template<typename T>
inline static typename extract<T>::return_type unwrap(const Any &value) {
    return extract<T>()(value);
}

//------------------------------------------------------------------------------
// FUNCTION CALL TEMPLATES
//------------------------------------------------------------------------------

template<typename ... Args>
inline static void _call(const BuiltinClosure *cl, Args&& ... args) {
    Any wrapped_args[] = { wrap(args) ... };
    return cl->function(cl, sizeof...(args), wrapped_args);
}

template<typename ... Args>
inline static void _call(const Any &cl, Args&& ... args) {
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
inline static void _call(const T &caller, Args&& ... args) {
    Any wrapped_args[] = { wrap(args) ... };
    Any cl = wrap(const_cast<T &>(caller));
    assert(cl.type == TYPE_BuiltinClosure);
    return cl.builtin_closure->function(
        cl.builtin_closure, sizeof...(args), wrapped_args);
}

template<typename T, bool has_upvars>
struct _call_struct {};

template<typename T>
struct _call_struct<T, true> {
    template<typename ... Args>
    inline static T capture(Args&& ... args) {
        return T::capture(wrap(args) ...);
    }
};

template<typename T>
struct _call_struct<T, false> {
    template<typename ... Args>
    inline static void call(Args&& ... args) {
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

struct GC_Context;

static size_t sizeof_payload(GC_Context &ctx, const Any &from);
static void mark_payload(GC_Context &ctx, Any &val);

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
        assert(!(bits[page] & (1ull<<(int)bit)));
        bits[page] |= (1ull<<(int)bit);
    }

    bool is_marked(const char *dstptr) {
        size_t offset = g_stack_start - dstptr;
        assert((offset % 8) == 0);
        size_t page = offset / 512;
        size_t bit = (offset / 8) % 64;
        assert (page < numpages);
        return bits[page] & (1ull<<(int)bit);
    }

    bool is_on_stack(const char *ptr) {
        return is_stackptr(ptr);
    }

    bool _move_memory(size_t plsize, const char *&ptr) {
        if (is_marked(ptr)) {
            char **dest = (char **)ptr;
            ptr = *dest;
            return false;
        } else {
            mark_addr(ptr);
            // copy data to new heap
            memcpy(heap_end, ptr, plsize);
            // write pointer to old location
            char **dest = (char **)ptr;
            *dest = heap_end;
            ptr = heap_end;
            heap_end += align(plsize, 8);
            return true;
        }
    }

    void move_and_mark(size_t plsize, Any &from) {
        if (_move_memory(plsize, from.c_str))
            *head_end++ = from;
    }

    void force_move(Any &from) {
        size_t plsize = sizeof_payload(*this, from);
        if (plsize) {
            move_and_mark(plsize, from);
        }
    }

    bool move_memory(size_t size, const char *&ptr) {
        if (is_on_stack(ptr)) {
            return _move_memory(size, ptr);
        }
        return false;
    }

    void move(Any &from) {
        size_t plsize = sizeof_payload(*this, from);
        if (plsize) {
            if (is_on_stack(from.c_str)) {
                move_and_mark(plsize, from);
            }
        }
    }

    void dump_bits() {
        for (size_t i = 0; i < numpages; ++i) {
            if (bits[i]) {
                printf("page %i: ", (int)i);
                for (size_t k = 0; k < 64; ++k) {
                    if (bits[i] & (1ull << k))
                        putchar('1');
                    else
                        putchar('0');
                }
                putchar('\n');
            }
        }
    }

    GC_Context(char *stack_addr) {
        assert(stack_addr > g_stack_end);
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

static void resume_closure(BuiltinClosure *_self, size_t numargs, const Any *_args) {
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

#if BANGRA_NOISY_GC
    stb_printf("GC!\n");
#endif
    ctx.force_move(ret);

    Any *headptr = ctx.head;
    while(headptr < ctx.head_end) {
        Any &val = *headptr;
        mark_payload(ctx, val);
        headptr++;
    }

#if BANGRA_NOISY_GC
    stb_printf("%zd values / %zd bytes moved\n",
        ctx.head_end - ctx.head,
        ctx.heap_end - ctx.heap);
#endif
    return ret;
}

static void GC(Function f, BuiltinClosure *cl, size_t numargs, const Any *args) {
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
    g_stack_limit = g_stack_start - B_GC_NURSERY_LIMIT;
    g_stack_end = g_stack_start - B_GC_NURSERY_SIZE;

    g_contobj = entry;

    switch(setjmp(g_retjmp)) {
    case 0: // first set
    case 1: _call(g_contobj); break; // loop
    default: break; // abort
    }
    return 0;
}

//------------------------------------------------------------------------------
// ANCHORS
//------------------------------------------------------------------------------

struct Anchor {
    const char *path;
    int lineno;
    int column;
    int offset;

    Anchor() {
        path = nullptr; lineno = 0; column = 0; offset = 0;
    }

    Anchor(const char *_path, int _lineno, int _column, int _offset = -1) :
        path(_path), lineno(_lineno), column(_column), offset(_offset) {
    }

    bool is_offset_valid() const {
        return offset >= 0;
    }

    bool is_valid() const {
        return (path != nullptr) && (lineno != 0) && (column != 0);
    }

    bool operator ==(const Anchor &other) const {
        return
            path == other.path
                && lineno == other.lineno
                && column == other.column
                && offset == other.offset;
    }

    void print(FILE *stream) const {
        assert(is_valid());
        fputs(ANSI(STYLE_LOCATION), stream); fputs(path, stream);
        fputs(ANSI(STYLE_OPERATOR), stream); fputc(':', stream);
        fputs(ANSI(STYLE_NUMBER), stream); stb_fprintf(stream, "%i", lineno);
        fputs(ANSI(STYLE_OPERATOR), stream); fputc(':', stream);
        fputs(ANSI(STYLE_NUMBER), stream); stb_fprintf(stream, "%i", column);
        fputs(ANSI(RESET), stream);
    }
};

static Anchor active_anchor;
static void set_anchor(const Anchor &anchor) {
    active_anchor = anchor;
}

#define ANCHOR() Anchor(__FILE__, __LINE__, 1)
#define FROM_HERE() set_anchor(ANCHOR())

#define INTERNAL_ERROR(FMT, ...) \
    { FROM_HERE(); RCALL(error, str(FMT), ## __VA_ARGS__); }
#define DEBUG_PRINT(FMT, ...) \
    RCALL(message, ANCHOR(), str(FMT), ## __VA_ARGS__)
#define ERROR(FMT, ...) \
    { RCALL(error, str(FMT), ## __VA_ARGS__); }

//------------------------------------------------------------------------------
// FILE I/O
//------------------------------------------------------------------------------

struct SourceFile {
    int fd;
    off_t length;
    void *ptr;

    SourceFile() :
        fd(-1),
        length(0),
        ptr(MAP_FAILED)
        {}

    void close() {
        if (ptr != MAP_FAILED) {
            munmap(ptr, length);
            ptr = MAP_FAILED;
            length = 0;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    bool is_open() const { return (fd != -1); }
    const char *strptr() const { assert(is_open()); return (const char *)ptr; }
    size_t size() const { return length; }

    void dump_line(size_t offset, FILE *stream) {
        const char *str = strptr();
        if (offset >= (size_t)length) {
            return;
        }
        size_t start = offset;
        size_t end = offset;
        while (start > 0) {
            if (str[start-1] == '\n')
                break;
            start--;
        }
        while (end < (size_t)length) {
            if (str[end] == '\n')
                break;
            end++;
        }
        stb_fprintf(stream, "%.*s\n", (int)(end - start), str + start);
        size_t column = offset - start;
        for (size_t i = 0; i < column; ++i) {
            putc(' ', stream);
        }
        fputs(ANSI(STYLE_OPERATOR), stream); fputc('^', stream);
        fputs(ANSI(RESET), stream); fputc('\n', stream);
    }

    static SourceFile open(const char *path) {
        SourceFile file;
        file.fd = ::open(path, O_RDONLY);
        if (file.fd >= 0) {
            file.length = lseek(file.fd, 0, SEEK_END);
            file.ptr = mmap(nullptr, file.length, PROT_READ, MAP_PRIVATE, file.fd, 0);
            if (file.ptr != MAP_FAILED) {
                return file;
            }
        }
        file.close();
        return file;
    }

};

static void dump_source_file_line(const char *path, int offset, FILE *stream) {
    auto file = SourceFile::open(path);
    if (file.is_open()) {
        file.dump_line((size_t)offset, stream);
        file.close();
    }
}

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
        p += stb_snprintf(p, B_SNFORMAT - (p - buf), FMT, ## __VA_ARGS__); }
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
                        case TYPE_Void: VSFCB_PRINT("none"); goto success;
                        case TYPE_I8: VSFCB_PRINT("%" PRId8, arg->i8); goto success;
                        case TYPE_I16: VSFCB_PRINT("%" PRId16, arg->i16); goto success;
                        case TYPE_I32: VSFCB_PRINT("%" PRId32, arg->i32); goto success;
                        case TYPE_I64: VSFCB_PRINT("%lld", arg->i64); goto success;
                        case TYPE_U8: VSFCB_PRINT("%" PRIu8, arg->u8); goto success;
                        case TYPE_U16: VSFCB_PRINT("%" PRIu16, arg->u16); goto success;
                        case TYPE_U32: VSFCB_PRINT("%" PRIu32, arg->u32); goto success;
                        case TYPE_U64: VSFCB_PRINT("%llu", arg->u64); goto success;
                        case TYPE_R32: VSFCB_PRINT("%g", arg->r32); goto success;
                        case TYPE_R64: VSFCB_PRINT("%g", arg->r64); goto success;
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

struct fvprint_cb_ctx {
    FILE *out;
    char tmp[B_SNFORMAT];
};

static char *fvprint_cb(const char *buf, void *user, int len) {
    fvprint_cb_ctx *ctx = (fvprint_cb_ctx *)user;
    fwrite(buf, 1, len, ctx->out);
    fflush(ctx->out);
    return ctx->tmp;
}

static void fvprint(FILE *f, const char *fmt, size_t numargs, const Any *args) {
    fvprint_cb_ctx ctx;
    ctx.out = f;
    vsformatcb(fvprint_cb, &ctx, ctx.tmp, fmt, numargs, args);
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
        return ctx.count + 1;
    }
}

static void fvmessage (
    FILE *stream, const Anchor &anchor, const char *fmt,
    size_t numargs, const Any *args) {
    if (anchor.is_valid()) {
        anchor.print(stream);
        fputs(ANSI(STYLE_OPERATOR), stream); fputs(": ", stream);
    }
    if (stream == stderr) {
        fputs(ANSI(STYLE_ERROR), stream); fputs("error: ", stream);
    }
    fputs(ANSI(RESET), stream);
    fvprint(stream, fmt, numargs, args);
    putc('\n', stream);
    if (anchor.is_valid() && anchor.is_offset_valid()) {
        dump_source_file_line(anchor.path, anchor.offset, stream);
    }
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

#define DEF_UPVAR_MEMBER(NAME) Any NAME;
#define CLOSURE_CAPTURE_FIELDS(...) \
    MACRO_FOREACH(DEF_UPVAR_MEMBER, __VA_ARGS__) \
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

#define CLOSURE_BODY(...) \
    static Any _wrap(this_struct &self) { return wrap((BuiltinClosure *)&self); } \
    operator Any() { return wrap((BuiltinClosure *)this); } \
    static void run (BuiltinClosure *_self, size_t numargs, const Any *_args) { \
        char _stack_marker; char *_stack_addr = &_stack_marker; \
        if (_stack_addr <= g_stack_limit) { \
            GC(run, _self, numargs, _args); \
        } else { \
            this_struct *self = (this_struct *)_self; \
            if (this_struct::NUMUPVARS) { \
                assert(self && (self->numupvars >= this_struct::NUMUPVARS)); \
            } \
            self->_run(numargs, _args); \
        } \
    } \
    void _run (size_t numargs, const Any *_args) { \
        CLOSURE_UNPACK_PARAMS(__VA_ARGS__) \
        CLOSURE_VARARG_DEFS(__VA_ARGS__)

#define RFN_BODY(...) \
    static void run (BuiltinClosure *, size_t numargs, const Any *_args) { \
        CLOSURE_UNPACK_PARAMS(__VA_ARGS__) \
        CLOSURE_VARARG_DEFS(__VA_ARGS__)

#define LETFN(NAME) \
    struct NAME { \
        typedef NAME this_struct; \
        CLOSURE_BODY
#define LETFN_END \
        }; \
        enum { \
            has_upvars = false, \
            NUMUPVARS = 0 \
        }; \
        Function f; \
        size_t numupvars; \
    };

#define FN_CLASS_BEGIN }
#define FN_CLASS_END enum { has_class // bogus enum to close the dangling brace

#define LETFN_END_BINDWITH(...) \
        }; \
        enum { \
            has_upvars = true, \
            NUMUPVARS = COUNT_VARARGS(__VA_ARGS__) \
        }; \
        Function f; \
        size_t numupvars; \
        CLOSURE_CAPTURE_FIELDS(__VA_ARGS__) \
    };

#define LETRFN(NAME) \
    struct NAME { \
        typedef NAME this_struct; \
        RFN_BODY
#define LETRFN_END LETFN_END

#define FN(...) \
    ({ LETFN(_)(__VA_ARGS__)

#define FN_END \
    LETFN_END \
    CAPTURE(_); })
#define FN_END_BINDWITH(...) \
    LETFN_END_BINDWITH(__VA_ARGS__) \
    CAPTURE(_, __VA_ARGS__); })

#define VARARG(i) _args[i]

#define RET(...) return _call(__VA_ARGS__)
#define CC(T, ...) return _call_struct<T, T::has_upvars>::call(__VA_ARGS__)
#define RCALL(T, ...) _call_struct<T, T::has_upvars>::call(__VA_ARGS__)
#define CAPTURE(T, ...) _call_struct<T, T::has_upvars>::capture(__VA_ARGS__)

//------------------------------------------------------------------------------
// FN-BASED UTILITIES
//------------------------------------------------------------------------------

LETRFN(print_format)(fmt, VARARGS)
    auto fmtstr = unwrap<const char *>(fmt);
    fvprint(stdout, fmtstr, numargs - VARARG_START, &VARARG(VARARG_START));
LETRFN_END

LETRFN(print)(VARARGS)
    for (size_t i = VARARG_START; i < numargs; ++i) {
        if (i != VARARG_START) fputs(" ", stdout);
        fvprint(stdout, "{}", 1, &VARARG(i));
    }
    fputs("\n", stdout);
LETRFN_END

LETFN(format)(cont, fmt, VARARGS)
    auto fmtstr = unwrap<const char *>(fmt);
    auto size = vsformat(nullptr,
        fmtstr, numargs - VARARG_START, &VARARG(VARARG_START));
    char dest[size + 1];
    vsformat(dest,
        fmtstr, numargs - VARARG_START, &VARARG(VARARG_START));
    RET(cont, str(dest, size));
LETFN_END

LETFN(escape_string)(cont, s, VARARGS)
    auto sstr = unwrap<String>(s);
    const char *cquote_chars = nullptr;
    if (numargs >= 3) {
        cquote_chars = unwrap<const char *>(VARARG(VARARG_START)); }
    auto size = escapestr(nullptr, sstr, cquote_chars);
    char dest[size + 1];
    escapestr(dest, sstr, cquote_chars);
    RET(cont, str(dest, size));
LETFN_END

LETFN(concat_strings)(cont, first, VARARGS)
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
    RET(cont, str(dest, destsize));
LETFN_END

LETFN(color)(cont, code, content)
    if (support_ansi) {
        CC(concat_strings, cont, code, content, str(ANSI_RESET));
    } else {
        RET(cont, content);
    }
LETFN_END

LETFN(repr_string)(cont, s)
    CC(escape_string,
        FN(s)
            auto q = str("\"");
            CC(concat_strings,
                FN(s)
                    CC(color, cont, str(ANSI_STYLE_STRING), s);
                FN_END_BINDWITH(cont),
                q, s, q);
        FN_END_BINDWITH(cont),
        s, str("\""));
LETFN_END

LETRFN(message)(anchor, fmt, VARARGS)
    Anchor panchor = active_anchor;
    if (!is_none(anchor)) { panchor = unwrap<Anchor>(anchor); }
    auto fmtstr = unwrap<const char *>(fmt);
    fvmessage(stdout, panchor, fmtstr,
        numargs - VARARG_START, &VARARG(VARARG_START));
LETRFN_END

LETRFN(errormessage)(anchor, fmt, VARARGS)
    Anchor panchor = active_anchor;
    if (!is_none(anchor)) { panchor = unwrap<Anchor>(anchor); }
    auto fmtstr = unwrap<const char *>(fmt);
    fvmessage(stderr, panchor, fmtstr,
        numargs - VARARG_START, &VARARG(VARARG_START));
LETRFN_END

LETRFN(error)(fmt, VARARGS)
    auto fmtstr = unwrap<const char *>(fmt);
    fvmessage(stderr, active_anchor, fmtstr,
        numargs - VARARG_START, &VARARG(VARARG_START));
    exit(1);
LETRFN_END

//------------------------------------------------------------------------------
// SYMBOL CACHE
//------------------------------------------------------------------------------

static uint64_t next_symbol_id = SYM_Count;
static std::unordered_map<Symbol, std::string> map_symbol_name;
static std::unordered_map<std::string, Symbol> map_name_symbol;

static void map_symbol(Symbol id, const String &name) {
    std::string sname = std::string(name.ptr, name.count);
    map_name_symbol[sname] = id;
    map_symbol_name[id] = sname;
}

static Symbol get_symbol(const String &name) {
    std::string sname = std::string(name.ptr, name.count);
    auto it = map_name_symbol.find(sname);
    if (it != map_name_symbol.end()) {
        return it->second;
    } else {
        Symbol id = Symbol::wrap(++next_symbol_id);
        map_symbol(id, name);
        return id;
    }
}

static std::string get_symbol_name_stdstr(Symbol sym) {
    return map_symbol_name[sym];
}

LETFN(get_symbol_name)(cont, id)
    auto sym = unwrap<Symbol>(id);
    const std::string &key = map_symbol_name[sym];
    RET(cont, str(key.c_str(), key.size()));
LETFN_END

static void init_symbols() {
#define T(sym, name) map_symbol(sym, str(name));
    B_MAP_SYMBOLS()
#undef T
}

//------------------------------------------------------------------------------
// S-EXPR LEXER / TOKENIZER
//------------------------------------------------------------------------------

#define LEXER_TOKENS() \
    T(token_none, -1) \
    T(token_eof, 0) \
    T(token_open, '(') \
    T(token_close, ')') \
    T(token_square_open, '[') \
    T(token_square_close, ']') \
    T(token_curly_open, '{') \
    T(token_curly_close, '}') \
    T(token_string, '"') \
    T(token_symbol, 'S') \
    T(token_escape, '\\') \
    T(token_statement, ';') \
    T(token_integer, 'I') \
    T(token_real, 'R')

typedef enum {
#define T(NAME, VALUE) NAME = VALUE,
LEXER_TOKENS()
#undef T
} Token;

static const char token_terminators[]    = "()[]{}\"';#";

static const char *get_lexer_token_name(int token) {
    switch(token) {
#define T(NAME, VALUE) case NAME: return #NAME;
LEXER_TOKENS()
#undef T
        default: return "???";
    }
}

static void verify_good_taste(char c) {
    if (c == '\t') {
        ERROR("please use spaces instead of tabs.");
    }
}

template<typename FromType, typename ToType>
static bool is_lossless_cast(FromType x) {
    return (FromType(ToType(x)) == x);
}

LETFN(Lexer)()
FN_CLASS_BEGIN
    static Lexer create(String &buffer, String &path, int offset = 0) {
        return CAPTURE(Lexer,
            buffer, path,
            0, 0, // cursor_offset, next_cursor_offset, // int
            0, 0, // line_offset, next_line_offset, // int
            0, 1, // lineno, next_lineno, // int
            offset, // base_offset

            (int)token_none, // token
            0, 0, // string_offset, string_len, // int
            int64_t(0), // integer
            0.0); // real
    }

    int offset() {
        return base_offset.i32 + cursor_offset.i32;
    }

    int column() {
        return cursor_offset.i32 - line_offset.i32 + 1;
    }

    Anchor get_anchor() {
        return Anchor(path.str->ptr, lineno.i32, column(), offset());
    }

    const char *at() {
        return buffer.str->ptr + cursor_offset.i32;
    }

    char next() {
        return buffer.str->ptr[next_cursor_offset.i32++];
    }

    bool is_eof() {
        return (size_t)next_cursor_offset.i32 == buffer.str->count;
    }

    void newline() {
        ++next_lineno.i32;
        next_line_offset.i32 = next_cursor_offset.i32;
    }

    void read_symbol() {
        bool escape = false;
        while (true) {
            if (is_eof()) {
                break;
            }
            char c = next();
            if (escape) {
                if (c == '\n') newline();
                // ignore character
                escape = false;
            } else if (c == '\\') {
                // escape
                escape = true;
            } else if (isspace(c)
                || strchr(token_terminators, c)) {
                --next_cursor_offset.i32;
                break;
            }
        }
        string_offset.i32 = cursor_offset.i32;
        string_len.i32 = next_cursor_offset.i32 - cursor_offset.i32;
    }

    void read_string(char terminator) {
        bool escape = false;
        while (true) {
            if (is_eof()) {
                ERROR("unterminated sequence");
                break;
            }
            char c = next();
            if (c == '\n') newline();
            if (escape) {
                // ignore character
                escape = false;
            } else if (c == '\\') {
                // escape
                escape = true;
            } else if (c == terminator) {
                break;
            }
        }
        string_offset.i32 = cursor_offset.i32;
        string_len.i32 = next_cursor_offset.i32 - cursor_offset.i32;
    }

    bool read_int64() {
        char *end;
        errno = 0;
        integer.type = TYPE_I64;
        integer.i64 = std::strtoll(at(), &end, 0);
        int end_offset = (int)(end - buffer.str->ptr);
        if ((end_offset == cursor_offset.i32)
            || (errno == ERANGE)
            || ((size_t)end_offset > buffer.str->count)
            || (!isspace(*end) && !strchr(token_terminators, *end)))
            return false;
        next_cursor_offset.i32 = end_offset;
        return true;
    }

    bool read_uint64() {
        char *end;
        errno = 0;
        integer.type = TYPE_U64;
        integer.u64 = std::strtoull(at(), &end, 0);
        int end_offset = (int)(end - buffer.str->ptr);
        if ((end_offset == cursor_offset.i32)
            || (errno == ERANGE)
            || ((size_t)end_offset > buffer.str->count)
            || (!isspace(*end) && !strchr(token_terminators, *end)))
            return false;
        next_cursor_offset.i32 = end_offset;
        return true;
    }

    bool read_real() {
        char *end;
        errno = 0;
        real.type = TYPE_R32;
        real.r32 = std::strtof(at(), &end);
        int end_offset = (int)(end - buffer.str->ptr);
        if ((end_offset == cursor_offset.i32)
            || (errno == ERANGE)
            || ((size_t)end_offset > buffer.str->count)
            || (!isspace(*end) && !strchr(token_terminators, *end)))
            return false;
        next_cursor_offset.i32 = end_offset;
        return true;
    }

    int get_token() {
        return token.i32;
    }

    void next_token() {
        lineno.i32 = next_lineno.i32;
        line_offset.i32 = next_line_offset.i32;
        cursor_offset.i32 = next_cursor_offset.i32;
        active_anchor = get_anchor();
    }

    int read_token () {
        char c;
    skip:
        next_token();
        if (is_eof()) { token.i32 = token_eof; goto done; }
        c = next();
        verify_good_taste(c);
        if (c == '\n') newline();
        if (isspace(c)) { goto skip; }
        switch(c) {
        case '#': read_string('\n'); goto skip;
        case '(': token.i32 = token_open; break;
        case ')': token.i32 = token_close; break;
        case '[': token.i32 = token_square_open; break;
        case ']': token.i32 = token_square_close; break;
        case '{': token.i32 = token_curly_open; break;
        case '}': token.i32 = token_curly_close; break;
        case '\\': token.i32 = token_escape; break;
        case '"': token.i32 = token_string; read_string(c); break;
        //case '\'': token.i32 = token_string; read_string(c); break;
        case ';': token.i32 = token_statement; break;
        default: {
            if (read_int64() || read_uint64()) { token.i32 = token_integer; }
            else if (read_real()) { token.i32 = token_real; }
            else { token.i32 = token_symbol; read_symbol(); }
        } break;
        }
    done:
        return token.i32;
    }

    void get_any(Any cont) {
        switch(token.i32) {
        case token_integer: RET(cont, convert_integer());
        case token_real: RET(cont, real);
        case token_string: {
            size_t l1 = string_len.i32 - 2;
            char dest[l1 + 1];
            memcpy(dest, buffer.str->ptr + string_offset.i32 + 1, l1);
            dest[l1] = 0;
            size_t size = inplace_unescape(dest);
            RET(cont, str(dest, size));
        }
        case token_symbol: RET(cont, convert_symbol());
        default: RET(cont, none);
        }
    }

    Symbol convert_symbol() {
        char dest[string_len.i32 + 1];
        memcpy(dest, buffer.str->ptr + string_offset.i32, string_len.i32);
        dest[string_len.i32] = 0;
        size_t size = inplace_unescape(dest);
        auto s = str(dest, size);
        return get_symbol(s);
    }

    Any convert_integer() {
        if ((integer.type == TYPE_I64)
            && is_lossless_cast<int64_t, int32_t>(integer.i64)) {
            return wrap(int32_t(integer.i64));
        } else if ((integer.type == TYPE_U64)
            && is_lossless_cast<uint64_t, uint32_t>(integer.u64)) {
            return wrap(uint32_t(integer.u64));
        }
        return integer;
    }

FN_CLASS_END
LETFN_END_BINDWITH(
    buffer, path, // string
    cursor_offset, next_cursor_offset, // int
    line_offset, next_line_offset, // int
    lineno, next_lineno, // int
    base_offset, // int

    token, // int
    string_offset, string_len, // int
    integer, // int64
    real // float
)

DEF_EXTRACT_CLASS_PTR(Lexer);

//------------------------------------------------------------------------------
// LIST
//------------------------------------------------------------------------------

struct List {
    Anchor anchor;
    Any at;
    List *next;
    size_t count;
};

inline static List list_create(const Any &at, List *next, const Anchor &anchor) {
    return { anchor, at, next, next?(next->count + 1):1 };
}

inline static List list_create(const Any &at, List *next) {
    return list_create(at, next, Anchor());
}

/*
LETFN(_list_create_from_args)(index)


LETFN_END_BINDWITH(cont)

LETFN(list_create_from_args)(cont, VARARGS)
    BuiltinClosure *cl = ALLOC_CLOSURE(numargs);
    cl->size = numargs;
    cl->function = _list_create_from_args;
    for (size_t i = 0; i < numargs; ++i) {
        cl->args[i] = _args[i];
    }
    RET(cl, 0);
LETFN_END

static const List *list_create_from_c_array(const Any *values, size_t count) {
    List *result = nullptr;
    while (count) {
        --count;
        result = create(values[count], result);
    }
    return result;
}
*/

//------------------------------------------------------------------------------
// PRINTING
//------------------------------------------------------------------------------

#if 0
template<typename T>
static void streamTraceback(T &stream) {
    void *array[10];
    size_t size;
    char **strings;
    size_t i;

    size = backtrace (array, 10);
    strings = backtrace_symbols (array, size);

    for (i = 0; i < size; i++) {
        stream << format("%s\n", strings[i]);
    }

    free (strings);
}

static std::string formatTraceback() {
    std::stringstream ss;
    streamTraceback(ss);
    return ss.str();
}
#endif

static bool isNested(const Any &e) {
    if (e.type == TYPE_List) {
        auto it = unwrap<List *>(e);
        while (it) {
            if (it->at.type == TYPE_List)
                return true;
            it = it->next;
        }
    }
    return false;
}

static const Anchor *find_valid_anchor(const Any &expr);
static const Anchor *find_valid_anchor(List *l) {
    const Anchor *a = nullptr;
    while (l) {
        a = &l->anchor;
        if (!a->is_valid()) {
            a = find_valid_anchor(l->at);
        }
        if (!a || !a->is_valid()) {
            l = l->next;
        } else {
            break;
        }
    }
    return a;
}

static const Anchor *find_valid_anchor(const Any &expr) {
    if (expr.type == TYPE_List) {
        return find_valid_anchor(expr.list);
    }
    return nullptr;
}

template<typename T>
static void streamAnchor(T &stream, const Any &e, size_t depth=0) {
    const Anchor *anchor = find_valid_anchor(e);
    if (anchor) {
        stream << anchor->path << ":" << anchor->lineno << ":" << anchor->column;
    }
    for(size_t i = 0; i < depth; i++)
        stream << "    ";
}

enum {
    STRV_Naked = (1<<0),
};

struct StreamValueFormat {
    size_t depth;
    uint32_t flags;
    size_t maxdepth;
    size_t maxlength;

    StreamValueFormat(size_t depth, bool naked) :
        maxdepth((size_t)-1),
        maxlength((size_t)-1) {
        this->depth = depth;
        this->flags = naked?STRV_Naked:0;
    }

    StreamValueFormat() :
        depth(0),
        flags(STRV_Naked),
        maxdepth((size_t)-1),
        maxlength((size_t)-1) {
    }
};

template<typename T>
static void streamValue(T &stream, const Any &e,
    const StreamValueFormat &format) {
    size_t depth = format.depth;
    size_t maxdepth = format.maxdepth;
    bool naked = format.flags & STRV_Naked;

    if (naked) {
        streamAnchor(stream, e, depth);
    }

    if (e.type == TYPE_List) {
        if (!maxdepth) {
            stream << "(<...>)";
            if (naked)
                stream << '\n';
        } else {
            StreamValueFormat subfmt(format);
            subfmt.maxdepth = subfmt.maxdepth - 1;

            //auto slist = llvm::cast<ListValue>(e);
            auto it = unwrap<List *>(e);
            if (!it) {
                stream << "()";
                if (naked)
                    stream << '\n';
                return;
            }
            size_t offset = 0;
            if (naked) {
                bool single = !it->next;
            print_terse:
                subfmt.depth = depth;
                subfmt.flags &= ~STRV_Naked;
                streamValue(stream, it->at, subfmt);
                it = it->next;
                offset++;
                while (it) {
                    if (isNested(it->at))
                        break;
                    stream << ' ';
                    streamValue(stream, it->at, subfmt);
                    offset++;
                    it = it->next;
                }
                stream << (single?";\n":"\n");
            //print_sparse:
                while (it) {
                    subfmt.depth = depth + 1;
                    subfmt.flags |= STRV_Naked;
                    auto value = it->at;
                    if ((value.type != TYPE_List) // not a list
                        && (offset >= 1) // not first element in list
                        && it->next // not last element in list
                        && !isNested(it->next->at)) { // next element can be terse packed too
                        single = false;
                        streamAnchor(stream, value, depth + 1);
                        stream << "\\ ";
                        goto print_terse;
                    }
                    if (offset >= subfmt.maxlength) {
                        streamAnchor(stream, value, depth + 1);
                        stream << "<...>\n";
                        return;
                    }
                    streamValue(stream, value, subfmt);
                    offset++;
                    it = it->next;
                }

            } else {
                subfmt.depth = depth + 1;
                subfmt.flags &= ~STRV_Naked;
                stream << '(';
                while (it) {
                    if (offset > 0)
                        stream << ' ';
                    if (offset >= subfmt.maxlength) {
                        stream << "...";
                        break;
                    }
                    streamValue(stream, it->at, subfmt);
                    offset++;
                    it = it->next;
                }
                stream << ')';
                if (naked)
                    stream << '\n';
            }
        }
    } else {
        if (e.type == TYPE_Symbol) {
            //stream << ANSI(STYLE_KEYWORD);
            std::string s = get_symbol_name_stdstr(unwrap<Symbol>(e));
            auto ss = str(s.c_str(),s.size());
            int sz = escapestr(nullptr, ss, "[]{}()\"");
            char dest[sz + 1];
            escapestr(dest, ss, "[]{}()\"");
            stream << dest;
            //stream << ANSI(RESET);
        } else if (e.type == TYPE_String) {
            stream << ANSI(STYLE_STRING);
            stream << '"';
            auto &&ss = *e.str;
            int sz = escapestr(nullptr, ss, "\"");
            char dest[sz + 1];
            escapestr(dest, ss, "\"");
            stream << dest;
            stream << '"';
            stream << ANSI(RESET);
        } else {
            int sz = vsformat(nullptr, "{}", 1, &e);
            char dest[sz + 1];
            vsformat(dest, "{}", 1, &e);
            switch(e.type.value()) {
                case TYPE_Bool: {
                    stream << ANSI(STYLE_KEYWORD);
                    stream << dest;
                    stream << ANSI(RESET);
                } break;
                case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
                case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
                case TYPE_R32: case TYPE_R64: {
                    stream << ANSI(STYLE_NUMBER);
                    stream << dest;
                    stream << ANSI(RESET);
                } break;
                default: {
                    stream << dest;
                } break;
            }
        }
        if (naked)
            stream << '\n';
    }
}

static std::string formatValue(const Any &e, const StreamValueFormat &fmt) {
    std::stringstream ss;
    streamValue(ss, e, fmt);
    return ss.str();
}

static std::string formatValue(const Any &e, size_t depth=0, bool naked=false) {
    std::stringstream ss;
    streamValue(ss, e, StreamValueFormat(depth, naked));
    return ss.str();
}

static void printValue(const Any &e, const StreamValueFormat &fmt) {
    streamValue(std::cout, e, fmt);
}

static void printValue(const Any &e, size_t depth=0, bool naked=false) {
    printValue(e, StreamValueFormat(depth, naked));
}

//------------------------------------------------------------------------------
// S-EXPR PARSER
//------------------------------------------------------------------------------

// (a . (b . (c . (d . NIL)))) -> (d . (c . (b . (a . NIL))))
// this is the mutating version; input lists are modified, direction is inverted
static List *reverse_list_inplace(
    List *l, List *eol = nullptr, List *cat_to = nullptr) {
    List *next = cat_to;
    size_t count = cat_to?cat_to->count:0;
    while (l != eol) {
        ++count;
        List *iternext = l->next;
        l->next = next;
        l->count = count;
        next = l;
        l = iternext;
    }
    return next;
}

// init prev, eol to nullptr

LETFN(list_builder_split)(cont, prev, eol, anchor) // -> prev, eol
    // if we haven't appended anything, that's an error
    if (!unwrap<List *>(prev))
        ERROR("cannot split empty expression");
    auto elem =
        // reverse what we have, up to last split point and wrap result
        // in cell
        list_create(
            wrap(
                reverse_list_inplace(unwrap<List *>(prev), unwrap<List *>(eol))),
            unwrap<List *>(eol),
            unwrap<Anchor>(anchor));
    RET(cont, &elem, prev);
LETFN_END

bool list_builder_is_single_result(List *prev) {
    return prev && !prev->next;
}

Any list_builder_get_single_result(List *prev) {
    return prev?prev->at:none;
}

List *list_builder_get_result(List *prev) {
    return reverse_list_inplace(prev);
}

/*
Any parseFile (const char *path) {
    auto file = MappedFile::open(path);
    if (file) {
        return parseMemory(
            file->strptr(), file->strptr() + file->size(),
            path);
    } else {
        stb_fprintf(stderr, "unable to open file: %s\n", path);
        return const_none;
    }
}
*/

LETFN(ListParser)()
FN_CLASS_BEGIN
FN_CLASS_END
LETFN_END_BINDWITH(
    lexer, // Lexer
    eol, // int
    end_token // int
);
DEF_EXTRACT_CLASS_PTR(ListParser);


LETFN(NakedParser)()
FN_CLASS_BEGIN
    bool is_same_line() {
        return unwrap<Lexer>(lexer).lineno.i32 == lineno.i32;
    }
    bool is_next_line() {
        return unwrap<Lexer>(lexer).lineno.i32 > lineno.i32;
    }
FN_CLASS_END
LETFN_END_BINDWITH(
    lexer, // Lexer
    eol, // int
    column, // int
    end_token, // int
    lineno, // int
    subcolumn, // int
    escape // bool
);
DEF_EXTRACT_CLASS_PTR(NakedParser);

LETFN(parse_naked)(cont, lexer, column /*=0*/, end_token /*=token_none*/)
    // naked depends on any, which depends on list, which depends on naked,
    // therefore nest all three functions so they can see each other
    LETFN(parse_any)(cont, _lexer)
        LETFN(parse_list)(cont, _lexer, end_token)
            auto parser = CAPTURE(ListParser,
                _lexer, // lexer
                (List *)nullptr, // eol
                end_token // end_token
            );

            auto &&lexer = unwrap<Lexer>(_lexer);
            lexer.read_token();

            LETFN(loop)(cont, _parser, _prev)
                auto &&parser = unwrap<ListParser>(_parser);
                auto &&lexer = unwrap<Lexer>(parser.lexer);
                auto &&token = lexer.token.i32;

                if (token == parser.end_token.i32)
                    RET(cont, _prev);
                switch(token) {
                case token_eof:
                    // TODO: point to beginning of list
                    // error_origin = builder.getAnchor();
                    ERROR("missing closing bracket");
                case token_escape: {
                    int column = lexer.column();
                    lexer.read_token();
                    CC(parse_naked,
                        FN(elem)
                            auto nextelem = list_create(elem, unwrap<List *>(_prev));
                            CC(loop, cont, _parser, &nextelem);
                        FN_END_BINDWITH(cont, _parser, _prev),
                        parser.lexer,
                        column,
                        parser.end_token);
                }
                case token_statement:
                    CC(list_builder_split,
                        FN(prev, eol)
                            auto &&parser = unwrap<ListParser>(_parser);
                            auto &&lexer = unwrap<Lexer>(parser.lexer);
                            parser.eol = eol;
                            lexer.read_token();
                            CC(loop, cont, _parser, prev);
                        FN_END_BINDWITH(cont, _parser),
                        _prev, parser.eol, active_anchor);
                default:
                CC(parse_any,
                    FN(elem)
                        auto &&parser = unwrap<ListParser>(_parser);
                        auto &&lexer = unwrap<Lexer>(parser.lexer);
                        auto nextelem = list_create(elem, unwrap<List *>(_prev));
                        lexer.read_token();
                        CC(loop, cont, _parser, &nextelem);
                    FN_END_BINDWITH(cont, _parser, _prev),
                    parser.lexer);
                }
            LETFN_END

            CC(loop,
                FN(_prev)
                    auto &&prev = unwrap<List *>(_prev);
                    RET(cont, list_builder_get_result(prev));
                FN_END_BINDWITH(cont),
                parser, (List *)nullptr);
        LETFN_END

        auto &&lexer = unwrap<Lexer>(_lexer);
        assert(lexer.token.i32 != token_eof);

        switch(lexer.token.i32) {
            case token_open:
                CC(parse_list, cont, lexer, token_close);
            case token_square_open:
                CC(parse_list,
                    FN(elem)
                        auto newelem = list_create(wrap(SYM_SquareList), unwrap<List *>(elem));
                        RET(cont, &newelem);
                    FN_END_BINDWITH(cont),
                    lexer, token_square_close);
            case token_curly_open:
                CC(parse_list,
                    FN(elem)
                        auto newelem = list_create(wrap(SYM_CurlyList), unwrap<List *>(elem));
                        RET(cont, &newelem);
                    FN_END_BINDWITH(cont),
                    lexer, token_curly_close);
            case token_close:
            case token_square_close:
            case token_curly_close:
                ERROR("stray closing bracket");
            case token_string:
            case token_symbol:
            case token_integer:
            case token_real:
                lexer.get_any(cont);
            default:
                ERROR("unexpected token: %c (%i)", *lexer.at(), (int)*lexer.at());
        }
    LETFN_END

    auto parser = CAPTURE(NakedParser,
        lexer, // lexer
        (List *)nullptr, // eol
        column, // column
        end_token, // end_token
        unwrap<Lexer>(lexer).lineno, // lineno
        0, // subcolumn
        false // escape
    );

    LETFN(loop)(cont, _parser, _prev)
        LETFN(repeat)(cont, _parser, _prev)
            auto &&parser = unwrap<NakedParser>(_parser);
            auto &&lexer = unwrap<Lexer>(parser.lexer);
            if ((!parser.escape.i1 || parser.is_next_line())
                && (lexer.column() <= parser.column.i32)) {
                RET(cont, _prev);
            } else {
                CC(loop, cont, _parser, _prev);
            }
        LETFN_END

        auto &&parser = unwrap<NakedParser>(_parser);
        auto &&lexer = unwrap<Lexer>(parser.lexer);

        auto &&token = lexer.token.i32;

        if ((token == token_eof)||(token == parser.end_token.i32))
            RET(cont, _prev);
        if (token == token_escape) {
            parser.escape.i1 = true;
            lexer.read_token();
            if (lexer.lineno.i32 <= parser.lineno.i32)
                ERROR("escape character is not at end of line");
            parser.lineno = lexer.lineno;
            CC(repeat, cont, _parser, _prev);
        } else if (parser.is_next_line()) {
            if (parser.subcolumn.i32 == 0) {
                parser.subcolumn.i32 = lexer.column();
            } else if (lexer.column() != parser.subcolumn.i32) {
                ERROR("indentation mismatch");
            }
            if (parser.column.i32 != parser.subcolumn.i32) {
                if ((parser.column.i32 + 4) != parser.subcolumn.i32) {
                    ERROR("indentations must nest by 4 spaces.");
                }
            }

            parser.escape.i1 = false;
            parser.eol = _prev;
            parser.lineno = lexer.lineno;
            LETFN(loop_add_elems)(cont, _parser, _prev)
                auto &&parser = unwrap<NakedParser>(_parser);
                auto &&lexer = unwrap<Lexer>(parser.lexer);
                // keep adding elements while we're in the same line
                if ((lexer.token.i32 != token_eof)
                        && (lexer.token.i32 != parser.end_token.i32)
                        && parser.is_same_line()) {
                    CC(parse_naked,
                        FN(elem)
                            auto nextelem = list_create(elem, unwrap<List *>(_prev));
                            CC(loop_add_elems, cont, _parser, &nextelem);
                        FN_END_BINDWITH(cont, _parser, _prev),
                        parser.lexer,
                        parser.subcolumn,
                        parser.end_token);
                } else {
                    CC(repeat, cont, _parser, _prev);
                }
            LETFN_END
            CC(loop_add_elems, cont, _parser, _prev);

        } else if (token == token_statement) {
            CC(list_builder_split,
                FN(prev, eol)
                    auto &&parser = unwrap<NakedParser>(_parser);
                    auto &&lexer = unwrap<Lexer>(parser.lexer);
                    parser.eol = eol;
                    lexer.read_token();
                    // if we are in the same line, continue in parent
                    if (parser.is_same_line()) {
                        RET(cont, prev);
                    } else {
                        CC(repeat, cont, _parser, prev);
                    }
                FN_END_BINDWITH(cont, _parser),
                _prev, parser.eol, active_anchor);
        } else {
            CC(parse_any,
                FN(elem)
                    auto &&parser = unwrap<NakedParser>(_parser);
                    auto &&lexer = unwrap<Lexer>(parser.lexer);
                    auto nextelem = list_create(elem, unwrap<List *>(_prev));
                    parser.lineno = lexer.next_lineno;
                    lexer.read_token();
                    CC(repeat, cont, _parser, &nextelem);
                FN_END_BINDWITH(cont, _parser, _prev),
                parser.lexer);
        }
    LETFN_END

    CC(loop,
        FN(_prev)
            auto &&prev = unwrap<List *>(_prev);
            if (list_builder_is_single_result(prev)) {
                RET(cont, list_builder_get_single_result(prev));
            } else {
                RET(cont, list_builder_get_result(prev));
            }
        FN_END_BINDWITH(cont),
        parser, (List *)nullptr);
LETFN_END


LETFN(parse_root)(cont, lexer)
    unwrap<Lexer>(lexer).read_token();

    LETFN(loop)(cont, lexer, prev)
        switch(unwrap<Lexer>(lexer).get_token()) {
            case token_eof:
            case token_none:
                RET(cont, list_builder_get_result(unwrap<List *>(prev)));
            default: {
                CC(parse_naked,
                    FN(elem)
                        auto nextelem = list_create(elem, unwrap<List *>(prev));
                        CC(loop, cont, lexer, &nextelem);
                    FN_END_BINDWITH(cont, lexer, prev),
                    lexer, 1, token_none);
            } break;
        }
    LETFN_END
    CC(loop, cont, lexer, (List *)nullptr);
LETFN_END

//------------------------------------------------------------------------------
// TYPE REFLECTION & GC WALKING
//------------------------------------------------------------------------------

static size_t sizeof_payload(GC_Context &ctx, const Any &from) {
    switch(from.type.value()) {
        case TYPE_Void: case TYPE_Bool:
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_R16: case TYPE_R32: case TYPE_R64:
        case TYPE_Symbol:
        case TYPE_Function:
            return 0;
        case TYPE_Anchor:
            return sizeof(Anchor);
        case TYPE_List:
            return sizeof(List);
        case TYPE_String:
            return sizeof(String);
        case TYPE_BuiltinClosure:
            if (!from.builtin_closure) return 0;
            return sizeof(BuiltinClosure)
                + sizeof(Any) * from.builtin_closure->size - sizeof(Any);
        default:
            INTERNAL_ERROR("cannot determine payload size of type {}\n",
                strc(get_builtin_name(from.type)));
            return 0;
    }
}

static void mark_payload(GC_Context &ctx, Any &val) {
    switch(val.type.value()) {
    case TYPE_String: {
        auto &&s = val.str;
        ctx.move_memory(s->count + 1, s->ptr);
    } break;
    case TYPE_List: {
        // todo: move memory?
        auto &&l = val.list;
        ctx.move(l->at);
        Any next = wrap(l->next);
        ctx.move(next);
        l->next = next.list;
    } break;
    case TYPE_BuiltinClosure:
        if (val.builtin_closure) {
            auto &cl = *const_cast<BuiltinClosure *>(val.builtin_closure);
            for(size_t i = 0; i < cl.size; ++i) {
                ctx.move(cl.args[i]);
            }
        } break;
    default:
        INTERNAL_ERROR("cannot mark payload size of type {}\n",
            strc(get_builtin_name(val.type)));
    }
}

//------------------------------------------------------------------------------
// MAIN
//------------------------------------------------------------------------------

LETFN(cmain)()
#if 0
    auto buf = str("test(-1 0x7fffffff 0xffffffff 0xffffffffff 0x7fffffffffffffff 0xffffffffffffffff 0.00012345 1 2 3.5 10.0 1001.0 1001.1 1001.001 1. .1 0.1 .01 0.01 1e-22 3.1415914159141591415914159 inf nan 1.33 1.0 0.0 \"te\\\"st\\n\\ttest!\")test\ntest((3;))\n");
    auto path = str("<path>");
    Lexer lexer = Lexer::create(buf, path);
    RET(
        FN()
            auto &&repeat = *this;
            int token = unwrap<Lexer>(lexer).read_token();
            stb_printf("token: %s ", get_lexer_token_name(token));
            if (token == token_eof)
                exit_loop(0);
            unwrap<Lexer>(lexer).get_any(
                FN(val)
                    RCALL(print_format, str("= {} {}\n"),
                        strc(get_builtin_name(val.type)), val);
                    RET(repeat);
                FN_END_BINDWITH(repeat));
        FN_END_BINDWITH(lexer));
#elif 1
    auto buf = str("test\n    test test\n(-1 0x7fffffff 0xffffffff 0xffffffffff 0x7fffffffffffffff 0xffffffffffffffff 0.00012345 1 2 3.5 10.0 1001.0 1001.1 1001.001 1. .1 0.1 .01 0.01 1e-22 3.1415914159141591415914159 inf nan 1.33 1.0 0.0 \"te\\\"st\\n\\ttest!\")test\ntest((1 2; 3 4))\n");
    auto path = str("<path>");
    Lexer lexer = Lexer::create(buf, path);
    CC(parse_root,
        FN(val)
            printValue(val);
            exit_loop(0);
        FN_END,
        lexer);
#else
    RCALL(print_format, str("hi {} {} {}!\n"), 1, 2, cmain::run);
    CC(color,
        FN(s)
            stb_printf("%s\n", unwrap<const char *>(s));
            CC(repr_string,
                FN(s2)
                    stb_printf("'%s'\n", unwrap<const char *>(s2));
                    stb_printf("..'%s'\n", unwrap<const char *>(s));
                FN_END_BINDWITH(s),
                str("this \n \r \\ is a \0 s\"t\"ring"));
        FN_END,
        str(ANSI_STYLE_STRING),
        str("the quick brown fox, guys!"));
#endif
LETFN_END

} // namespace bangra

int main(int argc, char ** argv) {

    bangra::support_ansi = isatty(fileno(stdout));
    bangra::init_symbols();

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
