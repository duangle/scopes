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
BEWARE: If you build this with anything else but a recent enough clang,
        you will have a bad time.
*/

#define BANGRA_VERSION_MAJOR 0
#define BANGRA_VERSION_MINOR 7
#define BANGRA_VERSION_PATCH 0

#define BANGRA_DEBUG_CODEGEN 0
#define BANGRA_OPTIMIZE_ASSEMBLY 1

#ifndef BANGRA_CPP
#define BANGRA_CPP

//------------------------------------------------------------------------------
// C HEADER
//------------------------------------------------------------------------------

#include <sys/types.h>
#ifdef _WIN32
#include "mman.h"
#include "stdlib_ex.h"
#include "external/linenoise-ng/include/linenoise.h"
#else
#include <sys/mman.h>
#include <unistd.h>
#include "external/linenoise-ng/include/linenoise.h"
#endif
#include <ctype.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#define STB_SPRINTF_DECORATE(name) stb_##name
#include "external/stb_sprintf.h"
#include "external/cityhash/city.h"

#include <ffi.h>

#if defined __cplusplus
extern "C" {
#endif

#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define EXPORT_DEFINES \
    T(ERANGE) \
    \
    T(O_RDONLY) \
    \
    T(SEEK_SET) \
    T(SEEK_CUR) \
    T(SEEK_END) \
    \
    T(PROT_READ) \
    \
    T(MAP_PRIVATE)

// make sure ffi.cdef() can see C defines we care about
enum {
#define T(NAME) \
    BANGRA_ ## NAME = NAME,
EXPORT_DEFINES
#undef T
#undef EXPORT_DEFINES
};

const char *bangra_interpreter_path;
const char *bangra_interpreter_dir;
size_t bangra_argc;
char **bangra_argv;

// C namespace exports
int unescape_string(char *buf);
int escape_string(char *buf, const char *str, int strcount, const char *quote_chars);

void bangra_strtod(double *v, const char *str, char **str_end, int base );
void bangra_strtoll(int64_t *v, const char* str, char** endptr, int base);
void bangra_strtoull(uint64_t *v, const char* str, char** endptr, int base);

void bangra_f32_mod(float *out, float a, float b);
void bangra_f64_mod(double *out, double a, double b);

bool bangra_is_debug();

const char *bangra_compile_time_date();

#define DEF_UNOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype x);
#define IMPL_UNOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype x) { *out = op x; }

#define DEF_BINOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, ctype b);
#define IMPL_BINOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, ctype b) { *out = a op b; }

#define DEF_BOOL_BINOP_FUNC(tag, ctype, name, op) \
    bool bangra_ ## tag ## _ ## name(ctype a, ctype b);
#define IMPL_BOOL_BINOP_FUNC(tag, ctype, name, op) \
    bool bangra_ ## tag ## _ ## name(ctype a, ctype b) { return a op b; }

#define DEF_WRAP_BINOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, ctype b);
#define IMPL_WRAP_BINOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, ctype b) { *out = op(a, b); }

#define DEF_SHIFTOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, int b);
#define IMPL_SHIFTOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, int b) { *out = a op b; }

#define WALK_BOOL_BINOPS(tag, ctype, T) \
    T(tag, ctype, eq, ==) \
    T(tag, ctype, ne, !=) \
    T(tag, ctype, lt, <) \
    T(tag, ctype, le, <=) \
    T(tag, ctype, gt, >) \
    T(tag, ctype, ge, >=)

#define WALK_ARITHMETIC_BINOPS(tag, ctype, T) \
    T(tag, ctype, add, +) \
    T(tag, ctype, sub, -) \
    T(tag, ctype, mul, *) \
    T(tag, ctype, div, /)
#define WALK_ARITHMETIC_WRAP_BINOPS(tag, ctype, T) \
    T(tag, ctype, pow, powimpl)

#define WALK_INTEGER_ARITHMETIC_BINOPS(tag, ctype, T) \
    T(tag, ctype, bor, |) \
    T(tag, ctype, bxor, ^) \
    T(tag, ctype, band, &) \
    T(tag, ctype, mod, %)
#define WALK_INTEGER_SHIFTOPS(tag, ctype, T) \
    T(tag, ctype, shl, <<) \
    T(tag, ctype, shr, >>)
#define WALK_INTEGER_ARITHMETIC_UNOPS(tag, ctype, T) \
    T(tag, ctype, bnot, ~)

#define WALK_INTEGER_TYPES(T, T2) \
    T(i8, int8_t, T2) \
    T(i16, int16_t, T2) \
    T(i32, int32_t, T2) \
    T(i64, int64_t, T2) \
    T(u8, uint8_t, T2) \
    T(u16, uint16_t, T2) \
    T(u32, uint32_t, T2) \
    T(u64, uint64_t, T2)
#define WALK_REAL_TYPES(T, T2) \
    T(f32, float, T2) \
    T(f64, double, T2)
#define WALK_PRIMITIVE_TYPES(T, T2) \
    WALK_INTEGER_TYPES(T, T2) \
    WALK_REAL_TYPES(T, T2)

WALK_PRIMITIVE_TYPES(WALK_ARITHMETIC_BINOPS, DEF_BINOP_FUNC)
WALK_PRIMITIVE_TYPES(WALK_ARITHMETIC_WRAP_BINOPS, DEF_WRAP_BINOP_FUNC)
WALK_PRIMITIVE_TYPES(WALK_BOOL_BINOPS, DEF_BOOL_BINOP_FUNC)
WALK_INTEGER_TYPES(WALK_INTEGER_ARITHMETIC_BINOPS, DEF_BINOP_FUNC)
WALK_INTEGER_TYPES(WALK_INTEGER_ARITHMETIC_UNOPS, DEF_UNOP_FUNC)
WALK_INTEGER_TYPES(WALK_INTEGER_SHIFTOPS, DEF_SHIFTOP_FUNC)

#if defined __cplusplus
}
#endif

#endif // BANGRA_CPP
#ifdef BANGRA_CPP_IMPL

//#define BANGRA_DEBUG_IL

#include "external/cityhash/city.cpp"

#undef NDEBUG
#ifdef _WIN32
#include <windows.h>
#include "stdlib_ex.h"
#include "dlfcn.h"
#else
// for backtrace
#include <execinfo.h>
#include <dlfcn.h>
//#include "external/linenoise/linenoise.h"
#endif
#include <assert.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdlib.h>
#include <libgen.h>

#include <cstdlib>
//#include <string>
#include <sstream>
#include <iostream>
#include <unordered_set>
#include <deque>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>
#include <llvm-c/Disassembler.h>
#include <llvm-c/Support.h>

#include "llvm/IR/Module.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Object/SymbolSize.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/MultiplexConsumer.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "bangra.bin.h"
#include "bangra.b.bin.h"

} // extern "C"

namespace blobs {
// fix C++11 complaining about > 127 char literals
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wkeyword-macro"
#define char unsigned char
#include "bangra_luasrc.bin.h"
#undef char
#pragma GCC diagnostic pop
}

#define STB_SPRINTF_IMPLEMENTATION
#include "external/stb_sprintf.h"

#pragma GCC diagnostic ignored "-Wvla-extension"
// #pragma GCC diagnostic ignored "-Wzero-length-array"
// #pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
// #pragma GCC diagnostic ignored "-Wembedded-directive"
// #pragma GCC diagnostic ignored "-Wgnu-statement-expression"
#pragma GCC diagnostic ignored "-Wc99-extensions"
// #pragma GCC diagnostic ignored "-Wmissing-braces"
// this one is only enabled for code cleanup
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wdate-time"

//------------------------------------------------------------------------------
// UTILITIES
//------------------------------------------------------------------------------

void bangra_strtod(double *v, const char *str, char **str_end, int base ) {
    *v = std::strtod(str, str_end);
}
void bangra_strtoll(int64_t *v, const char* str, char** endptr, int base) {
    *v = std::strtoll(str, endptr, base);
}
void bangra_strtoull(uint64_t *v, const char* str, char** endptr, int base) {
    *v = std::strtoull(str, endptr, base);
}

static size_t align(size_t offset, size_t align) {
    return (offset + align - 1) & ~(align - 1);
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

int unescape_string(char *buf) {
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

static int escapestrcb(vsformatcb_t cb, void *user, char *buf,
    const char *str, int strcount,
    const char *quote_chars = nullptr) {
    assert(buf);
    const char *fmt_start = str;
    const char *fmt = fmt_start;
    char *p = buf;
#define VSFCB_CHECKWRITE(N) \
    if (((p - buf) + (N)) > B_SNFORMAT) { buf = p = cb(buf, user, p - buf); }
#define VSFCB_PRINT(MAXCOUNT, FMT, SRC) { \
        VSFCB_CHECKWRITE(MAXCOUNT+1); \
        p += snprintf(p, B_SNFORMAT - (p - buf), FMT, SRC); }
    for(;;) {
        char c = *fmt;
        switch(c) {
        case '\n': VSFCB_CHECKWRITE(2); *p++ = '\\'; *p++ = 'n'; break;
        case '\r': VSFCB_CHECKWRITE(2); *p++ = '\\'; *p++ = 'r'; break;
        case '\t': VSFCB_CHECKWRITE(2); *p++ = '\\'; *p++ = 't'; break;
        case 0: if ((fmt - fmt_start) == strcount) goto done;
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

int escape_string(char *buf, const char *str, int strcount, const char *quote_chars) {
    vsformat_cb_ctx ctx;
    if (buf) {
        ctx.dest = buf;
        escapestrcb(vsformat_cb, &ctx, ctx.tmp, str, strcount, quote_chars);
        int l = ctx.dest - buf;
        buf[l] = 0;
        return l;
    } else {
        ctx.count = 0;
        escapestrcb(vsformat_cb_null, &ctx, ctx.tmp, str, strcount, quote_chars);
        return ctx.count + 1;
    }
}

float powimpl(float a, float b) { return std::pow(a, b); }
double powimpl(double a, double b) { return std::pow(a, b); }
// thx to fabian for this one
template<typename T>
inline T powimpl(T base, T exponent) {
    T result = 1, cur = base;
    while (exponent) {
        if (exponent & 1) result *= cur;
        cur *= cur;
        exponent >>= 1;
    }
    return result;
}

void bangra_f32_mod(float *out, float a, float b) { *out = std::fmod(a,b); }
void bangra_f64_mod(double *out, double a, double b) { *out = std::fmod(a,b); }

WALK_PRIMITIVE_TYPES(WALK_ARITHMETIC_BINOPS, IMPL_BINOP_FUNC)
WALK_PRIMITIVE_TYPES(WALK_ARITHMETIC_WRAP_BINOPS, IMPL_WRAP_BINOP_FUNC)
WALK_PRIMITIVE_TYPES(WALK_BOOL_BINOPS, IMPL_BOOL_BINOP_FUNC)
WALK_INTEGER_TYPES(WALK_INTEGER_ARITHMETIC_BINOPS, IMPL_BINOP_FUNC)
WALK_INTEGER_TYPES(WALK_INTEGER_ARITHMETIC_UNOPS, IMPL_UNOP_FUNC)
WALK_INTEGER_TYPES(WALK_INTEGER_SHIFTOPS, IMPL_SHIFTOP_FUNC)

bool bangra_is_debug() {
#ifdef BANGRA_DEBUG
        return true;
#else
        return false;
#endif
}

const char *bangra_compile_time_date() {
    return __DATE__ ", " __TIME__;
}

// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// GetMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement GetMainExecutable
// without being given the address of a function in the main executable).
std::string GetExecutablePath(const char *Argv0) {
  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *MainAddr = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

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
extern "C" {
int stb_printf(const char *fmt, ...) {
    stb_printf_ctx ctx;
    ctx.dest = stdout;
    va_list va;
    va_start(va, fmt);
    int c = stb_vsprintfcb(_printf_cb, &ctx, ctx.tmp, fmt, va);
    va_end(va);
    return c;
}
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

//------------------------------------------------------------------------------
// SYMBOL ENUM
//------------------------------------------------------------------------------

// list of symbols to be exposed as builtins to the default global namespace
#define B_GLOBALS() \
    T(FN_Branch) T(KW_FnCC) T(KW_SyntaxApplyBlock) \
    T(KW_Call) T(KW_CCCall) T(SYM_QuoteForm) T(FN_Dump) \
    T(OP_ICmpEQ) T(OP_ICmpNE) T(FN_AnyExtract) T(FN_AnyWrap) T(FN_IsConstant) \
    T(OP_ICmpUGT) T(OP_ICmpUGE) T(OP_ICmpULT) T(OP_ICmpULE) \
    T(OP_ICmpSGT) T(OP_ICmpSGE) T(OP_ICmpSLT) T(OP_ICmpSLE) \
    T(FN_Purify) T(FN_Unconst) T(FN_TypeOf) T(FN_Bitcast) \
    T(FN_IntToPtr) T(FN_PtrToInt) T(FN_Load) T(FN_Store) \
    T(FN_ExtractValue) T(FN_Trunc) T(FN_ZExt) T(FN_SExt) \
    T(FN_GetElementPtr) T(FN_CompilerError) T(FN_VaCountOf) T(FN_VaAt) \
    T(FN_CompilerMessage) T(FN_Typify) T(FN_Compile) \
    T(FN_FPTrunc) T(FN_FPExt) \
    T(FN_FPToUI) T(FN_FPToSI) \
    T(FN_UIToFP) T(FN_SIToFP) \
    T(OP_Add) T(OP_AddNUW) T(OP_AddNSW) \
    T(OP_Sub) T(OP_SubNUW) T(OP_SubNSW) \
    T(OP_Mul) T(OP_MulNUW) T(OP_MulNSW) \
    T(OP_SDiv) T(OP_UDiv) \
    T(OP_SRem) T(OP_URem) \
    T(OP_Shl) T(OP_LShr) T(OP_AShr) \
    T(OP_BAnd) T(OP_BOr) T(OP_BXor) \
    T(OP_FAdd) T(OP_FSub) T(OP_FMul) T(OP_FDiv) T(OP_FRem)

#define B_MAP_SYMBOLS() \
    T(SYM_Unnamed, "") \
    \
    /* types */ \
    T(TYPE_Void, "void") \
    T(TYPE_Nothing, "Nothing") \
    T(TYPE_Any, "Any") \
    \
    T(TYPE_Type, "type") \
    T(TYPE_Symbol, "Symbol") \
    T(TYPE_Builtin, "Builtin") \
    \
    T(TYPE_Bool, "bool") \
    \
    T(TYPE_I8, "i8") \
    T(TYPE_I16, "i16") \
    T(TYPE_I32, "i32") \
    T(TYPE_I64, "i64") \
    \
    T(TYPE_U8, "u8") \
    T(TYPE_U16, "u16") \
    T(TYPE_U32, "u32") \
    T(TYPE_U64, "u64") \
    \
    T(TYPE_R16, "r16") \
    T(TYPE_F32, "f32") \
    T(TYPE_F64, "f64") \
    T(TYPE_R80, "r80") \
    \
    T(TYPE_List, "list") \
    T(TYPE_Syntax, "Syntax") \
    T(TYPE_Anchor, "Anchor") \
    T(TYPE_String, "string") \
    T(TYPE_Ref, "ref") \
    \
    T(TYPE_Scope, "Scope") \
    T(TYPE_SourceFile, "SourceFile") \
    \
    T(TYPE_Parameter, "Parameter") \
    T(TYPE_Label, "Label") \
    \
    T(TYPE_SizeT, "size_t") \
    \
    /* keywords and macros */ \
    T(KW_CatRest, "::*") T(KW_CatOne, "::@") \
    T(KW_Parenthesis, "...") \
    T(KW_Assert, "assert") T(KW_Break, "break") \
    T(KW_Call, "call") T(KW_CCCall, "cc/call") T(KW_Continue, "continue") \
    T(KW_Define, "define") T(KW_Do, "do") T(KW_DumpSyntax, "dump-syntax") \
    T(KW_Else, "else") T(KW_ElseIf, "elseif") T(KW_EmptyList, "empty-list") \
    T(KW_EmptyTuple, "empty-tuple") T(KW_Escape, "escape") \
    T(KW_Except, "except") T(KW_False, "false") T(KW_Fn, "fn") \
    T(KW_FnTypes, "fn-types") T(KW_FnCC, "fn/cc") T(KW_Globals, "globals") \
    T(KW_If, "if") T(KW_In, "in") T(KW_Let, "let") T(KW_Loop, "loop") \
    T(KW_LoopFor, "loop-for") T(KW_None, "none") T(KW_Null, "null") \
    T(KW_QQuoteSyntax, "qquote-syntax") T(KW_Quote, "quote") \
    T(KW_QuoteSyntax, "quote-syntax") T(KW_Raise, "raise") T(KW_Recur, "recur") \
    T(KW_Return, "return") T(KW_Splice, "splice") T(KW_SyntaxApplyBlock, "syntax-apply-block") \
    T(KW_SyntaxExtend, "syntax-extend") T(KW_True, "true") T(KW_Try, "try") \
    T(KW_Unquote, "unquote") T(KW_UnquoteSplice, "unquote-splice") T(KW_ListEmpty, "eol") \
    T(KW_With, "with") T(KW_XFn, "xfn") T(KW_XLet, "xlet") T(KW_Yield, "yield") \
    \
    /* builtin and global functions */ \
    T(FN_Alignof, "alignof") \
    T(FN_Args, "args") T(FN_Alloc, "alloc") T(FN_Arrayof, "arrayof") \
    T(FN_AnchorPath, "Anchor-path") T(FN_AnchorLineNumber, "Anchor-line-number") \
    T(FN_AnchorColumn, "Anchor-column") T(FN_AnchorOffset, "Anchor-offset") \
    T(FN_AnchorSource, "Anchor-source") \
    T(FN_AnyExtract, "Any-extract") T(FN_AnyWrap, "Any-wrap") \
    T(FN_ActiveAnchor, "active-anchor") T(FN_ActiveFrame, "active-frame") \
    T(FN_Bitcast, "bitcast") T(FN_IntToPtr, "inttoptr") T(FN_PtrToInt, "ptrtoint") \
    T(FN_BlockMacro, "block-macro") \
    T(FN_BlockScopeMacro, "block-scope-macro") T(FN_BoolEq, "bool==") \
    T(FN_BuiltinEq, "Builtin==") \
    T(FN_Branch, "branch") T(FN_IsCallable, "callable?") T(FN_Cast, "cast") \
    T(FN_Concat, "concat") T(FN_Cons, "cons") T(FN_IsConstant, "constant?") \
    T(FN_Countof, "countof") \
    T(FN_Compile, "compile") \
    T(FN_CompilerMessage, "compiler-message") \
    T(FN_CompilerError, "compiler-error") \
    T(FN_CStr, "cstr") T(FN_DatumToSyntax, "datum->syntax") \
    T(FN_DatumToQuotedSyntax, "datum->quoted-syntax") \
    T(FN_DefaultStyler, "default-styler") T(FN_StyleToString, "style->string") \
    T(FN_Disqualify, "disqualify") T(FN_Dump, "dump") \
    T(FN_DumpLabel, "dump-label") \
    T(FN_FormatFrame, "Frame-format") \
    T(FN_ElementType, "element-type") T(FN_IsEmpty, "empty?") \
    T(FN_Enumerate, "enumerate") T(FN_Eval, "eval") \
    T(FN_Exit, "exit") T(FN_Expand, "expand") \
    T(FN_ExternLibrary, "extern-library") \
    T(FN_ExtractMemory, "extract-memory") \
    T(FN_ExtractValue, "extractvalue") \
    T(FN_GetElementPtr, "getelementptr") \
    T(FN_FFISymbol, "ffi-symbol") T(FN_FFICall, "ffi-call") \
    T(FN_FrameEq, "Frame==") T(FN_Free, "free") \
    T(FN_GetExceptionHandler, "get-exception-handler") \
    T(FN_GetScopeSymbol, "get-scope-symbol") T(FN_Hash, "hash") \
    T(OP_ICmpEQ, "icmp==") T(OP_ICmpNE, "icmp!=") \
    T(OP_ICmpUGT, "icmp>u") T(OP_ICmpUGE, "icmp>=u") T(OP_ICmpULT, "icmp<u") T(OP_ICmpULE, "icmp<=u") \
    T(OP_ICmpSGT, "icmp>s") T(OP_ICmpSGE, "icmp>=s") T(OP_ICmpSLT, "icmp<s") T(OP_ICmpSLE, "icmp<=s") \
    T(OP_Add, "add") T(OP_AddNUW, "add-nuw") T(OP_AddNSW, "add-nsw") \
    T(OP_Sub, "sub") T(OP_SubNUW, "sub-nuw") T(OP_SubNSW, "sub-nsw") \
    T(OP_Mul, "mul") T(OP_MulNUW, "mul-nuw") T(OP_MulNSW, "mul-nsw") \
    T(OP_SDiv, "sdiv") T(OP_UDiv, "udiv") \
    T(OP_SRem, "srem") T(OP_URem, "urem") \
    T(OP_Shl, "shl") T(OP_LShr, "lshr") T(OP_AShr, "ashr") \
    T(OP_BAnd, "band") T(OP_BOr, "bor") T(OP_BXor, "bxor") \
    T(OP_FAdd, "fadd") T(OP_FSub, "fsub") T(OP_FMul, "fmul") T(OP_FDiv, "fdiv") T(OP_FRem, "frem") \
    T(FN_FPTrunc, "fptrunc") T(FN_FPExt, "fpext") \
    T(FN_FPToUI, "fptoui") T(FN_FPToSI, "fptosi") \
    T(FN_UIToFP, "uitofp") T(FN_SIToFP, "sitofp") \
    T(FN_ImportC, "import-c") T(FN_IsInteger, "integer?") \
    T(FN_InterpreterVersion, "interpreter-version") \
    T(FN_Iter, "iter") \
    T(FN_IsIterator, "iterator?") T(FN_IsLabel, "label?") \
    T(FN_LabelEq, "Label==") \
    T(FN_LabelNew, "Label-new") T(FN_LabelParameters, "Label-parameters") \
    T(FN_ClosureEq, "Closure==") \
    T(FN_ListAtom, "list-atom?") T(FN_ListCountOf, "list-countof") \
    T(FN_ListLoad, "list-load") T(FN_ListJoin, "list-join") \
    T(FN_ListParse, "list-parse") T(FN_IsList, "list?") T(FN_Load, "load") \
    T(FN_ListAt, "list-at") T(FN_ListNext, "list-next") T(FN_ListCons, "list-cons") \
    T(FN_IsListEmpty, "list-empty?") \
    T(FN_Malloc, "malloc") T(FN_Unconst, "unconst") \
    T(FN_Macro, "macro") T(FN_Max, "max") T(FN_Min, "min") \
    T(FN_MemCpy, "memcpy") \
    T(FN_IsNone, "none?") \
    T(FN_IsNull, "null?") T(FN_OrderedBranch, "ordered-branch") \
    T(FN_ParameterEq, "Parameter==") \
    T(FN_ParameterNew, "Parameter-new") T(FN_ParameterName, "Parameter-name") \
    T(FN_ParameterAnchor, "Parameter-anchor") \
    T(FN_ParseC, "parse-c") T(FN_PointerOf, "pointerof") \
    T(FN_Purify, "purify") \
    T(FN_Write, "io-write") \
    T(FN_Flush, "io-flush") \
    T(FN_Product, "product") T(FN_Prompt, "prompt") T(FN_Qualify, "qualify") \
    T(FN_Range, "range") T(FN_RefNew, "ref-new") T(FN_RefAt, "ref@") \
    T(FN_Repeat, "repeat") T(FN_Repr, "Any-repr") \
    T(FN_Require, "require") T(FN_ScopeOf, "scopeof") T(FN_ScopeAt, "Scope@") \
    T(FN_ScopeEq, "Scope==") \
    T(FN_ScopeNew, "Scope-new") T(FN_ScopeNextSymbol, "Scope-next-symbol") T(FN_SizeOf, "sizeof") \
    T(FN_Slice, "slice") T(FN_Store, "store") \
    T(FN_StringAt, "string@") T(FN_StringCmp, "string-compare") \
    T(FN_StringCountOf, "string-countof") T(FN_StringNew, "string-new") \
    T(FN_StringJoin, "string-join") T(FN_StringSlice, "string-slice") \
    T(FN_StructOf, "structof") \
    T(FN_SymbolEq, "Symbol==") T(FN_SymbolNew, "string->Symbol") \
    T(FN_StringToRawstring, "string->rawstring") \
    T(FN_IsSymbol, "symbol?") \
    T(FN_SyntaxToAnchor, "syntax->anchor") T(FN_SyntaxToDatum, "syntax->datum") \
    T(FN_SyntaxCons, "syntax-cons") T(FN_SyntaxDo, "syntax-do") \
    T(FN_IsSyntaxHead, "syntax-head?") \
    T(FN_SyntaxList, "syntax-list") T(FN_SyntaxQuote, "syntax-quote") \
    T(FN_IsSyntaxQuoted, "syntax-quoted?") \
    T(FN_SyntaxUnquote, "syntax-unquote") \
    T(FN_Translate, "translate") T(FN_Trunc, "trunc") \
    T(FN_ZExt, "zext") T(FN_SExt, "sext") \
    T(FN_TupleOf, "tupleof") T(FN_TypeNew, "type-new") T(FN_TypeName, "type-name") \
    T(FN_TypeSizeOf, "type-sizeof") \
    T(FN_Typify, "typify") \
    T(FN_TypeEq, "type==") T(FN_IsType, "type?") T(FN_TypeOf, "typeof") \
    T(FN_VaCountOf, "va-countof") T(FN_VaAter, "va-iter") T(FN_VaAt, "va@") \
    T(FN_VectorOf, "vectorof") T(FN_XPCall, "xpcall") T(FN_Zip, "zip") \
    T(FN_ZipFill, "zip-fill") \
    \
    /* builtin and global functions with side effects */ \
    T(SFXFN_CopyMemory, "copy-memory!") \
    T(SFXFN_LabelAppendParameter, "label-append-parameter!") \
    T(SFXFN_RefSet, "ref-set!") \
    T(SFXFN_SetExceptionHandler, "set-exception-handler!") \
    T(SFXFN_SetGlobals, "set-globals!") \
    T(SFXFN_SetGlobalApplyFallback, "set-global-apply-fallback!") \
    T(SFXFN_SetScopeSymbol, "set-scope-symbol!") \
    T(SFXFN_SetTypeSymbol, "set-type-symbol!") \
    T(SFXFN_TranslateLabelBody, "translate-label-body!") \
    \
    /* builtin operator functions that can also be used as infix */ \
    T(OP_NotEq, "!=") T(OP_Mod, "%") T(OP_InMod, "%=") T(OP_BitAnd, "&") T(OP_InBitAnd, "&=") \
    T(OP_IFXMul, "*") T(OP_Pow, "**") T(OP_InMul, "*=") T(OP_IFXAdd, "+") T(OP_Incr, "++") \
    T(OP_InAdd, "+=") T(OP_Comma, ",") T(OP_IFXSub, "-") T(OP_Decr, "--") T(OP_InSub, "-=") \
    T(OP_Dot, ".") T(OP_Join, "..") T(OP_Div, "/") T(OP_InDiv, "/=") \
    T(OP_Colon, ":") T(OP_Let, ":=") T(OP_Less, "<") T(OP_LeftArrow, "<-") T(OP_Subtype, "<:") \
    T(OP_ShiftL, "<<") T(OP_LessThan, "<=") T(OP_Set, "=") T(OP_Eq, "==") \
    T(OP_Greater, ">") T(OP_GreaterThan, ">=") T(OP_ShiftR, ">>") T(OP_Tertiary, "?") \
    T(OP_At, "@") T(OP_Xor, "^") T(OP_InXor, "^=") T(OP_And, "and") T(OP_Not, "not") \
    T(OP_Or, "or") T(OP_BitOr, "|") T(OP_InBitOr, "|=") T(OP_BitNot, "~") \
    T(OP_InBitNot, "~=") \
    \
    /* globals */ \
    T(SYM_DebugBuild, "debug-build?") \
    T(SYM_InterpreterDir, "interpreter-dir") \
    T(SYM_InterpreterPath, "interpreter-path") \
    T(SYM_InterpreterTimestamp, "interpreter-timestamp") \
    \
    /* parse-c keywords */ \
    T(SYM_Struct, "struct") \
    T(SYM_Union, "union") \
    T(SYM_TypeDef, "typedef") \
    T(SYM_Enum, "enum") \
    T(SYM_Array, "array") \
    T(SYM_Vector, "vector") \
    T(SYM_FNType, "fntype") \
    T(SYM_External, "external") \
    \
    /* styles */ \
    T(Style_None, "style-none") \
    T(Style_Symbol, "style-symbol") \
    T(Style_String, "style-string") \
    T(Style_Number, "style-number") \
    T(Style_Keyword, "style-keyword") \
    T(Style_Function, "style-function") \
    T(Style_SfxFunction, "style-sfxfunction") \
    T(Style_Operator, "style-operator") \
    T(Style_Instruction, "style-instruction") \
    T(Style_Type, "style-type") \
    T(Style_Comment, "style-comment") \
    T(Style_Error, "style-error") \
    T(Style_Location, "style-location") \
    \
    /* builtins, forms, etc */ \
    T(SYM_FnCCForm, "form-fn-body") \
    T(SYM_QuoteForm, "form-quote") \
    T(SYM_DoForm, "form-do") \
    T(SYM_SyntaxScope, "syntax-scope") \
    \
    T(SYM_ListWildcard, "#list") \
    T(SYM_SymbolWildcard, "#symbol") \
    T(SYM_ThisFnCC, "#this-fn/cc") \
    \
    T(SYM_Compare, "compare") \
    T(SYM_Size, "size") \
    T(SYM_Alignment, "alignment") \
    T(SYM_Unsigned, "unsigned") \
    T(SYM_Bitwidth, "bitwidth") \
    T(SYM_Super, "super") \
    T(SYM_ApplyType, "apply-type") \
    T(SYM_Styler, "styler") \
    \
    /* compile flags */ \
    T(SYM_DumpDisassembly, "dump-disassembly") \
    T(SYM_DumpModule, "dump-module") \
    T(SYM_SkipOpts, "skip-opts") \
    \
    /* ad-hoc builtin names */ \
    T(SYM_ExecuteReturn, "execute-return") \
    T(SYM_RCompare, "rcompare") \
    T(SYM_CountOfForwarder, "countof-forwarder") \
    T(SYM_SliceForwarder, "slice-forwarder") \
    T(SYM_JoinForwarder, "join-forwarder") \
    T(SYM_RCast, "rcast") \
    T(SYM_ROp, "rop") \
    T(SYM_CompareListNext, "compare-list-next") \
    T(SYM_ReturnSafecall, "return-safecall") \
    T(SYM_ReturnError, "return-error") \
    T(SYM_XPCallReturn, "xpcall-return")

enum KnownSymbol {
#define T(sym, name) sym,
#define T0 T
#define T1 T2
#define T2T T2
#define T2(UNAME, LNAME, PFIX, OP) \
    FN_ ## UNAME ## PFIX,
    B_MAP_SYMBOLS()
#undef T
#undef T0
#undef T1
#undef T2
#undef T2T
    SYM_Count,
};

enum {
    TYPE_FIRST = TYPE_Nothing,
    TYPE_LAST = TYPE_Label,

    KEYWORD_FIRST = KW_CatRest,
    KEYWORD_LAST = KW_Yield,

    FUNCTION_FIRST = FN_Alignof,
    FUNCTION_LAST = FN_ZipFill,

    SFXFUNCTION_FIRST = SFXFN_CopyMemory,
    SFXFUNCTION_LAST = SFXFN_TranslateLabelBody,

    OPERATOR_FIRST = OP_NotEq,
    OPERATOR_LAST = OP_InBitNot,

    STYLE_FIRST = Style_None,
    STYLE_LAST = Style_Location,

};

static const char *get_known_symbol_name(KnownSymbol sym) {
    switch(sym) {
#define T(SYM, NAME) case SYM: return #SYM;
#define T0 T
#define T1 T2
#define T2T T2
#define T2(UNAME, LNAME, PFIX, OP) \
    case FN_ ## UNAME ## PFIX: return "FN_" #UNAME #PFIX;
    B_MAP_SYMBOLS()
#undef T
#undef T0
#undef T1
#undef T2
#undef T2T
    case SYM_Count: return "SYM_Count";
    }
}

class NullBuffer : public std::streambuf {
public:
  int overflow(int c) { return c; }
};

class NullStream : public std::ostream {
    public: NullStream() : std::ostream(&m_sb) {}
private:
    NullBuffer m_sb;
};

static NullStream nullout;

//------------------------------------------------------------------------------
// ANSI COLOR FORMATTING
//------------------------------------------------------------------------------

namespace ANSI {
static const char RESET[]           = "\033[0m";
static const char COLOR_BLACK[]     = "\033[30m";
static const char COLOR_RED[]       = "\033[31m";
static const char COLOR_GREEN[]     = "\033[32m";
static const char COLOR_YELLOW[]    = "\033[33m";
static const char COLOR_BLUE[]      = "\033[34m";
static const char COLOR_MAGENTA[]   = "\033[35m";
static const char COLOR_CYAN[]      = "\033[36m";
static const char COLOR_GRAY60[]    = "\033[37m";

static const char COLOR_GRAY30[]    = "\033[30;1m";
static const char COLOR_XRED[]      = "\033[31;1m";
static const char COLOR_XGREEN[]    = "\033[32;1m";
static const char COLOR_XYELLOW[]   = "\033[33;1m";
static const char COLOR_XBLUE[]     = "\033[34;1m";
static const char COLOR_XMAGENTA[]  = "\033[35;1m";
static const char COLOR_XCYAN[]     = "\033[36;1m";
static const char COLOR_WHITE[]     = "\033[37;1m";

static void COLOR_RGB(std::ostream &ost, const char prefix[], int hexcode) {
    ost << prefix
        << ((hexcode >> 16) & 0xff) << ";"
        << ((hexcode >> 8) & 0xff) << ";"
        << (hexcode & 0xff) << "m";
}

static void COLOR_RGB_FG(std::ostream &ost, int hexcode) {
    return COLOR_RGB(ost, "\033[38;2;", hexcode);
}
static void COLOR_RGB_BG(std::ostream &ost, int hexcode) {
    return COLOR_RGB(ost, "\033[48;2;", hexcode);
}


} // namespace ANSI

typedef KnownSymbol Style;

// support 24-bit ANSI colors (ISO-8613-3)
// works on most bash shells as well as windows 10
#define RGBCOLORS
static void ansi_from_style(std::ostream &ost, Style style) {
    switch(style) {
#ifdef RGBCOLORS
    case Style_None: ost << ANSI::RESET; break;
    case Style_Symbol: ANSI::COLOR_RGB_FG(ost, 0xCCCCCC); break;
    case Style_String: ANSI::COLOR_RGB_FG(ost, 0xCC99CC); break;
    case Style_Number: ANSI::COLOR_RGB_FG(ost, 0x99CC99); break;
    case Style_Keyword: ANSI::COLOR_RGB_FG(ost, 0x6699CC); break;
    case Style_Function: ANSI::COLOR_RGB_FG(ost, 0xFFCC66); break;
    case Style_SfxFunction: ANSI::COLOR_RGB_FG(ost, 0xCC6666); break;
    case Style_Operator: ANSI::COLOR_RGB_FG(ost, 0x66CCCC); break;
    case Style_Instruction: ost << ANSI::COLOR_YELLOW; break;
    case Style_Type: ANSI::COLOR_RGB_FG(ost, 0xF99157); break;
    case Style_Comment: ANSI::COLOR_RGB_FG(ost, 0x999999); break;
    case Style_Error: ost << ANSI::COLOR_XRED; break;
    case Style_Location: ANSI::COLOR_RGB_FG(ost, 0x999999); break;
#else
    case Style_None: ost << ANSI::RESET; break;
    case Style_Symbol: ost << ANSI::COLOR_GRAY60; break;
    case Style_String: ost << ANSI::COLOR_XMAGENTA; break;
    case Style_Number: ost << ANSI::COLOR_XGREEN; break;
    case Style_Keyword: ost << ANSI::COLOR_XBLUE; break;
    case Style_Function: ost << ANSI::COLOR_GREEN; break;
    case Style_SfxFunction: ost << ANSI::COLOR_RED; break;
    case Style_Operator: ost << ANSI::COLOR_XCYAN; break;
    case Style_Instruction: ost << ANSI::COLOR_YELLOW; break;
    case Style_Type: ost << ANSI::COLOR_XYELLOW; break;
    case Style_Comment: ost << ANSI::COLOR_GRAY30; break;
    case Style_Error: ost << ANSI::COLOR_XRED; break;
    case Style_Location: ost << ANSI::COLOR_GRAY30; break;
#endif
    default: break;
    }
}

typedef void (*StreamStyleFunction)(std::ostream &, Style);

static void stream_ansi_style(std::ostream &ost, Style style) {
    ansi_from_style(ost, style);
}

static void stream_plain_style(std::ostream &ost, Style style) {
}

static StreamStyleFunction stream_default_style = stream_plain_style;

struct StyledStream {
    StreamStyleFunction _ssf;
    std::ostream &_ost;

    StyledStream(std::ostream &ost, StreamStyleFunction ssf) :
        _ssf(ssf),
        _ost(ost)
    {}

    StyledStream(std::ostream &ost) :
        _ssf(stream_default_style),
        _ost(ost)
    {}

    static StyledStream plain(std::ostream &ost) {
        return StyledStream(ost, stream_plain_style);
    }

    static StyledStream plain(StyledStream &ost) {
        return StyledStream(ost._ost, stream_plain_style);
    }

    template<typename T>
    StyledStream& operator<<(const T &o) { _ost << o; return *this; }
    template<typename T>
    StyledStream& operator<<(const T *o) { _ost << o; return *this; }
    template<typename T>
    StyledStream& operator<<(T &o) { _ost << o; return *this; }
    StyledStream& operator<<(std::ostream &(*o)(std::ostream&)) {
        _ost << o; return *this; }

    StyledStream& operator<<(Style s) {
        _ssf(_ost, s);
        return *this;
    }

    StyledStream& operator<<(bool s) {
        _ssf(_ost, Style_Keyword);
        _ost << (s?"true":"false");
        _ssf(_ost, Style_None);
        return *this;
    }

    StyledStream& stream_number(int8_t x) {
        _ssf(_ost, Style_Number); _ost << (int)x; _ssf(_ost, Style_None);
        return *this;
    }
    StyledStream& stream_number(uint8_t x) {
        _ssf(_ost, Style_Number); _ost << (int)x; _ssf(_ost, Style_None);
        return *this;
    }

    StyledStream& stream_number(double x) {
        size_t size = stb_snprintf( nullptr, 0, "%g", x );
        char dest[size+1];
        stb_snprintf( dest, size + 1, "%g", x );
        _ssf(_ost, Style_Number); _ost << dest; _ssf(_ost, Style_None);
        return *this;
    }
    StyledStream& stream_number(float x) {
        return stream_number((double)x);
    }

    template<typename T>
    StyledStream& stream_number(T x) {
        _ssf(_ost, Style_Number);
        _ost << x;
        _ssf(_ost, Style_None);
        return *this;
    }
};

#define STREAM_STYLED_NUMBER(T) \
    StyledStream& operator<<(StyledStream& ss, T x) { \
        ss.stream_number(x); \
        return ss; \
    }
STREAM_STYLED_NUMBER(int8_t)
STREAM_STYLED_NUMBER(int16_t)
STREAM_STYLED_NUMBER(int32_t)
STREAM_STYLED_NUMBER(int64_t)
STREAM_STYLED_NUMBER(uint8_t)
STREAM_STYLED_NUMBER(uint16_t)
STREAM_STYLED_NUMBER(uint32_t)
STREAM_STYLED_NUMBER(uint64_t)
STREAM_STYLED_NUMBER(float)
STREAM_STYLED_NUMBER(double)

//------------------------------------------------------------------------------
// NONE
//------------------------------------------------------------------------------

struct Nothing {
};

static Nothing none;

static StyledStream& operator<<(StyledStream& ost, const Nothing &value) {
    ost << Style_Keyword << "none" << Style_None;
    return ost;
}

//------------------------------------------------------------------------------
// STRING
//------------------------------------------------------------------------------

struct String {
    size_t count;
    char data[1];

    static String *alloc(size_t count) {
        String *str = (String *)malloc(
            sizeof(size_t) + sizeof(char) * (count + 1));
        str->count = count;
        return str;
    }

    static const String *from(const char *s, size_t count) {
        String *str = (String *)malloc(
            sizeof(size_t) + sizeof(char) * (count + 1));
        str->count = count;
        memcpy(str->data, s, sizeof(char) * count);
        str->data[count] = 0;
        return str;
    }

    static const String *from_cstr(const char *s) {
        return from(s, strlen(s));
    }

    static const String *join(const String *a, const String *b) {
        size_t ac = a->count;
        size_t bc = b->count;
        size_t cc = ac + bc;
        String *str = alloc(cc);
        memcpy(str->data, a->data, sizeof(char) * ac);
        memcpy(str->data + ac, b->data, sizeof(char) * bc);
        str->data[cc] = 0;
        return str;
    }

    template<unsigned N>
    static const String *from(const char (&s)[N]) {
        return from(s, N - 1);
    }

    static const String *from_stdstring(const std::string &s) {
        return from(s.c_str(), s.size());
    }

    StyledStream& stream(StyledStream& ost, const char *escape_chars) const {
        auto c = escape_string(nullptr, data, count, escape_chars);
        char deststr[c + 1];
        escape_string(deststr, data, count, escape_chars);
        ost << deststr;
        return ost;
    }

    const String *substr(int64_t i0, int64_t i1) const {
        assert(i1 >= i0);
        return from(data + i0, (size_t)(i1 - i0));
    }
};

static StyledStream& operator<<(StyledStream& ost, const String *s) {
    ost << Style_String << "\"";
    s->stream(ost, "\"");
    ost << "\"" << Style_None;
    return ost;
}

struct StyledString {
    std::stringstream _ss;
    StyledStream out;

    StyledString() :
        out(_ss) {
    }

    StyledString(StreamStyleFunction ssf) :
        out(_ss, ssf) {
    }

    static StyledString plain() {
        return StyledString(stream_plain_style);
    }

    const String *str() const {
        return String::from_stdstring(_ss.str());
    }
};

static const String *vformat( const char *fmt, va_list va ) {
    va_list va2;
    va_copy(va2, va);
    size_t size = stb_vsnprintf( nullptr, 0, fmt, va2 );
    va_end(va2);
    String *str = String::alloc(size);
    stb_vsnprintf( str->data, size + 1, fmt, va );
    return str;
}

static const String *format( const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    const String *result = vformat(fmt, va);
    va_end(va);
    return result;
}

// computes the levenshtein distance between two strings
static size_t distance(const String *_s, const String *_t) {
    const char *s = _s->data;
    const char *t = _t->data;
    const size_t n = _s->count;
    const size_t m = _t->count;
    if (!m) return n;
    if (!n) return m;

    size_t _v0[m + 1];
    size_t _v1[m + 1];

    size_t *v0 = _v0;
    size_t *v1 = _v1;
    for (size_t i = 0; i <= m; ++i) {
        v0[i] = i;
    }

    for (size_t i = 0; i < n; ++i) {
        v1[0] = i + 1;

        for (size_t j = 0; j < m; ++j) {
            size_t cost = (s[i] == t[j])?0:1;
            v1[j + 1] = std::min(v1[j] + 1,
                std::min(v0[j + 1] + 1, v0[j] + cost));
        }

        size_t *tmp = v0;
        v0 = v1;
        v1 = tmp;
    }

    //std::cout << "lev(" << s << ", " << t << ") = " << v0[m] << std::endl;

    return v0[m];
}


//------------------------------------------------------------------------------
// SYMBOL
//------------------------------------------------------------------------------

static const char SYMBOL_ESCAPE_CHARS[] = " []{}()\"";

//------------------------------------------------------------------------------
// SYMBOL TYPE
//------------------------------------------------------------------------------

struct Symbol {
    typedef KnownSymbol EnumT;
    enum { end_value = SYM_Count };

    struct Hash {
        std::size_t operator()(const bangra::Symbol & s) const {
            return s.hash();
        }
    };

protected:
    struct StringKey {
        struct Hash {
            std::size_t operator()(const StringKey &s) const {
                return CityHash64(s.str->data, s.str->count);
            }
        };

        const String *str;

        bool operator ==(const StringKey &rhs) const {
            if (str->count == rhs.str->count) {
                return !memcmp(str->data, rhs.str->data, str->count);
            }
            return false;
        }
    };

    static std::unordered_map<Symbol, const String *, Hash> map_symbol_name;
    static std::unordered_map<StringKey, Symbol, StringKey::Hash> map_name_symbol;
    static uint64_t next_symbol_id;

    static void verify_unmapped(Symbol id, const String *name) {
        auto it = map_name_symbol.find({ name });
        if (it != map_name_symbol.end()) {
            printf("known symbols %s and %s mapped to same string.\n",
               get_known_symbol_name(id.known_value()),
               get_known_symbol_name(it->second.known_value()));
        }
    }

    static void map_symbol(Symbol id, const String *name) {
        map_name_symbol[{ name }] = id;
        map_symbol_name[id] = name;
    }

    static void map_known_symbol(Symbol id, const String *name) {
        verify_unmapped(id, name);
        map_symbol(id, name);
    }

    static Symbol get_symbol(const String *name) {
        auto it = map_name_symbol.find({ name });
        if (it != map_name_symbol.end()) {
            return it->second;
        }
        Symbol id = Symbol::wrap(++next_symbol_id);
        // make copy
        map_symbol(id, String::from(name->data, name->count));
        return id;
    }

    static const String *get_symbol_name(Symbol id) {
        auto it = map_symbol_name.find(id);
        assert (it != map_symbol_name.end());
        return it->second;
    }

    uint64_t _value;

    Symbol(uint64_t tid) :
        _value(tid) {
    }

public:
    static Symbol wrap(uint64_t value) {
        return { value };
    }

    Symbol() :
        _value(SYM_Unnamed) {}

    Symbol(EnumT id) :
        _value(id) {
    }

    template<unsigned N>
    Symbol(const char (&str)[N]) :
        _value(get_symbol(String::from(str))._value) {
    }

    Symbol(const String *str) :
        _value(get_symbol(str)._value) {
    }

    bool is_known() const {
        return _value < end_value;
    }

    EnumT known_value() const {
        assert(is_known());
        return (EnumT)_value;
    }

    // for std::map support
    bool operator < (Symbol b) const {
        return _value < b._value;
    }

    bool operator ==(Symbol b) const {
        return _value == b._value;
    }

    bool operator !=(Symbol b) const {
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

    const String *name() const {
        return get_symbol_name(*this);
    }

    static void _init_symbols() {
    #define T(sym, name) map_known_symbol(sym, String::from(name));
    #define T0 T
    #define T1 T2
    #define T2T T2
    #define T2(UNAME, LNAME, PFIX, OP) \
        map_known_symbol(FN_ ## UNAME ## PFIX, String::from(#LNAME #OP));
        B_MAP_SYMBOLS()
    #undef T
    #undef T0
    #undef T1
    #undef T2
    #undef T2T
    }

    StyledStream& stream(StyledStream& ost) const {
        auto s = name();
        assert(s);
        ost << Style_Symbol << "'";
        s->stream(ost, SYMBOL_ESCAPE_CHARS);
        ost << Style_None;
        return ost;
    }

};

std::unordered_map<Symbol, const String *, Symbol::Hash> Symbol::map_symbol_name;
std::unordered_map<Symbol::StringKey, Symbol, Symbol::StringKey::Hash> Symbol::map_name_symbol;
uint64_t Symbol::next_symbol_id = SYM_Count;

static StyledStream& operator<<(StyledStream& ost, Symbol sym) {
    return sym.stream(ost);
}

//------------------------------------------------------------------------------
// SOURCE FILE
//------------------------------------------------------------------------------

struct SourceFile {
protected:
    static std::unordered_map<Symbol, SourceFile *, Symbol::Hash> file_cache;

    SourceFile(Symbol _path) :
        path(_path),
        fd(-1),
        length(0),
        ptr(MAP_FAILED),
        _str(nullptr) {
    }

public:
    Symbol path;
    int fd;
    int length;
    void *ptr;
    const String *_str;

    void close() {
        assert(!_str);
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

    bool is_open() {
        return fd != -1;
    }

    const char *strptr() {
        assert(is_open() || _str);
        return (const char *)ptr;
    }

    static SourceFile *from_file(Symbol _path) {
        auto it = file_cache.find(_path);
        if (it != file_cache.end()) {
            return it->second;
        }
        SourceFile *file = new SourceFile(_path);
        file->fd = ::open(_path.name()->data, O_RDONLY);
        if (file->fd >= 0) {
            file->length = lseek(file->fd, 0, SEEK_END);
            file->ptr = mmap(nullptr,
                file->length, PROT_READ, MAP_PRIVATE, file->fd, 0);
            if (file->ptr != MAP_FAILED) {
                file_cache[_path] = file;
                return file;
            }
            file->close();
        }
        file->close();
        delete file;
        return nullptr;
    }

    static SourceFile *from_string(Symbol _path, const String *str) {
        SourceFile *file = new SourceFile(_path);
        // loading from string buffer rather than file
        file->ptr = (void *)str->data;
        file->length = str->count;
        file->_str = str;
        return file;
    }

    StyledStream &stream(StyledStream &ost, int offset,
        const char *indent = "    ") {
        auto str = strptr();
        if (offset >= length) {
            ost << "<cannot display location in source file>" << std::endl;
            return ost;
        }
        auto start = offset;
        auto send = offset;
        while (start > 0) {
            if (str[start-1] == '\n') {
                break;
            }
            start = start - 1;
        }
        while (start < offset) {
            if (!isspace(str[start])) {
                break;
            }
            start = start + 1;
        }
        while (send < length) {
            if (str[send] == '\n') {
                break;
            }
            send = send + 1;
        }
        auto linelen = send - start;
        char line[linelen + 1];
        memcpy(line, str + start, linelen);
        line[linelen] = 0;
        ost << indent << line << std::endl;
        auto column = offset - start;
        if (column > 0) {
            ost << indent;
            for (int i = 0; i < column; ++i) {
                ost << " ";
            }
            ost << Style_Operator << "^" << Style_None << std::endl;
        }
        return ost;
    }
};

std::unordered_map<Symbol, SourceFile *, Symbol::Hash> SourceFile::file_cache;

//------------------------------------------------------------------------------
// ANCHOR
//------------------------------------------------------------------------------

struct Anchor {
protected:
    Anchor(SourceFile *_file, int _lineno, int _column, int _offset) :
        file(_file),
        lineno(_lineno),
        column(_column),
        offset(_offset) {}

public:
    SourceFile *file;
    int lineno;
    int column;
    int offset;

    Symbol path() const {
        return file->path;
    }

    static const Anchor *from(
        SourceFile *_file, int _lineno, int _column, int _offset = 0) {
        return new Anchor(_file, _lineno, _column, _offset);
    }

    StyledStream& stream(StyledStream& ost) const {
        ost << Style_Location;
        auto ss = StyledStream::plain(ost);
        ss << path().name()->data << ":" << lineno << ":" << column << ":";
        ost << Style_None;
        return ost;
    }

    StyledStream &stream_source_line(StyledStream &ost, const char *indent = "    ") const {
        file->stream(ost, offset, indent);
        return ost;
    }
};

static StyledStream& operator<<(StyledStream& ost, const Anchor *anchor) {
    return anchor->stream(ost);
}

//------------------------------------------------------------------------------
// TYPE
//------------------------------------------------------------------------------

static void location_error(const String *msg);

struct Type {
    typedef KnownSymbol EnumT;

    struct Hash {
        std::size_t operator()(const bangra::Type & s) const {
            return s.hash();
        }
    };

protected:
    Symbol _name;

public:
    Type(EnumT id) :
        _name(id) {
    }

    Type(Symbol name) :
        _name(name) {
    }

    template<unsigned N>
    Type(const char (&str)[N]) :
        _name(Symbol(String::from(str))) {
    }

    Type(const String *str) :
        _name(Symbol(str)) {
    }

    bool is_known() const {
        return _name.is_known();
    }

    EnumT known_value() const {
        return _name.known_value();
    }

    uint64_t value() const {
        return _name.value();
    }

    // for std::map support
    bool operator < (Type b) const {
        return _name < b._name;
    }

    bool operator ==(Type b) const {
        return _name == b._name;
    }

    bool operator !=(Type b) const {
        return _name != b._name;
    }

    bool operator ==(EnumT b) const {
        return _name == b;
    }

    bool operator !=(EnumT b) const {
        return _name != b;
    }

    std::size_t hash() const {
        return _name.hash();
    }

    Symbol name() const {
        return _name;
    }

    StyledStream& stream(StyledStream& ost) const {
        ost << Style_Type;
        ost << name().name()->data;
        ost << Style_None;
        return ost;
    }
};

static bool is_opaque(Type T);
static size_t size_of(Type T);
static size_t align_of(Type T);

static StyledStream& operator<<(StyledStream& ost, Type type) {
    return type.stream(ost);
}

//------------------------------------------------------------------------------
// TYPE CHECK PREDICATES
//------------------------------------------------------------------------------

static void verify(Type typea, Type typeb) {
    if (typea != typeb) {
        StyledString ss;
        ss.out << "type " << typea << " expected, got " << typeb;
        location_error(ss.str());
    }
}

template<KnownSymbol T>
static void verify(Type type) {
    verify(T, type);
}

static void verify_integer(Type type) {
    switch(type.value()) {
    case TYPE_Bool:
    case TYPE_I8:
    case TYPE_I16:
    case TYPE_I32:
    case TYPE_I64:
    case TYPE_U8:
    case TYPE_U16:
    case TYPE_U32:
    case TYPE_U64: return;
    default: {
        StyledString ss;
        ss.out << "integer or bool type expected, got " << type;
        location_error(ss.str());
    } break;
    }
}

static void verify_real(Type type) {
    switch(type.value()) {
    case TYPE_F32:
    case TYPE_F64: return;
    default: {
        StyledString ss;
        ss.out << "real type expected, got " << type;
        location_error(ss.str());
    } break;
    }
}

static void verify_range(size_t idx, size_t count) {
    if (idx >= count) {
        StyledString ss;
        ss.out << "index out of range (" << idx
            << " >= " << count << ")";
        location_error(ss.str());
    }
}

//------------------------------------------------------------------------------
// TYPE FACTORIES
//------------------------------------------------------------------------------

template<typename T>
struct TypeFactory {
    typedef std::unordered_map<Type, T, Type::Hash> DescMap;
    DescMap descs;

    bool is(Type type) {
        return descs.count(type);
    }

    T &get(Type type) {
        auto it = descs.find(type);
        assert(it != descs.end());
        return it->second;
    }

    std::pair<typename DescMap::iterator, bool> insert(Type type) {
        return descs.insert({type, T()});
    }
};

//------------------------------------------------------------------------------
// BUILTIN
//------------------------------------------------------------------------------

struct Builtin {
    typedef KnownSymbol EnumT;
protected:
    Symbol _name;

public:
    Builtin(EnumT name) :
        _name(name) {
    }

    EnumT value() const {
        return _name.known_value();
    }

    bool operator < (Builtin b) const { return _name < b._name; }
    bool operator ==(Builtin b) const { return _name == b._name; }
    bool operator !=(Builtin b) const { return _name != b._name; }
    bool operator ==(EnumT b) const { return _name == b; }
    bool operator !=(EnumT b) const { return _name != b; }
    std::size_t hash() const { return _name.hash(); }
    Symbol name() const { return _name; }

    StyledStream& stream(StyledStream& ost) const {
        ost << Style_Function; name().name()->stream(ost, ""); ost << Style_None;
        return ost;
    }
};

static StyledStream& operator<<(StyledStream& ost, Builtin builtin) {
    return builtin.stream(ost);
}

//------------------------------------------------------------------------------
// ANY
//------------------------------------------------------------------------------

struct Syntax;
struct List;
struct Label;
struct Parameter;
struct Scope;

struct Any {
    Type type;
    union {
        char content[8];
        bool i1;
        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        size_t sizeval;
        float f32;
        double f64;
        Type typeref;
        const String *string;
        Symbol symbol;
        const Syntax *syntax;
        const Anchor *anchor;
        const List *list;
        Label *label;
        Parameter *parameter;
        Builtin builtin;
        Scope *scope;
        Any *ref;
        void *pointer;
    };

    Any(Nothing x) : type(TYPE_Nothing), u64(0) {}
    Any(Type x) : type(TYPE_Type), typeref(x) {}
    Any(bool x) : type(TYPE_Bool), i1(x) {}
    Any(int8_t x) : type(TYPE_I8), i8(x) {}
    Any(int16_t x) : type(TYPE_I16), i16(x) {}
    Any(int32_t x) : type(TYPE_I32), i32(x) {}
    Any(int64_t x) : type(TYPE_I64), i64(x) {}
    Any(uint8_t x) : type(TYPE_U8), u8(x) {}
    Any(uint16_t x) : type(TYPE_U16), u16(x) {}
    Any(uint32_t x) : type(TYPE_U32), u32(x) {}
    Any(uint64_t x) : type(TYPE_U64), u64(x) {}
    Any(float x) : type(TYPE_F32), f32(x) {}
    Any(double x) : type(TYPE_F64), f64(x) {}
    Any(const String *x) : type(TYPE_String), string(x) {}
    Any(Symbol x) : type(TYPE_Symbol), symbol(x) {}
    Any(const Syntax *x) : type(TYPE_Syntax), syntax(x) {}
    Any(const Anchor *x) : type(TYPE_Anchor), anchor(x) {}
    Any(const List *x) : type(TYPE_List), list(x) {}
    Any(Label *x) : type(TYPE_Label), label(x) {}
    Any(Parameter *x) : type(TYPE_Parameter), parameter(x) {}
    Any(Builtin x) : type(TYPE_Builtin), builtin(x) {}
    Any(Scope *x) : type(TYPE_Scope), scope(x) {}
    Any(Any *x) : type(TYPE_Ref), ref(x) {}
    template<unsigned N>
    Any(const char (&str)[N]) : type(TYPE_String), string(String::from(str)) {}
    // a catch-all for unsupported types
    template<typename T>
    Any(const T &x);

    Any toref() {
        Any *pvalue = new Any(*this);
        return pvalue;
    }

    static Any from_pointer(Type type, void *ptr) {
        Any val = none;
        val.type = type;
        val.pointer = ptr;
        return val;
    }

    template<KnownSymbol T>
    void verify() const {
        bangra::verify<T>(type);
    }

    template<KnownSymbol T>
    void verify_indirect() const;
    Type indirect_type() const;

    operator const List *() const { verify<TYPE_List>(); return list; }
    operator const Syntax *() const { verify<TYPE_Syntax>(); return syntax; }
    operator const Anchor *() const { verify<TYPE_Anchor>(); return anchor; }
    operator const String *() const { verify<TYPE_String>(); return string; }
    operator Label *() const { verify<TYPE_Label>(); return label; }
    operator Scope *() const { verify<TYPE_Scope>(); return scope; }
    operator Parameter *() const { verify<TYPE_Parameter>(); return parameter; }

    struct AnyStreamer {
        StyledStream& ost;
        const Type &type;
        bool annotate_type;
        AnyStreamer(StyledStream& _ost, const Type &_type, bool _annotate_type) :
            ost(_ost), type(_type), annotate_type(_annotate_type) {}
        void stream_type_suffix() const {
            if (annotate_type) {
                ost << Style_Operator << ":" << Style_None;
                ost << type;
            }
        }
        template<typename T>
        void naked(const T &x) const {
            ost << x;
        }
        template<typename T>
        void typed(const T &x) const {
            ost << x;
            stream_type_suffix();
        }
    };

    StyledStream& stream(StyledStream& ost, bool annotate_type = true) const {
        AnyStreamer as(ost, type, annotate_type);
        switch(type.value()) {
            case TYPE_Nothing: as.naked(none); break;
            case TYPE_Type: as.naked(typeref); break;
            case TYPE_Bool: as.naked(i1); break;
            case TYPE_I8: as.typed(i8); break;
            case TYPE_I16: as.typed(i16); break;
            case TYPE_I32: as.naked(i32); break;
            case TYPE_I64: as.typed(i64); break;
            case TYPE_U8: as.typed(u8); break;
            case TYPE_U16: as.typed(u16); break;
            case TYPE_U32: as.typed(u32); break;
            case TYPE_U64: as.typed(u64); break;
            case TYPE_F32: as.naked(f32); break;
            case TYPE_F64: as.typed(f64); break;
            case TYPE_String: as.naked(string); break;
            case TYPE_Symbol: as.naked(symbol); break;
            case TYPE_Syntax: as.naked(syntax); break;
            case TYPE_Anchor: as.typed(anchor); break;
            case TYPE_List: as.naked(list); break;
            case TYPE_Builtin: as.typed(builtin); break;
            case TYPE_Label: as.typed(label); break;
            case TYPE_Parameter: as.typed(parameter); break;
            case TYPE_Scope: as.typed(scope); break;
            case TYPE_Ref:
                ost << Style_Operator << "[" << Style_None;
                ref->stream(ost);
                ost << Style_Operator << "]" << Style_None;
                as.stream_type_suffix();
                break;
            default: as.typed(pointer); break;
        }
        return ost;
    }

    bool operator ==(const Any &other) const;

    bool operator !=(const Any &other) const {
        return !(*this == other);
    }

    size_t hash() const;
};

static StyledStream& operator<<(StyledStream& ost, Any value) {
    return value.stream(ost);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

static Any wrap_pointer(Type type, void *ptr);

//------------------------------------------------------------------------------
// POINTER TYPE
//------------------------------------------------------------------------------

struct PointerInfo {
    Type element_type;
    size_t stride;

    PointerInfo() : element_type(TYPE_Nothing) {}

    void *getelementptr(void *src, size_t i) {
        return (void *)((char *)src + stride * i);
    }

    Any unpack(void *src) {
        return wrap_pointer(element_type, src);
    }
    static size_t size() {
        return sizeof(uint64_t);
    }
};

static TypeFactory<PointerInfo> pointers;

static Type Pointer(Type element_type) {
    std::stringstream ss;
    ss << element_type.name().name()->data << "*";
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = pointers.insert(type);
    if (result.second) {
        auto &&pi = result.first->second;
        pi.element_type = element_type;
        if (is_opaque(element_type)) {
            pi.stride = 0;
        } else {
            pi.stride = size_of(element_type);
        }
    }
    return type;
}

//------------------------------------------------------------------------------
// ARRAY TYPE
//------------------------------------------------------------------------------

struct StorageInfo {
    size_t size;
    size_t align;
};

struct ArrayInfo : StorageInfo {
    Type element_type;
    size_t count;
    size_t stride;

    ArrayInfo() : element_type(TYPE_Nothing), count(0) {}

    void *getelementptr(void *src, size_t i) {
        verify_range(i, count);
        return (void *)((char *)src + stride * i);
    }

    Any unpack(void *src, size_t i) {
        return wrap_pointer(type_at_index(i), getelementptr(src, i));
    }

    Type type_at_index(size_t i) {
        verify_range(i, count);
        return element_type;
    }
};

static TypeFactory<ArrayInfo> arrays;

static Type Array(Type element_type, size_t count) {
    std::stringstream ss;
    ss << "[" << element_type.name().name()->data << " x " << count << "]";
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = arrays.insert(type);
    if (result.second) {
        auto &&ai = result.first->second;
        ai.element_type = element_type;
        ai.count = count;
        ai.stride = size_of(element_type);
        ai.size = ai.stride * count;
        ai.align = align_of(element_type);
    }
    return type;
}

//------------------------------------------------------------------------------
// VECTOR TYPE
//------------------------------------------------------------------------------

struct VectorInfo : StorageInfo {
    Type element_type;
    size_t count;
    size_t stride;

    VectorInfo() : element_type(TYPE_Nothing), count(0) {}

    void *getelementptr(void *src, size_t i) {
        verify_range(i, count);
        return (void *)((char *)src + stride * i);
    }

    Any unpack(void *src, size_t i) {
        return wrap_pointer(type_at_index(i), getelementptr(src, i));
    }

    Type type_at_index(size_t i) {
        verify_range(i, count);
        return element_type;
    }
};

static TypeFactory<VectorInfo> vectors;

static Type Vector(Type element_type, size_t count) {
    std::stringstream ss;
    ss << "<" << element_type.name().name()->data << " x " << count << ">";
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = vectors.insert(type);
    if (result.second) {
        auto &&ai = result.first->second;
        ai.element_type = element_type;
        ai.count = count;
        ai.stride = size_of(element_type);
        ai.size = ai.stride * count;
        ai.align = align_of(element_type);
    }
    return type;
}

//------------------------------------------------------------------------------
// FUNCTION TYPE
//------------------------------------------------------------------------------

enum {
    // takes variable number of arguments
    FF_Variadic = (1 << 0),
    // can be evaluated at compile time
    FF_Pure = (1 << 1),
};

struct FunctionInfo {
    Type return_type;
    std::vector<Type> argument_types;
    uint32_t flags;

    bool vararg() const {
        return flags & FF_Variadic;
    }
    bool pure() const {
        return flags & FF_Pure;
    }
    FunctionInfo() : return_type(TYPE_Nothing), flags(0) {}
};

static TypeFactory<FunctionInfo> functions;

static Type Function(Type return_type,
    const std::vector<Type> &argument_types, uint32_t flags = 0) {
    std::stringstream ss;
    ss <<  return_type.name().name()->data;
    if (flags & FF_Pure) {
        ss << "<~";
    } else {
        ss << "<-";
    }
    ss << "(";
    for (size_t i = 0; i < argument_types.size(); ++i) {
        if (i > 0) {
            ss << " ";
        }
        ss << argument_types[i].name().name()->data;
    }
    if (flags & FF_Variadic) {
        ss << " ...";
    }
    ss << ")";
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = functions.insert(type);
    if (result.second) {
        auto &&ti = result.first->second;
        ti.return_type = return_type;
        ti.argument_types = argument_types;
        ti.flags = flags;
    }
    return type;
}

static bool is_function_pointer(Type type) {
    if (!pointers.is(type)) return false;
    auto &&pi = pointers.get(type);
    return functions.is(pi.element_type);
}

static bool is_pure_function_pointer(Type type) {
    if (!pointers.is(type)) return false;
    auto &&pi = pointers.get(type);
    if (!functions.is(pi.element_type)) return false;
    auto &&fi = functions.get(pi.element_type);
    return fi.flags & FF_Pure;
}

//------------------------------------------------------------------------------
// UNION TYPE
//------------------------------------------------------------------------------

struct UnionInfo : StorageInfo {
    std::vector<Type> types;

    Any unpack(void *src, size_t i) {
        return wrap_pointer(type_at_index(i), src);
    }

    Type type_at_index(size_t i) {
        verify_range(i, types.size());
        return types[i];
    }
};

static TypeFactory<UnionInfo> unions;

static Type Union(const std::vector<Type> &types) {
    std::stringstream ss;
    ss << "{";
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            ss << " | ";
        }
        ss << types[i].name().name()->data;
    }
    ss << "}";
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = unions.insert(type);
    if (result.second) {
        auto &&ui = result.first->second;
        ui.types = types;
        size_t sz = 0;
        size_t al = 1;
        for (size_t i = 0; i < types.size(); ++i) {
            Type ET = types[i];
            sz = std::max(sz, size_of(ET));
            al = std::max(al, align_of(ET));
        }
        ui.size = align(sz, al);
        ui.align = al;
    }

    return type;
}

//------------------------------------------------------------------------------
// CONSTANT TYPE
//------------------------------------------------------------------------------

struct ConstantInfo {
    Type type;

    ConstantInfo() : type(TYPE_Void) {
    }
};

static TypeFactory<ConstantInfo> constants;

static Type Constant(Type T) {
    std::stringstream ss;
    ss << "!" << T.name().name()->data;
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = constants.insert(type);
    if (result.second) {
        auto &&ci = result.first->second;
        ci.type = T;
    }
    return type;
}

//------------------------------------------------------------------------------
// TYPED LABEL TYPE
//------------------------------------------------------------------------------

struct TypedLabelInfo {
    std::vector<Type> types;

    bool has_constants() const {
        for (size_t i = 0; i < types.size(); ++i) {
            if (constants.is(types[i]))
                return true;
        }
        return false;
    }

    Type strip_constants() const;
};

static TypeFactory<TypedLabelInfo> typed_labels;

static Type TypedLabel(const std::vector<Type> &types) {
    assert(!types.empty());
    std::stringstream ss;
    ss << "λ(";
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            ss << " ";
        }
        ss << types[i].name().name()->data;
    }
    ss << ")";
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = typed_labels.insert(type);
    if (result.second) {
        auto &&tli = result.first->second;
        tli.types = types;
    }
    return type;
}

Type TypedLabelInfo::strip_constants() const {
    std::vector<Type> dest_types;
    dest_types.reserve(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        Type T = types[i];
        if (constants.is(T)) {
            ConstantInfo &ci = constants.get(T);
            T = ci.type;
        }
        dest_types.push_back(T);
    }
    return TypedLabel(dest_types);
}

//------------------------------------------------------------------------------
// TYPESET TYPE
//------------------------------------------------------------------------------

struct TypeSetInfo {
    std::unordered_set<Type, Type::Hash> types;
};

static TypeFactory<TypeSetInfo> typesets;

static Type TypeSet(const std::vector<Type> &types) {
    std::unordered_set<Type, Type::Hash> typeset;

    for (auto &&entry : types) {
        if (typesets.is(entry)) {
            auto &&tsi = typesets.get(entry);
            for (auto &&t : tsi.types) {
                typeset.insert(t);
            }
        } else {
            typeset.insert(entry);
        }
    }

    assert(!typeset.empty());

    if (typeset.size() == 1) {
        return *typeset.begin();
    }

    std::vector<Type> type_array;
    type_array.reserve(typeset.size());
    for (auto &&entry : typeset) {
        type_array.push_back(entry);
    }

    std::sort(type_array.begin(), type_array.end());

    std::stringstream ss;
    for (size_t i = 0; i < type_array.size(); ++i) {
        if (i > 0) {
            ss << " | ";
        }
        ss << type_array[i].name().name()->data;
    }
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = typesets.insert(type);
    if (result.second) {
        auto &&tsi = result.first->second;
        tsi.types = typeset;
    }

    return type;
}

//------------------------------------------------------------------------------
// TUPLE TYPE
//------------------------------------------------------------------------------

struct TupleInfo : StorageInfo {
    std::vector<Type> types;
    std::vector<size_t> offsets;

    void *getelementptr(void *src, size_t i) {
        verify_range(i, offsets.size());
        return (void *)((char *)src + offsets[i]);
    }

    Any unpack(void *src, size_t i) {
        return wrap_pointer(type_at_index(i), getelementptr(src, i));
    }

    Type type_at_index(size_t i) {
        verify_range(i, types.size());
        return types[i];
    }
};

static TypeFactory<TupleInfo> tuples;

static Type Tuple(const std::vector<Type> &types) {
    std::stringstream ss;
    ss << "{";
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            ss << " ";
        }
        ss << types[i].name().name()->data;
    }
    ss << "}";
    auto type = Type(Symbol(String::from_stdstring(ss.str())));
    auto result = tuples.insert(type);
    if (result.second) {
        auto &&ti = result.first->second;
        ti.types = types;
        ti.offsets.resize(types.size());
        size_t sz = 0;
        size_t al = 1;
        for (size_t i = 0; i < types.size(); ++i) {
            Type ET = types[i];
            size_t etal = align_of(ET);
            sz = align(sz, etal);
            ti.offsets[i] = sz;
            al = std::max(al, etal);
            sz += size_of(ET);
        }
        ti.size = align(sz, al);
        ti.align = al;
    }
    return type;
}

//------------------------------------------------------------------------------
// TYPENAME
//------------------------------------------------------------------------------

struct TypenameInfo {
    Type storage_type;
    bool finalized;
    std::vector<Symbol> field_names;
    std::unordered_map<Symbol, Any, Symbol::Hash> symbols;

    TypenameInfo() :
        storage_type(TYPE_Nothing),
        finalized(false)
    {}

    void finalize(Type _type);

    void bind(Symbol name, const Any &value) {
        auto ret = symbols.insert({ name, value });
        if (!ret.second) {
            ret.first->second = value;
        }
    }

    void del(Symbol name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) {
            symbols.erase(it);
        }
    }

    bool lookup(Symbol name, Any &dest) const {
        auto it = symbols.find(name);
        if (it != symbols.end()) {
            dest = it->second;
            return true;
        }
        return false;
    }
};

static TypeFactory<TypenameInfo> typenames;

void TypenameInfo::finalize(Type _type) {
    if (finalized) {
        location_error(String::from("typename is already final"));
    }
    if (typenames.is(_type)) {
        location_error(String::from("cannot use typename as storage type"));
    }
#if 0
    if (constants.is(_type)) {
        location_error(String::from("cannot use constant as storage type"));
    }
#endif

    storage_type = _type;
    finalized = true;
}

// always generates a new type
static Type Typename(Symbol name, bool builtin = false) {
    const String *str = name.name();
    auto ss = StyledString::plain();
    if (!builtin) {
        ss.out << "$";
    }
    str->stream(ss.out, " *");
    const String *newstr = ss.str();

    auto type = Type(Symbol(newstr));
    size_t idx = 2;
    while (typenames.is(type)) {
        // keep testing until we hit a name that's free
        auto ss = StyledString::plain();
        ss.out << newstr->data << idx++;
        type = Type(Symbol(ss.str()));
    }
    auto result = typenames.insert(type);
    assert(result.second);

    return type;
}

static Type storage_type(Type T) {
    if (typenames.is(T)) {
        auto &tn = typenames.get(T);
        if (!tn.finalized) {
            StyledString ss;
            ss.out << "type " << T << " is opaque";
            location_error(ss.str());
        }
        return tn.storage_type;
    } else {
        return T;
    }
}

//------------------------------------------------------------------------------
// TYPE INQUIRIES
//------------------------------------------------------------------------------

enum Category {
    CAT_Basic = 0,
    CAT_Pointer,
    CAT_Array,
    CAT_Vector,
    CAT_Tuple,
    CAT_Union,
    CAT_Typename,
    CAT_TypedLabel,
    CAT_TypeSet,
    CAT_Function,
    CAT_Constant,
};

static Category category_of(Type T) {
    if (pointers.is(T)) {
        return CAT_Pointer;
    } else if (typenames.is(T)) {
        return CAT_Typename;
    } else if (arrays.is(T)) {
        return CAT_Array;
    } else if (vectors.is(T)) {
        return CAT_Vector;
    } else if (tuples.is(T)) {
        return CAT_Tuple;
    } else if (unions.is(T)) {
        return CAT_Union;
    } else if (functions.is(T)) {
        return CAT_Function;
    } else if (constants.is(T)) {
        return CAT_Constant;
    }
    return CAT_Basic;
}


template<Category cat>
static void verify_category(Type T) {
    if (category_of(T) != cat) {
        StyledString ss;
        switch(cat) {
        case CAT_Basic: ss.out << "primitive"; break;
        case CAT_Pointer: ss.out << "pointer"; break;
        case CAT_Array: ss.out << "array"; break;
        case CAT_Vector: ss.out << "vector"; break;
        case CAT_Tuple: ss.out << "tuple"; break;
        case CAT_Union: ss.out << "union"; break;
        case CAT_Typename: ss.out << "typename"; break;
        case CAT_TypedLabel: ss.out << "typed label"; break;
        case CAT_TypeSet: ss.out << "typeset"; break;
        case CAT_Function: ss.out << "function"; break;
        }
        ss.out << " expected, got " << T;
        location_error(ss.str());
    }
}

static void verify_function_pointer(Type type) {
    if (!is_function_pointer(type)) {
        StyledString ss;
        ss.out << "function pointer expected, got " << type;
        location_error(ss.str());
    }
}

static bool is_opaque(Type T) {
    switch (T.value()) {
    case TYPE_Void:
    case TYPE_Nothing: return true;
    default: break;
    }
    switch(category_of(T)) {
    case CAT_Constant:
    case CAT_Function: return true;
    case CAT_Typename: {
        auto &&tn = typenames.get(T);
        return !tn.finalized;
    } break;
    default: break;
    }
    return false;
}

static size_t size_of(Type T) {
    switch(T.value()) {
    case TYPE_Bool: return sizeof(bool);

    case TYPE_I8:
    case TYPE_U8: return sizeof(int8_t);

    case TYPE_I16:
    case TYPE_U16: return sizeof(int16_t);

    case TYPE_I32:
    case TYPE_U32: return sizeof(int32_t);

    case TYPE_I64:
    case TYPE_U64: return sizeof(int64_t);

    case TYPE_F32: return sizeof(float);
    case TYPE_F64: return sizeof(double);
    default: {
    } break;
    }

    switch(category_of(T)) {
    case CAT_Pointer: return sizeof(uint64_t);
    case CAT_Array: return arrays.get(T).size;
    case CAT_Vector: return vectors.get(T).size;
    case CAT_Tuple: return tuples.get(T).size;
    case CAT_Union: return unions.get(T).size;
    case CAT_Typename: {
        auto &&tn = typenames.get(T);
        if (tn.finalized) {
            return size_of(tn.storage_type);
        }
    } break;
    default: break;
    }

    StyledString ss;
    ss.out << "opaque type " << T << " has no size";
    location_error(ss.str());
    return -1;
}

static size_t align_of(Type T) {
    switch(T.value()) {
    case TYPE_Bool: return sizeof(bool);

    case TYPE_I8:
    case TYPE_U8: return sizeof(int8_t);

    case TYPE_I16:
    case TYPE_U16: return sizeof(int16_t);

    case TYPE_I32:
    case TYPE_U32: return sizeof(int32_t);

    case TYPE_I64:
    case TYPE_U64: return sizeof(int64_t);

    case TYPE_F32: return sizeof(float);
    case TYPE_F64: return sizeof(double);
    default: {
    } break;
    }

    switch(category_of(T)) {
    case CAT_Pointer: return sizeof(uint64_t);
    case CAT_Array: return arrays.get(T).align;
    case CAT_Vector: return vectors.get(T).align;
    case CAT_Tuple: return tuples.get(T).align;
    case CAT_Union: return unions.get(T).align;
    case CAT_Typename: {
        auto &&tn = typenames.get(T);
        if (tn.finalized) {
            return align_of(tn.storage_type);
        }
    } break;
    default: break;
    }

    StyledString ss;
    ss.out << "opaque type " << T << " has no size";
    location_error(ss.str());
    return -1;
}

static Any wrap_pointer(Type type, void *ptr) {
    Any result = none;
    result.type = type;
    switch(type.value()) {
    case TYPE_I8: case TYPE_U8:
        result.u8 = *(uint8_t *)ptr;
        return result;
    case TYPE_I16: case TYPE_U16:
        result.u16 = *(uint16_t *)ptr;
        return result;
    case TYPE_I32: case TYPE_U32:
        result.u32 = *(uint32_t *)ptr;
        return result;
    case TYPE_I64: case TYPE_U64:
        result.u64 = *(uint64_t *)ptr;
        return result;
    case TYPE_F32:
        result.f32 = *(float *)ptr;
        return result;
    case TYPE_F64:
        result.f64 = *(float *)ptr;
        return result;
    case TYPE_Bool:
        result.i1 = *(bool *)ptr;
        return result;
    default: break;
    }

    switch(category_of(type)) {
    case CAT_Pointer:
        result.pointer = *(void **)ptr;
        return result;
    case CAT_Typename: {
        auto &&tn = typenames.get(type);
        if (!tn.finalized) {
            StyledString ss;
            ss.out << "FFI: cannot wrap opaque type " << type;
            location_error(ss.str());
        }
        result = wrap_pointer(tn.storage_type, ptr);
        result.type = type;
        return result;
    } break;
    case CAT_Array:
    case CAT_Vector:
    case CAT_Tuple:
    case CAT_Union:
        result.pointer = ptr;
        return result;
    default: break;
    };

    StyledString ss;
    ss.out << "FFI: cannot wrap data of type " << type;
    location_error(ss.str());
    return none;
}

//------------------------------------------------------------------------------
// ANY HASH & COMPARISON
//------------------------------------------------------------------------------

size_t Any::hash() const {
    if (type == TYPE_String) {
        return CityHash64(string->data, string->count);
    }
    if (is_opaque(type))
        return 0;
    Type T = storage_type(type);
    switch(T.value()) {
    case TYPE_Bool: return std::hash<bool>{}(i1);
    case TYPE_U8: case TYPE_I8: return std::hash<uint8_t>{}(u8);
    case TYPE_U16: case TYPE_I16: return std::hash<uint16_t>{}(u16);
    case TYPE_U32: case TYPE_I32: return std::hash<uint32_t>{}(u32);
    case TYPE_U64: case TYPE_I64: return std::hash<uint64_t>{}(u64);
    case TYPE_F32: return std::hash<float>{}(f32);
    case TYPE_F64: return std::hash<double>{}(f64);
    default: break;
    }
    switch(category_of(T)) {
    case CAT_Pointer:
        return std::hash<void *>{}(pointer);
    case CAT_Array: {
        auto &&ai = arrays.get(T);
        size_t h = 0;
        for (size_t i = 0; i < ai.count; ++i) {
            h = HashLen16(h, ai.unpack(pointer, i).hash());
        }
        return h;
    } break;
    case CAT_Vector:{
        auto &&vi = vectors.get(T);
        size_t h = 0;
        for (size_t i = 0; i < vi.count; ++i) {
            h = HashLen16(h, vi.unpack(pointer, i).hash());
        }
        return h;
    } break;
    case CAT_Tuple:{
        auto &&ti = tuples.get(T);
        size_t h = 0;
        for (size_t i = 0; i < ti.types.size(); ++i) {
            h = HashLen16(h, ti.unpack(pointer, i).hash());
        }
        return h;
    } break;
    case CAT_Union:
        return CityHash64((const char *)pointer, size_of(T));
    default: {
        StyledStream ss(std::cout);
        ss << "unhashable value: " << T << std::endl;
        assert(false && "unhashable value");
    } break;
    }
    return 0;
}

bool Any::operator ==(const Any &other) const {
    if (type != other.type) return false;
    if (type == TYPE_String) {
        if (string->count != other.string->count)
            return false;
        return !memcmp(string->data, other.string->data, string->count);
    }
    if (is_opaque(type))
        return true;
    Type T = storage_type(type);
    switch(T.value()) {
    case TYPE_Bool: return (i1 == other.i1);
    case TYPE_U8: case TYPE_I8: return (u8 == other.u8);
    case TYPE_U16: case TYPE_I16: return (u16 == other.u16);
    case TYPE_U32: case TYPE_I32: return (u32 == other.u32);
    case TYPE_U64: case TYPE_I64: return (u64 == other.u64);
    case TYPE_F32: return (f32 == other.f32);
    case TYPE_F64: return (f64 == other.f64);
    default: break;
    }
    switch(category_of(T)) {
    case CAT_Pointer:
        return pointer == other.pointer;
    case CAT_Array: {
        auto &&ai = arrays.get(T);
        for (size_t i = 0; i < ai.count; ++i) {
            if (ai.unpack(pointer, i) != ai.unpack(other.pointer, i))
                return false;
        }
        return true;
    } break;
    case CAT_Vector:{
        auto &&vi = vectors.get(T);
        for (size_t i = 0; i < vi.count; ++i) {
            if (vi.unpack(pointer, i) != vi.unpack(other.pointer, i))
                return false;
        }
        return true;
    } break;
    case CAT_Tuple:{
        auto &&ti = tuples.get(T);
        for (size_t i = 0; i < ti.types.size(); ++i) {
            if (ti.unpack(pointer, i) != ti.unpack(other.pointer, i))
                return false;
        }
        return true;
    } break;
    case CAT_Union:
        return !memcmp(pointer, other.pointer, size_of(T));
    default:
        assert(false && "incomparable values");
    }
    return false;
}

//------------------------------------------------------------------------------
// ERROR HANDLING
//------------------------------------------------------------------------------

static const Anchor *_active_anchor = nullptr;

static void set_active_anchor(const Anchor *anchor) {
    assert(anchor);
    _active_anchor = anchor;
}

static const Anchor *get_active_anchor() {
    return _active_anchor;
}

struct Exception {
    const Anchor *anchor;
    const String *msg;
    const String *translate;

    Exception(const Anchor *_anchor, const String *_msg) :
        anchor(_anchor),
        msg(_msg),
        translate(nullptr) {}
};

static void location_error(const String *msg) {
    throw Exception(_active_anchor, msg);
}

//------------------------------------------------------------------------------
// SCOPE
//------------------------------------------------------------------------------

struct Scope {
protected:
    Scope(Scope *_parent = nullptr) : parent(_parent) {}

public:
    std::unordered_map<Symbol, Any, Symbol::Hash> map;
    Scope *parent;

    size_t count() const {
        return map.size();
    }

    size_t totalcount() const {
        const Scope *self = this;
        size_t count = 0;
        while (self) {
            count += self->count();
            self = self->parent;
        }
        return count;
    }

    void bind(Symbol name, const Any &value) {
        auto ret = map.insert(std::pair<Symbol, Any>(name, value));
        if (!ret.second) {
            ret.first->second = value;
        }
    }

    void del(Symbol name) {
        auto it = map.find(name);
        if (it != map.end()) {
            map.erase(it);
        }
    }

    std::vector<Symbol> find_closest_match(Symbol name) const {
        const String *s = name.name();
        std::unordered_set<Symbol, Symbol::Hash> done;
        std::vector<Symbol> best_syms;
        size_t best_dist = (size_t)-1;
        const Scope *self = this;
        do {
            for (auto &&k : self->map) {
                Symbol sym = k.first;
                if (done.count(sym))
                    continue;
                size_t dist = distance(s, sym.name());
                if (dist == best_dist) {
                    best_syms.push_back(sym);
                } else if (dist < best_dist) {
                    best_dist = dist;
                    best_syms = { sym };
                }
                done.insert(sym);
            }
            self = self->parent;
        } while (self);
        std::sort(best_syms.begin(), best_syms.end());
        return best_syms;
    }

    bool lookup(Symbol name, Any &dest) const {
        const Scope *self = this;
        do {
            auto it = self->map.find(name);
            if (it != self->map.end()) {
                dest = it->second;
                return true;
            }
            self = self->parent;
        } while (self);
        return false;
    }

    StyledStream &stream(StyledStream &ss) {
        size_t totalcount = this->totalcount();
        size_t count = this->count();
        ss << Style_Keyword << "Scope" << Style_Comment << "<" << Style_None
            << format("%i+%i symbols", count, totalcount - count)->data
            << Style_Comment << ">" << Style_None;
        return ss;
    }

    static Scope *from(Scope *_parent = nullptr) {
        return new Scope(_parent);
    }
};

static Scope *globals = Scope::from();

static StyledStream& operator<<(StyledStream& ost, Scope *scope) {
    scope->stream(ost);
    return ost;
}

//------------------------------------------------------------------------------
// SYNTAX OBJECTS
//------------------------------------------------------------------------------

struct Syntax {
protected:
    Syntax(const Anchor *_anchor, const Any &_datum, bool _quoted) :
        anchor(_anchor),
        datum(_datum),
        quoted(_quoted) {}

public:
    const Anchor *anchor;
    Any datum;
    bool quoted;

    static const Syntax *from(const Anchor *_anchor, const Any &_datum) {
        assert(_anchor);
        return new Syntax(_anchor, _datum, false);
    }

    static const Syntax *from_quoted(const Anchor *_anchor, const Any &_datum) {
        assert(_anchor);
        return new Syntax(_anchor, _datum, true);
    }
};

static Any unsyntax(const Any &e) {
    e.verify<TYPE_Syntax>();
    return e.syntax->datum;
}

static Any maybe_unsyntax(const Any &e) {
    if (e.type == TYPE_Syntax) {
        return e.syntax->datum;
    } else {
        return e;
    }
}

static StyledStream& operator<<(StyledStream& ost, const Syntax *value) {
    ost << value->anchor << value->datum;
    return ost;
}

//------------------------------------------------------------------------------
// LIST
//------------------------------------------------------------------------------

static const List *EOL = nullptr;


#define LIST_POOLSIZE 0x10000
struct List {
protected:
    List(const Any &_at, const List *_next, size_t _count) :
        at(_at),
        next(_next),
        count(_count) {}

public:
    Any at;
    const List *next;
    size_t count;

    Any first() const {
        if (this == EOL) {
            return none;
        } else {
            return at;
        }
    }

    static const List *from(const Any &_at, const List *_next) {
        return new List(_at, _next, (_next != EOL)?(_next->count + 1):1);
    }

    static const List *from(const Any *values, int N) {
        const List *list = EOL;
        for (int i = N - 1; i >= 0; --i) {
            list = from(values[i], list);
        }
        return list;
    }

    template<unsigned N>
    static const List *from(const Any (&values)[N]) {
        return from(values, N);
    }

    static const List *join(const List *a, const List *b);
};

// (a . (b . (c . (d . NIL)))) -> (d . (c . (b . (a . NIL))))
// this is the mutating version; input lists are modified, direction is inverted
const List *reverse_list_inplace(
    const List *l, const List *eol = EOL, const List *cat_to = EOL) {
    const List *next = cat_to;
    size_t count = 0;
    if (cat_to != EOL) {
        count = cat_to->count;
    }
    while (l != eol) {
        count = count + 1;
        const List *iternext = l->next;
        const_cast<List *>(l)->next = next;
        const_cast<List *>(l)->count = count;
        next = l;
        l = iternext;
    }
    return next;
}

const List *List::join(const List *la, const List *lb) {
    const List *l = lb;
    while (la != EOL) {
        l = List::from(la->at, l);
        la = la->next;
    }
    return reverse_list_inplace(l, lb, lb);
}

static StyledStream& operator<<(StyledStream& ost, const List *list);

//------------------------------------------------------------------------------
// S-EXPR LEXER & PARSER
//------------------------------------------------------------------------------

#define B_TOKENS() \
    T(none, -1) \
    T(eof, 0) \
    T(open, '(') \
    T(close, ')') \
    T(square_open, '[') \
    T(square_close, ']') \
    T(curly_open, '{') \
    T(curly_close, '}') \
    T(string, '"') \
    T(quote, '\'') \
    T(symbol, 'S') \
    T(escape, '\\') \
    T(statement, ';') \
    T(number, 'N')

enum Token {
#define T(NAME, VALUE) tok_ ## NAME = VALUE,
    B_TOKENS()
#undef T
};

static const char *get_token_name(Token tok) {
    switch(tok) {
#define T(NAME, VALUE) case tok_ ## NAME: return #NAME;
    B_TOKENS()
#undef T
    }
}

static const char TOKEN_TERMINATORS[] = "()[]{}\"';#,";

struct LexerParser {
    // LEXER
    //////////////////////////////

    void verify_good_taste(char c) {
        if (c == '\t') {
            location_error(String::from("please use spaces instead of tabs."));
        }
    }

    Token token;
    int base_offset;
    SourceFile *file;
    const char *input_stream;
    const char *eof;
    const char *cursor;
    const char *next_cursor;
    int lineno;
    int next_lineno;
    const char *line;
    const char *next_line;

    const char *string;
    int string_len;

    Any value;

    LexerParser(SourceFile *_file, int offset = 0) :
            value(none) {
        file = _file;
        input_stream = file->strptr();
        token = tok_eof;
        base_offset = offset;
        eof = file->strptr() + file->length;
        cursor = next_cursor = input_stream;
        lineno = next_lineno = 1;
        line = next_line = input_stream;
    }

    int offset() {
        return base_offset + (cursor - input_stream);
    }

    int column() {
        return cursor - line + 1;
    }

    int next_column() {
        return next_cursor - next_line + 1;
    }

    const Anchor *anchor() {
        return Anchor::from(file, lineno, column(), offset());
    }

    char next() {
        char c = next_cursor[0];
        verify_good_taste(c);
        next_cursor = next_cursor + 1;
        return c;
    }

    bool is_eof() {
        return next_cursor == eof;
    }

    void newline() {
        next_lineno = next_lineno + 1;
        next_line = next_cursor;
    }

    void select_string() {
        string = cursor;
        string_len = next_cursor - cursor;
    }

    void read_single_symbol() {
        select_string();
    }

    void read_symbol() {
        bool escape = false;
        while (true) {
            if (is_eof()) {
                break;
            }
            char c = next();
            if (escape) {
                if (c == '\n') {
                    newline();
                }
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (isspace(c) || strchr(TOKEN_TERMINATORS, c)) {
                next_cursor = next_cursor - 1;
                break;
            }
        }
        select_string();
    }

    void read_string(char terminator) {
        bool escape = false;
        while (true) {
            if (is_eof()) {
                location_error(String::from("unterminated sequence"));
                break;
            }
            char c = next();
            if (c == '\n') {
                newline();
            }
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == terminator) {
                break;
            }
        }
        select_string();
    }

    void read_comment() {
        int col = column();
        while (true) {
            if (is_eof()) {
                break;
            }
            int next_col = next_column();
            char c = next();
            if (c == '\n') {
                newline();
            } else if (!isspace(c) && (next_col <= col)) {
                next_cursor = next_cursor - 1;
                break;
            }
        }
    }

    template<unsigned N>
    bool is_suffix(const char (&str)[N]) {
        if (string_len != (N - 1)) {
            return false;
        }
        return !strncmp(string, str, N - 1);
    }

    enum {
        RN_Invalid = 0,
        RN_Untyped = 1,
        RN_Typed = 2,
    };

    template<typename T>
    int read_integer(void (*strton)(T *, const char*, char**, int)) {
        char *cend;
        errno = 0;
        T srcval;
        strton(&srcval, cursor, &cend, 0);
        if ((cend == cursor)
            || (errno == ERANGE)
            || (cend > eof)) {
            return RN_Invalid;
        }
        value = Any(srcval);
        next_cursor = cend;
        if ((cend != eof)
            && (!isspace(*cend))
            && (!strchr(TOKEN_TERMINATORS, *cend))) {
            if (strchr(".e", *cend)) return false;
            // suffix
            next_token();
            read_symbol();
            return RN_Typed;
        } else {
            return RN_Untyped;
        }
    }

    template<typename T>
    int read_real(void (*strton)(T *, const char*, char**, int)) {
        char *cend;
        errno = 0;
        T srcval;
        strton(&srcval, cursor, &cend, 0);
        if ((cend == cursor)
            || (errno == ERANGE)
            || (cend > eof)) {
            return RN_Invalid;
        }
        value = Any(srcval);
        next_cursor = cend;
        if ((cend != eof)
            && (!isspace(*cend))
            && (!strchr(TOKEN_TERMINATORS, *cend))) {
            // suffix
            next_token();
            read_symbol();
            return RN_Typed;
        } else {
            return RN_Untyped;
        }
    }

    bool select_integer_suffix() {
        if (is_suffix(":i8")) { value = Any(value.i8); return true; }
        else if (is_suffix(":i16")) { value = Any(value.i16); return true; }
        else if (is_suffix(":i32")) { value = Any(value.i32); return true; }
        else if (is_suffix(":i64")) { value = Any(value.i64); return true; }
        else if (is_suffix(":u8")) { value = Any(value.u8); return true; }
        else if (is_suffix(":u16")) { value = Any(value.u16); return true; }
        else if (is_suffix(":u32")) { value = Any(value.u32); return true; }
        else if (is_suffix(":u64")) { value = Any(value.u64); return true; }
        else if (is_suffix(":isize")) { value = Any(value.i64); return true; }
        else if (is_suffix(":usize")) { value = Any(value.u64); return true; }
        else {
            StyledString ss;
            ss.out << "invalid suffix for integer literal: "
                << String::from(string, string_len);
            location_error(ss.str());
            return false;
        }
    }

    bool select_real_suffix() {
        if (is_suffix(":f32")) { value = Any((float)value.f64); return true; }
        else if (is_suffix(":f64")) { value = Any(value.f64); return true; }
        else {
            StyledString ss;
            ss.out << "invalid suffix for floating point literal: "
                << String::from(string, string_len);
            location_error(ss.str());
            return false;
        }
    }

    bool read_int64() {
        switch(read_integer(bangra_strtoll)) {
        case RN_Invalid: return false;
        case RN_Untyped:
            if ((value.i64 >= -0x80000000ll) && (value.i64 <= 0x7fffffffll)) {
                value = Any(int32_t(value.i64));
            } else if ((value.i64 >= 0x80000000ll) && (value.i64 <= 0xffffffffll)) {
                value = Any(uint32_t(value.i64));
            }
            return true;
        case RN_Typed:
            return select_integer_suffix();
        default: assert(false); return false;
        }
    }
    bool read_uint64() {
        switch(read_integer(bangra_strtoull)) {
        case RN_Invalid: return false;
        case RN_Untyped:
            return true;
        case RN_Typed:
            return select_integer_suffix();
        default: assert(false); return false;
        }
    }
    bool read_real64() {
        switch(read_real(bangra_strtod)) {
        case RN_Invalid: return false;
        case RN_Untyped:
            value = Any(float(value.f64));
            return true;
        case RN_Typed:
            return select_real_suffix();
        default: assert(false); return false;
        }
    }

    void next_token() {
        lineno = next_lineno;
        line = next_line;
        cursor = next_cursor;
        set_active_anchor(anchor());
    }

    Token read_token() {
        char c;
    skip:
        next_token();
        if (is_eof()) { token = tok_eof; goto done; }
        c = next();
        if (c == '\n') { newline(); }
        if (isspace(c)) { goto skip; }
        if (c == '#') { read_comment(); goto skip; }
        else if (c == '(') { token = tok_open; }
        else if (c == ')') { token = tok_close; }
        else if (c == '[') { token = tok_square_open; }
        else if (c == ']') { token = tok_square_close; }
        else if (c == '{') { token = tok_curly_open; }
        else if (c == '}') { token = tok_curly_close; }
        else if (c == '\\') { token = tok_escape; }
        else if (c == '"') { token = tok_string; read_string(c); }
        else if (c == ';') { token = tok_statement; }
        else if (c == '\'') { token = tok_quote; }
        else if (c == ',') { token = tok_symbol; read_single_symbol(); }
        else if (read_int64() || read_uint64() || read_real64()) { token = tok_number; }
        else { token = tok_symbol; read_symbol(); }
    done:
        return token;
    }

    Any get_symbol() {
        char dest[string_len + 1];
        memcpy(dest, string, string_len);
        dest[string_len] = 0;
        auto size = unescape_string(dest);
        return Symbol(String::from(dest, size));
    }
    Any get_string() {
        auto len = string_len - 2;
        char dest[len + 1];
        memcpy(dest, string + 1, len);
        dest[len] = 0;
        auto size = unescape_string(dest);
        return String::from(dest, size);
    }
    Any get_number() {
        return value;
    }
    Any get() {
        if (token == tok_number) {
            return get_number();
        } else if (token == tok_symbol) {
            return get_symbol();
        } else if (token == tok_string) {
            return get_string();
        } else {
            return none;
        }
    }

    // PARSER
    //////////////////////////////

    struct ListBuilder {
        LexerParser &lexer;
        const List *prev;
        const List *eol;

        ListBuilder(LexerParser &_lexer) :
            lexer(_lexer),
            prev(EOL),
            eol(EOL) {}

        void append(const Any &value) {
            assert(value.type == TYPE_Syntax);
            prev = List::from(value, prev);
        }

        bool is_empty() const {
            return (prev == EOL);
        }

        bool is_expression_empty() const {
            return (prev == EOL);
        }

        void reset_start() {
            eol = prev;
        }

        void split(const Anchor *anchor) {
            // reverse what we have, up to last split point and wrap result
            // in cell
            prev = List::from(
                Syntax::from(anchor,reverse_list_inplace(prev, eol)), eol);
            reset_start();
        }

        const List *get_result() {
            return reverse_list_inplace(prev);
        }
    };

    // parses a list to its terminator and returns a handle to the first cell
    const List *parse_list(Token end_token) {
        const Anchor *start_anchor = this->anchor();
        ListBuilder builder(*this);
        this->read_token();
        while (true) {
            if (this->token == end_token) {
                break;
            } else if (this->token == tok_escape) {
                int column = this->column();
                this->read_token();
                builder.append(parse_naked(column, end_token));
            } else if (this->token == tok_eof) {
                set_active_anchor(start_anchor);
                location_error(String::from("unclosed open bracket"));
            } else if (this->token == tok_statement) {
                builder.split(this->anchor());
                this->read_token();
            } else {
                builder.append(parse_any());
                this->read_token();
            }
        }
        return builder.get_result();
    }

    // parses the next sequence and returns it wrapped in a cell that points
    // to prev
    Any parse_any(bool quoted = false) {
        assert(this->token != tok_eof);
        const Anchor *anchor = this->anchor();
        if (this->token == tok_open) {
            return Syntax::from(anchor, parse_list(tok_close));
        } else if (this->token == tok_square_open) {
            return Syntax::from(anchor,
                List::from(Symbol("square-list"),
                    parse_list(tok_square_close)));
        } else if (this->token == tok_curly_open) {
            return Syntax::from(anchor,
                List::from(Symbol("curly-list"),
                    parse_list(tok_curly_close)));
        } else if ((this->token == tok_close)
            || (this->token == tok_square_close)
            || (this->token == tok_curly_close)) {
            location_error(String::from("stray closing bracket"));
        } else if (this->token == tok_string) {
            return Syntax::from(anchor, get_string());
        } else if (this->token == tok_symbol) {
            return Syntax::from(anchor, get_symbol());
        } else if (this->token == tok_number) {
            return Syntax::from(anchor, get_number());
        } else if (!quoted && (this->token == tok_quote)) {
            this->read_token();
            const Syntax *sx = parse_any(true);
            const_cast<Syntax *>(sx)->anchor = anchor;
            const_cast<Syntax *>(sx)->quoted = true;
            if (sx->datum.type == TYPE_List) {
                return Syntax::from_quoted(anchor,
                    List::from({
                        Syntax::from_quoted(anchor, Builtin(SYM_QuoteForm)),
                        sx }));
            }
            return sx;
        } else {
            location_error(format("unexpected token: %c (%i)",
                this->cursor[0], (int)this->cursor[0]));
        }
        return none;
    }

    Any parse_naked(int column, Token end_token) {
        int lineno = this->lineno;

        bool escape = false;
        int subcolumn = 0;

        const Anchor *anchor = this->anchor();
        ListBuilder builder(*this);

        while (this->token != tok_eof) {
            if (this->token == end_token) {
                break;
            } else if (this->token == tok_escape) {
                escape = true;
                this->read_token();
                if (this->lineno <= lineno) {
                    location_error(String::from(
                        "escape character is not at end of line"));
                }
                lineno = this->lineno;
            } else if (this->lineno > lineno) {
                if (subcolumn == 0) {
                    subcolumn = this->column();
                } else if (this->column() != subcolumn) {
                    location_error(String::from("indentation mismatch"));
                }
                if (column != subcolumn) {
                    if ((column + 4) != subcolumn) {
                        location_error(String::from(
                            "indentations must nest by 4 spaces."));
                    }
                }

                escape = false;
                lineno = this->lineno;
                // keep adding elements while we're in the same line
                while ((this->token != tok_eof)
                        && (this->token != end_token)
                        && (this->lineno == lineno)) {
                    builder.append(parse_naked(subcolumn, end_token));
                }
            } else if (this->token == tok_statement) {
                this->read_token();
                if (!builder.is_empty()) {
                    break;
                }
            } else {
                builder.append(parse_any());
                lineno = this->next_lineno;
                this->read_token();
            }
            if ((!escape || (this->lineno > lineno))
                && (this->column() <= column)) {
                break;
            }
        }

        return Syntax::from(anchor, builder.get_result());
    }

    Any parse() {
        this->read_token();
        int lineno = 0;
        bool escape = false;

        const Anchor *anchor = this->anchor();
        ListBuilder builder(*this);

        while (this->token != tok_eof) {
            if (this->token == tok_none) {
                break;
            } else if (this->token == tok_escape) {
                escape = true;
                this->read_token();
                if (this->lineno <= lineno) {
                    location_error(String::from(
                        "escape character is not at end of line"));
                }
                lineno = this->lineno;
            } else if (this->lineno > lineno) {
                if (this->column() != 1) {
                    location_error(String::from(
                        "indentation mismatch"));
                }

                escape = false;
                lineno = this->lineno;
                // keep adding elements while we're in the same line
                while ((this->token != tok_eof)
                        && (this->token != tok_none)
                        && (this->lineno == lineno)) {
                    builder.append(parse_naked(1, tok_none));
                }
            } else if (this->token == tok_statement) {
                location_error(String::from(
                    "unexpected statement token"));
            } else {
                builder.append(parse_any());
                lineno = this->next_lineno;
                this->read_token();
            }
        }
        return Syntax::from(anchor, builder.get_result());
    }

};

//------------------------------------------------------------------------------
// EXPRESSION PRINTER
//------------------------------------------------------------------------------

static const char INDENT_SEP[] = "⁞";

static Style default_symbol_styler(Symbol name) {
    if (!name.is_known())
        return Style_Symbol;
    auto val = name.known_value();
    if ((val >= KEYWORD_FIRST) && (val <= KEYWORD_LAST))
        return Style_Keyword;
    else if ((val >= FUNCTION_FIRST) && (val <= FUNCTION_LAST))
        return Style_Function;
    else if ((val >= SFXFUNCTION_FIRST) && (val <= SFXFUNCTION_LAST))
        return Style_SfxFunction;
    else if ((val >= OPERATOR_FIRST) && (val <= OPERATOR_LAST))
        return Style_Operator;
    else if ((val >= TYPE_FIRST) && (val <= TYPE_LAST))
        return Style_Type;
    return Style_Symbol;
}

struct StreamExprFormat {
    enum Tagging {
        All,
        Line,
        None,
    };

    bool naked;
    Tagging anchors;
    int maxdepth;
    int maxlength;
    Style (*symbol_styler)(Symbol);
    int depth;

    StreamExprFormat() :
        naked(true),
        anchors(None),
        maxdepth(1<<30),
        maxlength(1<<30),
        symbol_styler(default_symbol_styler),
        depth(0)
    {}

    static StreamExprFormat singleline() {
        auto fmt = StreamExprFormat();
        fmt.naked = false;
        return fmt;
    }

    static StreamExprFormat digest() {
        auto fmt = StreamExprFormat();
        fmt.maxdepth = 5;
        fmt.maxlength = 5;
        return fmt;
    }

    static StreamExprFormat singleline_digest() {
        auto fmt = StreamExprFormat();
        fmt.maxdepth = 5;
        fmt.maxlength = 5;
        fmt.naked = false;
        return fmt;
    }


};

struct StreamAnchors {
    StyledStream &ss;
    const Anchor *last_anchor;

    StreamAnchors(StyledStream &_ss) :
        ss(_ss), last_anchor(nullptr) {
    }

    void stream_anchor(const Anchor *anchor, bool quoted = false) {
        if (anchor) {
            ss << Style_Location;
            auto rss = StyledStream::plain(ss);
            // ss << path.name()->data << ":" << lineno << ":" << column << ":";
            if (!last_anchor || (last_anchor->path() != anchor->path())) {
                rss << anchor->path().name()->data
                    << ":" << anchor->lineno
                    << ":" << anchor->column
                    << ":";
            } else if (!last_anchor || (last_anchor->lineno != anchor->lineno)) {
                rss << ":" << anchor->lineno
                    << ":" << anchor->column
                    << ":";
            } else if (!last_anchor || (last_anchor->column != anchor->column)) {
                rss << "::" << anchor->column
                    << ":";
            } else {
                rss << ":::";
            }
            if (quoted) { rss << "'"; }
            ss << Style_None;
            last_anchor = anchor;
        }
    }
};

struct StreamExpr : StreamAnchors {
    StreamExprFormat fmt;
    bool line_anchors;
    bool atom_anchors;

    StreamExpr(StyledStream &_ss, const StreamExprFormat &_fmt) :
        StreamAnchors(_ss), fmt(_fmt) {
        line_anchors = (fmt.anchors == StreamExprFormat::Line);
        atom_anchors = (fmt.anchors == StreamExprFormat::All);
    }

    void stream_indent(int depth = 0) {
        if (depth >= 1) {
            ss << Style_Comment << "    ";
            for (int i = 2; i <= depth; ++i) {
                ss << INDENT_SEP << "   ";
            }
            ss << Style_None;
        }
    }

    static bool is_nested(const Any &_e) {
        auto e = maybe_unsyntax(_e);
        if (e.type == TYPE_List) {
            auto it = e.list;
            while (it != EOL) {
                auto q = maybe_unsyntax(it->at);
                if (q.type.is_known()) {
                    switch(q.type.known_value()) {
                    case TYPE_Symbol:
                    case TYPE_String:
                    case TYPE_I32:
                    case TYPE_F32:
                        break;
                    default: return true;
                    }
                }
                it = it->next;
            }
        }
        return false;
    }

    static bool is_list (const Any &_value) {
        auto value = maybe_unsyntax(_value);
        return value.type == TYPE_List;
    }

    void walk(Any e, int depth, int maxdepth, bool naked) {
        bool quoted = false;

        const Anchor *anchor = nullptr;
        if (e.type == TYPE_Syntax) {
            anchor = e.syntax->anchor;
            quoted = e.syntax->quoted;
            e = e.syntax->datum;
        }

        if (naked) {
            stream_indent(depth);
        }
        if (atom_anchors) {
            stream_anchor(anchor, quoted);
        }

        if (e.type == TYPE_List) {
            if (naked && line_anchors && !atom_anchors) {
                stream_anchor(anchor, quoted);
            }

            maxdepth = maxdepth - 1;

            auto it = e.list;
            if (it == EOL) {
                ss << Style_Operator << "()" << Style_None;
                if (naked) { ss << std::endl; }
                return;
            }
            if (maxdepth == 0) {
                ss << Style_Operator << "("
                   << Style_Comment << "<...>"
                   << Style_Operator << ")"
                   << Style_None;
                if (naked) { ss << std::endl; }
                return;
            }
            int offset = 0;
            // int numsublists = 0;
            if (naked) {
                if (is_list(it->at)) {
                    ss << ";" << std::endl;
                    goto print_sparse;
                }
            print_terse:
                walk(it->at, depth, maxdepth, false);
                it = it->next;
                offset = offset + 1;
                while (it != EOL) {
                    /*if (is_list(it->at)) {
                        numsublists = numsublists + 1;
                        if (numsublists >= 2) {
                            break;
                        }
                    }*/
                    if (is_nested(it->at)) {
                        break;
                    }
                    ss << " ";
                    walk(it->at, depth, maxdepth, false);
                    offset = offset + 1;
                    it = it->next;
                }
                ss << std::endl;
            print_sparse:
                int subdepth = depth + 1;
                while (it != EOL) {
                    auto value = it->at;
                    if (!is_list(value) // not a list
                        && (offset >= 1)) { // not first element in list
                        stream_indent(subdepth);
                        ss << "\\ ";
                        goto print_terse;
                    }
                    if (offset >= fmt.maxlength) {
                        stream_indent(subdepth);
                        ss << "<...>" << std::endl;
                        return;
                    }
                    walk(value, subdepth, maxdepth, true);
                    offset = offset + 1;
                    it = it->next;
                }
            } else {
                depth = depth + 1;
                ss << Style_Operator << "(" << Style_None;
                while (it != EOL) {
                    if (offset > 0) {
                        ss << " ";
                    }
                    if (offset >= fmt.maxlength) {
                        ss << Style_Comment << "..." << Style_None;
                        break;
                    }
                    walk(it->at, depth, maxdepth, false);
                    offset = offset + 1;
                    it = it->next;
                }
                ss << Style_Operator << ")" << Style_None;
            }
        } else {
            if (e.type == TYPE_Symbol) {
                ss << fmt.symbol_styler(e.symbol);
                e.symbol.name()->stream(ss, SYMBOL_ESCAPE_CHARS);
                ss << Style_None;
            } else {
                ss << e;
            }
            if (naked) { ss << std::endl; }
        }
    }

    void stream(const Any &e) {
        walk(e, fmt.depth, fmt.maxdepth, fmt.naked);
    }
};

static void stream_expr(
    StyledStream &_ss, const Any &e, const StreamExprFormat &_fmt) {
    StreamExpr streamer(_ss, _fmt);
    streamer.stream(e);
}

static StyledStream& operator<<(StyledStream& ost, const List *list) {
    stream_expr(ost, list, StreamExprFormat::singleline());
    return ost;
}

//------------------------------------------------------------------------------
// IL OBJECTS
//------------------------------------------------------------------------------

// CFF form implemented after
// Leissa et al., Graph-Based Higher-Order Intermediate Representation
// http://compilers.cs.uni-saarland.de/papers/lkh15_cgo.pdf

//------------------------------------------------------------------------------

enum {
    ARG_Cont = 0,
    ARG_Arg0 = 1,
    PARAM_Cont = 0,
    PARAM_Arg0 = 1,
};

struct ILNode {
    std::unordered_map<Label *, int> users;

    void add_user(Label *label, int argindex) {
        auto it = users.find(label);
        if (it == users.end()) {
            users.insert(std::pair<Label *,int>(label, 1));
        } else {
            it->second++;
        }
    }
    void del_user(Label *label, int argindex) {
        auto it = users.find(label);
        if (it == users.end()) {
            std::cerr << "internal warning: attempting to remove user, but user is unknown." << std::endl;
        } else {
            if (it->second == 1) {
                // remove last reference
                users.erase(it);
            } else {
                it->second--;
            }
        }
    }

    void stream_users(StyledStream &ss) const;
};

struct Parameter : ILNode {
protected:
    Parameter(const Anchor *_anchor, Symbol _name, Type _type, bool _vararg) :
        anchor(_anchor), name(_name), type(_type), label(nullptr), index(-1),
        vararg(_vararg) {}

public:
    const Anchor *anchor;
    Symbol name;
    Type type;
    Label *label;
    int index;
    bool vararg;

    bool is_typed() const {
        return type != TYPE_Void;
    }

    StyledStream &stream_local(StyledStream &ss) const {
        if ((name != SYM_Unnamed) || !label) {
            ss << Style_Comment << "%" << Style_Symbol;
            name.name()->stream(ss, SYMBOL_ESCAPE_CHARS);
            ss << Style_None;
        } else {
            ss << Style_Operator << "@" << Style_None << index;
        }
        if (vararg) {
            ss << Style_Keyword << "…" << Style_None;
        }
        if (is_typed()) {
            ss << Style_Operator << ":" << Style_None << type;
        }
        return ss;
    }
    StyledStream &stream(StyledStream &ss) const;

    static Parameter *from(const Parameter *_param) {
        return new Parameter(
            _param->anchor, _param->name, _param->type, _param->vararg);
    }

    static Parameter *from(const Anchor *_anchor, Symbol _name, Type _type) {
        return new Parameter(_anchor, _name, _type, false);
    }

    static Parameter *vararg_from(const Anchor *_anchor, Symbol _name, Type _type) {
        return new Parameter(_anchor, _name, _type, true);
    }
};

template<KnownSymbol T>
void Any::verify_indirect() const {
    bangra::verify<T>(indirect_type());
}

Type Any::indirect_type() const {
    if (type == TYPE_Parameter) {
        return parameter->type;
    } else {
        return type;
    }
}

static StyledStream& operator<<(StyledStream& ss, Parameter *param) {
    param->stream(ss);
    return ss;
}

struct Body {
    const Anchor *anchor;
    Any enter;
    std::vector<Any> args;

    Body() : anchor(nullptr), enter(none) {}
};

static const char CONT_SEP[] = "▶";

template<typename T>
struct Tag {
    static uint64_t active_gen;
    uint64_t gen;

    Tag() :
        gen(active_gen) {}

    static void clear() {
        active_gen++;
    }
    bool visited() const {
        return gen == active_gen;
    }
    void visit() {
        gen = active_gen;
    }
};

template<typename T>
uint64_t Tag<T>::active_gen = 0;

typedef Tag<Label> LabelTag;

struct Label : ILNode {
protected:
    static uint64_t next_uid;

    Label(const Anchor *_anchor, Symbol _name) :
        uid(++next_uid), anchor(_anchor), name(_name), paired(nullptr),
        num_instances(0)
        {}

public:
    uint64_t uid;
    const Anchor *anchor;
    Symbol name;
    std::vector<Parameter *> params;
    Body body;
    LabelTag tag;
    Label *paired;
    uint64_t flags;
    uint64_t num_instances;
    // if return_constants are specified, the continuation must be inlined
    // with these arguments
    std::vector<Any> return_constants;

    Label *get_label_enter() const {
        assert(body.enter.type == TYPE_Label);
        return body.enter.label;
    }

    Builtin get_builtin_enter() const {
        assert(body.enter.type == TYPE_Builtin);
        return body.enter.builtin;
    }

    Label *get_label_cont() const {
        assert(!body.args.empty());
        assert(body.args[0].type == TYPE_Label);
        return body.args[0].label;
    }

    Type get_function_type() const {

        std::vector<Type> rettypes;
        std::vector<Type> argtypes;

        for (size_t i = 1; i < params.size(); ++i) {
            argtypes.push_back(params[i]->type);
        }
        auto &&tli = typed_labels.get(params[0]->type);
        for (size_t i = 1; i < tli.types.size(); ++i) {
            rettypes.push_back(tli.types[i]);
        }

        Type rtype = TYPE_Void;
        if (rettypes.size() == 1) {
            rtype = rettypes[0];
        } else {
            rtype = Tuple(rettypes);
        }

        return Function(rtype, argtypes);
    }

    void use(const Any &arg, int i) {
        if (arg.type == TYPE_Parameter && (arg.parameter->label != this)) {
            arg.parameter->add_user(this, i);
        } else if (arg.type == TYPE_Label && (arg.label != this)) {
            arg.label->add_user(this, i);
        }
    }

    void unuse(const Any &arg, int i) {
        if (arg.type == TYPE_Parameter && (arg.parameter->label != this)) {
            arg.parameter->del_user(this, i);
        } else if (arg.type == TYPE_Label && (arg.label != this)) {
            arg.label->del_user(this, i);
        }
    }

    void link_backrefs() {
        use(body.enter, -1);
        size_t count = body.args.size();
        for (size_t i = 0; i < count; ++i) {
            use(body.args[i], i);
        }
    }

    void unlink_backrefs() {
        unuse(body.enter, -1);
        size_t count = body.args.size();
        for (size_t i = 0; i < count; ++i) {
            unuse(body.args[i], i);
        }
    }

    void append(Parameter *param) {
        assert(!param->label);
        param->label = this;
        param->index = (int)params.size();
        params.push_back(param);
    }

    void set_parameters(const std::vector<Parameter *> &_params) {
        assert(params.empty());
        params = _params;
        for (size_t i = 0; i < params.size(); ++i) {
            Parameter *param = params[i];
            assert(!param->label);
            param->label = this;
            param->index = (int)i;
        }
    }

    void build_reachable(std::vector<Label *> &labels, std::unordered_set<Label *> &visited) {
        labels.clear();
        visited.clear();

        visited.insert(this);
        std::vector<Label *> stack = { this };
        while (!stack.empty()) {
            Label *parent = stack.back();
            stack.pop_back();
            labels.push_back(parent);

            int size = (int)parent->body.args.size();
            for (int i = -1; i < size; ++i) {
                Any arg = none;
                if (i == -1) {
                    arg = parent->body.enter;
                } else {
                    arg = parent->body.args[i];
                }

                switch(arg.type.value()) {
                case TYPE_Label: {
                    Label *label = arg.label;
                    if (!visited.count(label)) {
                        visited.insert(label);
                        stack.push_back(label);
                    }
                } break;
                default: break;
                }
            }
        }
    }

    void build_reachable(std::unordered_set<Label *> &labels) {
        labels.clear();

        labels.insert(this);
        std::vector<Label *> stack = { this };
        while (!stack.empty()) {
            Label *parent = stack.back();
            stack.pop_back();

            int size = (int)parent->body.args.size();
            for (int i = -1; i < size; ++i) {
                Any arg = none;
                if (i == -1) {
                    arg = parent->body.enter;
                } else {
                    arg = parent->body.args[i];
                }

                switch(arg.type.value()) {
                case TYPE_Label: {
                    Label *label = arg.label;
                    if (!labels.count(label)) {
                        labels.insert(label);
                        stack.push_back(label);
                    }
                } break;
                default: break;
                }
            }
        }
    }

    void build_scope(std::vector<Label *> &tempscope) {
        std::unordered_set<Label *> reachable;
        build_reachable(reachable);

        tempscope.clear();

        std::unordered_set<Label *> visited;
        visited.clear();
        visited.insert(this);

        for (auto &&param : params) {
            // every label using one of our parameters is live in scope
            for (auto &&kv : param->users) {
                Label *live_label = kv.first;
                if (!visited.count(live_label)) {
                    visited.insert(live_label);
                    if (reachable.count(live_label)) {
                        tempscope.push_back(live_label);
                    }
                }
            }
        }

        size_t index = 0;
        while (index < tempscope.size()) {
            Label *scope_label = tempscope[index++];

            // users of scope_label are indirectly live in scope
            for (auto &&kv : scope_label->users) {
                Label *live_label = kv.first;
                if (!visited.count(live_label)) {
                    visited.insert(live_label);
                    if (reachable.count(live_label)) {
                        tempscope.push_back(live_label);
                    }
                }
            }

            for (auto &&param : scope_label->params) {
                // every label using scope_label's parameters is live in scope
                for (auto &&kv : param->users) {
                    Label *live_label = kv.first;
                    if (!visited.count(live_label)) {
                        visited.insert(live_label);
                        if (reachable.count(live_label)) {
                            tempscope.push_back(live_label);
                        }
                    }
                }
            }
        }
    }

    StyledStream &stream_short(StyledStream &ss) const {
        ss << Style_Keyword << "λ" << Style_Symbol;
        name.name()->stream(ss, SYMBOL_ESCAPE_CHARS);
        ss << Style_Operator << "#" << Style_None << uid;
        return ss;
    }

    StyledStream &stream(StyledStream &ss, bool users = false) const {
        stream_short(ss);
        if (users) {
            stream_users(ss);
        }
        ss << Style_Operator << "(" << Style_None;
        size_t count = params.size();
        for (size_t i = 1; i < count; ++i) {
            if (i > 1) {
                ss << " ";
            }
            params[i]->stream_local(ss);
            if (users) {
                params[i]->stream_users(ss);
            }
        }
        ss << Style_Operator << ")" << Style_None;
        if (count && (params[0]->type != TYPE_Nothing)) {
            ss << Style_Comment << CONT_SEP << Style_None;
            params[0]->stream_local(ss);
            if (users) {
                params[0]->stream_users(ss);
            }
        }
        return ss;
    }

    static Label *from(const Anchor *_anchor, Symbol _name) {
        return new Label(_anchor, _name);
    }
    // only inherits name and anchor
    static Label *from(Label *label) {
        Label *result = new Label(label->anchor, label->name);
        label->num_instances++;
        result->num_instances = label->num_instances;
        return result;
    }

    // a continuation that never returns
    static Label *continuation_from(const Anchor *_anchor, Symbol _name) {
        Label *value = from(_anchor, _name);
        // first argument is present, but unused
        value->append(Parameter::from(_anchor, _name, TYPE_Nothing));
        return value;
    }

    // a function that eventually returns
    static Label *function_from(const Anchor *_anchor, Symbol _name) {
        Label *value = from(_anchor, _name);
        // continuation is always first argument
        // this argument will be called when the function is done
        value->append(
            Parameter::from(_anchor,
                Symbol(format("return-%s", _name.name()->data)),
                TYPE_Void));
        return value;
    }

};

uint64_t Label::next_uid = 0;

static StyledStream& operator<<(StyledStream& ss, Label *label) {
    label->stream(ss);
    return ss;
}

static StyledStream& operator<<(StyledStream& ss, const Label *label) {
    label->stream(ss);
    return ss;
}

StyledStream &Parameter::stream(StyledStream &ss) const {
    if (label) {
        label->stream_short(ss);
    } else {
        ss << Style_Comment << "<unbound>" << Style_None;
    }
    stream_local(ss);
    return ss;
}

void ILNode::stream_users(StyledStream &ss) const {
    if (!users.empty()) {
        ss << Style_Comment << "{" << Style_None;
        size_t i = 0;
        for (auto &&kv : users) {
            if (i > 0) {
                ss << " ";
            }
            Label *label = kv.first;
            label->stream_short(ss);
            i++;
        }
        ss << Style_Comment << "}" << Style_None;
    }
}

//------------------------------------------------------------------------------
// IL PRINTER
//------------------------------------------------------------------------------

struct StreamLabelFormat {
    enum Tagging {
        All,
        Line,
        Scope,
        None,
    };

    Tagging anchors;
    Tagging follow;
    bool show_users;

    StreamLabelFormat() :
        anchors(None),
        follow(All),
        show_users(false)
        {}

    static StreamLabelFormat debug_all() {
        StreamLabelFormat fmt;
        fmt.follow = All;
        fmt.show_users = true;
        return fmt;
    }

    static StreamLabelFormat debug_scope() {
        StreamLabelFormat fmt;
        fmt.follow = Scope;
        fmt.show_users = true;
        return fmt;
    }

    static StreamLabelFormat debug_single() {
        StreamLabelFormat fmt;
        fmt.follow = None;
        fmt.show_users = true;
        return fmt;
    }

    static StreamLabelFormat single() {
        StreamLabelFormat fmt;
        fmt.follow = None;
        return fmt;
    }

    static StreamLabelFormat scope() {
        StreamLabelFormat fmt;
        fmt.follow = Scope;
        return fmt;
    }

};

struct StreamLabel : StreamAnchors {
    StreamLabelFormat fmt;
    bool line_anchors;
    bool atom_anchors;
    bool follow_labels;
    bool follow_scope;
    std::unordered_set<Label *> visited;

    StreamLabel(StyledStream &_ss, const StreamLabelFormat &_fmt) :
        StreamAnchors(_ss), fmt(_fmt) {
        line_anchors = (fmt.anchors == StreamLabelFormat::Line);
        atom_anchors = (fmt.anchors == StreamLabelFormat::All);
        follow_labels = (fmt.follow == StreamLabelFormat::All);
        follow_scope = (fmt.follow == StreamLabelFormat::Scope);
    }

    void stream_label_label(Label *alabel) {
        alabel->stream_short(ss);
    }

    void stream_label_label_user(Label *alabel) {
        alabel->stream_short(ss);
    }

    void stream_param_label(Parameter *param, Label *alabel) {
        if (param->label == alabel) {
            param->stream_local(ss);
        } else {
            param->stream(ss);
        }
    }

    void stream_argument(Any arg, Label *alabel) {
        if (arg.type == TYPE_Parameter) {
            stream_param_label(arg.parameter, alabel);
        } else if (arg.type == TYPE_Label) {
            stream_label_label(arg.label);
        } else if (arg.type == TYPE_List) {
            stream_expr(ss, arg, StreamExprFormat::singleline_digest());
        } else {
            ss << arg;
        }
    }

    #if 0
    local function stream_scope(_scope)
        if _scope then
            writer(" ")
            writer(styler(Style.Comment, "<"))
            local k = 0
            for dest,_ in pairs(_scope) do
                if k > 0 then
                    writer(" ")
                end
                stream_label_label_user(dest)
                k = k + 1
            end
            writer(styler(Style.Comment, ">"))
        end
    end
    #endif

    void stream_label (Label *alabel) {
        if (visited.count(alabel)) {
            return;
        }
        visited.insert(alabel);
        if (line_anchors) {
            stream_anchor(alabel->anchor);
        }
        alabel->stream(ss, fmt.show_users);
        ss << Style_Operator << ":" << Style_None;
        //stream_scope(scopes[alabel])
        ss << std::endl;
        ss << "    ";
        if (line_anchors && alabel->body.anchor) {
            stream_anchor(alabel->body.anchor);
            ss << " ";
        }
        stream_argument(alabel->body.enter, alabel);
        for (size_t i=1; i < alabel->body.args.size(); ++i) {
            ss << " ";
            stream_argument(alabel->body.args[i], alabel);
        }
        if (!alabel->body.args.empty()) {
            auto &&cont = alabel->body.args[0];
            if (cont.type != TYPE_Nothing) {
                ss << Style_Comment << CONT_SEP << Style_None;
                stream_argument(cont, alabel);
            }
        }
        ss << std::endl;

        if (follow_labels) {
            for (size_t i=0; i < alabel->body.args.size(); ++i) {
                stream_any(alabel->body.args[i]);
            }
            stream_any(alabel->body.enter);
        }
    }

    void stream_any(const Any &afunc) {
        if (afunc.type == TYPE_Label) {
            stream_label(afunc.label);
        }
    }

    void stream(Label *label) {
        stream_label(label);
        if (follow_scope) {
            std::vector<Label *> scope;
            label->build_scope(scope);
            size_t i = scope.size();
            while (i > 0) {
                --i;
                stream_label(scope[i]);
            }
        }
    }

};

static void stream_label(
    StyledStream &_ss, Label *label, const StreamLabelFormat &_fmt) {
    StreamLabel streamer(_ss, _fmt);
    streamer.stream(label);
}

//------------------------------------------------------------------------------
// SCC
//------------------------------------------------------------------------------

// build strongly connected component map of label graph
// uses Dijkstra's Path-based strong component algorithm
struct SCCBuilder {
    struct Group {
        size_t index;
        std::vector<Label *> labels;
    };

    std::vector<Label *> S;
    std::vector<Label *> P;
    std::unordered_map<Label *, size_t> Cmap;
    std::vector<Group> groups;
    std::unordered_map<Label *, size_t> SCCmap;
    size_t C;

    SCCBuilder(Label *top) :
        C(0) {
        walk(top);
    }

    void stream_group(StyledStream &ss, const Group &group) {
        ss << "group #" << group.index << " (" << group.labels.size() << " labels):" << std::endl;
        for (size_t k = 0; k < group.labels.size(); ++k) {
            stream_label(ss, group.labels[k], StreamLabelFormat::single());
        }
    }

    bool is_recursive(Label *l) {
        return group(l).labels.size() > 1;
    }

    bool contains(Label *l) {
        auto it = SCCmap.find(l);
        return it != SCCmap.end();
    }

    size_t group_id(Label *l) {
        auto it = SCCmap.find(l);
        assert(it != SCCmap.end());
        return it->second;
    }

    Group &group(Label *l) {
        return groups[group_id(l)];
    }

    void walk(Label *obj) {
        Cmap[obj] = C++;
        S.push_back(obj);
        P.push_back(obj);

        int size = (int)obj->body.args.size();
        for (int i = -1; i < size; ++i) {
            Any arg = none;
            if (i == -1) {
                arg = obj->body.enter;
            } else {
                arg = obj->body.args[i];
            }

            switch(arg.type.value()) {
            case TYPE_Label: {
                Label *label = arg.label;

                auto it = Cmap.find(label);
                if (it == Cmap.end()) {
                    walk(label);
                } else if (!SCCmap.count(label)) {
                    size_t Cw = it->second;
                    while (true) {
                        assert(!P.empty());
                        auto it = Cmap.find(P.back());
                        assert(it != Cmap.end());
                        if (it->second <= Cw) break;
                        P.pop_back();
                    }
                }
            } break;
            default: break;
            }
        }

        assert(!P.empty());
        if (P.back() == obj) {
            groups.emplace_back();
            Group &scc = groups.back();
            scc.index = groups.size() - 1;
            while (true) {
                assert(!S.empty());
                Label *q = S.back();
                scc.labels.push_back(q);
                SCCmap[q] = groups.size() - 1;
                S.pop_back();
                if (q == obj) {
                    break;
                }
            }
            P.pop_back();
        }
    }
};

//------------------------------------------------------------------------------
// IL MANGLING
//------------------------------------------------------------------------------

typedef std::unordered_map<ILNode *, std::vector<Any> > MangleMap;

static Any first(const std::vector<Any> &values) {
    return values.empty()?none:values.front();
}

static void mangle_remap_body(Label *ll, Label *entry, MangleMap &map) {
    Any enter = entry->body.enter;
    std::vector<Any> &args = entry->body.args;
    std::vector<Any> &body = ll->body.args;
    if (enter.type == TYPE_Label) {
        auto it = map.find(enter.label);
        if (it != map.end()) {
            enter = first(it->second);
        }
    } else if (enter.type == TYPE_Parameter) {
        auto it = map.find(enter.parameter);
        if (it != map.end()) {
            enter = first(it->second);
        }
    }
    ll->body.anchor = entry->body.anchor;
    ll->body.enter = enter;

    StyledStream ss(std::cout);
    size_t lasti = (args.size() - 1);
    for (size_t i = 0; i < args.size(); ++i) {
        Any arg = args[i];
        if (arg.type == TYPE_Label) {
            auto it = map.find(arg.label);
            if (it != map.end()) {
                body.push_back(first(it->second));
            } else {
                body.push_back(arg);
            }
        } else if (arg.type == TYPE_Parameter) {
            auto it = map.find(arg.parameter);
            if (it != map.end()) {
                if (i == lasti) {
                    body.insert(body.end(),
                        it->second.begin(), it->second.end());
                } else {
                    body.push_back(first(it->second));
                }
            } else {
                body.push_back(arg);
            }
        } else {
            body.push_back(arg);
        }
    }

    ll->link_backrefs();
}

enum MangleFlag {
    Mangle_Verbose = (1<<0),
};

static Label *mangle(Label *entry, std::vector<Parameter *> params, MangleMap &map,
    int verbose = 0) {

    std::vector<Label *> entry_scope;
    entry->build_scope(entry_scope);

    // remap entry point
    Label *le = Label::from(entry);
    le->set_parameters(params);
    // create new labels and map new parameters
    for (auto &&l : entry_scope) {
        Label *ll = Label::from(l);
        l->paired = ll;
        map.insert(MangleMap::value_type(l, {Any(ll)}));
        ll->params.reserve(l->params.size());
        for (auto &&param : l->params) {
            Parameter *pparam = Parameter::from(param);
            map.insert(MangleMap::value_type(param, {Any(pparam)}));
            ll->append(pparam);
        }
    }

    // remap label bodies
    for (auto &&l : entry_scope) {
        Label *ll = l->paired;
        l->paired = nullptr;
        mangle_remap_body(ll, l, map);
    }
    mangle_remap_body(le, entry, map);

    if (verbose & Mangle_Verbose) {
    StyledStream ss(std::cout);
    ss << "IN[\n";
    stream_label(ss, entry, StreamLabelFormat::debug_single());
    for (auto && l : entry_scope) {
        stream_label(ss, l, StreamLabelFormat::debug_single());
    }
    ss << "]IN\n";
    ss << "OUT[\n";
    stream_label(ss, le, StreamLabelFormat::debug_single());
    for (auto && l : entry_scope) {
        auto it = map.find(l);
        stream_label(ss, it->second.front(), StreamLabelFormat::debug_single());
    }
    ss << "]OUT\n";
    }

    return le;
}

//------------------------------------------------------------------------------
// C BRIDGE (CLANG)
//------------------------------------------------------------------------------

class CVisitor : public clang::RecursiveASTVisitor<CVisitor> {
public:

    typedef std::unordered_map<Symbol, Type, Symbol::Hash> NamespaceMap;

    Scope *dest;
    clang::ASTContext *Context;
    std::unordered_map<clang::RecordDecl *, bool> record_defined;
    std::unordered_map<clang::EnumDecl *, bool> enum_defined;
    NamespaceMap named_structs;
    NamespaceMap named_unions;
    NamespaceMap named_enums;
    NamespaceMap typedefs;

    CVisitor() : dest(nullptr), Context(NULL) {}

    const Anchor *anchorFromLocation(clang::SourceLocation loc) {
        auto &SM = Context->getSourceManager();

        auto PLoc = SM.getPresumedLoc(loc);

        if (PLoc.isValid()) {
            auto fname = PLoc.getFilename();
            const String *strpath = String::from_cstr(fname);
            Symbol key(strpath);
            SourceFile *sf = SourceFile::from_file(key);
            if (!sf) {
                sf = SourceFile::from_string(key, Symbol(SYM_Unnamed).name());
            }
            return Anchor::from(sf, PLoc.getLine(), PLoc.getColumn(), 0);
        }

        return get_active_anchor();
    }

    void SetContext(clang::ASTContext * ctx, Scope *_dest) {
        Context = ctx;
        dest = _dest;
    }

    void GetFields(TypenameInfo &tni, clang::RecordDecl * rd) {
        //auto &rl = Context->getASTRecordLayout(rd);

        std::vector<Symbol> names;
        std::vector<Type> types;
        //auto anchors = new std::vector<Anchor>();

        for(clang::RecordDecl::field_iterator it = rd->field_begin(), end = rd->field_end(); it != end; ++it) {
            clang::DeclarationName declname = it->getDeclName();

            //unsigned idx = it->getFieldIndex();
            //auto offset = rl.getFieldOffset(idx);
            //unsigned width = it->getBitWidthValue(*Context);

            if(it->isBitField() || (!it->isAnonymousStructOrUnion() && !declname)) {
                break;
            }
            clang::QualType FT = it->getType();
            Type fieldtype = TranslateType(FT);

            // todo: work offset into structure
            names.push_back(
                it->isAnonymousStructOrUnion()?
                    Symbol("") : Symbol(
                        String::from_stdstring(declname.getAsString())));
            types.push_back(fieldtype);
        }

        tni.finalize(rd->isUnion()?Union(types):Tuple(types));
        tni.field_names = names;
    }

    Type get_typename(Symbol name, NamespaceMap &map) {
        if (name == SYM_Unnamed) {
            auto it = map.find(name);
            if (it != map.end()) {
                return it->second;
            }
            Type T = Typename(name);
            auto ok = map.insert({name, T});
            assert(ok.second);
            return T;
        }
        return Typename(name);
    }

    Type TranslateRecord(clang::RecordDecl *rd) {
        if (!rd->isStruct() && !rd->isUnion())
            location_error(String::from("can not translate record: is neither struct nor union"));

        Symbol name(String::from_stdstring(rd->getName().data()));

        Type struct_type = get_typename(name,
            rd->isUnion()?named_unions:named_structs);

        //const Anchor *anchor = anchorFromLocation(rd->getSourceRange().getBegin());

        clang::RecordDecl * defn = rd->getDefinition();
        if (defn && !record_defined[rd]) {
            record_defined[rd] = true;

            GetFields(typenames.get(struct_type), defn);

            #if 0
            auto &rl = Context->getASTRecordLayout(rd);
            auto align = (size_t)rl.getAlignment().getQuantity();
            auto size = (size_t)rl.getSize().getQuantity();
            #endif
        }

        return struct_type;
    }

    Any make_integer(Type T, int64_t v) {
        switch(T.value()) {
        case TYPE_I8: return Any((int8_t)v);
        case TYPE_I16: return Any((int16_t)v);
        case TYPE_I32: return Any((int32_t)v);
        case TYPE_I64: return Any((int64_t)v);
        case TYPE_U8: return Any((uint8_t)v);
        case TYPE_U16: return Any((uint16_t)v);
        case TYPE_U32: return Any((uint32_t)v);
        case TYPE_U64: return Any((uint64_t)v);
        default: assert(false); return none;
        }
    }

    Type TranslateEnum(clang::EnumDecl *ed) {

        Symbol name(String::from_stdstring(ed->getName()));

        Type enum_type = get_typename(name, named_enums);

        //const Anchor *anchor = anchorFromLocation(ed->getIntegerTypeRange().getBegin());

        clang::EnumDecl * defn = ed->getDefinition();
        if (defn && !enum_defined[ed]) {
            enum_defined[ed] = true;

            Type tag_type = TranslateType(ed->getIntegerType());

            auto &tni = typenames.get(enum_type);
            tni.finalize(tag_type);

            for (auto it : ed->enumerators()) {
                //const Anchor *anchor = anchorFromLocation(it->getSourceRange().getBegin());
                auto &val = it->getInitVal();

                auto name = Symbol(String::from_stdstring(it->getName().data()));
                auto value = make_integer(tag_type, val.getExtValue());

                tni.bind(name, value);
                dest->bind(name, value);
            }
        }

        return enum_type;
    }

    Type TranslateType(clang::QualType T) {
        using namespace clang;

        const clang::Type *Ty = T.getTypePtr();
        assert(Ty);

        switch (Ty->getTypeClass()) {
        case clang::Type::Elaborated: {
            const ElaboratedType *et = dyn_cast<ElaboratedType>(Ty);
            return TranslateType(et->getNamedType());
        } break;
        case clang::Type::Paren: {
            const ParenType *pt = dyn_cast<ParenType>(Ty);
            return TranslateType(pt->getInnerType());
        } break;
        case clang::Type::Typedef: {
            const TypedefType *tt = dyn_cast<TypedefType>(Ty);
            TypedefNameDecl * td = tt->getDecl();
            auto it = typedefs.find(
                Symbol(String::from_stdstring(td->getName().data())));
            if (it == typedefs.end()) {
                break;
            }
            return it->second;
        } break;
        case clang::Type::Record: {
            const RecordType *RT = dyn_cast<RecordType>(Ty);
            RecordDecl * rd = RT->getDecl();
            return TranslateRecord(rd);
        }  break;
        case clang::Type::Enum: {
            const clang::EnumType *ET = dyn_cast<clang::EnumType>(Ty);
            EnumDecl * ed = ET->getDecl();
            return TranslateEnum(ed);
        } break;
        case clang::Type::Builtin:
            switch (cast<BuiltinType>(Ty)->getKind()) {
            case clang::BuiltinType::Void:
                return Type(TYPE_Void);
            case clang::BuiltinType::Bool:
                return Type(TYPE_Bool);
            case clang::BuiltinType::Char_S:
            case clang::BuiltinType::SChar:
            case clang::BuiltinType::Char_U:
            case clang::BuiltinType::UChar:
            case clang::BuiltinType::Short:
            case clang::BuiltinType::UShort:
            case clang::BuiltinType::Int:
            case clang::BuiltinType::UInt:
            case clang::BuiltinType::Long:
            case clang::BuiltinType::ULong:
            case clang::BuiltinType::LongLong:
            case clang::BuiltinType::ULongLong:
            case clang::BuiltinType::WChar_S:
            case clang::BuiltinType::WChar_U:
            case clang::BuiltinType::Char16:
            case clang::BuiltinType::Char32: {
                int sz = Context->getTypeSize(T);
                if (Ty->isUnsignedIntegerType()) {
                    switch(sz) {
                    case 8: return Type(TYPE_U8);
                    case 16: return Type(TYPE_U16);
                    case 32: return Type(TYPE_U32);
                    case 64: return Type(TYPE_U64);
                    default: break;
                    }
                } else {
                    switch(sz) {
                    case 8: return Type(TYPE_I8);
                    case 16: return Type(TYPE_I16);
                    case 32: return Type(TYPE_I32);
                    case 64: return Type(TYPE_I64);
                    default: break;
                    }
                }
            } break;
            case clang::BuiltinType::Half: return Type(TYPE_R16);
            case clang::BuiltinType::Float:
                return Type(TYPE_F32);
            case clang::BuiltinType::Double:
                return Type(TYPE_F64);
            case clang::BuiltinType::LongDouble: return Type(TYPE_R80);
            case clang::BuiltinType::NullPtr:
            case clang::BuiltinType::UInt128:
            default:
                break;
            }
        case clang::Type::Complex:
        case clang::Type::LValueReference:
        case clang::Type::RValueReference:
            break;
        case clang::Type::Decayed: {
            const clang::DecayedType *DTy = cast<clang::DecayedType>(Ty);
            return TranslateType(DTy->getDecayedType());
        } break;
        case clang::Type::Pointer: {
            const clang::PointerType *PTy = cast<clang::PointerType>(Ty);
            QualType ETy = PTy->getPointeeType();
            return Pointer(TranslateType(ETy));
        } break;
        case clang::Type::VariableArray:
        case clang::Type::IncompleteArray:
            break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            Type at = TranslateType(ATy->getElementType());
            uint64_t sz = ATy->getSize().getZExtValue();
            return Array(at, sz);
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
            const clang::VectorType *VT = cast<clang::VectorType>(T);
            Type at = TranslateType(VT->getElementType());
            uint64_t n = VT->getNumElements();
            return Vector(at, n);
        } break;
        case clang::Type::FunctionNoProto:
        case clang::Type::FunctionProto: {
            const clang::FunctionType *FT = cast<clang::FunctionType>(Ty);
            return TranslateFuncType(FT);
        } break;
        case clang::Type::ObjCObject: break;
        case clang::Type::ObjCInterface: break;
        case clang::Type::ObjCObjectPointer: break;
        case clang::Type::BlockPointer:
        case clang::Type::MemberPointer:
        case clang::Type::Atomic:
        default:
            break;
        }
        location_error(format("cannot convert type: %s (%s)\n",
            T.getAsString().c_str(),
            Ty->getTypeClassName()));
        return TYPE_Void;
    }

    Type TranslateFuncType(const clang::FunctionType * f) {

        clang::QualType RT = f->getReturnType();

        Type returntype = TranslateType(RT);

        uint64_t flags = 0;

        std::vector<Type> argtypes;

        const clang::FunctionProtoType * proto = f->getAs<clang::FunctionProtoType>();
        if(proto) {
            if (proto->isVariadic()) {
                flags |= FF_Variadic;
            }
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                clang::QualType PT = proto->getParamType(i);
                argtypes.push_back(TranslateType(PT));
            }
        }

        return Function(returntype, argtypes, flags);
    }

    void exportType(Symbol name, Type type) {
        dest->bind(name, type);
    }

    void exportExternal(Symbol name, Type type,
        const Anchor *anchor) {
        auto ptr = LLVMSearchForAddressOfSymbol(name.name()->data);
        assert(ptr);
        dest->bind(name,
            Any::from_pointer(type, ptr));
    }

    bool TraverseRecordDecl(clang::RecordDecl *rd) {
        if (rd->isFreeStanding()) {
            TranslateRecord(rd);
        }
        return true;
    }

    bool TraverseEnumDecl(clang::EnumDecl *ed) {
        if (ed->isFreeStanding()) {
            TranslateEnum(ed);
        }
        return true;
    }

    bool TraverseVarDecl(clang::VarDecl *vd) {
        if (vd->isExternC()) {
            const Anchor *anchor = anchorFromLocation(vd->getSourceRange().getBegin());

            exportExternal(
                String::from_stdstring(vd->getName().data()),
                TranslateType(vd->getType()),
                anchor);
        }

        return true;
    }

    bool TraverseTypedefDecl(clang::TypedefDecl *td) {

        //const Anchor *anchor = anchorFromLocation(td->getSourceRange().getBegin());

        Type type = TranslateType(td->getUnderlyingType());

        Symbol name = Symbol(String::from_stdstring(td->getName().data()));

        typedefs.insert({name, type});
        exportType(name, type);

        return true;
    }

    bool TraverseFunctionDecl(clang::FunctionDecl *f) {
        clang::DeclarationName DeclName = f->getNameInfo().getName();
        std::string FuncName = DeclName.getAsString();
        const clang::FunctionType * fntyp = f->getType()->getAs<clang::FunctionType>();

        if(!fntyp)
            return true;

        if(f->getStorageClass() == clang::SC_Static) {
            return true;
        }

        Type functype = TranslateFuncType(fntyp);

        std::string InternalName = FuncName;
        clang::AsmLabelAttr * asmlabel = f->getAttr<clang::AsmLabelAttr>();
        if(asmlabel) {
            InternalName = asmlabel->getLabel();
            #ifndef __linux__
                //In OSX and Windows LLVM mangles assembler labels by adding a '\01' prefix
                InternalName.insert(InternalName.begin(), '\01');
            #endif
        }
        const Anchor *anchor = anchorFromLocation(f->getSourceRange().getBegin());

        exportExternal(Symbol(String::from_stdstring(FuncName)),
            Pointer(functype), anchor);

        return true;
    }
};

class CodeGenProxy : public clang::ASTConsumer {
public:
    Scope *dest;

    CVisitor visitor;

    CodeGenProxy(Scope *dest_) : dest(dest_) {}
    virtual ~CodeGenProxy() {}

    virtual void Initialize(clang::ASTContext &Context) {
        visitor.SetContext(&Context, dest);
    }

    virtual bool HandleTopLevelDecl(clang::DeclGroupRef D) {
        for (clang::DeclGroupRef::iterator b = D.begin(), e = D.end(); b != e; ++b)
            visitor.TraverseDecl(*b);
        return true;
    }
};

// see ASTConsumers.h for more utilities
class BangEmitLLVMOnlyAction : public clang::EmitLLVMOnlyAction {
public:
    Scope *dest;

    BangEmitLLVMOnlyAction(Scope *dest_) :
        EmitLLVMOnlyAction((llvm::LLVMContext *)LLVMGetGlobalContext()),
        dest(dest_)
    {
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                 clang::StringRef InFile) override {

        std::vector< std::unique_ptr<clang::ASTConsumer> > consumers;
        consumers.push_back(clang::EmitLLVMOnlyAction::CreateASTConsumer(CI, InFile));
        consumers.push_back(llvm::make_unique<CodeGenProxy>(dest));
        return llvm::make_unique<clang::MultiplexConsumer>(std::move(consumers));
    }
};

static LLVMExecutionEngineRef ee = nullptr;

static void init_llvm() {
    LLVMEnablePrettyStackTrace();
    LLVMLinkInMCJIT();
    //LLVMLinkInInterpreter();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeDisassembler();

    char *errormsg = nullptr;
    if (LLVMCreateJITCompilerForModule(&ee,
        LLVMModuleCreateWithName("main"), 0, &errormsg)) {
        stb_fprintf(stderr, "error: %s\n", errormsg);
        exit(1);
    }
}

static Scope *import_c_module (
    const std::string &path, const std::vector<std::string> &args,
    const char *buffer = nullptr) {
    using namespace clang;

    std::vector<const char *> aargs;
    aargs.push_back("clang");
    aargs.push_back(path.c_str());
    for (size_t i = 0; i < args.size(); ++i) {
        aargs.push_back(args[i].c_str());
    }

    CompilerInstance compiler;
    compiler.setInvocation(createInvocationFromCommandLine(aargs));

    if (buffer) {
        auto &opts = compiler.getPreprocessorOpts();

        llvm::MemoryBuffer * membuffer =
            llvm::MemoryBuffer::getMemBuffer(buffer, "<buffer>").release();

        opts.addRemappedFile(path, membuffer);
    }

    // Create the compilers actual diagnostics engine.
    compiler.createDiagnostics();

    // Infer the builtin include path if unspecified.
    //~ if (compiler.getHeaderSearchOpts().UseBuiltinIncludes &&
        //~ compiler.getHeaderSearchOpts().ResourceDir.empty())
        //~ compiler.getHeaderSearchOpts().ResourceDir =
            //~ CompilerInvocation::GetResourcesPath(bangra_argv[0], MainAddr);

    LLVMModuleRef M = NULL;


    Scope *result = Scope::from();

    // Create and execute the frontend to generate an LLVM bitcode module.
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction(result));
    if (compiler.ExecuteAction(*Act)) {
        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        //llvm_modules.push_back(M);
        LLVMAddModule(ee, M);
        return result;
    } else {
        location_error(String::from("compilation failed"));
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// INTERPRETER
//------------------------------------------------------------------------------

static void apply_type_error(const Any &enter) {
    StyledString ss;
    ss.out << "don't know how to apply value of type " << enter.type;
    location_error(ss.str());
}

template<int mincount, int maxcount>
inline int checkargs(size_t argsize) {
    int count = (int)argsize - 1;
    if ((mincount <= 0) && (maxcount == -1)) {
        return count;
    }

    // arguments can overshoot, then we just truncate the count
    if ((maxcount >= 0) && (count > maxcount)) {
        count = maxcount;
    }
    if ((mincount >= 0) && (count < mincount)) {
        location_error(
            format("at least %i arguments expected", mincount));
    }
    return count;
}

static void *global_c_namespace = nullptr;

static void default_exception_handler(const Exception &exc) {
    auto cerr = StyledStream(std::cerr);
    if (exc.anchor) {
        cerr << exc.anchor << " ";
    }
    cerr << Style_Error << "error:" << Style_None << " "
        << exc.msg->data << std::endl;
    if (exc.anchor) {
        exc.anchor->stream_source_line(cerr);
    }
    exit(1);
}

static int integer_type_bit_size(Type T) {
    switch(T.value()) {
    case TYPE_Bool: return 1;
    case TYPE_I8: case TYPE_U8: return 8;
    case TYPE_I16: case TYPE_U16: return 16;
    case TYPE_I32: case TYPE_U32: return 32;
    case TYPE_I64: case TYPE_U64: return 64;
    default: assert(false); break;
    }
    return 0;
}

template<typename T>
static T cast_number(const Any &value) {
    switch(value.type.value()) {
    case TYPE_Bool: return (T)value.i1;
    case TYPE_I8: return (T)value.i8;
    case TYPE_I16: return (T)value.i16;
    case TYPE_I32: return (T)value.i32;
    case TYPE_I64: return (T)value.i64;
    case TYPE_U8: return (T)value.u8;
    case TYPE_U16: return (T)value.u16;
    case TYPE_U32: return (T)value.u32;
    case TYPE_U64: return (T)value.u64;
    case TYPE_F32: return (T)value.f32;
    case TYPE_F64: return (T)value.f64;
    default: {
        StyledString ss;
        ss.out << "type " << value.type << " can not be converted to numerical type";
        location_error(ss.str());
    } break;
    }
    return 0;
}

#if 0

struct Instruction {
    Any enter;
    std::vector<Any> args;

    Instruction() :
        enter(none) {
        args.reserve(256);
    }

    void clear() {
        enter = none;
        args.clear();
    }

    StyledStream &stream(StyledStream &ss) const {
        ss << enter;
        for (size_t i = 0; i < args.size(); ++i) {
            ss << " " << args[i];
        }
        return ss;
    }
};

static StyledStream& operator<<(StyledStream& ost, Instruction instr) {
    instr.stream(ost);
    return ost;
}

#define CHECKARGS(MINARGS, MAXARGS) \
    checkargs<MINARGS, MAXARGS>(in.args.size())
#define RETARGS(...) \
    out.args = { none, __VA_ARGS__ }

template<typename T>
static T PowT(T a, T b) {
    return powimpl(a, b);
}

template<typename T>
static T ModT(T a, T b) {
    return std::fmod(a, b);
}

static void translate_function_expr_list(
    Label *func, const List *it, const Anchor *anchor);
static Label *translate_root(const List *it, const Anchor *anchor);

static const Closure *apply_unknown_type = nullptr;
static const Closure *exception_handler = nullptr;
static bool handle_builtin(const Frame *frame, Instruction &in, Instruction &out) {
    switch(in.enter.builtin.value()) {
    case FN_AnchorPath: {
        CHECKARGS(1, 1);
        const Anchor *anchor = in.args[1];
        RETARGS(anchor->path());
    } break;
    case FN_AnchorLineNumber: {
        CHECKARGS(1, 1);
        const Anchor *anchor = in.args[1];
        RETARGS(anchor->lineno);
    } break;
    case FN_AnchorColumn: {
        CHECKARGS(1, 1);
        const Anchor *anchor = in.args[1];
        RETARGS(anchor->column);
    } break;
    case FN_AnchorOffset: {
        CHECKARGS(1, 1);
        const Anchor *anchor = in.args[1];
        RETARGS(anchor->offset);
    } break;
    case FN_AnchorSource: {
        CHECKARGS(1, 1);
        const Anchor *anchor = in.args[1];
        StyledString ss;
        anchor->stream_source_line(ss.out);
        RETARGS(ss.str());
    } break;
    case FN_Args: {
        CHECKARGS(0, 0);
        out.args = { none };
        for (size_t i = 0; i < bangra_argc; ++i) {
            const char *s = bangra_argv[i];
            out.args.push_back(String::from_cstr(s));
        }
    } break;
    case FN_ActiveAnchor: {
        CHECKARGS(0, 0);
        RETARGS(get_active_anchor());
    } break;
    case FN_ActiveFrame: {
        CHECKARGS(0, 0);
        RETARGS(frame);
    } break;
    case FN_Bitcast: {
        CHECKARGS(2, 2);
        // very dangerous
        in.args[1].verify<TYPE_Type>();
        Any result = in.args[2];
        result.type = in.args[1].typeref;
        RETARGS(result);
    } break;
    case FN_Branch: {
        CHECKARGS(3, 3);
        Any cond = in.args[1];
        cond.verify<TYPE_Bool>();
        out.enter = in.args[cond.i1?2:3];
        out.args = { in.args[0] };
    } break;
    case FN_BuiltinEq: {
        CHECKARGS(2, 2);
        in.args[1].verify<TYPE_Builtin>();
        in.args[2].verify<TYPE_Builtin>();
        RETARGS(Any(in.args[1].builtin == in.args[2].builtin));
    } break;
    case FN_DatumToSyntax: {
        const Anchor *anchor = nullptr;
        switch(CHECKARGS(1, 2)) {
        case 1: {
            switch(in.args[1].type.value()) {
            case TYPE_Parameter: {
                anchor = in.args[1].parameter->anchor;
            } break;
            case TYPE_Label: {
                anchor = in.args[1].label->anchor;
            } break;
            case TYPE_Syntax: {
                anchor = in.args[1].syntax->anchor;
            } break;
            default: {
                location_error(String::from("can't extract anchor from datum."));
            } break;
            }
        } break;
        case 2: {
            switch(in.args[2].type.value()) {
            case TYPE_Syntax: {
                anchor = in.args[2].syntax->anchor;
            } break;
            case TYPE_Anchor: {
                anchor = in.args[2].anchor;
            } break;
            default: {
                location_error(String::from("anchor or syntax expected."));
            } break;
            }
        } break;
        }
        RETARGS(Syntax::from(anchor, in.args[1]));
    } break;
    case FN_DatumToQuotedSyntax: {
        CHECKARGS(2, 2);
        const Anchor *anchor = in.args[2];
        RETARGS(Syntax::from_quoted(anchor, in.args[1]));
    } break;
    case FN_DefaultStyler: {
        CHECKARGS(2, 2);
        in.args[1].verify<TYPE_Symbol>();
        const String *s = in.args[2];
        StyledString ss;
        ss.out << (Style)in.args[1].symbol.known_value()
            << s->data << Style_None;
        RETARGS(ss.str());
    } break;
    case FN_Dump: {
        CHECKARGS(1, 1);
        StyledStream ss(std::cout);
        stream_expr(ss, in.args[1], StreamExprFormat());
        RETARGS(in.args[1]);
    } break;
    case FN_Exit:
        return false;
#define UNOP_CASE(NAME, TYPE, MEMBER, OP) \
    case NAME: { \
        CHECKARGS(1, 1); \
        in.args[1].verify<TYPE>(); \
        RETARGS(OP (in.args[1]. MEMBER)); \
    } break
#define BINOP_CASE(NAME, TYPE, MEMBER, OP) \
    case NAME: { \
        CHECKARGS(2, 2); \
        in.args[1].verify<TYPE>(); \
        in.args[2].verify<TYPE>(); \
        RETARGS(in.args[1]. MEMBER OP in.args[2]. MEMBER); \
    } break
#define TBINOP_CASE(NAME, TYPE, MEMBER, OP) \
    case NAME: { \
        CHECKARGS(2, 2); \
        in.args[1].verify<TYPE>(); \
        in.args[2].verify<TYPE>(); \
        RETARGS(OP(in.args[1]. MEMBER, in.args[2]. MEMBER)); \
    } break
#define SHIFT_CASES(UNAME, LNAME) \
    case FN_ ## UNAME ## LShift: { \
        CHECKARGS(2, 2); \
        in.args[1].verify<TYPE_ ## UNAME>(); \
        RETARGS((LNAME)in.args[1].u64 << cast_number<int32_t>(in.args[2])); \
    } break; \
    case FN_ ## UNAME ## RShift: { \
        CHECKARGS(2, 2); \
        in.args[1].verify<TYPE_ ## UNAME>(); \
        RETARGS((LNAME)in.args[1].u64 >> cast_number<int32_t>(in.args[2])); \
    } break
    BINOP_CASE(FN_ParameterEq, TYPE_Parameter, parameter, ==);
    BINOP_CASE(FN_LabelEq, TYPE_Label, label, ==);
    BINOP_CASE(FN_ScopeEq, TYPE_Scope, scope, ==);
    BINOP_CASE(FN_FrameEq, TYPE_Frame, frame, ==);
    BINOP_CASE(FN_ClosureEq, TYPE_Closure, closure, ==);
#define T0(NAME, STR)
#define T1(UNAME, LNAME, PFIX, OP) \
    UNOP_CASE(FN_ ## UNAME ## PFIX, TYPE_ ## UNAME, LNAME, OP);
#define T2(UNAME, LNAME, PFIX, OP) \
    BINOP_CASE(FN_ ## UNAME ## PFIX, TYPE_ ## UNAME, LNAME, OP);
#define T2T(UNAME, LNAME, PFIX, OP) \
    TBINOP_CASE(FN_ ## UNAME ## PFIX, TYPE_ ## UNAME, LNAME, PFIX ## T);
#undef T0
#undef T1
#undef T2
#undef T2T
#undef BINOP_CASE
#undef TBINOP_CASE
#undef UNOP_CASE
    case FN_FFISymbol: {
        CHECKARGS(1, 1);
        const String *str = in.args[1];
        RETARGS(Any::from_pointer(dlsym(global_c_namespace, str->data)));
    } break;
    case FN_FFICall: {
        run_ffi_function(in, out);
    } break;
    case FN_Flush: {
        CHECKARGS(0, 0);
        std::cout << std::flush;
    } break;
    case FN_FormatFrame: {
        CHECKARGS(1, 1);
        const Frame *frame = in.args[1];
        StyledString ss;
        stream_frame(ss.out, frame, StreamFrameFormat());
        RETARGS(ss.str());
    } break;
    case FN_Free: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_Pointer>();
        free(in.args[1].pointer);
    } break;
    case KW_Globals: {
        CHECKARGS(0, 0);
        RETARGS(globals);
    } break;
    case FN_InterpreterVersion: {
        CHECKARGS(0, 0);
        RETARGS(BANGRA_VERSION_MAJOR,
            BANGRA_VERSION_MINOR,
            BANGRA_VERSION_PATCH);
    } break;
    case FN_IsListEmpty: {
        CHECKARGS(1, 1);
        const List *a = in.args[1];
        RETARGS(a == EOL);
    } break;
    case FN_IsSyntaxQuoted: {
        CHECKARGS(1, 1);
        const Syntax *sx = in.args[1];
        RETARGS(Any(sx->quoted));
    } break;
    case SFXFN_LabelAppendParameter: {
        CHECKARGS(2, 2);
        Label *label = in.args[1];
        Parameter *param = in.args[2];
        label->append(param);
    } break;
    case FN_LabelNew: {
        CHECKARGS(2, 2);
        in.args[2].verify<TYPE_Symbol>();
        RETARGS(Label::from(in.args[1], in.args[2].symbol));
    } break;
    case FN_LabelParameters: {
        CHECKARGS(1, 1);
        Label *label = in.args[1];
        out.args = { none };
        size_t count = label->params.size();
        for (size_t i = 0; i < count; ++i) {
            out.args.push_back(label->params[i]);
        }
    } break;
    case FN_ListAt: {
        CHECKARGS(1, 1);
        const List *a = in.args[1];
        RETARGS((a == EOL)?none:a->at);
    } break;
    case FN_ListCons: {
        CHECKARGS(2, 2);
        RETARGS(List::from(in.args[1], in.args[2]));
    } break;
    case FN_ListCountOf: {
        CHECKARGS(1, 1);
        const List *l = in.args[1];
        if (l == EOL) {
            RETARGS((size_t)0);
        } else {
            RETARGS(l->count);
        }
    } break;
    case FN_ListJoin: {
        CHECKARGS(2, 2);
        const List *a = in.args[1];
        const List *b = in.args[2];
        RETARGS(List::join(a, b));
    } break;
    case FN_ListLoad: {
        CHECKARGS(1, 1);
        const String *path = in.args[1];
        LexerParser parser(SourceFile::from_file(path));
        RETARGS(parser.parse());
    } break;
    case FN_ListParse: {
        const String *text = nullptr;
        const String *path = nullptr;
        switch(CHECKARGS(1, 2)) {
        case 1: {
            text = in.args[1]; path = String::from("<string>");
        } break;
        case 2: {
            text = in.args[1]; path = in.args[2];
        } break;
        }
        assert(text);
        assert(path);
        LexerParser parser(SourceFile::from_string(path, text));
        RETARGS(parser.parse());
    } break;
    case FN_ListNext: {
        CHECKARGS(1, 1);
        const List *a = in.args[1];
        RETARGS((a == EOL)?EOL:a->next);
    } break;
    case FN_Malloc: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_U64>();
        RETARGS(Any::from_pointer(malloc(in.args[1].sizeval)));
    } break;
    case FN_MemCpy: {
        CHECKARGS(3, 3);
        in.args[1].verify<TYPE_Pointer>();
        in.args[2].verify<TYPE_Pointer>();
        in.args[3].verify<TYPE_U64>();
        memcpy(in.args[1].pointer, in.args[2].pointer, in.args[3].sizeval);
    } break;
    case OP_Not: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_Bool>();
        RETARGS(!in.args[1].i1);
    } break;
    case FN_ParameterAnchor: {
        CHECKARGS(1, 1);
        Parameter *param = in.args[1];
        RETARGS(param->anchor);
    } break;
    case FN_ParameterNew: {
        CHECKARGS(4, 4);
        in.args[2].verify<TYPE_Symbol>();
        in.args[3].verify<TYPE_Type>();
        in.args[4].verify<TYPE_Bool>();
        Parameter *param = nullptr;
        if (in.args[4].i1) {
            param = Parameter::vararg_from(in.args[1], in.args[2].symbol, in.args[3].type);
        } else {
            param = Parameter::from(in.args[1], in.args[2].symbol, in.args[3].type);
        }
        RETARGS(param);
    } break;
    case FN_ParameterName: {
        CHECKARGS(1, 1);
        Parameter *param = in.args[1];
        RETARGS(param->name);
    } break;
    case FN_StyleToString: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_Symbol>();
        StyledString ss;
        ss.out << in.args[1].symbol.known_value();
        RETARGS(ss.str());
    } break;
    case FN_ParseC: {
        int count = CHECKARGS(2, -1);
        const String *path = in.args[1];
        const String *buffer = in.args[2];
        std::vector<std::string> args;
        for (int i = 3; i <= count; ++i) {
            const String *arg = in.args[i];
            args.push_back(arg->data);
        }
        const List *result = import_c_module(path->data, args, buffer->data);
        RETARGS(result);
    } break;
    case FN_Prompt: {
        switch(CHECKARGS(1, 2)) {
        case 2: {
            const String *pre = in.args[2];
            linenoisePreloadBuffer(pre->data);
        }
        case 1: {
            const String *s = in.args[1];
            const char *r = linenoise(s->data);
            if (r) {
                linenoiseHistoryAdd(r);
                RETARGS(String::from_cstr(r));
            }
        } break;
        default: break;
        }
    } break;
    case FN_RefAt: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_Ref>();
        Any result = *in.args[1].ref;
        RETARGS(result);
    } break;
    case SFXFN_RefSet: {
        CHECKARGS(2, 2);
        in.args[1].verify<TYPE_Ref>();
        *in.args[1].ref = in.args[2];
    } break;
    case FN_RefNew: {
        CHECKARGS(1, 1);
        RETARGS(in.args[1].toref());
    } break;
    case FN_Repr: {
        CHECKARGS(1, 1);
        StyledString ss;
        ss.out << in.args[1];
        RETARGS(ss.str());
    } break;
    case FN_ScopeAt: {
        CHECKARGS(2, 2);
        Scope *scope = in.args[1];
        in.args[2].verify<TYPE_Symbol>();
        Any result = none;
        bool success = scope->lookup(in.args[2].symbol, result);
        RETARGS(result, Any(success));
    } break;
    case FN_ScopeNew: {
        switch(CHECKARGS(0, 1)) {
        case 0: {
            RETARGS(Scope::from());
        } break;
        case 1: {
            RETARGS(Scope::from(in.args[1]));
        } break;
        default: break;
        }
    } break;
    case FN_ScopeNextSymbol: {
        CHECKARGS(2,2);
        Scope *scope = in.args[1];
        switch(in.args[2].type.value()) {
        case TYPE_Nothing: {
            auto it = scope->map.begin();
            if (it != scope->map.end()) {
                RETARGS(it->first, it->second);
            }
        } break;
        case TYPE_Symbol: {
            auto it = scope->map.find(in.args[2].symbol);
            if (it != scope->map.end()) {
                it++;
                if (it != scope->map.end()) {
                    RETARGS(it->first, it->second);
                }
            }
        } break;
        default:
            location_error(String::from("symbol or none expected"));
            break;
        }
    } break;
    case SFXFN_SetExceptionHandler: {
        CHECKARGS(1, 1);
        exception_handler = in.args[1];
    } break;
    case SFXFN_SetGlobals: {
        CHECKARGS(1, 1);
        Scope *scope = in.args[1];
        globals = scope;
    } break;
    case SFXFN_SetGlobalApplyFallback: {
        CHECKARGS(1, 1);
        apply_unknown_type = in.args[1];
    } break;
    case SFXFN_SetScopeSymbol: {
        CHECKARGS(3, 3);
        Scope *scope = in.args[1];
        in.args[2].verify<TYPE_Symbol>();
        scope->bind(in.args[2].symbol, in.args[3]);
    } break;
    case FN_StringCmp: {
        CHECKARGS(2, 2);
        const String *a = in.args[1];
        const String *b = in.args[2];
        if (a->count == b->count) {
            RETARGS(memcmp(a->data, b->data, a->count));
        } else if (a->count < b->count) {
            RETARGS(Any(-1));
        } else {
            RETARGS(Any(1));
        }
    } break;
    case FN_StringCountOf: {
        CHECKARGS(1, 1);
        const String *a = in.args[1];
        RETARGS(a->count);
    } break;
    case FN_StringAt: {
        CHECKARGS(2, 2);
        const String *a = in.args[1];
        int64_t offset = cast_number<int64_t>(in.args[2]);
        while (offset < 0) {
            offset += (int64_t)a->count;
        }
        if (offset > (int64_t)a->count) {
            location_error(String::from("string index out of bounds"));
        }
        RETARGS(a->substr(offset, offset + 1));
    } break;
    case FN_StringJoin: {
        CHECKARGS(2, 2);
        const String *a = in.args[1];
        const String *b = in.args[2];
        RETARGS(String::join(a, b));
    } break;
    case FN_StringNew: {
        CHECKARGS(1, 1);
        switch(in.args[1].type.value()) {
        case TYPE_String: {
            RETARGS(in.args[1]);
        } break;
        case TYPE_Symbol: {
            RETARGS(in.args[1].symbol.name());
        } break;
        default: {
            StyledString ss = StyledString::plain();
            in.args[1].stream(ss.out, false);
            RETARGS(ss.str());
        } break;
        }
    } break;
    case FN_StringSlice: {
        CHECKARGS(3, 3);
        const String *a = in.args[1];
        int64_t i0 = cast_number<int64_t>(in.args[2]);
        int64_t i1 = cast_number<int64_t>(in.args[3]);
        RETARGS(a->substr(i0, i1));
    } break;
    case FN_SymbolNew: {
        CHECKARGS(1, 1);
        switch(in.args[1].type.value()) {
        case TYPE_String: {
            const String *str = in.args[1];
            RETARGS(Symbol(str));
        } break;
        case TYPE_Type: {
            RETARGS(in.args[1].typeref.name());
        } break;
        default:
            location_error(String::from("string or type expected"));
            break;
        }
    } break;
    case FN_SymbolEq: {
        CHECKARGS(2, 2);
        in.args[1].verify<TYPE_Symbol>();
        in.args[2].verify<TYPE_Symbol>();
        RETARGS(in.args[1].symbol == in.args[2].symbol);
    } break;
    case FN_SyntaxToDatum: {
        CHECKARGS(1, 1);
        if (in.args[1].type == TYPE_Syntax) {
            const Syntax *sx = in.args[1].syntax;
            RETARGS(sx->datum);
        } else {
            RETARGS(in.args[1]);
        }
    } break;
    case FN_SyntaxToAnchor: {
        CHECKARGS(1, 1);
        const Syntax *sx = in.args[1];
        RETARGS(sx->anchor);
    } break;
    case FN_Translate: {
        CHECKARGS(2, 2);
        const Anchor *body_anchor = in.args[1];
        const List *expr = in.args[2];
        Label *label = translate_root(expr, body_anchor);
        RETARGS(Closure::from(label, nullptr));
    } break;
    case SFXFN_TranslateLabelBody: {
        CHECKARGS(3, 3);
        Label *label = in.args[1];
        const Anchor *body_anchor = in.args[2];
        const List *expr = in.args[3];
        translate_function_expr_list(label, expr, body_anchor);
    } break;
    case FN_TypeEq: {
        CHECKARGS(2, 2);
        in.args[1].verify<TYPE_Type>();
        in.args[2].verify<TYPE_Type>();
        RETARGS(in.args[1].typeref == in.args[2].typeref);
    } break;
    case FN_TypeNew: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_Symbol>();
        RETARGS(Type(in.args[1].symbol));
    } break;
    case FN_TypeOf: {
        CHECKARGS(1, 1);
        RETARGS(in.args[1].type);
    } break;
    case FN_TypeName: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_Type>();
        RETARGS(in.args[1].typeref.name());
    } break;
    case FN_TypeSizeOf: {
        CHECKARGS(1, 1);
        in.args[1].verify<TYPE_Type>();
        RETARGS(in.args[1].typeref.bytesize());
    } break;
    case FN_VaCountOf: {
        RETARGS(CHECKARGS(0, -1));
    } break;
    case FN_Write: {
        CHECKARGS(1, 1);
        const String *s = in.args[1];
        std::cout << s->data;
    } break;
    default: {
        StyledString ss;
        ss.out << "builtin " << in.enter.builtin << " is not implemented";
        location_error(ss.str());
        return false;
        } break;
    }
    return true;
}

#undef CHECKARGS
#undef RETARGS

static const Frame *find_frame(const Frame *frame, Parameter *param) {
    while (frame) {
        if (param->label == frame->label) {
            return frame;
        }
        frame = frame->parent;
    }
    location_error(String::from("unbound parameter encountered"));
    return nullptr;
}

static Any evaluate_param(const Frame *frame, Parameter *param) {
    frame = find_frame(frame, param);
    if (frame) {
        if (param->vararg) {
            if ((size_t)param->index < frame->args.size()) {
                return frame->args[param->index];
            } else {
                return none;
            }
        } else {
            return frame->args[param->index];
        }
    }
    return none;
}

static void evaluate_param(std::vector<Any> &destargs,
    const Frame *frame, Parameter *param) {
    frame = find_frame(frame, param);
    if (frame) {
        if (param->vararg) {
            auto &&args = frame->args;
            size_t count = args.size();
            for (size_t i = param->index; i < count; ++i) {
                destargs.push_back(args[i]);
            }
        } else {
            destargs.push_back(frame->args[param->index]);
        }
    }
}

static Any evaluate_enter(const Frame *frame, Any arg) {
    switch(arg.type.value()) {
    case TYPE_Parameter: {
        return evaluate_param(frame, arg.parameter);
    } break;
    default: return arg;
    }
    return none;
}

static Any evaluate(const Frame *frame, Any arg) {
    switch(arg.type.value()) {
    case TYPE_Parameter: {
        return evaluate_param(frame, arg.parameter);
    } break;
    case TYPE_Label: {
        return Closure::from(arg.label, frame);
    } break;
    default: return arg;
    }
    return none;
}

static void evaluate(std::vector<Any> &destargs, const Frame *frame, Any arg) {
    switch(arg.type.value()) {
    case TYPE_Parameter: {
        evaluate_param(destargs, frame, arg.parameter);
    } break;
    case TYPE_Label: {
        destargs.push_back(Closure::from(arg.label, frame));
    } break;
    default: {
        destargs.push_back(arg);
    } break;
    }
}

static void interpreter_loop(Instruction &_in) {
    Instruction _out;

    Instruction *in = &_in;
    Instruction *out = &_out;

    const Frame *frame = nullptr;

restart_loop:
    try {
loop:
    out->clear();
    Any &enter = in->enter;
    Any &next_enter = out->enter;
    const std::vector<Any> &args = in->args;
    std::vector<Any> &next_args = out->args;
    switch(enter.type.value()) {
    case TYPE_Closure: {
        frame = enter.closure->frame;
        enter = enter.closure->label;
        goto loop;
    } break;
    case TYPE_Label: {
        //debugger.enter_call(dest, cont, ...)

        Label *label = enter.label;
        Frame *nextframe = Frame::from(frame, label);
        // map arguments
        size_t srci = 0;
        size_t rcount = args.size();
        size_t pcount = label->params.size();
        auto &&frameargs = nextframe->args;
        frameargs.reserve(pcount);
        for (size_t i = 0; i < pcount; ++i) {
            Parameter *param = label->params[i];
            if (param->vararg) {
                assert(i != 0);
                assert(i == (pcount - 1));

                while (srci < rcount) {
                    frameargs.push_back(args[srci++]);
                }
            } else if (srci < rcount) {
                frameargs.push_back(args[srci]);
                srci = srci + 1;
            } else {
                frameargs.push_back(none);
            }
        }
        frame = nextframe;

        next_enter = evaluate_enter(frame, label->body.enter);

        auto &&args = label->body.args;
        size_t acount = args.size();
        if (acount) {
            size_t alast = acount - 1;
            for (size_t i = 0; i < alast; ++i) {
                next_args.push_back(evaluate(frame, args[i]));
            }
            evaluate(next_args, frame, args[alast]);
        }

        if (label->body.anchor) {
            set_active_anchor(label->body.anchor);
        } else if (label->anchor) {
            set_active_anchor(label->anchor);
        }
    } break;
    case TYPE_Builtin: {
        //debugger.enter_call(dest, cont, ...)
        next_enter = args[0];
        if (!handle_builtin(frame, *in, *out))
            return;
    } break;
    default: {
        if (apply_unknown_type) {
            next_enter = apply_unknown_type;
            next_args = { args[0], get_active_anchor(), enter };
            for (size_t i = 1; i < args.size(); ++i) {
                next_args.push_back(args[i]);
            }
        } else {
            apply_type_error(enter);
        }
    } break;
    }

    // flip
    Instruction *tmp = in;
    in = out;
    out = tmp;
    goto loop;
    } catch (const Exception &exc) {
        if (!exception_handler) {
        #if 1
            StyledStream cerr(std::cout);
            cerr << *in << std::endl;
        #endif
            default_exception_handler(exc);
        } else {
            in->enter = exception_handler;
            Any cont = in->args[0];
            in->args = { cont, exc.anchor, exc.msg };
        }
    }

    goto restart_loop;
}

#endif

//------------------------------------------------------------------------------
// IL->IR LOWERING
//------------------------------------------------------------------------------

static bool is_basic_block_like(Label *label) {
    if (label->params.empty())
        return true;
    if (label->params[0]->type == TYPE_Nothing)
        return true;
    return false;
}

static bool has_free_parameters(Label *label) {
    std::vector<Label *> scope;
    label->build_scope(scope);
    scope.push_back(label);
    std::unordered_set<Label *> labels;
    for (auto &&label : scope) {
        labels.insert(label);
    }
    for (auto &&label : scope) {
        auto &&enter = label->body.enter;
        if (enter.type == TYPE_Parameter && !labels.count(enter.parameter->label))
            return true;
        for (auto &&arg : label->body.args) {
            if (arg.type == TYPE_Parameter && !labels.count(arg.parameter->label))
                return true;
        }
    }
    return false;
}

static void build_and_run_opt_passes(LLVMModuleRef module) {
    LLVMPassManagerBuilderRef passBuilder;

    passBuilder = LLVMPassManagerBuilderCreate();
    LLVMPassManagerBuilderSetOptLevel(passBuilder, 3);
    LLVMPassManagerBuilderSetSizeLevel(passBuilder, 0);
    LLVMPassManagerBuilderUseInlinerWithThreshold(passBuilder, 225);

    LLVMPassManagerRef functionPasses =
      LLVMCreateFunctionPassManagerForModule(module);
    LLVMPassManagerRef modulePasses =
      LLVMCreatePassManager();

    LLVMPassManagerBuilderPopulateFunctionPassManager(passBuilder,
                                                      functionPasses);
    LLVMPassManagerBuilderPopulateModulePassManager(passBuilder, modulePasses);

    LLVMPassManagerBuilderDispose(passBuilder);

    LLVMInitializeFunctionPassManager(functionPasses);
    for (LLVMValueRef value = LLVMGetFirstFunction(module);
         value; value = LLVMGetNextFunction(value))
      LLVMRunFunctionPassManager(functionPasses, value);
    LLVMFinalizeFunctionPassManager(functionPasses);

    LLVMRunPassManager(modulePasses, module);

    LLVMDisposePassManager(functionPasses);
    LLVMDisposePassManager(modulePasses);
}

struct GenerateCtx {
    std::unordered_map<Label *, LLVMValueRef> label2func;
    std::unordered_map<Label *, LLVMBasicBlockRef> label2bb;
    std::unordered_map<Parameter *, LLVMValueRef> param2value;
    std::unordered_map<Type, LLVMTypeRef, Type::Hash> type_cache;
    std::vector<Type> type_todo;

    LLVMModuleRef module;
    LLVMBuilderRef builder;

    LLVMTypeRef voidT;
    LLVMTypeRef i1T;
    LLVMTypeRef i8T;
    LLVMTypeRef i16T;
    LLVMTypeRef i32T;
    LLVMTypeRef i64T;
    LLVMTypeRef f32T;
    LLVMTypeRef f64T;
    LLVMTypeRef rawstringT;
    LLVMTypeRef noneT;

    LLVMValueRef noneV;

    Label *active_function;

    GenerateCtx() :
        active_function(nullptr) {
    }

    void define_builtin_functions() {
        voidT = LLVMVoidType();
        i1T = LLVMInt1Type();
        i8T = LLVMInt8Type();
        i16T = LLVMInt16Type();
        i32T = LLVMInt32Type();
        i64T = LLVMInt64Type();
        f32T = LLVMFloatType();
        f64T = LLVMDoubleType();
        noneV = LLVMConstStruct(nullptr, 0, false);
        noneT = LLVMTypeOf(noneV);
        rawstringT = LLVMPointerType(LLVMInt8Type(), 0);
    }

#undef DEFINE_BUILTIN

    static bool all_parameters_lowered(Label *label) {
        for (auto &&param : label->params) {
            if (param->vararg)
                return false;
            switch (param->type.value()) {
            case TYPE_Type:
            case TYPE_Label:
                return false;
            default: {
                if (typesets.is(param->type))
                    return false;
                if (typed_labels.is(param->type) && (param->index != 0))
                    return false;
            } break;
            }
        }
        return true;
    }

    LLVMTypeRef create_llvm_type(Type type) {
        switch (type.value()) {
        case TYPE_Void: return voidT;
        case TYPE_Nothing: return noneT;
        case TYPE_Bool: return i1T;
        case TYPE_I8:
        case TYPE_U8: return i8T;
        case TYPE_I16:
        case TYPE_U16: return i16T;
        case TYPE_I32:
        case TYPE_U32: return i32T;
        case TYPE_I64:
        case TYPE_U64: return i64T;
        case TYPE_F32: return f32T;
        case TYPE_F64: return f64T;
        default: break;
        }

        switch(category_of(type)) {
        case CAT_Pointer: return LLVMPointerType(
            _type_to_llvm_type(pointers.get(type).element_type), 0);
        case CAT_Array: {
            auto &&ai = arrays.get(type);
            return LLVMArrayType(_type_to_llvm_type(ai.element_type), ai.count);
        } break;
        case CAT_Vector: {
            auto &&vi = vectors.get(type);
            return LLVMVectorType(_type_to_llvm_type(vi.element_type), vi.count);
        } break;
        case CAT_Tuple: {
            auto &&ti = tuples.get(type);
            size_t count = ti.types.size();
            LLVMTypeRef elements[count];
            for (size_t i = 0; i < count; ++i) {
                elements[i] = _type_to_llvm_type(ti.types[i]);
            }
            return LLVMStructType(elements, count, false);
        } break;
        case CAT_Union: {
            auto &&ui = unions.get(type);
            size_t count = ui.types.size();
            size_t sz = ui.size;
            size_t al = ui.align;
            // find member with the same alignment
            for (size_t i = 0; i < count; ++i) {
                Type ET = ui.types[i];
                size_t etal = align_of(ET);
                if (etal == al) {
                    size_t remsz = sz - size_of(ET);
                    LLVMTypeRef values[2];
                    values[0] = _type_to_llvm_type(ET);
                    if (remsz) {
                        // too small, add padding
                        values[1] = LLVMArrayType(i8T, remsz);
                        return LLVMStructType(values, 2, false);
                    } else {
                        return LLVMStructType(values, 1, false);
                    }
                }
            }
            // should never get here
            assert(false);
        } break;
        case CAT_Typename: {
            auto &tn = typenames.get(type);
            if (tn.finalized) {
                switch(category_of(tn.storage_type)) {
                case CAT_Tuple:
                case CAT_Union: {
                    type_todo.push_back(type);
                } break;
                default: {
                    return create_llvm_type(tn.storage_type);
                } break;
                }
            }
            return LLVMStructCreateNamed(
                LLVMGetGlobalContext(), type.name().name()->data);
        } break;
        case CAT_Function: {
            auto &fi = functions.get(type);
            size_t count = fi.argument_types.size();
            LLVMTypeRef elements[count];
            for (size_t i = 0; i < count; ++i) {
                elements[i] = _type_to_llvm_type(fi.argument_types[i]);
            }
            return LLVMFunctionType(
                _type_to_llvm_type(fi.return_type),
                elements, count, fi.vararg());
        } break;
        case CAT_TypedLabel:
        case CAT_TypeSet:
        default: break;
        };

        StyledString ss;
        ss.out << "IL->IR: cannot convert type " << type;
        location_error(ss.str());
        return nullptr;
    }

    void finalize_types() {
        while (!type_todo.empty()) {
            Type T = type_todo.back();
            type_todo.pop_back();
            assert(typenames.is(T));
            auto &&tn = typenames.get(T);
            if (!tn.finalized)
                continue;
            LLVMTypeRef LLT = _type_to_llvm_type(T);
            Type ST = tn.storage_type;
            switch(category_of(ST)) {
            case CAT_Tuple: {
                auto &&ti = tuples.get(ST);
                size_t count = ti.types.size();
                LLVMTypeRef elements[count];
                for (size_t i = 0; i < count; ++i) {
                    elements[i] = _type_to_llvm_type(ti.types[i]);
                }
                LLVMStructSetBody(LLT, elements, count, false);
            } break;
            case CAT_Union: {
                auto &&ui = unions.get(ST);
                size_t count = ui.types.size();
                size_t sz = ui.size;
                size_t al = ui.align;
                // find member with the same alignment
                for (size_t i = 0; i < count; ++i) {
                    Type ET = ui.types[i];
                    size_t etal = align_of(ET);
                    if (etal == al) {
                        size_t remsz = sz - size_of(ET);
                        LLVMTypeRef values[2];
                        values[0] = _type_to_llvm_type(ET);
                        if (remsz) {
                            // too small, add padding
                            values[1] = LLVMArrayType(i8T, remsz);
                            LLVMStructSetBody(LLT, values, 2, false);
                        } else {
                            LLVMStructSetBody(LLT, values, 1, false);
                        }
                        break;
                    }
                }
            } break;
            default: assert(false); break;
            }
        }
    }

    LLVMTypeRef _type_to_llvm_type(Type type) {
        auto it = type_cache.find(type);
        if (it == type_cache.end()) {
            LLVMTypeRef result = create_llvm_type(type);
            type_cache.insert({type, result});
            return result;
        } else {
            return it->second;
        }
    }

    LLVMTypeRef type_to_llvm_type(Type type) {
        auto typeref = _type_to_llvm_type(type);
        finalize_types();
        return typeref;
    }

    LLVMTypeRef return_type_to_llvm_type(Type type) {
        if (type == TYPE_Void) {
            StyledString ss;
            ss.out << "IL->IR: untyped continuation encountered";
            location_error(ss.str());
        } else if (!typed_labels.is(type)) {
            StyledString ss;
            ss.out << "IL->IR: invalid continuation type: " << type;
            location_error(ss.str());
        }
        auto &&tli = typed_labels.get(type);
        assert(tli.types[0] == TYPE_Nothing);
        size_t count = tli.types.size() - 1;
        if (!count) {
            return LLVMVoidType();
        }
        LLVMTypeRef element_types[count];
        for (size_t i = 0; i < count; ++i) {
            Type arg = tli.types[i + 1];
            element_types[i] = type_to_llvm_type(arg);
        }
        if (count == 1) {
            return element_types[0];
        } else {
            return LLVMStructType(element_types, count, false);
        }
    }

    LLVMValueRef argument_to_value(Any value) {
        switch(value.type.value()) {
        case TYPE_Nothing:
            return noneV;
        case TYPE_I8: return LLVMConstInt(i8T, value.i8, true);
        case TYPE_I16: return LLVMConstInt(i16T, value.i16, true);
        case TYPE_I32: return LLVMConstInt(i32T, value.i32, true);
        case TYPE_I64: return LLVMConstInt(i64T, value.i64, true);
        case TYPE_Bool: return LLVMConstInt(i1T, value.i1, false);
        case TYPE_U8: return LLVMConstInt(i8T, value.u8, false);
        case TYPE_U16: return LLVMConstInt(i16T, value.u16, false);
        case TYPE_U32: return LLVMConstInt(i32T, value.u32, false);
        case TYPE_U64: return LLVMConstInt(i64T, value.u64, false);
        case TYPE_F32: return LLVMConstReal(f32T, value.f32);
        case TYPE_F64: return LLVMConstReal(f64T, value.f64);
        case TYPE_Parameter: {
            auto it = param2value.find(value.parameter);
            if (it == param2value.end()) {
                StyledString ss;
                ss.out << "IL->IR: untranslated parameter: " << value.parameter;
                location_error(ss.str());
            }
            return it->second;
        } break;
        case TYPE_Label: {
            if (is_basic_block_like(value.label)) {
                return LLVMBasicBlockAsValue(label_to_basic_block(value.label));
            } else {
                return label_to_function(value.label);
            }
        } break;
        default: break;
        }

        switch(category_of(value.type)) {
        case CAT_Pointer: {
            LLVMTypeRef LLT = type_to_llvm_type(value.type);
            if (!value.pointer) {
                return LLVMConstPointerNull(LLT);
            } else {
                return LLVMConstIntToPtr(
                    LLVMConstInt(i64T, *(uint64_t*)&value.pointer, false),
                    LLT);
            }
        } break;
        case CAT_Typename: {
            LLVMTypeRef LLT = type_to_llvm_type(value.type);
            auto &&tn = typenames.get(value.type);
            switch(category_of(tn.storage_type)) {
            case CAT_Tuple: {
                auto &&ti = tuples.get(tn.storage_type);
                size_t count = ti.types.size();
                LLVMValueRef values[count];
                for (size_t i = 0; i < count; ++i) {
                    values[i] = argument_to_value(ti.unpack(value.pointer, i));
                }
                return LLVMConstNamedStruct(LLT, values, count);
            } break;
            default: {
                Any storage_value = value;
                storage_value.type = tn.storage_type;
                LLVMValueRef val = argument_to_value(storage_value);
                return LLVMConstBitCast(val, LLT);
            } break;
            }
        } break;
        case CAT_Tuple: {
            auto &&ti = tuples.get(value.type);
            size_t count = ti.types.size();
            LLVMValueRef values[count];
            for (size_t i = 0; i < count; ++i) {
                values[i] = argument_to_value(ti.unpack(value.pointer, i));
            }
            return LLVMConstStruct(values, count, false);
        } break;
        case CAT_Array:
        case CAT_Vector:
        case CAT_Union:
        case CAT_TypedLabel:
        case CAT_TypeSet:
        default: break;
        };

        StyledString ss;
        ss.out << "IL->IR: cannot convert argument of type " << value.type;
        location_error(ss.str());
        return nullptr;
    }

    void write_label_body(Label *label) {
        auto &&body = label->body;
        auto &&enter = body.enter;
        auto &&args = body.args;

        set_active_anchor(label->body.anchor);

        assert(!args.empty());
        size_t argcount = args.size() - 1;
        size_t argn = 1;
#define READ_ANY(NAME) \
        assert(argn <= argcount); \
        Any &NAME = args[argn++];
#define READ_VALUE(NAME) \
        assert(argn <= argcount); \
        LLVMValueRef NAME = argument_to_value(args[argn++]);
#define READ_TYPE(NAME) \
        assert(argn <= argcount); \
        assert(args[argn].type == TYPE_Type); \
        LLVMTypeRef NAME = type_to_llvm_type(args[argn++].typeref);

        LLVMValueRef retvalue = nullptr;
        switch(enter.type.value()) {
        case TYPE_Builtin: {
            switch(enter.builtin.value()) {
            case FN_Branch: {
                READ_VALUE(cond);
                READ_VALUE(then_block);
                READ_VALUE(else_block);
                assert(LLVMValueIsBasicBlock(then_block));
                assert(LLVMValueIsBasicBlock(else_block));
                LLVMBuildCondBr(builder, cond,
                    LLVMValueAsBasicBlock(then_block),
                    LLVMValueAsBasicBlock(else_block));
            } break;
            case FN_Unconst: {
                READ_VALUE(val);
                retvalue = val;
            } break;
            case FN_ExtractValue: {
                READ_VALUE(val);
                READ_ANY(index);
                retvalue = LLVMBuildExtractValue(
                    builder, val, cast_number<int32_t>(index), "");
            } break;
            case FN_GetElementPtr: {
                READ_VALUE(pointer);
                assert(argcount > 1);
                size_t count = argcount - 1;
                LLVMValueRef indices[count];
                for (size_t i = 0; i < count; ++i) {
                    indices[i] = argument_to_value(args[argn + i]);
                }
                retvalue = LLVMBuildGEP(builder, pointer, indices, count, "");
            } break;
            case FN_Bitcast: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildBitCast(builder, val, ty, ""); } break;
            case FN_IntToPtr: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildIntToPtr(builder, val, ty, ""); } break;
            case FN_PtrToInt: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildPtrToInt(builder, val, ty, ""); } break;
            case FN_Trunc: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildTrunc(builder, val, ty, ""); } break;
            case FN_SExt: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildSExt(builder, val, ty, ""); } break;
            case FN_ZExt: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildZExt(builder, val, ty, ""); } break;
            case FN_Load: { READ_VALUE(val);
                retvalue = LLVMBuildLoad(builder, val, ""); } break;
            case FN_Store: { READ_VALUE(val); READ_VALUE(ptr);
                retvalue = LLVMBuildStore(builder, val, ptr); } break;
            case OP_ICmpEQ:
            case OP_ICmpNE:
            case OP_ICmpUGT:
            case OP_ICmpUGE:
            case OP_ICmpULT:
            case OP_ICmpULE:
            case OP_ICmpSGT:
            case OP_ICmpSGE:
            case OP_ICmpSLT:
            case OP_ICmpSLE: {
                READ_VALUE(a); READ_VALUE(b);
                LLVMIntPredicate pred;
                switch(enter.builtin.value()) {
                    case OP_ICmpEQ: pred = LLVMIntEQ; break;
                    case OP_ICmpNE: pred = LLVMIntNE; break;
                    case OP_ICmpUGT: pred = LLVMIntUGT; break;
                    case OP_ICmpUGE: pred = LLVMIntUGE; break;
                    case OP_ICmpULT: pred = LLVMIntULT; break;
                    case OP_ICmpULE: pred = LLVMIntULE; break;
                    case OP_ICmpSGT: pred = LLVMIntSGT; break;
                    case OP_ICmpSGE: pred = LLVMIntSGE; break;
                    case OP_ICmpSLT: pred = LLVMIntSLT; break;
                    case OP_ICmpSLE: pred = LLVMIntSLE; break;
                    default: assert(false); break;
                }
                retvalue = LLVMBuildICmp(builder, pred, a, b, "");
            } break;
            case OP_Add: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildAdd(builder, a, b, ""); } break;
            case OP_AddNUW: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildNUWAdd(builder, a, b, ""); } break;
            case OP_AddNSW: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildNSWAdd(builder, a, b, ""); } break;
            case OP_Sub: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildSub(builder, a, b, ""); } break;
            case OP_SubNUW: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildNUWSub(builder, a, b, ""); } break;
            case OP_SubNSW: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildNSWSub(builder, a, b, ""); } break;
            case OP_Mul: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildMul(builder, a, b, ""); } break;
            case OP_MulNUW: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildNUWMul(builder, a, b, ""); } break;
            case OP_MulNSW: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildNSWMul(builder, a, b, ""); } break;
            case OP_SDiv: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildSDiv(builder, a, b, ""); } break;
            case OP_UDiv: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildUDiv(builder, a, b, ""); } break;
            case OP_SRem: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildSRem(builder, a, b, ""); } break;
            case OP_URem: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildURem(builder, a, b, ""); } break;
            case OP_Shl: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildShl(builder, a, b, ""); } break;
            case OP_LShr: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildLShr(builder, a, b, ""); } break;
            case OP_AShr: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildAShr(builder, a, b, ""); } break;
            case OP_BAnd: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildAnd(builder, a, b, ""); } break;
            case OP_BOr: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildOr(builder, a, b, ""); } break;
            case OP_BXor: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildXor(builder, a, b, ""); } break;
            case OP_FAdd: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildFAdd(builder, a, b, ""); } break;
            case OP_FSub: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildFSub(builder, a, b, ""); } break;
            case OP_FMul: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildFMul(builder, a, b, ""); } break;
            case OP_FDiv: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildFDiv(builder, a, b, ""); } break;
            case OP_FRem: { READ_VALUE(a); READ_VALUE(b);
                retvalue = LLVMBuildFRem(builder, a, b, ""); } break;
            default: {
                StyledString ss;
                ss.out << "IL->IR: unsupported builtin " << enter.builtin << " encountered";
                location_error(ss.str());
            } break;
            }
        } break;
        case TYPE_Label: {
            LLVMValueRef values[argcount];
            for (size_t i = 0; i < argcount; ++i) {
                values[i] = argument_to_value(args[i + 1]);
            }
            LLVMValueRef value = argument_to_value(enter);
            if (LLVMValueIsBasicBlock(value)) {
                auto bbfrom = LLVMGetInsertBlock(builder);
                // assign phi nodes
                auto &&params = enter.label->params;
                LLVMBasicBlockRef incobbs[] = { bbfrom };
                for (size_t i = 1; i < params.size(); ++i) {
                    Parameter *param = params[i];
                    LLVMValueRef phinode = argument_to_value(param);
                    LLVMValueRef incovals[] = { values[i - 1] };
                    LLVMAddIncoming(phinode, incovals, incobbs, 1);
                }
                LLVMBuildBr(builder, LLVMValueAsBasicBlock(value));
            } else {
                retvalue = LLVMBuildCall(builder, value, values, argcount, "");
                if (LLVMGetReturnType(LLVMGetElementType(LLVMTypeOf(value))) == voidT)
                    retvalue = nullptr;
            }
        } break;
        case TYPE_Parameter: {
            LLVMValueRef values[argcount];
            for (size_t i = 0; i < argcount; ++i) {
                values[i] = argument_to_value(args[i + 1]);
            }
            // must be a return
            assert(enter.parameter->index == 0);
            // must be returning from this function
            assert(enter.parameter->label == active_function);
            if (argcount > 1) {
                LLVMBuildAggregateRet(builder, values, argcount);
            } else if (argcount == 1) {
                LLVMBuildRet(builder, values[0]);
            } else {
                LLVMBuildRetVoid(builder);
            }
        } break;
        default: {
            LLVMValueRef values[argcount];
            for (size_t i = 0; i < argcount; ++i) {
                values[i] = argument_to_value(args[i + 1]);
            }
            if (is_function_pointer(enter.type)) {
                auto &&pi = pointers.get(enter.type);
                auto &&fi = functions.get(pi.element_type);

                size_t fargcount = fi.argument_types.size();
                assert(argcount >= fargcount);
                // make variadic calls C compatible
                if (fi.flags & FF_Variadic) {
                    for (size_t i = fargcount; i < argcount; ++i) {
                        auto value = values[i];
                        // floats need to be widened to doubles
                        if (LLVMTypeOf(value) == f32T) {
                            values[i] = LLVMBuildFPExt(builder, value, f64T, "");
                        }
                    }
                }

                auto ret = LLVMBuildCall(builder,
                    argument_to_value(enter), values, argcount, "");
                if (fi.return_type != TYPE_Void) {
                    retvalue = ret;
                }
            } else {
                assert(false && "todo: translate non-builtin call");
            }
        } break;
        }

        Any contarg = args[0];
        if (contarg.type == TYPE_Parameter) {
            assert(contarg.parameter->index == 0);
            assert(contarg.parameter->label == active_function);
            if (retvalue) {
                LLVMBuildRet(builder, retvalue);
            } else {
                LLVMBuildRetVoid(builder);
            }
        } else if (contarg.type == TYPE_Label) {
            auto bb = label_to_basic_block(contarg.label);
            if (retvalue) {
                auto bbfrom = LLVMGetInsertBlock(builder);
                // assign phi nodes
                auto &&params = contarg.label->params;
                LLVMBasicBlockRef incobbs[] = { bbfrom };
                for (size_t i = 1; i < params.size(); ++i) {
                    Parameter *param = params[i];
                    LLVMValueRef phinode = argument_to_value(param);
                    LLVMValueRef incoval = nullptr;
                    if (params.size() == 2) {
                        // single argument
                        incoval = retvalue;
                    } else {
                        // multiple arguments
                        incoval = LLVMBuildExtractValue(builder, retvalue, i - 1, "");
                    }
                    LLVMAddIncoming(phinode, &incoval, incobbs, 1);
                }
            }
            LLVMBuildBr(builder, bb);
        } else if (contarg.type == TYPE_Nothing) {
        } else {
            assert(false && "todo: continuing with unexpected value");
        }
    }
#undef READ_ANY
#undef READ_VALUE
#undef READ_TYPE

    LLVMBasicBlockRef label_to_basic_block(Label *label) {
        auto it = label2bb.find(label);
        if (it == label2bb.end()) {
            auto old_bb = LLVMGetInsertBlock(builder);
            LLVMValueRef func = LLVMGetBasicBlockParent(old_bb);
            const char *name = label->name.name()->data;
            auto bb = LLVMAppendBasicBlock(func, name);
            label2bb[label] = bb;
            LLVMPositionBuilderAtEnd(builder, bb);

            auto &&params = label->params;
            if (!params.empty()) {
                size_t paramcount = label->params.size() - 1;
                for (size_t i = 0; i < paramcount; ++i) {
                    Parameter *param = params[i + 1];
                    auto pvalue = LLVMBuildPhi(builder,
                        type_to_llvm_type(param->type),
                        param->name.name()->data);
                    param2value[param] = pvalue;
                }
            }

            write_label_body(label);

            LLVMPositionBuilderAtEnd(builder, old_bb);
            return bb;
        } else {
            return it->second;
        }
    }

    LLVMValueRef label_to_function(Label *label) {
        auto it = label2func.find(label);
        if (it == label2func.end()) {
            const Anchor *old_anchor = get_active_anchor();
            set_active_anchor(label->anchor);
            Label *last_function = active_function;
            active_function = label;

            auto old_bb = LLVMGetInsertBlock(builder);
            const char *name = label->name.name()->data;

            auto &&params = label->params;
            auto &&contparam = params[0];

            LLVMTypeRef return_type = return_type_to_llvm_type(contparam->type);

            size_t paramcount = label->params.size() - 1;
            LLVMTypeRef arg_types[paramcount];
            for (size_t i = 0; i < paramcount; ++i) {
                arg_types[i] = type_to_llvm_type(params[i + 1]->type);
            }
            auto functype = LLVMFunctionType(return_type, arg_types, paramcount, false);
            auto func = LLVMAddFunction(module, name, functype);
            LLVMSetLinkage(func, LLVMPrivateLinkage);

            for (size_t i = 0; i < paramcount; ++i) {
                Parameter *param = params[i + 1];
                auto pvalue = LLVMGetParam(func, i);
                param2value[param] = pvalue;
            }

            label2func[label] = func;

            auto bb = LLVMAppendBasicBlock(func, "");
            LLVMPositionBuilderAtEnd(builder, bb);

            write_label_body(label);

            LLVMPositionBuilderAtEnd(builder, old_bb);
            active_function = last_function;
            set_active_anchor(old_anchor);
            return func;
        } else {
            return it->second;
        }
    }

    std::pair<LLVMModuleRef, LLVMValueRef> generate(Label *entry) {
        assert(!has_free_parameters(entry));
        assert(all_parameters_lowered(entry));
        assert(!is_basic_block_like(entry));

        const char *name = entry->name.name()->data;
        module = LLVMModuleCreateWithName(name);
        builder = LLVMCreateBuilder();
        define_builtin_functions();

        auto func = label_to_function(entry);
        LLVMSetLinkage(func, LLVMExternalLinkage);

        finalize_types();

#if BANGRA_DEBUG_CODEGEN
        LLVMDumpModule(module);
#endif
        char *errmsg = NULL;
        LLVMVerifyModule(module, LLVMAbortProcessAction, &errmsg);
        LLVMDisposeMessage(errmsg);

        return std::pair<LLVMModuleRef, LLVMValueRef>(module, func);
    }

};

//------------------------------------------------------------------------------
// IL COMPILER
//------------------------------------------------------------------------------

static void pprint(int pos, unsigned char *buf, int len, const char *disasm) {
  int i;
  printf("%04x:  ", pos);
  for (i = 0; i < 8; i++) {
    if (i < len) {
      printf("%02x ", buf[i]);
    } else {
      printf("   ");
    }
  }

  printf("   %s\n", disasm);
}

static void do_disassemble(LLVMTargetMachineRef tm, void *fptr, int siz) {

    unsigned char *buf = (unsigned char *)fptr;

  LLVMDisasmContextRef D = LLVMCreateDisasmCPUFeatures(
    LLVMGetTargetMachineTriple(tm),
    LLVMGetTargetMachineCPU(tm),
    LLVMGetTargetMachineFeatureString(tm),
    NULL, 0, NULL, NULL);
    LLVMSetDisasmOptions(D,
        LLVMDisassembler_Option_PrintImmHex);
  char outline[1024];
  int pos;

  if (!D) {
    printf("ERROR: Couldn't create disassembler\n");
    return;
  }

  pos = 0;
  while (pos < siz) {
    size_t l = LLVMDisasmInstruction(D, buf + pos, siz - pos, 0, outline,
                                     sizeof(outline));
    if (!l) {
      pprint(pos, buf + pos, 1, "\t???");
      pos++;
        break;
    } else {
      pprint(pos, buf + pos, l, outline);
      pos += l;
    }
  }

  LLVMDisasmDispose(D);
}

class DisassemblyListener : public llvm::JITEventListener {
public:
    llvm::ExecutionEngine *ee;
    DisassemblyListener(llvm::ExecutionEngine *_ee) : ee(_ee) {}

    std::unordered_map<void *, size_t> sizes;

    void InitializeDebugData(
        llvm::StringRef name,
        llvm::object::SymbolRef::Type type, uint64_t sz) {
        if(type == llvm::object::SymbolRef::ST_Function) {
            #if !defined(__arm__) && !defined(__linux__)
            name = name.substr(1);
            #endif
            void * addr = (void*)ee->getFunctionAddress(name);
            if(addr) {
                assert(addr);
                sizes[addr] = sz;
            }
        }
    }

    virtual void NotifyObjectEmitted(
        const llvm::object::ObjectFile &Obj,
        const llvm::RuntimeDyld::LoadedObjectInfo &L) {
        auto size_map = llvm::object::computeSymbolSizes(Obj);
        for(auto & S : size_map) {
            llvm::object::SymbolRef sym = S.first;
            auto name = sym.getName();
            auto type = sym.getType();
            if(name && type)
                InitializeDebugData(name.get(),type.get(),S.second);
        }
    }
};

enum {
    CF_DumpDisassembly  = (1 << 0),
    CF_DumpModule       = (1 << 1),
    CF_SkipOpts         = (1 << 2),
};

Any compile(Label *fn, uint64_t flags) {
    GenerateCtx ctx;
    auto result = ctx.generate(fn);

    auto module = result.first;
    auto func = result.second;

    LLVMExecutionEngineRef ee = nullptr;
    char *errormsg = nullptr;

    LLVMMCJITCompilerOptions opts;
    LLVMInitializeMCJITCompilerOptions(&opts, sizeof(opts));
    opts.OptLevel = 0;

    if (LLVMCreateMCJITCompilerForModule(&ee, module, &opts,
        sizeof(opts), &errormsg)) {
        location_error(String::from_cstr(errormsg));
    }
    bangra::ee = ee;

    llvm::ExecutionEngine *pEE = reinterpret_cast<llvm::ExecutionEngine*>(ee);
    auto listener = new DisassemblyListener(pEE);
    pEE->RegisterJITEventListener(listener);

#if BANGRA_OPTIMIZE_ASSEMBLY
    if (!(flags & CF_SkipOpts)) {
        build_and_run_opt_passes(module);
    }
#endif
    if (flags & CF_DumpModule) {
        LLVMDumpModule(module);
    }

    void *pfunc = LLVMGetPointerToGlobal(ee, func);
    if (flags & CF_DumpDisassembly) {
        //auto td = LLVMGetExecutionEngineTargetData(ee);
        auto tm = LLVMGetExecutionEngineTargetMachine(ee);
        auto it = listener->sizes.find(pfunc);
        if (it != listener->sizes.end()) {
            do_disassemble(tm, pfunc, it->second);
        } else {
            std::cout << "no disassembly available\n";
        }
    }

    return Any::from_pointer(
        Pointer(fn->get_function_type()),
        pfunc);
}

//------------------------------------------------------------------------------
// COMMON ERRORS
//------------------------------------------------------------------------------

void invalid_op2_types_error(Type A, Type B) {
    StyledString ss;
    ss.out << "invalid operand types " << A << " and " << B;
    location_error(ss.str());
}

//------------------------------------------------------------------------------
// NORMALIZE
//------------------------------------------------------------------------------

#define B_ARITH_OPS() \
        IARITH_NUW_NSW_OPS(Add, +) \
        IARITH_NUW_NSW_OPS(Sub, -) \
        IARITH_NUW_NSW_OPS(Mul, *) \
         \
        IARITH_OP(SDiv, /, i) \
        IARITH_OP(UDiv, /, u) \
        IARITH_OP(SRem, %, i) \
        IARITH_OP(URem, %, u) \
         \
        IARITH_OP(BAnd, &, u) \
        IARITH_OP(BOr, |, u) \
        IARITH_OP(BXor, ^, u) \
         \
        IARITH_OP(Shl, <<, u) \
        IARITH_OP(LShr, >>, u) \
        IARITH_OP(AShr, >>, i) \
         \
        FARITH_OP(FAdd, +) \
        FARITH_OP(FSub, -) \
        FARITH_OP(FMul, *) \
        FARITH_OP(FDiv, /) \
        FARITH_OPF(FRem, std::fmod)

struct NormalizeCtx {
    StyledStream ss_cout;

    struct LabelInstances {
        std::unordered_map<Type, Label *, Type::Hash> instances;
    };
    std::unordered_map<Label *, LabelInstances> label2instance;

    struct HashLabelNodePair {
        std::size_t operator()(const std::pair<Label *, ILNode *> & s) const {
            std::size_t h1 = std::hash<Label *>{}(s.first);
            std::size_t h2 = std::hash<ILNode *>{}(s.second);
            return HashLen16(h1, h2);
        }
    };

    std::unordered_map<std::pair<Label *, ILNode *>, Label *, HashLabelNodePair> labels2ic;


    struct LabelArgs {
        Label *label;
        std::vector<Any> args;

        bool operator==(const LabelArgs &other) const {
            if (label != other.label) return false;
            if (args.size() != other.args.size()) return false;
            for (size_t i = 0; i < args.size(); ++i) {
                auto &&a = args[i];
                auto &&b = other.args[i];
                if (a != b)
                    return false;
            }
            return true;
        }
    };
    struct HashLabelArgs {
        std::size_t operator()(const LabelArgs& s) const {
            std::size_t h = std::hash<Label *>{}(s.label);
            for (auto &&arg : s.args) {
                h = HashLen16(h, arg.hash());
            }
            return h;
        }
    };

    std::unordered_map<LabelArgs, Label *, HashLabelArgs> label2ia;

    Label *start_entry;

    NormalizeCtx() :
#if BANGRA_DEBUG_CODEGEN
        ss_cout(std::cout),
#else
        ss_cout(nullout),
#endif
        start_entry(nullptr)
    {}

    ILNode *node_from_continuation(Any cont) {
        switch(cont.type.value()) {
        case TYPE_Nothing: return nullptr;
        case TYPE_Label: return cont.label;
        case TYPE_Parameter: return cont.parameter;
        default: {
            StyledString ss;
            ss.out << "don't know how to apply continuation of type " << cont.type;
            location_error(ss.str());
        } break;
        }
        return nullptr;
    }

    // inlining the continuation of a branch label without arguments
    Label *inline_branch_continuation(Label *label, Any cont) {
        //ss_cout << "inline_branch_continuation: " << label << std::endl;

        ILNode *node = node_from_continuation(cont);

        auto it = labels2ic.find({label, node });
        if (it == labels2ic.end()) {
            if (is_basic_block_like(label)) {
                labels2ic.insert({{label, node}, label});
                return label;
            } else {
                assert(label->params.size() == 1);

                MangleMap map;
                std::vector<Parameter *> newparams;
                Parameter *param = label->params[0];
                Parameter *newparam = Parameter::from(param);
                newparam->type = TYPE_Nothing;
                newparams.push_back(newparam);
                map[param] = {cont};
                Label *newlabel = mangle(label, newparams, map);
                labels2ic.insert({{label, node}, newlabel});
                return newlabel;
            }
        } else {
            return it->second;
        }
    }

    void verify_instance_count(Label *label) {
        if (label->num_instances < 32)
            return;
        if (label->name == SYM_Unnamed)
            return;
        SCCBuilder scc(label);
        if (!scc.is_recursive(label))
            return;
        set_active_anchor(label->anchor);
        StyledString ss;
        ss.out << "instance limit reached while unrolling named recursive function. "
            "Use less constant arguments.";
        location_error(ss.str());
    }

    // inlining the arguments of an untyped scope (including continuation)
    Label *inline_arguments(Label *label, const std::vector<Any> &args) {
#if 0
        ss_cout << "inline-arguments " << label << ":";
        for (size_t i = 0; i < args.size(); ++i) {
            ss_cout << " " << args[i];
        }
        ss_cout << std::endl;
#endif

        struct LabelArgs la;
        la.label = label;
        la.args = args;
        auto it = label2ia.find(la);
        if (it == label2ia.end()) {
            assert(!label->params.empty());

            verify_instance_count(label);

            MangleMap map;
            std::vector<Parameter *> newparams;
            size_t lasti = label->params.size() - 1;
            size_t srci = 0;
            for (size_t i = 0; i < label->params.size(); ++i) {
                Parameter *param = label->params[i];
                if (param->vararg) {
                    assert(i == lasti);
                    size_t ncount = args.size();
                    if (srci < ncount) {
                        ncount -= srci;
                        std::vector<Any> vargs;
                        for (size_t k = 0; k < ncount; ++k) {
                            Any value = args[srci + k];
                            if (value.type == TYPE_Void) {
                                Parameter *newparam = Parameter::from(param);
                                newparam->vararg = false;
                                newparam->type = TYPE_Void;
                                newparam->name = Symbol(SYM_Unnamed);
                                newparams.push_back(newparam);
                                vargs.push_back(newparam);
                            } else {
                                vargs.push_back(value);
                            }
                        }
                        map[param] = vargs;
                        srci = ncount;
                    } else {
                        map[param] = {};
                    }
                } else if (srci < args.size()) {
                    Any value = args[srci];
                    if (value.type == TYPE_Void) {
                        Parameter *newparam = Parameter::from(param);
                        newparams.push_back(newparam);
                        map[param] = {newparam};
                    } else {
                        if (!srci) {
                            Parameter *newparam = Parameter::from(param);
                            newparam->type = TYPE_Nothing;
                            newparams.push_back(newparam);
                        }
                        map[param] = {value};
                    }
                    srci++;
                } else {
                    map[param] = {none};
                    srci++;
                }
            }
            Label *newlabel = mangle(label, newparams, map);//, Mangle_Verbose);
            label2ia.insert({la, newlabel});
            return newlabel;
        } else {
            return it->second;
        }
    }

    Label *typify(Label *label, const std::vector<Type> &argtypes) {
#if 0
        ss_cout << "typify " << label << ":";
        for (size_t i = 0; i < argtypes.size(); ++i) {
            ss_cout << " " << argtypes[i];
        }
        ss_cout << std::endl;
#endif

        assert(!argtypes.empty());
        assert(!label->params.empty());

        std::vector<Type> hashargs = { TYPE_Nothing };
        for (size_t i = 1; i < argtypes.size(); ++i) {
            hashargs.push_back(argtypes[i]);
        }
        auto labeltype = TypedLabel(hashargs);

        auto it = label2instance.find(label);
        if (it != label2instance.end()) {
            auto it2 = it->second.instances.find(labeltype);
            if (it2 != it->second.instances.end()) {
                return it2->second;
            }
        }

        bool needs_typing = false;
        {
            // check if function needs typing
            size_t srci = 1;
            for (size_t i = 1; i < label->params.size(); ++i) {
                Parameter *param = label->params[i];
                if (param->vararg) {
                    // vararg parameters must be expanded
                    assert(!param->is_typed());
                    needs_typing = true;
                    break;
                } else if (srci < argtypes.size()) {
                    Type argtype = argtypes[srci];
                    if (param->type != argtype) {
                        needs_typing = true;
                        break;
                    } else {
                        srci++;
                    }
                } else {
                    needs_typing = true;
                    break;
                }
            }
        }

        Label *newlabel = nullptr;
        if (needs_typing) {
            verify_instance_count(label);

            MangleMap map;
            std::vector<Parameter *> newparams;
            size_t lasti = label->params.size() - 1;
            size_t srci = 0;
            for (size_t i = 0; i < label->params.size(); ++i) {
                Parameter *param = label->params[i];
                if (srci > 0) {
                    if (param->is_typed()) {
                        StyledString ss;
                        ss.out << "parameter is already typed as " << param->type;
                        location_error(ss.str());
                    }
                }
                if (param->vararg) {
                    assert(i == lasti);
                    size_t ncount = argtypes.size();
                    if (srci < ncount) {
                        ncount -= srci;
                        std::vector<Any> vargs;
                        for (size_t k = 0; k < ncount; ++k) {
                            Parameter *newparam = Parameter::from(param);
                            newparam->type = argtypes[srci + k];
                            newparam->vararg = false;
                            newparam->name = Symbol(SYM_Unnamed);
                            newparams.push_back(newparam);
                            vargs.push_back(newparam);
                        }
                        map[param] = vargs;
                        srci = ncount;
                    } else {
                        map[param] = {};
                    }
                } else if (srci < argtypes.size()) {
                    Type argtype = argtypes[srci];
                    Parameter *newparam = Parameter::from(param);
                    if (srci == 0) {
                        // don't touch type of continuation
                    } else {
                        newparam->type = argtype;
                    }
                    newparams.push_back(newparam);
                    map[param] = {newparam};
                    srci++;
                } else {
                    map[param] = {none};
                    srci++;
                }
            }
            newlabel = mangle(label, newparams, map);
        } else {
            newlabel = label;
        }
        if (it == label2instance.end()) {
            it = label2instance.insert({label, LabelInstances()}).first;
        }
        it->second.instances.insert({labeltype, newlabel});

#if 0
        StyledStream ss(std::cout);
        ss << "before:" << std::endl;
        stream_label(ss, label, StreamLabelFormat::debug_scope());
        ss << "after:" << std::endl;
        stream_label(ss, newlabel, StreamLabelFormat::debug_scope());
        ss << std::endl;
#endif

        return newlabel;
    }

    Any type_continuation(Any dest, const std::vector<Type> &argtypes) {
        //ss_cout << "type_continuation: " << dest << std::endl;

        switch (dest.type.value()) {
        case TYPE_Parameter: {
            Parameter *param = dest.parameter;
            switch(param->type.value()) {
            case TYPE_Nothing: {
                location_error(String::from("attempting to call none continuation"));
            } break;
            case TYPE_Void: {
                param->type = TypedLabel(argtypes);
            } break;
            default: {
                param->type = TypeSet({param->type, TypedLabel(argtypes)});
            } break;
            }
        } break;
        case TYPE_Label: {
            dest = typify(dest.label, argtypes);
        } break;
        default: {
            apply_type_error(dest);
        } break;
        }
        return dest;
    }

    static void verify_integer_ops(Any a, Any b) {
        verify_integer(a.indirect_type());
        verify(a.indirect_type(), b.indirect_type());
    }

    static void verify_real_ops(Any a, Any b) {
        verify_real(a.indirect_type());
        verify(a.indirect_type(), b.indirect_type());
    }

    static bool is_const(Any a) {
        return (a.type != TYPE_Parameter);
    }

    void copy_body(Label *dest, Label *source) {
        dest->unlink_backrefs();
        dest->body = source->body;
        dest->link_backrefs();
    }

    static bool has_args(Label *l) {
        return l->params.size() > 1;
    }

    static bool is_jumping(Label *l) {
        auto &&args = l->body.args;
        assert(!args.empty());
        return args[0].type == TYPE_Nothing;
    }

    static bool is_continuing_to_label(Label *l) {
        auto &&args = l->body.args;
        assert(!args.empty());
        return args[0].type == TYPE_Label;
    }

    static bool is_calling_label(Label *l) {
        auto &&enter = l->body.enter;
        return enter.type == TYPE_Label;
    }

    static bool is_calling_continuation(Label *l) {
        auto &&enter = l->body.enter;
        return (enter.type == TYPE_Parameter) && (enter.parameter->index == 0);
    }

    static bool is_calling_builtin(Label *l) {
        auto &&enter = l->body.enter;
        return enter.type == TYPE_Builtin;
    }

    static bool is_calling_function(Label *l) {
        auto &&enter = l->body.enter;
        return is_function_pointer(enter.type);
    }

    static bool is_calling_pure_function(Label *l) {
        auto &&enter = l->body.enter;
        return is_pure_function_pointer(enter.type);
    }

    static bool is_return_param_typed(Label *l) {
        auto &&params = l->params;
        assert(!params.empty());
        return params[0]->is_typed();
    }

    static bool all_params_typed(Label *l) {
        auto &&params = l->params;
        for (size_t i = 1; i < params.size(); ++i) {
            if (!params[i]->is_typed())
                return false;
        }
        return true;
    }

    static bool all_args_typed(Label *l) {
        auto &&args = l->body.args;
        for (size_t i = 1; i < args.size(); ++i) {
            if ((args[i].type == TYPE_Parameter)
                && (!args[i].parameter->is_typed()))
                return false;
        }
        return true;
    }

    static bool all_args_constant(Label *l) {
        auto &&args = l->body.args;
        for (size_t i = 1; i < args.size(); ++i) {
            if (!is_const(args[i]))
                return false;
        }
        return true;
    }

    static bool has_constant_args(Label *l) {
        auto &&args = l->body.args;
        for (size_t i = 1; i < args.size(); ++i) {
            if (is_const(args[i]))
                return true;
        }
        return false;
    }

    static bool is_called_by(Label *callee, Label *caller) {
        auto &&enter = caller->body.enter;
        return (enter.type == TYPE_Label) && (enter.label == callee);
    }

    static bool is_called_by(Parameter *callee, Label *caller) {
        auto &&enter = caller->body.enter;
        return (enter.type == TYPE_Parameter) && (enter.parameter == callee);
    }

    static bool is_continuing_from(Label *callee, Label *caller) {
        auto &&args = caller->body.args;
        assert(!args.empty());
        return (args[0].type == TYPE_Label) && (args[0].label == callee);
    }

    static bool is_continuing_from(Parameter *callee, Label *caller) {
        auto &&args = caller->body.args;
        assert(!args.empty());
        return (args[0].type == TYPE_Parameter) && (args[0].parameter == callee);
    }

    void verify_function_argument_signature(FunctionInfo &fi, Label *l) {
        auto &&args = l->body.args;
        verify_function_argument_count(fi, args.size() - 1);

        size_t fargcount = fi.argument_types.size();
        for (size_t i = 1; i < args.size(); ++i) {
            Any &arg = args[i];
            size_t k = i - 1;
            Type argT = arg.indirect_type();
            if (k < fargcount) {
                Type ft = fi.argument_types[k];
                if (ft != argT) {
                    StyledString ss;
                    ss.out << "argument of type " << ft << " expected, got " << argT;
                    location_error(ss.str());
                }
            }
        }
    }

    void argtypes_from_function_call(Label *l, std::vector<Type> &retargtypes) {

        auto &&enter = l->body.enter;
        //auto &&args = l->body.args;

        auto &&pi = pointers.get(enter.type);
        auto &&fi = functions.get(pi.element_type);

        verify_function_argument_signature(fi, l);

        retargtypes = { TYPE_Nothing };
        if (fi.return_type != TYPE_Void) {
            if (tuples.is(fi.return_type)) {
                auto &&ti = tuples.get(fi.return_type);
                for (size_t i = 0; i < ti.types.size(); ++i) {
                    retargtypes.push_back(ti.types[i]);
                }
            } else {
                retargtypes.push_back(fi.return_type);
            }
        }
    }

    void fold_pure_function_call(Label *l) {
        ss_cout << "folding pure function call in " << l << std::endl;

        auto &&enter = l->body.enter;
        auto &&args = l->body.args;

        auto &&pi = pointers.get(enter.type);
        auto &&fi = functions.get(pi.element_type);

        verify_function_argument_signature(fi, l);

        assert(!args.empty());
        Any result = none;

        if (fi.flags & FF_Variadic) {
            // convert C types
            size_t argcount = args.size() - 1;
            std::vector<Any> cargs;
            cargs.reserve(argcount);
            for (size_t i = 0; i < argcount; ++i) {
                Any &srcarg = args[i + 1];
                if (i >= fi.argument_types.size()) {
                    if (srcarg.type == TYPE_F32) {
                        cargs.push_back(Any((double)srcarg.f32));
                        continue;
                    }
                }
                cargs.push_back(srcarg);
            }
            result = run_ffi_function(enter, &cargs[0], cargs.size());
        } else {
            result = run_ffi_function(enter, &args[1], args.size() - 1);
        }

        l->unlink_backrefs();
        enter = args[0];
        args = { none };
        if (fi.return_type != TYPE_Void) {
            if (tuples.is(fi.return_type)) {
                // unpack
                auto &&ti = tuples.get(fi.return_type);
                size_t count = ti.types.size();
                for (size_t i = 0; i < count; ++i) {
                    args.push_back(ti.unpack(result.pointer, i));
                }
            } else {
                args.push_back(result);
            }
        }
        l->link_backrefs();
    }

    bool fold_constant_label_arguments(Label *l) {
        if (!has_constant_args(l)) {
            return false;
        }

        ss_cout << "folding constant arguments in " << l << std::endl;

        auto &&enter = l->body.enter;
        assert(enter.type == TYPE_Label);

        // inline constant arguments
        std::vector<Any> callargs;
        Any anyval = none;
        anyval.type = TYPE_Void;
        std::vector<Any> keys;
        auto &&args = l->body.args;
        callargs.push_back(args[0]);
        keys.push_back(anyval);
        for (size_t i = 1; i < args.size(); ++i) {
            auto &&arg = args[i];
            if (is_const(arg)) {
                keys.push_back(arg);
            } else {
                keys.push_back(anyval);
                callargs.push_back(arg);
            }
        }

        Label *newl = inline_arguments(enter.label, keys);
        l->unlink_backrefs();
        enter = newl;
        args = callargs;
        l->link_backrefs();

        return true;
    }

    void type_label_call(Label *l) {
        ss_cout << "typing label in " << l << std::endl;

        auto &&enter = l->body.enter;
        assert(enter.type == TYPE_Label);
        auto &&args = l->body.args;
        std::vector<Type> argtypes = {};
        for (auto &&arg : args) {
            argtypes.push_back(arg.indirect_type());
        }
        Label *newenter = typify(enter.label, argtypes);
        l->unlink_backrefs();
        enter = newenter;
        l->link_backrefs();
    }

    // returns true if the builtin folds regardless of whether the arguments are
    // constant
    bool builtin_always_folds(Builtin builtin) {
        switch(builtin.value()) {
        case FN_TypeOf:
        case FN_IsConstant:
        case FN_VaCountOf:
        case FN_VaAt:
            return true;
        default: return false;
        }
    }

    bool builtin_never_folds(Builtin builtin) {
        switch(builtin.value()) {
        case FN_Unconst:
            return true;
        default: return false;
        }
    }

#define CHECKARGS(MINARGS, MAXARGS) \
    checkargs<MINARGS, MAXARGS>(args.size())

#define RETARGTYPES(...) \
    retargtypes = { TYPE_Nothing, __VA_ARGS__ }

    void argtypes_from_builtin_call(Label *l, std::vector<Type> &retargtypes) {
        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        assert(enter.type == TYPE_Builtin);
        switch(enter.builtin.value()) {
        case FN_Unconst: {
            CHECKARGS(1, 1);
            RETARGTYPES(args[1].indirect_type());
        } break;
        case FN_Bitcast: {
            CHECKARGS(2, 2);
            // todo: verify source and dest type are non-aggregate
            // also, both must be of same category
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            RETARGTYPES(DestT);
        } break;
        case FN_IntToPtr: {
            CHECKARGS(2, 2);
            verify_integer(args[1].indirect_type());
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_category<CAT_Pointer>(storage_type(DestT));
            RETARGTYPES(DestT);
        } break;
        case FN_PtrToInt: {
            CHECKARGS(2, 2);
            verify_category<CAT_Pointer>(
                storage_type(args[1].indirect_type()));
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            RETARGTYPES(DestT);
        } break;
        case FN_Trunc: {
            CHECKARGS(2, 2);
            Type T = args[1].indirect_type();
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            RETARGTYPES(DestT);
        } break;
        case FN_FPTrunc: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            if ((T == TYPE_F64) && (DestT == TYPE_F32)) {
            } else { invalid_op2_types_error(T, DestT); }
            RETARGTYPES(DestT);
        } break;
        case FN_FPExt: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            if ((T == TYPE_F32) && (DestT == TYPE_F64)) {
            } else { invalid_op2_types_error(T, DestT); }
            RETARGTYPES(DestT);
        } break;
        case FN_FPToUI: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            switch(T.value()) {
            case TYPE_F32:
            case TYPE_F64: break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            RETARGTYPES(DestT);
        } break;
        case FN_FPToSI: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            switch(T.value()) {
            case TYPE_F32:
            case TYPE_F64: break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            RETARGTYPES(DestT);
        } break;
        case FN_UIToFP: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            switch(DestT.value()) {
            case TYPE_F32:
            case TYPE_F64: break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            RETARGTYPES(DestT);
        } break;
        case FN_SIToFP: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            switch(DestT.value()) {
            case TYPE_F32:
            case TYPE_F64: break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            RETARGTYPES(DestT);
        } break;
        case FN_ZExt: {
            CHECKARGS(2, 2);
            Type T = args[1].indirect_type();
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            RETARGTYPES(DestT);
        } break;
        case FN_SExt: {
            CHECKARGS(2, 2);
            Type T = args[1].indirect_type();
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            RETARGTYPES(DestT);
        } break;
        case FN_ExtractValue: {
            CHECKARGS(2, 2);
            size_t idx = cast_number<size_t>(args[2]);
            Type T = storage_type(args[1].indirect_type());
            switch(category_of(T)) {
            case CAT_Array: {
                auto &ai = arrays.get(T);
                RETARGTYPES(ai.type_at_index(idx));
            } break;
            case CAT_Tuple: {
                auto &ti = tuples.get(T);
                RETARGTYPES(ti.type_at_index(idx));
            } break;
            case CAT_Union: {
                auto &ui = unions.get(T);
                RETARGTYPES(ui.type_at_index(idx));
            } break;
            default: {
                StyledString ss;
                ss.out << "can not extract value from type " << T;
                location_error(ss.str());
            } break;
            }
        } break;
        case FN_GetElementPtr: {
            CHECKARGS(2, -1);
            Type T = storage_type(args[1].indirect_type());
            verify_category<CAT_Pointer>(T);
            auto &pi = pointers.get(T);
            T = pi.element_type;
            verify_integer(args[2].indirect_type());
            for (size_t i = 3; i < args.size(); ++i) {
                T = storage_type(T);
                auto &&arg = args[i];
                switch(category_of(T)) {
                case CAT_Array: {
                    auto &ai = arrays.get(T);
                    T = ai.element_type;
                    verify_integer(arg.indirect_type());
                } break;
                case CAT_Tuple: {
                    size_t idx = cast_number<size_t>(arg);
                    auto &ti = tuples.get(T);
                    T = ti.type_at_index(idx);
                } break;
                default: {
                    StyledString ss;
                    ss.out << "can not get element pointer from type " << T;
                    location_error(ss.str());
                } break;
                }
            }
            T = Pointer(T);
            RETARGTYPES(T);
        } break;
        case FN_Load: {
            CHECKARGS(1, 1);
            Type T = storage_type(args[1].indirect_type());
            verify_category<CAT_Pointer>(T);
            auto &pi = pointers.get(T);
            RETARGTYPES(pi.element_type);
        } break;
        case OP_ICmpEQ:
        case OP_ICmpNE:
        case OP_ICmpUGT:
        case OP_ICmpUGE:
        case OP_ICmpULT:
        case OP_ICmpULE:
        case OP_ICmpSGT:
        case OP_ICmpSGE:
        case OP_ICmpSLT:
        case OP_ICmpSLE: {
            CHECKARGS(2, 2);
            verify_integer_ops(args[1], args[2]);
            RETARGTYPES(TYPE_Bool);
        } break;
#define IARITH_NUW_NSW_OPS(NAME, OP) \
    case OP_ ## NAME: \
    case OP_ ## NAME ## NUW: \
    case OP_ ## NAME ## NSW: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1], args[2]); \
        RETARGTYPES(args[1].indirect_type()); \
    } break;
#define IARITH_OP(NAME, OP, PFX) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1], args[2]); \
        RETARGTYPES(args[1].indirect_type()); \
    } break;
#define FARITH_OP(NAME, OP) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_real_ops(args[1], args[2]); \
        RETARGTYPES(args[1].indirect_type()); \
    } break;
#define FARITH_OPF FARITH_OP

        B_ARITH_OPS()

#undef IARITH_NUW_NSW_OPS
#undef IARITH_OP
#undef FARITH_OP
#undef FARITH_OPF
        default: {
            StyledString ss;
            ss.out << "can not type builtin " << enter.builtin;
            location_error(ss.str());
        } break;
        }
    }

#define RETARGS(...) \
    l->unlink_backrefs(); \
    enter = args[0]; \
    args = { none, __VA_ARGS__ }; \
    l->link_backrefs();

    void print_traceback() {
        StyledStream ss(std::cerr);
        for (size_t i = 0; i < todo.size(); ++i) {
            Label *l = todo[i];
            ss << l->body.anchor << " in " << l << std::endl;
            l->body.anchor->stream_source_line(ss);
        }
    }

    bool fold_builtin_call(Label *l) {
        ss_cout << "folding builtin call in " << l << std::endl;

        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        assert(enter.type == TYPE_Builtin);
        switch(enter.builtin.value()) {
        case FN_IsConstant: {
            CHECKARGS(1, 1);
            RETARGS(is_const(args[1]));
        } break;
        case FN_VaCountOf: {
            RETARGS((int)(args.size()-1));
        } break;
        case FN_VaAt: {
            size_t idx = cast_number<size_t>(args[1]);
            std::vector<Any> result = { none };
            for (size_t i = (idx + 2); i < args.size(); ++i) {
                result.push_back(args[i]);
            }
            l->unlink_backrefs();
            enter = args[0];
            args = result;
            l->link_backrefs();
        } break;
        case FN_Branch: {
            CHECKARGS(3, 3);
            args[1].verify<TYPE_Bool>();
            // either branch label is typed and binds no parameters,
            // so we can directly inline it
            Label *newl = nullptr;
            if (args[1].i1) {
                newl = inline_branch_continuation(args[2], args[0]);
            } else {
                newl = inline_branch_continuation(args[3], args[0]);
            }
            copy_body(l, newl);
        } break;
        case FN_Bitcast: {
            CHECKARGS(2, 2);
            // todo: verify source and dest type are non-aggregate
            // also, both must be of same category
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            Any result = args[1];
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_IntToPtr: {
            CHECKARGS(2, 2);
            verify_integer(args[1].type);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_category<CAT_Pointer>(storage_type(DestT));
            Any result = args[1];
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_PtrToInt: {
            CHECKARGS(2, 2);
            verify_category<CAT_Pointer>(storage_type(args[1].type));
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            Any result = args[1];
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_Trunc: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            Any result = args[1];
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_FPTrunc: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            if ((T == TYPE_F64) && (DestT == TYPE_F32)) {
                RETARGS((float)args[1].f64);
            } else { invalid_op2_types_error(T, DestT); }
        } break;
        case FN_FPExt: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            if ((T == TYPE_F32) && (DestT == TYPE_F64)) {
                RETARGS((double)args[1].f32);
            } else { invalid_op2_types_error(T, DestT); }
        } break;
        case FN_FPToUI: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            uint64_t val = 0;
            switch(T.value()) {
            case TYPE_F32: val = (uint64_t)args[1].f32; break;
            case TYPE_F64: val = (uint64_t)args[1].f64; break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            Any result = val;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_FPToSI: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_real(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            int64_t val = 0;
            switch(T.value()) {
            case TYPE_F32: val = (int64_t)args[1].f32; break;
            case TYPE_F64: val = (int64_t)args[1].f64; break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            Any result = val;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_UIToFP: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            uint64_t src = cast_number<uint64_t>(args[1]);
            Any result = none;
            switch(DestT.value()) {
            case TYPE_F32: result = (float)src; break;
            case TYPE_F64: result = (double)src; break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            RETARGS(result);
        } break;
        case FN_SIToFP: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_real(DestT);
            int64_t src = cast_number<int64_t>(args[1]);
            Any result = none;
            switch(DestT.value()) {
            case TYPE_F32: result = (float)src; break;
            case TYPE_F64: result = (double)src; break;
            default: invalid_op2_types_error(T, DestT); break;
            }
            RETARGS(result);
        } break;
        case FN_ZExt: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            Any result = args[1];
            result.type = DestT;
            int oldbitnum = integer_type_bit_size(T);
            int newbitnum = integer_type_bit_size(DestT);
            for (int i = oldbitnum; i < newbitnum; ++i) {
                result.u64 &= ~(1ull << i);
            }
            RETARGS(result);
        } break;
        case FN_SExt: {
            CHECKARGS(2, 2);
            Type T = args[1].type;
            verify_integer(T);
            args[2].verify<TYPE_Type>();
            Type DestT = args[2].typeref;
            verify_integer(DestT);
            Any result = args[1];
            result.type = DestT;
            int oldbitnum = integer_type_bit_size(T);
            int newbitnum = integer_type_bit_size(DestT);
            uint64_t bit = (result.u64 >> (oldbitnum - 1)) & 1ull;
            for (int i = oldbitnum; i < newbitnum; ++i) {
                result.u64 &= ~(1ull << i);
                result.u64 |= bit << i;
            }
            RETARGS(result);
        } break;
        case FN_TypeOf: {
            CHECKARGS(1, 1);
            RETARGS(args[1].indirect_type());
        } break;
        case FN_ExtractValue: {
            CHECKARGS(2, 2);
            size_t idx = cast_number<size_t>(args[2]);
            Type T = storage_type(args[1].type);
            switch(category_of(T)) {
            case CAT_Array: {
                auto &ai = arrays.get(T);
                RETARGS(ai.unpack(args[1].pointer, idx));
            } break;
            case CAT_Tuple: {
                auto &ti = tuples.get(T);
                RETARGS(ti.unpack(args[1].pointer, idx));
            } break;
            case CAT_Union: {
                auto &ui = unions.get(T);
                RETARGS(ui.unpack(args[1].pointer, idx));
            } break;
            default: {
                StyledString ss;
                ss.out << "can not extract value from type " << T;
                location_error(ss.str());
            } break;
            }
        } break;
        case FN_Load: {
            CHECKARGS(1, 1);
            Type T = storage_type(args[1].type);
            verify_category<CAT_Pointer>(T);
            auto &pi = pointers.get(T);
            RETARGS(pi.unpack(args[1].pointer));
        } break;
        case FN_GetElementPtr: {
            CHECKARGS(2, -1);
            Type T = storage_type(args[1].type);
            verify_category<CAT_Pointer>(T);
            auto &pi = pointers.get(T);
            T = pi.element_type;
            void *ptr = args[1].pointer;
            size_t idx = cast_number<size_t>(args[2]);
            ptr = pi.getelementptr(ptr, idx);

            for (size_t i = 3; i < args.size(); ++i) {
                T = storage_type(T);
                auto &&arg = args[i];
                switch(category_of(T)) {
                case CAT_Array: {
                    auto &ai = arrays.get(T);
                    T = ai.element_type;
                    size_t idx = cast_number<size_t>(arg);
                    ptr = ai.getelementptr(ptr, idx);
                } break;
                case CAT_Tuple: {
                    size_t idx = cast_number<size_t>(arg);
                    auto &ti = tuples.get(T);
                    T = ti.type_at_index(idx);
                    ptr = ti.getelementptr(ptr, idx);
                } break;
                default: {
                    StyledString ss;
                    ss.out << "can not get element pointer from type " << T;
                    location_error(ss.str());
                } break;
                }
            }
            T = Pointer(T);
            RETARGS(Any::from_pointer(T, ptr));
        } break;
        case FN_AnyExtract: {
            CHECKARGS(1, 1);
            args[1].verify<TYPE_Any>();
            Any arg = *args[1].ref;
            RETARGS(arg);
        } break;
        case FN_AnyWrap: {
            CHECKARGS(1, 1);
            Any arg = args[1].toref();
            arg.type = TYPE_Any;
            RETARGS(arg);
        } break;
        case FN_Purify: {
            CHECKARGS(1, 1);
            Any arg = args[1];
            verify_function_pointer(arg.type);
            auto &pi = pointers.get(arg.type);
            auto &fi = functions.get(pi.element_type);
            if (fi.flags & FF_Pure) {
                RETARGS(args[1]);
            } else {
                arg.type = Pointer(Function(
                    fi.return_type, fi.argument_types, fi.flags | FF_Pure));
                RETARGS(arg);
            }
        } break;
        case FN_Compile: {
            CHECKARGS(1, -1);
            Label *srcl = args[1];
            uint64_t flags = 0;
            for (size_t i = 2; i < args.size(); ++i) {
                args[i].verify<TYPE_Symbol>();
                Symbol sym = args[i].symbol;
                uint64_t flag = 0;
                switch(sym.value()) {
                case SYM_DumpDisassembly: flag = CF_DumpDisassembly; break;
                case SYM_DumpModule: flag = CF_DumpModule; break;
                case SYM_SkipOpts: flag = CF_SkipOpts; break;
                default: {
                    StyledString ss;
                    ss.out << "illegal option: " << sym;
                    location_error(ss.str());
                } break;
                }
                flags |= flag;
            }
            RETARGS(compile(srcl, flags));
        } break;
        case FN_Typify: {
            CHECKARGS(1, -1);
            Label *srcl = args[1];
            std::vector<Type> types = { TYPE_Void };
            for (size_t i = 2; i < args.size(); ++i) {
                Any val = args[i];
                val.verify<TYPE_Type>();
                types.push_back(val.typeref);
            }
            srcl = typify(srcl, types);
            StyledStream ss(std::cerr);
            push_label(l);
            push_label(srcl);
            RETARGS(srcl);
            return false;
        } break;
        case FN_CompilerError: {
            CHECKARGS(1, 1);
            location_error(args[1]);
            RETARGS();
        } break;
        case FN_CompilerMessage: {
            CHECKARGS(1, 1);
            args[1].verify<TYPE_String>();
            StyledString ss;
            ss.out << l->body.anchor << " message: " << args[1].string->data << std::endl;
            std::cout << ss.str()->data;
            RETARGS();
        } break;
        case FN_Dump: {
            CHECKARGS(1, -1);
            StyledStream ss(std::cerr);
            ss << l->body.anchor << " dump: ";
            for (size_t i = 1; i < args.size(); ++i) {
                stream_expr(ss, args[i], StreamExprFormat());
            }
            l->unlink_backrefs();
            enter = args[0];
            args[0] = none;
            l->link_backrefs();
        } break;
        case OP_ICmpEQ:
        case OP_ICmpNE:
        case OP_ICmpUGT:
        case OP_ICmpUGE:
        case OP_ICmpULT:
        case OP_ICmpULE:
        case OP_ICmpSGT:
        case OP_ICmpSGE:
        case OP_ICmpSLT:
        case OP_ICmpSLE: {
            CHECKARGS(2, 2);
            verify_integer_ops(args[1], args[2]);
#define B_INT_OP2(OP, N) \
    switch(args[1].type.value()) { \
    case TYPE_Bool: result = (args[1].i1 OP args[2].i1); break; \
    case TYPE_I8: \
    case TYPE_U8: result = (args[1].N ## 8 OP args[2].N ## 8); break; \
    case TYPE_I16: \
    case TYPE_U16: result = (args[1].N ## 16 OP args[2].N ## 16); break; \
    case TYPE_I32: \
    case TYPE_U32: result = (args[1].N ## 32 OP args[2].N ## 32); break; \
    case TYPE_I64: \
    case TYPE_U64: result = (args[1].N ## 64 OP args[2].N ## 64); break; \
    default: assert(false); break; \
    }
#define B_FLOAT_OP2(OP) \
    switch(args[1].type.value()) { \
    case TYPE_F32: result = (args[1].f32 OP args[2].f32); break; \
    case TYPE_F64: result = (args[1].f64 OP args[2].f64); break; \
    default: assert(false); break; \
    }
#define B_FLOAT_OPF2(OP) \
    switch(args[1].type.value()) { \
    case TYPE_F32: result = OP(args[1].f32, args[2].f32); break; \
    case TYPE_F64: result = OP(args[1].f64, args[2].f64); break; \
    default: assert(false); break; \
    }
            bool result;
            switch(enter.builtin.value()) {
            case OP_ICmpEQ: B_INT_OP2(==, u); break;
            case OP_ICmpNE: B_INT_OP2(!=, u); break;
            case OP_ICmpUGT: B_INT_OP2(>, u); break;
            case OP_ICmpUGE: B_INT_OP2(>=, u); break;
            case OP_ICmpULT: B_INT_OP2(<, u); break;
            case OP_ICmpULE: B_INT_OP2(<=, u); break;
            case OP_ICmpSGT: B_INT_OP2(>, i); break;
            case OP_ICmpSGE: B_INT_OP2(>=, i); break;
            case OP_ICmpSLT: B_INT_OP2(<, i); break;
            case OP_ICmpSLE: B_INT_OP2(<=, i); break;
            default: assert(false); break;
            }
            RETARGS(result);
        } break;
#define IARITH_NUW_NSW_OPS(NAME, OP) \
    case OP_ ## NAME: \
    case OP_ ## NAME ## NUW: \
    case OP_ ## NAME ## NSW: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1], args[2]); \
        Any result = none; \
        switch(enter.builtin.value()) { \
        case OP_ ## NAME: B_INT_OP2(OP, u); break; \
        case OP_ ## NAME ## NUW: B_INT_OP2(OP, u); break; \
        case OP_ ## NAME ## NSW: B_INT_OP2(OP, i); break; \
        default: assert(false); break; \
        } \
        result.type = args[1].type; \
        RETARGS(result); \
    } break;
#define IARITH_OP(NAME, OP, PFX) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1], args[2]); \
        Any result = none; \
        B_INT_OP2(OP, PFX); \
        result.type = args[1].type; \
        RETARGS(result); \
    } break;
#define FARITH_OP(NAME, OP) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_real_ops(args[1], args[2]); \
        Any result = none; \
        B_FLOAT_OP2(OP); \
        RETARGS(result); \
    } break;
#define FARITH_OPF(NAME, OP) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_real_ops(args[1], args[2]); \
        Any result = none; \
        B_FLOAT_OPF2(OP); \
        RETARGS(result); \
    } break;
        B_ARITH_OPS()

        default: {
            StyledString ss;
            ss.out << "can not inline builtin " << enter.builtin;
            location_error(ss.str());
        } break;
        }
        return true;
    }

    void inline_branch_continuations(Label *l) {
        ss_cout << "inlining branch continuations in " << l << std::endl;

        auto &&args = l->body.args;
        CHECKARGS(3, 3);
        args[1].verify_indirect<TYPE_Bool>();
        Label *then_br = inline_branch_continuation(args[2], args[0]);
        Label *else_br = inline_branch_continuation(args[3], args[0]);
        l->unlink_backrefs();
        args[0] = none;
        args[2] = then_br;
        args[3] = else_br;
        l->link_backrefs();
    }

#undef IARITH_NUW_NSW_OPS
#undef IARITH_OP
#undef FARITH_OP
#undef FARITH_OPF
#undef B_INT_OP2
#undef RETARGTYPES
#undef CHECKARGS
#undef RETARGS

    /*
    static bool forwards_continuation_immediately(Label *l) {
        assert(!l->params.empty());
        return is_continuing_from(l->params[0], l);
    }
    */

    static bool returns_immediately(Label *l) {
        assert(!l->params.empty());
        return is_called_by(l->params[0], l);
    }

    static bool returns_higher_order_type(Label *l) {
        assert(!l->params.empty());
        Type T = l->params[0]->type;
        if (typesets.is(T)) return true;
        if (!typed_labels.is(T)) return false;
        auto &&tli = typed_labels.get(T);
        for (size_t i = 1; i < tli.types.size(); ++i) {
            switch (tli.types[i].value()) {
            case TYPE_Label:
            case TYPE_Type:
                return true;
            default: break;
            }
        }
        return false;
    }

    void clear_continuation_arg(Label *l) {
        auto &&args = l->body.args;
        l->unlink_backrefs();
        args[0] = none;
        l->link_backrefs();
    }

    // clear continuation argument and clear it for labels that use it
    void delete_continuation(Label *owner) {
        ss_cout << "deleting continuation of " << owner << std::endl;

        assert(!owner->params.empty());
        Parameter *param = owner->params[0];
        param->type = TYPE_Nothing;

        assert(!is_called_by(param, owner));
        if (is_continuing_from(param, owner)) {
            clear_continuation_arg(owner);
        }

        {
            // remove argument from users
            auto users = param->users; // make copy
            for (auto kv = users.begin(); kv != users.end(); ++kv) {
                Label *user = kv->first;
                assert(!is_called_by(param, user));
                if (is_continuing_from(param, user)) {
                    clear_continuation_arg(user);
                }
            }
        }

        {
            // calls to this label also need to be adjusted
            auto users = owner->users; // make copy
            for (auto kv = users.begin(); kv != users.end(); ++kv) {
                Label *user = kv->first;
                if (is_calling_label(user) && (user->get_label_enter() == owner)) {
                    if (is_continuing_to_label(user)) {
                        set_active_anchor(user->body.anchor);
                        location_error(String::from("call exits scope prematurely"));
                    }
                    //assert(!is_continuing_to_label(user));
                    clear_continuation_arg(user);
                }
            }
        }

    }

    void inline_label_call(Label *l) {
        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        assert(enter.type == TYPE_Label);
        Label *enter_label = enter.label;
        assert(!args.empty());
        ss_cout << "inlining label call to " << enter_label << " in " << l << std::endl;

        Any voidarg = none;
        voidarg.type = TYPE_Void;

        std::vector<Any> newargs = { args[0] };
        for (size_t i = 1; i < enter_label->params.size(); ++i) {
            newargs.push_back(voidarg);
        }
        Label *newl = inline_arguments(enter_label, newargs);
        l->unlink_backrefs();
        args[0] = none;
        enter = newl;
        l->link_backrefs();
    }

    void type_continuation_from_label_return_type(Label *l) {
        ss_cout << "typing continuation from label return type in " << l << std::endl;
        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        assert(enter.type == TYPE_Label);
        Label *enter_label = enter.label;
        assert(!args.empty());
        assert(!enter_label->params.empty());
        Parameter *cont_param = enter_label->params[0];
        Type cont_type = cont_param->type;

        if (typed_labels.is(cont_type)) {
            auto &&tli = typed_labels.get(cont_type);
            Any newarg = type_continuation(args[0], tli.types);
            l->unlink_backrefs();
            args[0] = newarg;
            l->link_backrefs();
        } else {



            ss_cout << "unexpected return type: " << cont_type << std::endl;
            assert(false && "todo: unexpected return type");
        }
    }

    void type_continuation_from_builtin_call(Label *l) {
        ss_cout << "typing continuation from builtin call in " << l << std::endl;
        std::vector<Type> argtypes;
        argtypes_from_builtin_call(l, argtypes);
        auto &&args = l->body.args;
        Any newarg = type_continuation(args[0], argtypes);
        l->unlink_backrefs();
        args[0] = newarg;
        l->link_backrefs();
    }

    void type_continuation_from_function_call(Label *l) {
        ss_cout << "typing continuation from function call in " << l << std::endl;
        std::vector<Type> argtypes;
        argtypes_from_function_call(l, argtypes);
        auto &&args = l->body.args;
        Any newarg = type_continuation(args[0], argtypes);
        l->unlink_backrefs();
        args[0] = newarg;
        l->link_backrefs();
    }

    void type_continuation_call(Label *l) {
        ss_cout << "typing continuation call in " << l << std::endl;
        auto &&args = l->body.args;
        assert(args[0].type == TYPE_Nothing);
        std::vector<Type> argtypes = {};
        for (auto &&arg : args) {
            argtypes.push_back(arg.indirect_type());
        }
        type_continuation(l->body.enter, argtypes);
    }

    void set_const(std::vector<Any> &consts, size_t i, Any arg) {
        if (arg.type == TYPE_Void) {
        } else if (is_const(arg)) {
        } else {
            arg = none;
            arg.type = TYPE_Void;
        }

        if (i == consts.size()) {
            consts.push_back(arg);
        } else {
            if (consts[i].type == TYPE_Void) {
            } else if (consts[i] != arg) {
                consts[i] = none;
                consts[i].type = TYPE_Void;
            }
        }
    }

    std::unordered_set<Label *> done;
    std::vector<Label *> todo;

    bool is_done(Label *l) {
        return done.count(l);
    }

    void set_done(Label *l) {
        done.insert(l);
    }

    void push_label(Label *l) {
        if (done.count(l))
            return;
        todo.push_back(l);
    }

    bool called_in_todo_list(Label *l) {
        for (size_t i = 0; i < todo.size(); ++i) {
            Label *ll = todo[i];
            if (is_calling_label(ll)
                && (ll->get_label_enter() == l))
                return true;
        }
        return false;
    }

    Label *pop_label() {
        Label *l = todo.back();
        todo.pop_back();
        return l;
    }

    Label *peek_label() {
        return todo.back();
    }

    void normalize(Label *entry) {
        try {
        done.clear();
        todo = { entry };

        while (!todo.empty()) {
            Label *l = pop_label();
            ss_cout << "processing " << l << std::endl;

        process_body:
            stream_label(ss_cout, l, StreamLabelFormat::debug_single());
            assert(all_params_typed(l));
            //assert(is_basic_block_like(l) || !is_return_param_typed(l));
            assert(all_args_typed(l));

            set_active_anchor(l->body.anchor);

            if (is_calling_function(l)) {
                if (is_calling_pure_function(l)
                    && all_args_constant(l)) {
                    fold_pure_function_call(l);
                    goto process_body;
                } else {
                    type_continuation_from_function_call(l);
                }
            } else if (is_calling_builtin(l)) {
                if ((all_args_constant(l)
                    && !builtin_never_folds(l->get_builtin_enter()))
                    || builtin_always_folds(l->get_builtin_enter())) {
                    if (fold_builtin_call(l)) {
                        goto process_body;
                    } else {
                        // exception for typify: the new label has to be
                        // normalized before we can continue
                        continue;
                    }
                } else if (l->body.enter.builtin == FN_Branch) {
                    inline_branch_continuations(l);
                    auto &&args = l->body.args;
                    push_label(args[3]);
                    push_label(args[2]);
                } else {
                    type_continuation_from_builtin_call(l);
                }
            } else if (is_calling_label(l)) {
                if (!is_done(l->get_label_enter())) {
                    Label *dest = l->get_label_enter();
                    SCCBuilder scc(dest);
                    if (scc.is_recursive(dest)) {
                        ss_cout << std::endl;
                        scc.stream_group(ss_cout, scc.group(dest));
                        ss_cout << std::endl;
                    }
                }

                fold_constant_label_arguments(l);

                if (!all_params_typed(l->get_label_enter())) {
                    type_label_call(l);
                }

                Label *enter_label = l->get_label_enter();
                if (is_basic_block_like(enter_label)) {
                    if (!has_args(enter_label)) {
                        ss_cout << "folding jump to label in " << l << std::endl;
                        copy_body(l, enter_label);
                        goto process_body;
                    } else {
                        assert(is_jumping(l));
                        push_label(enter_label);
                    }
                } else if (is_return_param_typed(enter_label)) {
                    if (returns_immediately(enter_label)
                        // this one causes problems
                        //|| forwards_continuation_immediately(enter_label)
                        || returns_higher_order_type(enter_label)) {
                        inline_label_call(l);
                        goto process_body;
                    } else {
                        type_continuation_from_label_return_type(l);
                    }
                } else {
                    // returning function with untyped continuation
                    if (is_done(enter_label)) {
                        SCCBuilder scc(enter_label);
                        if (scc.is_recursive(enter_label)) {
                        //if (called_in_todo_list(enter_label)) {
                            // possible recursion - entry label has already been
                            // processed, but not exited yet, so we don't have the
                            // continuation type yet.

                            // as long as we don't have to pass on the result,
                            // that's not a problem though
                            assert(!is_continuing_to_label(l));
                        } else {
                            // apparently we returned from this label, but
                            // its continuation has not been typed,
                            // which means that it's functioning more like
                            // a basic block
                            // cut it
                            assert(!is_return_param_typed(enter_label));
                            delete_continuation(enter_label);
                        }
                    } else {
                        // queue for processing
                        push_label(l); // we need to try again after label has been visited
                        push_label(enter_label);
                        continue;
                    }
                }
            } else if (is_calling_continuation(l)) {
                type_continuation_call(l);
            } else {
                StyledString ss;
                auto &&enter = l->body.enter;
                if (!is_const(enter)) {
                    ss.out << "unable to call variable of type " << enter.indirect_type();
                } else {
                    ss.out << "unable to call value of type " << enter.type;
                }
                location_error(ss.str());
            }

            ss_cout << "done: ";
            stream_label(ss_cout, l, StreamLabelFormat::debug_single());

            set_done(l);

            if (is_continuing_to_label(l)) {
                push_label(l->body.args[0].label);
            }
        }

        } catch (const Exception &exc) {
            print_traceback();
            throw exc;
        }
    }

    Label *lower2cff(Label *entry) {
        Any voidarg = none;
        voidarg.type = TYPE_Void;


        size_t numchanges = 0;
        size_t iterations = 0;
        do {
            numchanges = 0;
            iterations++;
            if (iterations > 256) {
                location_error(String::from(
                    "free variable elimination not terminated after 256 iterations"));
            }

            std::vector<Label *> labels;
            std::unordered_set<Label *> visited;
            entry->build_reachable(labels, visited);

            std::unordered_set<Label *> illegal;
            std::unordered_set<Label *> has_illegals;
            for (auto it = labels.begin(); it != labels.end(); ++it) {
                Label *l = *it;
                if (is_basic_block_like(l)) {
                    continue;
                }
                std::vector<Label *> scope;
                l->build_scope(scope);
                bool found = false;
                for (size_t i = 0; i < scope.size(); ++i) {
                    Label *subl = scope[i];
                    if (!is_basic_block_like(subl)) {
                        illegal.insert(subl);
                        found = true;
                    }
                }
                if (found) {
                    has_illegals.insert(l);
                }
            }

            for (auto it = illegal.begin(); it != illegal.end(); ++it) {
                Label *l = *it;
                // always process deepest illegal labels
                if (has_illegals.count(l))
                    continue;
                ss_cout << "invalid: ";
                stream_label(ss_cout, l, StreamLabelFormat::debug_single());
                auto users_copy = l->users;
                // continuation must be eliminated
                for (auto kv = users_copy.begin(); kv != users_copy.end(); ++kv) {
                    Label *user = kv->first;
                    if (!visited.count(user)) {
                        ss_cout << "warning: unreachable user encountered" << std::endl;
                        continue;
                    }
                    auto &&enter = user->body.enter;
                    auto &&args = user->body.args;
                    for (size_t i = 0; i < args.size(); ++i) {
                        auto &&arg = args[i];
                        if ((arg.type == TYPE_Label) && (arg.label == l)) {
                            assert(false && "unexpected use of label as argument");
                        }
                    }
                    if ((enter.type == TYPE_Label) && (enter.label == l)) {
                        assert(!args.empty());

                        auto &&cont = args[0];
                        if ((cont.type == TYPE_Parameter)
                            && (cont.parameter->label == l)) {
                            ss_cout << "skipping recursive call" << std::endl;
                        } else {
                            std::vector<Any> newargs = { cont };
                            for (size_t i = 1; i < l->params.size(); ++i) {
                                newargs.push_back(voidarg);
                            }
                            Label *newl = inline_arguments(l, newargs);

                            ss_cout << l << "(" << cont << ") -> " << newl << std::endl;

                            user->unlink_backrefs();
                            cont = none;
                            enter = newl;
                            user->link_backrefs();
                            numchanges++;
                        }
                    } else {
                        ss_cout << "warning: invalidated user encountered" << std::endl;
                    }
                }
            }
        } while (numchanges);

        ss_cout << "lowered to CFF in " << iterations << " steps" << std::endl;

        return entry;
    }

    std::unordered_map<Type, ffi_type *, Type::Hash> ffi_types;

    ffi_type *new_type() {
        ffi_type *result = (ffi_type *)malloc(sizeof(ffi_type));
        memset(result, 0, sizeof(ffi_type));
        return result;
    }

    ffi_type *create_ffi_type(Type type) {
        switch(type.value()) {
        case TYPE_Void:
        case TYPE_Nothing: return &ffi_type_void;
        case TYPE_Bool: return &ffi_type_uint8;
        case TYPE_I8: return &ffi_type_sint8;
        case TYPE_I16: return &ffi_type_sint16;
        case TYPE_I32: return &ffi_type_sint32;
        case TYPE_I64: return &ffi_type_sint64;
        case TYPE_U8: return &ffi_type_uint8;
        case TYPE_U16: return &ffi_type_uint16;
        case TYPE_U32: return &ffi_type_uint32;
        case TYPE_U64: return &ffi_type_uint64;
        case TYPE_F32: return &ffi_type_float;
        case TYPE_F64: return &ffi_type_double;
        default: break;
        }

        switch(category_of(type)) {
        case CAT_Pointer: return &ffi_type_pointer;
        case CAT_Typename: {
            auto &&tn = typenames.get(type);
            if (!tn.finalized) {
                StyledString ss;
                ss.out << "FFI: cannot convert opaque type " << type;
                location_error(ss.str());
            }
            return get_ffi_type(tn.storage_type);
        } break;
        case CAT_Array: {
            auto &&ai = arrays.get(type);
            size_t count = ai.count;
            ffi_type *ty = (ffi_type *)malloc(sizeof(ffi_type));
            ty->size = 0;
            ty->alignment = 0;
            ty->type = FFI_TYPE_STRUCT;
            ty->elements = (ffi_type **)malloc(sizeof(ffi_type*) * (count + 1));
            ffi_type *element_type = get_ffi_type(ai.element_type);
            for (size_t i = 0; i < count; ++i) {
                ty->elements[i] = element_type;
            }
            ty->elements[count] = nullptr;
            return ty;
        } break;
        case CAT_Tuple: {
            auto &&ti = tuples.get(type);
            size_t count = ti.types.size();
            ffi_type *ty = (ffi_type *)malloc(sizeof(ffi_type));
            ty->size = 0;
            ty->alignment = 0;
            ty->type = FFI_TYPE_STRUCT;
            ty->elements = (ffi_type **)malloc(sizeof(ffi_type*) * (count + 1));
            for (size_t i = 0; i < count; ++i) {
                ty->elements[i] = get_ffi_type(ti.types[i]);
            }
            ty->elements[count] = nullptr;
            return ty;
        } break;
        case CAT_Union: {
            auto &&ui = unions.get(type);
            size_t count = ui.types.size();
            size_t sz = ui.size;
            size_t al = ui.align;
            ffi_type *ty = (ffi_type *)malloc(sizeof(ffi_type));
            ty->size = 0;
            ty->alignment = 0;
            ty->type = FFI_TYPE_STRUCT;
            // find member with the same alignment
            for (size_t i = 0; i < count; ++i) {
                Type ET = ui.types[i];
                size_t etal = align_of(ET);
                if (etal == al) {
                    size_t remsz = sz - size_of(ET);
                    ffi_type *tvalue = get_ffi_type(ET);
                    if (remsz) {
                        ty->elements = (ffi_type **)malloc(sizeof(ffi_type*) * 3);
                        ty->elements[0] = tvalue;
                        ty->elements[1] = get_ffi_type(Array(TYPE_I8, remsz));
                        ty->elements[2] = nullptr;
                    } else {
                        ty->elements = (ffi_type **)malloc(sizeof(ffi_type*) * 2);
                        ty->elements[0] = tvalue;
                        ty->elements[1] = nullptr;
                    }
                    return ty;
                }
            }
            // should never get here
            assert(false);
        } break;
        default: break;
        };

        StyledString ss;
        ss.out << "FFI: cannot convert argument of type " << type;
        location_error(ss.str());
        return nullptr;
    }

    ffi_type *get_ffi_type(Type type) {
        auto it = ffi_types.find(type);
        if (it == ffi_types.end()) {
            auto result = create_ffi_type(type);
            ffi_types[type] = result;
            return result;
        } else {
            return it->second;
        }
    }

    void *get_ffi_argument(Type type, Any &value, bool create = false) {
        switch(type.value()) {
        case TYPE_Void: return value.content;
        case TYPE_I8: case TYPE_U8: return (void *)&value.u8;
        case TYPE_I16: case TYPE_U16: return (void *)&value.u16;
        case TYPE_I32: case TYPE_U32: return (void *)&value.u32;
        case TYPE_I64: case TYPE_U64: return (void *)&value.u64;
        case TYPE_F32: return (void *)&value.f32;
        case TYPE_F64: return (void *)&value.f64;
        case TYPE_Bool: return (void *)&value.i1;
        default: break;
        }

        switch(category_of(type)) {
        case CAT_Pointer: return (void *)&value.pointer;
        case CAT_Typename: {
            auto &&tn = typenames.get(type);
            if (!tn.finalized) {
                StyledString ss;
                ss.out << "FFI: cannot convert opaque type " << type;
                location_error(ss.str());
            }
            return get_ffi_argument(tn.storage_type, value, create);
        } break;
        case CAT_Array:
        case CAT_Vector:
        case CAT_Tuple:
        case CAT_Union:
            if (create) {
                value.pointer = malloc(size_of(type));
            }
            return value.pointer;
        default: break;
        };

        StyledString ss;
        ss.out << "FFI: cannot convert argument of type " << type;
        location_error(ss.str());
        return nullptr;
    }

    void verify_function_argument_count(const FunctionInfo &fi, size_t argcount) {

        size_t fargcount = fi.argument_types.size();
        if (fi.flags & FF_Variadic) {
            if (argcount < fargcount) {
                StyledString ss;
                ss.out << "FFI: argument count mismatch (need at least "
                    << fargcount << ", got " << argcount << ")";
                location_error(ss.str());
            }
        } else {
            if (argcount != fargcount) {
                StyledString ss;
                ss.out << "FFI: argument count mismatch (need "
                    << fargcount << ", got " << argcount << ")";
                location_error(ss.str());
            }
        }
    }

    Any run_ffi_function(Any enter, Any *args, size_t argcount) {
        auto &&pi = pointers.get(enter.type);
        auto &&fi = functions.get(pi.element_type);

        size_t fargcount = fi.argument_types.size();

        Type rettype = fi.return_type;

        ffi_cif cif;
        ffi_type *argtypes[argcount];
        void *avalues[argcount];
        for (size_t i = 0; i < argcount; ++i) {
            Any &arg = args[i];
            argtypes[i] = get_ffi_type(arg.type);
            avalues[i] = get_ffi_argument(arg.type, arg);
        }
        ffi_status prep_result;
        if (fi.flags & FF_Variadic) {
            prep_result = ffi_prep_cif_var(
                &cif, FFI_DEFAULT_ABI, fargcount, argcount, get_ffi_type(rettype), argtypes);
        } else {
            prep_result = ffi_prep_cif(
                &cif, FFI_DEFAULT_ABI, argcount, get_ffi_type(rettype), argtypes);
        }
        assert(prep_result == FFI_OK);

        Any result = Any::from_pointer(rettype, nullptr);
        ffi_call(&cif, FFI_FN(enter.pointer),
            get_ffi_argument(result.type, result, true), avalues);
        return result;
    }

};

static Label *normalize(Label *entry) {
    NormalizeCtx ctx;
    ctx.start_entry = entry;
    ctx.normalize(entry);
    ctx.lower2cff(entry);
    return entry;
}

//------------------------------------------------------------------------------
// IL TRANSLATOR
//------------------------------------------------------------------------------

// arguments must include continuation
// enter and args must be passed with syntax object removed
static void br(Label *state, Any enter,
    const std::vector<Any> &args, const Anchor *anchor) {
    assert(!args.empty());
    assert(anchor);
    if (!state) {
        set_active_anchor(anchor);
        location_error(String::from("can not define body: continuation already exited."));
        return;
    }
    assert(state->body.enter.type == TYPE_Nothing);
    assert(state->body.args.empty());
    state->body.enter = enter;
    size_t count = args.size();
    state->body.args.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        state->body.args.push_back(args[i]);
    }
    state->body.anchor = anchor;
    state->link_backrefs();
}

static bool is_return_callable(Any callable, const std::vector<Any> &args) {
    if (!args.empty()) {
        Any contarg = maybe_unsyntax(args[0]);
        if (contarg.type == TYPE_Nothing) {
            return true;
        }
    }
    Any ncallable = maybe_unsyntax(callable);
    if (ncallable.type == TYPE_Parameter) {
        if (ncallable.parameter->index == 0) {
            // return continuation is being called
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------

struct TranslateResult {
    Label *state;
    const Anchor *anchor;
    Any enter;
    std::vector<Any> args;

    TranslateResult(Label *_state, const Anchor *_anchor) :
        state(_state), anchor(_anchor), enter(none) {}

    TranslateResult(Label *_state, const Anchor *_anchor,
        const Any &_enter, const std::vector<Any> &_args) :
        state(_state), anchor(_anchor),
        enter(_enter), args(_args) {}
};

static TranslateResult translate(Label *state, const Any &sxexpr);

static TranslateResult translate_argument_list(
    Label *state, const List *it, const Anchor *anchor, bool explicit_ret) {
    std::vector<Any> args;
    int idx = 0;
    Any enter = none;
    if (!explicit_ret) {
        args.push_back(Any(false));
    }
loop:
    if (it == EOL) {
        return TranslateResult(state, anchor, enter, args);
    } else {
        Any sxvalue = it->at;
        // complex expression
        TranslateResult result = translate(state, sxvalue);
        state = result.state;
        anchor = result.anchor;
        assert(anchor);
        Any _enter = result.enter;
        Any arg = none;
        if (_enter.type != TYPE_Nothing) {
            auto &&_args = result.args;
            if (is_return_callable(_enter, _args)) {
                set_active_anchor(anchor);
                location_error(String::from("unexpected return in argument list"));
            }
            Label *next = Label::continuation_from(anchor, Symbol(SYM_Unnamed));
            next->append(Parameter::vararg_from(anchor, Symbol(SYM_Unnamed), TYPE_Void));
            assert(!result.args.empty());
            _args[0] = next;
            br(state, _enter, _args, anchor);
            state = next;
            arg = next->params[PARAM_Arg0];
        } else {
            assert(!result.args.empty());
            // a known value is returned - no need to generate code
            arg = result.args[0];
        }
        if (idx == 0) {
            enter = arg;
        } else {
            args.push_back(arg);
        }
        idx++;
        it = it->next;
        goto loop;
    }
}

static TranslateResult translate_implicit_call(Label *state, const List *it, const Anchor *anchor) {
    assert(it);
    size_t count = it->count;
    if (count < 1) {
        location_error(String::from("callable expected"));
    }
    return translate_argument_list(state, it, anchor, false);
}

static TranslateResult translate_call(Label *state, Any _it) {
    const Syntax *sx = _it;
    const Anchor *anchor = sx->anchor;
    const List *it = sx->datum;
    it = it->next;
    return translate_implicit_call(state, it, anchor);
}

static TranslateResult translate_contcall(Label *state, Any _it) {
    const Syntax *sx = _it;
    const Anchor *anchor = sx->anchor;
    const List *it = sx->datum;
    it = it->next;

    set_active_anchor(anchor);

    size_t count = it->count;
    if (count < 1) {
        location_error(String::from("callable expected"));
    } else if (count < 2) {
        location_error(String::from("continuation expected"));
    }
    return translate_argument_list(state, it, anchor, true);
}

static TranslateResult translate_quote(Label *state, Any _it) {
    const Syntax *sx = _it;
    const Anchor *anchor = sx->anchor;
    const List *it = sx->datum;
    it = it->next;
    assert(it);

    return TranslateResult(state, anchor, none, { unsyntax(it->at) });
}

static TranslateResult translate_expr_list(Label *state, const List *it, const Anchor *anchor) {
    assert(anchor);
loop:
    if (it == EOL) {
        return TranslateResult(state, anchor);
    } else if (it->next == EOL) { // last element goes to cont
        return translate(state, it->at);
    } else {
        Any sxvalue = it->at;
        const Syntax *sx = sxvalue;
        anchor = sx->anchor;
        TranslateResult result = translate(state, sxvalue);
        state = result.state;
        const Anchor *_anchor = result.anchor;
        assert(anchor);
        Any enter = result.enter;
        if (enter.type != TYPE_Nothing) {
            auto &&_args = result.args;
            // complex expression
            // continuation and results are ignored
            Label *next = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
            if (is_return_callable(enter, _args)) {
                set_active_anchor(anchor);
                location_error(String::from("return call is not last expression"));
            } else {
                _args[0] = next;
            }
            br(state, enter, _args, _anchor);
            state = next;
        }
        it = it->next;
        goto loop;
    }
}

static void translate_function_expr_list(
    Label *func, const List *it, const Anchor *anchor) {
    Parameter *dest = func->params[0];
    TranslateResult result = translate_expr_list(func, it, anchor);
    Label *_state = result.state;
    const Anchor *_anchor = result.anchor;
    auto &&enter = result.enter;
    auto &&args = result.args;
    assert(_anchor);
    if (enter.type != TYPE_Nothing) {
        assert(!args.empty());
        if ((args[0].type == TYPE_Bool)
            && !args[0].i1) {
            if (is_return_callable(enter, args)) {
                args[0] = none;
            } else {
                args[0] = dest;
            }
        }
        br(_state, enter, args, _anchor);
    } else if (args.empty()) {
        br(_state, dest, {none}, _anchor);
    } else {
        Any value = args[0];
        if (value.type == TYPE_Syntax) {
            _anchor = value.syntax->anchor;
            value = value.syntax->datum;
        }
        br(_state, dest, {none, value}, _anchor);
    }
    assert(!func->body.args.empty());
}

static TranslateResult translate(Label *state, const Any &sxexpr) {
    try {
        const Syntax *sx = sxexpr;
        const Anchor *anchor = sx->anchor;
        Any expr = sx->datum;

        set_active_anchor(anchor);

        if (expr.type == TYPE_List) {
            const List *slist = expr.list;
            if (slist == EOL) {
                location_error(String::from("empty expression"));
            }
            Any head = unsyntax(slist->at);
            if (head.type == TYPE_Builtin) {
                switch(head.builtin.value()) {
                case KW_Call: return translate_call(state, sxexpr);
                case KW_CCCall: return translate_contcall(state, sxexpr);
                case SYM_QuoteForm: return translate_quote(state, sxexpr);
                default: break;
                }
            }
            return translate_implicit_call(state, slist, anchor);
        } else {
            return TranslateResult(state, anchor, none, { expr });
        }
    } catch (Exception &exc) {
        if (!exc.translate) {
            const Syntax *sx = sxexpr;
            const Anchor *anchor = sx->anchor;
            StyledString ss;
            ss.out << anchor << " while translating expression" << std::endl;
            anchor->stream_source_line(ss.out);
            stream_expr(ss.out, sxexpr, StreamExprFormat::digest());
            exc.translate = ss.str();
        }
        throw exc;
    }
}

static Label *translate_root(const List *it, const Anchor *anchor) {
    Label *mainfunc = Label::function_from(anchor, anchor->path());
    translate_function_expr_list(mainfunc, it, anchor);
    return mainfunc;
}

// path must be resident
static Label *translate_root(Any _expr) {
    const Syntax *sx = _expr;
    const Anchor *anchor = sx->anchor;
    const List *expr = sx->datum;
    return translate_root(expr, anchor);
}

//------------------------------------------------------------------------------
// MACRO EXPANDER
//------------------------------------------------------------------------------
// a basic macro expander that is replaced by the boot script

static bool verify_list_parameter_count(const List *expr, int mincount, int maxcount) {
    assert(expr != EOL);
    if ((mincount <= 0) && (maxcount == -1)) {
        return true;
    }
    int argcount = (int)expr->count - 1;

    if ((maxcount >= 0) && (argcount > maxcount)) {
        location_error(
            format("excess argument. At most %i arguments expected", maxcount));
        return false;
    }
    if ((mincount >= 0) && (argcount < mincount)) {
        location_error(
            format("at least %i arguments expected", mincount));
        return false;
    }
    return true;
}

static void verify_at_parameter_count(const List *topit, int mincount, int maxcount) {
    assert(topit != EOL);
    verify_list_parameter_count(unsyntax(topit->at), mincount, maxcount);
}

//------------------------------------------------------------------------------

static const List *expand(Scope *env, const List *topit);

static const List *expand_expr_list(Scope *env, const List *it) {
    const List *l = EOL;
process:
    if (it == EOL) {
        return reverse_list_inplace(l);
    }
    const List *result = expand(env, it);
    //env = result.env;
    //assert(env);
    if (result == EOL) {
        return reverse_list_inplace(l);
    }
    it = result->next;
    l = List::from(result->at, l);
    goto process;
}

static Parameter *expand_parameter(Scope *env, Any value) {
    const Syntax *sxvalue = value;
    const Anchor *anchor = sxvalue->anchor;
    Any _value = sxvalue->datum;
    if (_value.type == TYPE_Parameter) {
        return _value.parameter;
    } else if (_value.type == TYPE_List && _value.list == EOL) {
        return Parameter::from(anchor, Symbol(SYM_Unnamed), TYPE_Nothing);
    } else {
        _value.verify<TYPE_Symbol>();
        Parameter *param = nullptr;
        if (_value.symbol == KW_Parenthesis) {
            param = Parameter::vararg_from(anchor, _value.symbol, TYPE_Void);
        } else {
            param = Parameter::from(anchor, _value.symbol, TYPE_Void);
        }
        env->bind(_value.symbol, param);
        return param;
    }
}

static const List *expand_fn_cc(Scope *env, const List *topit) {
    verify_at_parameter_count(topit, 1, -1);

    const Syntax *sxit = topit->at;
    //const Anchor *anchor = sxit->anchor;
    const List *it = sxit->datum;

    const Anchor *anchor_kw = ((const Syntax *)it->at)->anchor;

    it = it->next;

    assert(it != EOL);

    Label *func = nullptr;
    Any tryfunc_name = unsyntax(it->at);
    if (tryfunc_name.type == TYPE_Symbol) {
        // named self-binding
        func = Label::from(anchor_kw, tryfunc_name.symbol);
        env->bind(tryfunc_name.symbol, func);
        it = it->next;
    } else if (tryfunc_name.type == TYPE_String) {
        // named lambda
        func = Label::from(anchor_kw, Symbol(tryfunc_name.string));
        it = it->next;
    } else {
        // unnamed lambda
        func = Label::from(anchor_kw, Symbol(SYM_Unnamed));
    }

    const Syntax *sxplist = it->at;
    const Anchor *params_anchor = sxplist->anchor;
    const List *params = sxplist->datum;

    it = it->next;

    Scope *subenv = Scope::from(env);
    // hidden self-binding for subsequent macros
    subenv->bind(SYM_ThisFnCC, func);

    if (params == EOL) {
        func->append(Parameter::from(params_anchor, Symbol(SYM_Unnamed), TYPE_Nothing));
    } else {
        while (params != EOL) {
            func->append(expand_parameter(subenv, params->at));
            params = params->next;
        }
    }

    const List *result = expand_expr_list(subenv, it);
    translate_function_expr_list(func, result, anchor_kw);
    return List::from(Syntax::from_quoted(anchor_kw, func), topit->next);
}

static const List *expand_syntax_apply_block(Scope *env, const List *topit) {
    verify_at_parameter_count(topit, 1, 1);

    const Syntax *sxit = topit->at;
    //const Anchor *anchor = sxit->anchor;
    const List *it = sxit->datum;

    const Anchor *anchor_kw = ((const Syntax *)it->at)->anchor;

    it = it->next;

    return List::from(
        Syntax::from(anchor_kw,
            List::from({
                it->at,
                Syntax::from_quoted(anchor_kw, anchor_kw),
                Syntax::from(anchor_kw, List::from({
                    Syntax::from(anchor_kw, Builtin(SYM_QuoteForm)),
                    Syntax::from_quoted(anchor_kw, topit->next)})),
                Syntax::from_quoted(anchor_kw, env)})),
        EOL);
}

static const List *expand(Scope *env, const List *topit) {
process:
    assert(env);
    assert(topit != EOL);
    Any expr = topit->at;
    const Syntax *sx = expr;
    if (sx->quoted) {
        // return as-is
        return topit;
    }
    const Anchor *anchor = sx->anchor;
    set_active_anchor(anchor);
    expr = sx->datum;
    if (expr.type == TYPE_List) {
        const List *list = expr.list;
        if (list == EOL) {
            location_error(String::from("expression is empty"));
        }

        Any head = unsyntax(list->at);

        // resolve symbol
        if (head.type == TYPE_Symbol) {
            env->lookup(head.symbol, head);
        }

        if (head.type == TYPE_Builtin) {
            Builtin func = head.builtin;
            switch(func.value()) {
            case KW_FnCC: {
                topit = expand_fn_cc(env, topit);
                return topit;
            } break;
            case KW_SyntaxApplyBlock: {
                topit = expand_syntax_apply_block(env, topit);
                goto process;
            } break;
            default: break;
            }
        }

        list = expand_expr_list(env, list);
        return List::from(Syntax::from_quoted(anchor, list), topit->next);
    } else if (expr.type == TYPE_Symbol) {
        Symbol name = expr.symbol;

        Any result = none;
        if (!env->lookup(name, result)) {
            StyledString ss;
            ss.out << "no value bound to name " << name.name()->data << " in scope.";
            auto syms = env->find_closest_match(name);
            if (!syms.empty()) {
                ss.out << " Did you mean " << syms[0].name()->data;
                for (size_t i = 1; i < syms.size(); ++i) {
                    if ((i + 1) == syms.size()) {
                        ss.out << " or ";
                    } else {
                        ss.out << ", ";
                    }
                    ss.out << syms[i].name()->data;
                }
                ss.out << "?";
            }
            location_error(ss.str());
        }
        if (result.type == TYPE_List) {
            const List *list = result.list;
            // quote lists
            list = List::from(Syntax::from_quoted(anchor, result), EOL);
            result = List::from(Syntax::from_quoted(anchor, Builtin(SYM_QuoteForm)), list);
        }
        result = Syntax::from_quoted(anchor, result);
        return List::from(result, topit->next);
    } else {
        return List::from(Syntax::from_quoted(anchor, expr), topit->next);
    }
}

static Any expand_root(Any expr, Scope *scope = nullptr) {
    const Anchor *anchor = nullptr;
    if (expr.type == TYPE_Syntax) {
        anchor = expr.syntax->anchor;
        expr = expr.syntax->datum;
    }
    const List *result = expand_expr_list(scope?scope:globals, expr);
    if (anchor) {
        return Syntax::from(anchor, result);
    } else {
        return result;
    }
}

//------------------------------------------------------------------------------
// GLOBALS
//------------------------------------------------------------------------------

#define DEFINE_BASIC_TYPE(CT, T, BODY) { \
        Typename(Type(T).name(), true); \
        auto &&tn = typenames.get(T); \
        tn.finalize(BODY); \
        assert(sizeof(CT) == size_of(Type(T))); \
    }

#define DEFINE_STRUCT_TYPE(CT, T, ...) { \
        Typename(Type(T).name(), true); \
        auto &&tn = typenames.get(T); \
        tn.finalize(Tuple({ __VA_ARGS__ })); \
        assert(sizeof(CT) == size_of(Type(T))); \
    }

#define DEFINE_STRUCT_HANDLE_TYPE(CT, T, ...) { \
        Typename(Type(T).name(), true); \
        auto &&tn = typenames.get(T); \
        auto ET = Tuple({ __VA_ARGS__ }); \
        assert(sizeof(CT) == size_of(ET)); \
        tn.finalize(Pointer(ET)); \
    }

#define DEFINE_OPAQUE_HANDLE_TYPE(CT, T) { \
        Typename(Type(T).name(), true); \
        auto &&tn = typenames.get(T); \
        tn.finalize(Pointer(Typename(Symbol("_" #CT), true))); \
    }

static void init_types() {
    Type ty_size = TYPE_U64;

    DEFINE_BASIC_TYPE(Type, TYPE_Type, TYPE_U64);
    DEFINE_BASIC_TYPE(Symbol, TYPE_Symbol, TYPE_U64);
    DEFINE_BASIC_TYPE(Builtin, TYPE_Builtin, TYPE_U64);

    DEFINE_STRUCT_TYPE(Any, TYPE_Any,
        TYPE_Type,
        TYPE_U64
    );

    DEFINE_OPAQUE_HANDLE_TYPE(SourceFile, TYPE_SourceFile);
    DEFINE_OPAQUE_HANDLE_TYPE(Label, TYPE_Label);
    DEFINE_OPAQUE_HANDLE_TYPE(Parameter, TYPE_Parameter);
    DEFINE_OPAQUE_HANDLE_TYPE(Scope, TYPE_Scope);

    DEFINE_STRUCT_HANDLE_TYPE(Anchor, TYPE_Anchor,
        Pointer(TYPE_SourceFile),
        TYPE_I32,
        TYPE_I32,
        TYPE_I32
    );

    {
        Type cellT = Typename(Symbol("_List"), true);
        auto &&tn = typenames.get(cellT);
        auto ET = Tuple({ TYPE_Any, Pointer(cellT), ty_size });
        assert(sizeof(List) == size_of(ET));
        tn.finalize(ET);

        typenames
            .get(Typename(Type(TYPE_List).name(), true))
            .finalize(Pointer(cellT));
    }

    DEFINE_STRUCT_HANDLE_TYPE(Syntax, TYPE_Syntax,
        TYPE_Anchor,
        TYPE_Any,
        TYPE_Bool);

    DEFINE_STRUCT_HANDLE_TYPE(String, TYPE_String,
        ty_size,
        Array(TYPE_I8, 1)
    );
}

#undef DEFINE_STRUCT_TYPE

static const String *f_repr(Any value) {
    StyledString ss;
    ss.out << value;
    return ss.str();
}

static void f_write(const String *value) {
    fputs(value->data, stdout);
}
typedef struct { int x,y; } I2;
static I2 bangra_print_number(int a, int b, int c) {
    std::cout << a << " " << b << " " << c << std::endl;
    return { c , b };
}

static Scope *f_import_c(const String *path,
    const String *content, const List *arglist) {
    std::vector<std::string> args;
    while (arglist) {
        auto &&at = arglist->at;
        if (at.type == TYPE_String) {
            args.push_back(at.string->data);
        }
        arglist = arglist->next;
    }
    return import_c_module(path->data, args, content->data);
}

static void f_dump_label(Label *label) {
    StyledStream ss(std::cerr);
    stream_label(ss, label, StreamLabelFormat());
}

typedef struct { Any result; bool ok; } f_scope_at_result;
static f_scope_at_result f_scope_at(Scope *scope, Symbol key) {
    Any result = none;
    bool ok = scope->lookup(key, result);
    return { result, ok };
}

static Symbol f_symbol_new(const String *str) {
    return Symbol(str);
}

static const String *f_string_join(const String *a, const String *b) {
    return String::join(a,b);
}

static void init_globals() {

#define DEFINE_C_FUNCTION(SYMBOL, FUNC, RETTYPE, ...) \
    globals->bind(SYMBOL, \
        Any::from_pointer(Pointer(Function(RETTYPE, { __VA_ARGS__ })), \
            (void *)FUNC));
#define DEFINE_PURE_C_FUNCTION(SYMBOL, FUNC, RETTYPE, ...) \
    globals->bind(SYMBOL, \
        Any::from_pointer(Pointer(Function(RETTYPE, { __VA_ARGS__ }, FF_Pure)), \
            (void *)FUNC));

    //Type rawstring = Pointer(TYPE_I8);
    Type sizeT = TYPE_Nothing;
    if (sizeof(size_t) == sizeof(uint64_t)) {
        sizeT = TYPE_U64;
    } else {
        sizeT = TYPE_U32;
    }

    DEFINE_PURE_C_FUNCTION(FN_ImportC, f_import_c, TYPE_Scope, TYPE_String, TYPE_String, TYPE_List);
    DEFINE_PURE_C_FUNCTION(FN_ScopeAt, f_scope_at, Tuple({TYPE_Any,TYPE_Bool}), TYPE_Scope, TYPE_Symbol);
    DEFINE_PURE_C_FUNCTION(FN_SymbolNew, f_symbol_new, TYPE_Symbol, TYPE_String);
    DEFINE_PURE_C_FUNCTION(FN_Repr, f_repr, TYPE_String, TYPE_Any);
    DEFINE_PURE_C_FUNCTION(FN_StringJoin, f_string_join, TYPE_String, TYPE_String, TYPE_String);

    DEFINE_PURE_C_FUNCTION(FN_DumpLabel, f_dump_label, TYPE_Void, TYPE_Label);
    DEFINE_C_FUNCTION(FN_Write, f_write, TYPE_Void, TYPE_String)

    globals->bind(Symbol("print-number"),
        Any::from_pointer(Pointer(Function(
            Tuple({TYPE_I32,TYPE_I32}),
            {TYPE_I32, TYPE_I32, TYPE_I32},
            FF_Pure)),
            (void *)bangra_print_number));
#undef DEFINE_C_FUNCTION

    globals->bind(KW_True, true);
    globals->bind(KW_False, false);
    globals->bind(KW_ListEmpty, EOL);
    globals->bind(KW_None, none);
    globals->bind(SYM_InterpreterDir,
        String::from(bangra_interpreter_dir, strlen(bangra_interpreter_dir)));
    globals->bind(SYM_InterpreterPath,
        String::from(bangra_interpreter_path, strlen(bangra_interpreter_path)));
    globals->bind(SYM_DebugBuild, bangra_is_debug());
    globals->bind(SYM_InterpreterTimestamp,
        String::from_cstr(bangra_compile_time_date()));

    for (uint64_t i = STYLE_FIRST; i <= STYLE_LAST; ++i) {
        Symbol sym = Symbol((KnownSymbol)i);
        globals->bind(sym, sym);
    }

    globals->bind(TYPE_Bool, Type(TYPE_Bool));
    globals->bind(TYPE_I8, Type(TYPE_I8));
    globals->bind(TYPE_I16, Type(TYPE_I16));
    globals->bind(TYPE_I32, Type(TYPE_I32));
    globals->bind(TYPE_I64, Type(TYPE_I64));
    globals->bind(TYPE_U8, Type(TYPE_U8));
    globals->bind(TYPE_U16, Type(TYPE_U16));
    globals->bind(TYPE_U32, Type(TYPE_U32));
    globals->bind(TYPE_U64, Type(TYPE_U64));
    globals->bind(TYPE_F32, Type(TYPE_F32));
    globals->bind(TYPE_F64, Type(TYPE_F64));

    if (sizeof(size_t) == sizeof(uint64_t)) {
        globals->bind(TYPE_SizeT, Type(TYPE_U64));
    } else {
        globals->bind(TYPE_SizeT, Type(TYPE_U32));
    }
    globals->bind(TYPE_Symbol, Type(TYPE_Symbol));
    globals->bind(TYPE_List, Type(TYPE_List));
    globals->bind(TYPE_Any, Type(TYPE_Any));
    globals->bind(TYPE_String, Type(TYPE_String));
    globals->bind(TYPE_Builtin, Type(TYPE_Builtin));
    globals->bind(TYPE_Nothing, Type(TYPE_Nothing));
    globals->bind(TYPE_Type, Type(TYPE_Type));
    globals->bind(TYPE_Syntax, Type(TYPE_Syntax));
    globals->bind(TYPE_Label, Type(TYPE_Label));
    globals->bind(TYPE_Ref, Type(TYPE_Ref));
    globals->bind(TYPE_Parameter, Type(TYPE_Parameter));
    globals->bind(TYPE_Scope, Type(TYPE_Scope));
    globals->bind(TYPE_Anchor, Type(TYPE_Anchor));
#define T(NAME) globals->bind(NAME, Builtin(NAME));
#define T0(NAME, STR) globals->bind(NAME, Builtin(NAME));
#define T1 T2
#define T2T T2
#define T2(UNAME, LNAME, PFIX, OP) \
    globals->bind(FN_ ## UNAME ## PFIX, Builtin(FN_ ## UNAME ## PFIX));
    B_GLOBALS()
#undef T
#undef T0
#undef T1
#undef T2
#undef T2T
}

//------------------------------------------------------------------------------
// MAIN
//------------------------------------------------------------------------------

static void setup_stdio() {
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

    if (isatty(fileno(stdout))) {
        stream_default_style = stream_ansi_style;
    }
}

} // namespace bangra

int main(int argc, char *argv[]) {
    using namespace bangra;
    Symbol::_init_symbols();
    init_llvm();

    setup_stdio();
    bangra_argc = argc;
    bangra_argv = argv;

    bangra::global_c_namespace = dlopen(NULL, RTLD_LAZY);

    bangra_interpreter_path = nullptr;
    bangra_interpreter_dir = nullptr;
    if (argv) {
        if (argv[0]) {
            std::string loader = GetExecutablePath(argv[0]);
            // string must be kept resident
            bangra_interpreter_path = strdup(loader.c_str());
        } else {
            bangra_interpreter_path = strdup("");
        }

        bangra_interpreter_dir = dirname(strdup(bangra_interpreter_path));
    }

#define CATCH_EXCEPTION 1
#if CATCH_EXCEPTION
    try {
#endif
        init_types();
        init_globals();

        SourceFile *sf = nullptr;
#ifdef BANGRA_DEBUG
        char sourcepath[1024];
        strncpy(sourcepath, bangra_interpreter_dir, 1024);
        strncat(sourcepath, "/bangra.b", 1024);
        Symbol name = String::from_cstr(sourcepath);
        sf = SourceFile::from_file(name);
#else
        sf = SourceFile::from_string(Symbol("<boot>"),
            String::from((const char *)bangra_b, bangra_b_len));
#endif
        if (!sf) {
            location_error(String::from("bootscript missing\n"));
        }
        LexerParser parser(sf);
        auto expr = parser.parse();

        expr = expand_root(expr);
        Label *fn = translate_root(expr);

#if BANGRA_DEBUG_CODEGEN
        StyledStream ss(std::cout);
        std::cout << "non-normalized:" << std::endl;
        stream_label(ss, fn, StreamLabelFormat::debug_all());
        std::cout << std::endl;
#endif

        fn = normalize(fn);
#if BANGRA_DEBUG_CODEGEN
        std::cout << "normalized:" << std::endl;
        stream_label(ss, fn, StreamLabelFormat::debug_all());
        std::cout << std::endl;
#endif

        typedef void (*MainFuncType)();
        MainFuncType fptr = (MainFuncType)compile(fn, 0).pointer;
        fptr();

        //interpreter_loop(cmd);
#if CATCH_EXCEPTION
    } catch (const Exception &exc) {
        default_exception_handler(exc);
        throw exc;
    }
#endif

    return 0;
}

#endif // BANGRA_CPP_IMPL
