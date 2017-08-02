/*
Scopes Compiler
Copyright (c) 2016, 2017 Leonard Ritter

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

#define SCOPES_VERSION_MAJOR 0
#define SCOPES_VERSION_MINOR 8
#define SCOPES_VERSION_PATCH 0

#define SCOPES_DEBUG_CODEGEN 0
#define SCOPES_OPTIMIZE_ASSEMBLY 1

#define SCOPES_MAX_LABEL_INSTANCES 256

#ifndef SCOPES_WIN32
#   ifdef _WIN32
#   define SCOPES_WIN32
#   endif
#endif

#ifndef SCOPES_CPP
#define SCOPES_CPP

//------------------------------------------------------------------------------
// C HEADER
//------------------------------------------------------------------------------

#include <sys/types.h>
#ifdef SCOPES_WIN32
#include "mman.h"
#include "stdlib_ex.h"
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "external/linenoise-ng/include/linenoise.h"
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
    SCOPES_ ## NAME = NAME,
EXPORT_DEFINES
#undef T
#undef EXPORT_DEFINES
};

const char *scopes_compiler_path;
const char *scopes_compiler_dir;
size_t scopes_argc;
char **scopes_argv;

// C namespace exports
int unescape_string(char *buf);
int escape_string(char *buf, const char *str, int strcount, const char *quote_chars);

void scopes_strtod(double *v, const char *str, char **str_end, int base );
void scopes_strtoll(int64_t *v, const char* str, char** endptr, int base);
void scopes_strtoull(uint64_t *v, const char* str, char** endptr, int base);

bool scopes_is_debug();

const char *scopes_compile_time_date();

#if defined __cplusplus
}
#endif

#endif // SCOPES_CPP
#ifdef SCOPES_CPP_IMPL

//#define SCOPES_DEBUG_IL

#undef NDEBUG
#ifdef SCOPES_WIN32
#include <windows.h>
#include "stdlib_ex.h"
#include "dlfcn.h"
#else
// for backtrace
#include <execinfo.h>
#include <dlfcn.h>
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
#include <csignal>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>
#include <llvm-c/Disassembler.h>
#include <llvm-c/Support.h>

#include "llvm/IR/Module.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_os_ostream.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/LiteralSupport.h"

#define STB_SPRINTF_IMPLEMENTATION
#include "external/stb_sprintf.h"
extern "C" {
#include "external/minilibs/regexp.h"
}

#pragma GCC diagnostic ignored "-Wvla-extension"
// #pragma GCC diagnostic ignored "-Wzero-length-array"
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
// #pragma GCC diagnostic ignored "-Wembedded-directive"
// #pragma GCC diagnostic ignored "-Wgnu-statement-expression"
#pragma GCC diagnostic ignored "-Wc99-extensions"
// #pragma GCC diagnostic ignored "-Wmissing-braces"
// this one is only enabled for code cleanup
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wdate-time"

#ifdef SCOPES_WIN32
#include <setjmpex.h>
#else
#include <setjmp.h>
#endif

#include "external/cityhash/city.cpp"

//------------------------------------------------------------------------------
// UTILITIES
//------------------------------------------------------------------------------

void scopes_strtod(double *v, const char *str, char **str_end, int base ) {
    *v = std::strtod(str, str_end);
}
void scopes_strtoll(int64_t *v, const char* str, char** endptr, int base) {
    *v = std::strtoll(str, endptr, base);
}
void scopes_strtoull(uint64_t *v, const char* str, char** endptr, int base) {
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

extern "C" {
// used in test_assorted.sc
#pragma GCC visibility push(default)
extern int scopes_test_add(int a, int b) { return a + b; }
#pragma GCC visibility pop
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

bool scopes_is_debug() {
#ifdef SCOPES_DEBUG
        return true;
#else
        return false;
#endif
}

const char *scopes_compile_time_date() {
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

namespace scopes {

using llvm::isa;
using llvm::cast;
using llvm::dyn_cast;

template <typename R, typename... Args>
static std::function<R (Args...)> memoize(R (*fn)(Args...)) {
    std::map<std::tuple<Args...>, R> table;
    return [fn, table](Args... args) mutable -> R {
        auto argt = std::make_tuple(args...);
        auto memoized = table.find(argt);
        if(memoized == table.end()) {
            auto result = fn(args...);
            table[argt] = result;
            return result;
        } else {
            return memoized->second;
        }
    };
}

//------------------------------------------------------------------------------
// SYMBOL ENUM
//------------------------------------------------------------------------------

// list of symbols to be exposed as builtins to the default global namespace
#define B_GLOBALS() \
    T(FN_Branch) T(KW_Fn) T(KW_Label) T(KW_SyntaxApplyBlock) T(KW_Quote) \
    T(KW_Call) T(KW_RawCall) T(KW_CCCall) T(SYM_QuoteForm) T(FN_Dump) T(KW_Do) \
    T(FN_FunctionType) T(FN_TupleType) T(FN_Alloca) T(FN_Malloc) \
    T(FN_AllocaArray) T(FN_MallocArray) \
    T(FN_AnyExtract) T(FN_AnyWrap) T(FN_IsConstant) T(FN_Free) \
    T(OP_ICmpEQ) T(OP_ICmpNE) \
    T(OP_ICmpUGT) T(OP_ICmpUGE) T(OP_ICmpULT) T(OP_ICmpULE) \
    T(OP_ICmpSGT) T(OP_ICmpSGE) T(OP_ICmpSLT) T(OP_ICmpSLE) \
    T(OP_FCmpOEQ) T(OP_FCmpONE) T(OP_FCmpORD) \
    T(OP_FCmpOGT) T(OP_FCmpOGE) T(OP_FCmpOLT) T(OP_FCmpOLE) \
    T(OP_FCmpUEQ) T(OP_FCmpUNE) T(OP_FCmpUNO) \
    T(OP_FCmpUGT) T(OP_FCmpUGE) T(OP_FCmpULT) T(OP_FCmpULE) \
    T(FN_Purify) T(FN_Unconst) T(FN_TypeOf) T(FN_Bitcast) \
    T(FN_IntToPtr) T(FN_PtrToInt) T(FN_Load) T(FN_Store) \
    T(FN_VolatileLoad) T(FN_VolatileStore) \
    T(FN_ExtractValue) T(FN_InsertValue) T(FN_Trunc) T(FN_ZExt) T(FN_SExt) \
    T(FN_GetElementPtr) T(SFXFN_CompilerError) T(FN_VaCountOf) T(FN_VaAt) \
    T(FN_VaKeys) T(FN_CompilerMessage) T(FN_Undef) T(FN_NullOf) T(KW_Let) \
    T(KW_If) T(SFXFN_SetTypeSymbol) T(SFXFN_DelTypeSymbol) T(FN_ExternSymbol) \
    T(SFXFN_SetTypenameStorage) T(FN_ExternNew) \
    T(FN_TypeAt) T(KW_SyntaxExtend) T(FN_Location) T(SFXFN_Unreachable) \
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
    T(OP_FAdd) T(OP_FSub) T(OP_FMul) T(OP_FDiv) T(OP_FRem) \
    T(OP_Tertiary) T(KW_SyntaxLog)

#define B_MAP_SYMBOLS() \
    T(SYM_Unnamed, "") \
    \
    /* keywords and macros */ \
    T(KW_CatRest, "::*") T(KW_CatOne, "::@") \
    T(KW_SyntaxLog, "syntax-log") \
    T(KW_Assert, "assert") T(KW_Break, "break") T(KW_Label, "label") \
    T(KW_Call, "call") T(KW_RawCall, "rawcall") T(KW_CCCall, "cc/call") T(KW_Continue, "continue") \
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
    T(FN_AnyExtract, "Any-extract-constant") T(FN_AnyWrap, "Any-wrap") \
    T(FN_ActiveAnchor, "active-anchor") T(FN_ActiveFrame, "active-frame") \
    T(FN_BitCountOf, "bitcountof") T(FN_IsSigned, "signed?") \
    T(FN_Bitcast, "bitcast") T(FN_IntToPtr, "inttoptr") T(FN_PtrToInt, "ptrtoint") \
    T(FN_BlockMacro, "block-macro") \
    T(FN_BlockScopeMacro, "block-scope-macro") T(FN_BoolEq, "bool==") \
    T(FN_BuiltinEq, "Builtin==") \
    T(FN_Branch, "branch") T(FN_IsCallable, "callable?") T(FN_Cast, "cast") \
    T(FN_Concat, "concat") T(FN_Cons, "cons") T(FN_IsConstant, "constant?") \
    T(FN_Countof, "countof") \
    T(FN_Compile, "__compile") \
    T(FN_TypenameFieldIndex, "typename-field-index") \
    T(FN_TypenameFieldName, "typename-field-name") \
    T(FN_CompilerMessage, "compiler-message") \
    T(FN_CStr, "cstr") T(FN_DatumToSyntax, "datum->syntax") \
    T(FN_DatumToQuotedSyntax, "datum->quoted-syntax") \
    T(FN_DefaultStyler, "default-styler") T(FN_StyleToString, "style->string") \
    T(FN_Disqualify, "disqualify") T(FN_Dump, "dump") \
    T(FN_DumpLabel, "dump-label") \
    T(FN_FormatFrame, "Frame-format") \
    T(FN_ElementType, "element-type") T(FN_IsEmpty, "empty?") \
    T(FN_TypeCountOf, "type-countof") \
    T(FN_Enumerate, "enumerate") T(FN_Eval, "eval") \
    T(FN_Exit, "exit") T(FN_Expand, "expand") \
    T(FN_ExternLibrary, "extern-library") \
    T(FN_ExternSymbol, "extern-symbol") \
    T(FN_ExtractMemory, "extract-memory") \
    T(FN_ExtractValue, "extractvalue") T(FN_InsertValue, "insertvalue") \
    T(FN_GetElementPtr, "getelementptr") \
    T(FN_FFISymbol, "ffi-symbol") T(FN_FFICall, "ffi-call") \
    T(FN_FrameEq, "Frame==") T(FN_Free, "free") \
    T(FN_GetExceptionHandler, "get-exception-handler") \
    T(FN_GetScopeSymbol, "get-scope-symbol") T(FN_Hash, "hash") \
    T(OP_ICmpEQ, "icmp==") T(OP_ICmpNE, "icmp!=") \
    T(OP_ICmpUGT, "icmp>u") T(OP_ICmpUGE, "icmp>=u") T(OP_ICmpULT, "icmp<u") T(OP_ICmpULE, "icmp<=u") \
    T(OP_ICmpSGT, "icmp>s") T(OP_ICmpSGE, "icmp>=s") T(OP_ICmpSLT, "icmp<s") T(OP_ICmpSLE, "icmp<=s") \
    T(OP_FCmpOEQ, "fcmp==o") T(OP_FCmpONE, "fcmp!=o") T(OP_FCmpORD, "fcmp-ord") \
    T(OP_FCmpOGT, "fcmp>o") T(OP_FCmpOGE, "fcmp>=o") T(OP_FCmpOLT, "fcmp<o") T(OP_FCmpOLE, "fcmp<=o") \
    T(OP_FCmpUEQ, "fcmp==u") T(OP_FCmpUNE, "fcmp!=u") T(OP_FCmpUNO, "fcmp-uno") \
    T(OP_FCmpUGT, "fcmp>u") T(OP_FCmpUGE, "fcmp>=u") T(OP_FCmpULT, "fcmp<u") T(OP_FCmpULE, "fcmp<=u") \
    T(OP_Add, "add") T(OP_AddNUW, "add-nuw") T(OP_AddNSW, "add-nsw") \
    T(OP_Sub, "sub") T(OP_SubNUW, "sub-nuw") T(OP_SubNSW, "sub-nsw") \
    T(OP_Mul, "mul") T(OP_MulNUW, "mul-nuw") T(OP_MulNSW, "mul-nsw") \
    T(OP_SDiv, "sdiv") T(OP_UDiv, "udiv") \
    T(OP_SRem, "srem") T(OP_URem, "urem") \
    T(OP_Shl, "shl") T(OP_LShr, "lshr") T(OP_AShr, "ashr") \
    T(OP_BAnd, "band") T(OP_BOr, "bor") T(OP_BXor, "bxor") \
    T(FN_IsFile, "file?") \
    T(OP_FAdd, "fadd") T(OP_FSub, "fsub") T(OP_FMul, "fmul") T(OP_FDiv, "fdiv") T(OP_FRem, "frem") \
    T(FN_FPTrunc, "fptrunc") T(FN_FPExt, "fpext") \
    T(FN_FPToUI, "fptoui") T(FN_FPToSI, "fptosi") \
    T(FN_UIToFP, "uitofp") T(FN_SIToFP, "sitofp") \
    T(FN_ImportC, "import-c") T(FN_IsInteger, "integer?") \
    T(FN_IntegerType, "integer-type") \
    T(FN_CompilerVersion, "compiler-version") \
    T(FN_Iter, "iter") T(FN_FormatMessage, "format-message") \
    T(FN_IsIterator, "iterator?") T(FN_IsLabel, "label?") \
    T(FN_LabelEq, "Label==") \
    T(FN_LabelNew, "Label-new") T(FN_LabelParameters, "Label-parameters") \
    T(FN_ClosureEq, "Closure==") \
    T(FN_ListAtom, "list-atom?") T(FN_ListCountOf, "list-countof") \
    T(FN_ListLoad, "list-load") T(FN_ListJoin, "list-join") \
    T(FN_ListParse, "list-parse") T(FN_IsList, "list?") T(FN_Load, "load") \
    T(FN_LoadLibrary, "load-library") \
    T(FN_VolatileLoad, "volatile-load") \
    T(FN_VolatileStore, "volatile-store") \
    T(FN_ListAt, "list-at") T(FN_ListNext, "list-next") T(FN_ListCons, "list-cons") \
    T(FN_IsListEmpty, "list-empty?") \
    T(FN_Malloc, "malloc") T(FN_MallocArray, "malloc-array") T(FN_Unconst, "unconst") \
    T(FN_Macro, "macro") T(FN_Max, "max") T(FN_Min, "min") \
    T(FN_MemCpy, "memcpy") \
    T(FN_IsNone, "none?") \
    T(FN_IsNull, "null?") T(FN_OrderedBranch, "ordered-branch") \
    T(FN_ParameterEq, "Parameter==") \
    T(FN_ParameterNew, "Parameter-new") T(FN_ParameterName, "Parameter-name") \
    T(FN_ParameterAnchor, "Parameter-anchor") \
    T(FN_ParseC, "parse-c") T(FN_PointerOf, "pointerof") \
    T(FN_PointerType, "pointer-type") \
    T(FN_FunctionType, "function-type") \
    T(FN_TupleType, "tuple-type") \
    T(FN_ArrayType, "array-type") \
    T(FN_TypenameType, "typename-type") \
    T(FN_Purify, "purify") \
    T(FN_Write, "io-write!") \
    T(FN_Flush, "io-flush") \
    T(FN_Product, "product") T(FN_Prompt, "__prompt") T(FN_Qualify, "qualify") \
    T(FN_Range, "range") T(FN_RefNew, "ref-new") T(FN_RefAt, "ref@") \
    T(FN_Repeat, "repeat") T(FN_Repr, "Any-repr") T(FN_AnyString, "Any-string") \
    T(FN_Require, "require") T(FN_ScopeOf, "scopeof") T(FN_ScopeAt, "Scope@") \
    T(FN_ScopeEq, "Scope==") \
    T(FN_ScopeNew, "Scope-new") \
    T(FN_ScopeNewSubscope, "Scope-new-subscope") \
    T(FN_ScopeNext, "Scope-next") T(FN_SizeOf, "sizeof") \
    T(FN_Slice, "slice") T(FN_Store, "store") \
    T(FN_StringAt, "string@") T(FN_StringCmp, "string-compare") \
    T(FN_StringCountOf, "string-countof") T(FN_StringNew, "string-new") \
    T(FN_StringJoin, "string-join") T(FN_StringSlice, "string-slice") \
    T(FN_StructOf, "structof") T(FN_TypeStorage, "storageof") \
    T(FN_SymbolEq, "Symbol==") T(FN_SymbolNew, "string->Symbol") \
    T(FN_StringToRawstring, "string->rawstring") \
    T(FN_IsSymbol, "symbol?") \
    T(FN_SyntaxToAnchor, "syntax->anchor") T(FN_SyntaxToDatum, "syntax->datum") \
    T(FN_SyntaxCons, "syntax-cons") T(FN_SyntaxDo, "syntax-do") \
    T(FN_IsSyntaxHead, "syntax-head?") \
    T(FN_SyntaxList, "syntax-list") T(FN_SyntaxQuote, "syntax-quote") \
    T(FN_IsSyntaxQuoted, "syntax-quoted?") \
    T(FN_SyntaxUnquote, "syntax-unquote") \
    T(FN_SymbolToString, "Symbol->string") \
    T(FN_StringMatch, "string-match?") \
    T(FN_SuperOf, "superof") \
    T(FN_SyntaxNew, "Syntax-new") \
    T(FN_SyntaxWrap, "Syntax-wrap") \
    T(FN_SyntaxStrip, "Syntax-strip") \
    T(FN_Translate, "translate") T(FN_Trunc, "trunc") \
    T(FN_ZExt, "zext") T(FN_SExt, "sext") \
    T(FN_TupleOf, "tupleof") T(FN_TypeNew, "type-new") T(FN_TypeName, "type-name") \
    T(FN_TypeSizeOf, "type-sizeof") \
    T(FN_Typify, "__typify") \
    T(FN_TypeEq, "type==") T(FN_IsType, "type?") T(FN_TypeOf, "typeof") \
    T(FN_TypeKind, "type-kind") \
    T(FN_TypeAt, "type@") \
    T(FN_Undef, "undef") T(FN_NullOf, "nullof") T(FN_Alloca, "alloca") \
    T(FN_AllocaArray, "alloca-array") \
    T(FN_Location, "compiler-anchor") \
    T(FN_ExternNew, "extern-new") \
    T(FN_VaCountOf, "va-countof") T(FN_VaKeys, "va-keys") T(FN_VaAt, "va@") \
    T(FN_VectorOf, "vectorof") T(FN_XPCall, "xpcall") T(FN_Zip, "zip") \
    T(FN_ZipFill, "zip-fill") \
    \
    /* builtin and global functions with side effects */ \
    T(SFXFN_CopyMemory, "copy-memory!") \
    T(SFXFN_Unreachable, "unreachable!") \
    T(SFXFN_Error, "__error!") \
    T(SFXFN_Raise, "__raise!") \
    T(SFXFN_Abort, "abort!") \
    T(SFXFN_CompilerError, "compiler-error!") \
    T(SFXFN_SetAnchor, "set-anchor!") \
    T(SFXFN_LabelAppendParameter, "label-append-parameter!") \
    T(SFXFN_RefSet, "ref-set!") \
    T(SFXFN_SetExceptionHandler, "set-exception-handler!") \
    T(SFXFN_SetGlobals, "set-globals!") \
    T(SFXFN_SetTypenameSuper, "set-typename-super!") \
    T(SFXFN_SetGlobalApplyFallback, "set-global-apply-fallback!") \
    T(SFXFN_SetScopeSymbol, "__set-scope-symbol!") \
    T(SFXFN_DelScopeSymbol, "delete-scope-symbol!") \
    T(SFXFN_SetTypeSymbol, "set-type-symbol!") \
    T(SFXFN_DelTypeSymbol, "delete-type-symbol!") \
    T(SFXFN_SetTypenameStorage, "set-typename-storage!") \
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
    T(SYM_CompilerDir, "compiler-dir") \
    T(SYM_CompilerPath, "compiler-path") \
    T(SYM_CompilerTimestamp, "compiler-timestamp") \
    \
    /* parse-c keywords */ \
    T(SYM_Struct, "struct") \
    T(SYM_Union, "union") \
    T(SYM_TypeDef, "typedef") \
    T(SYM_Enum, "enum") \
    T(SYM_Array, "array") \
    T(SYM_Vector, "vector") \
    T(SYM_FNType, "fntype") \
    T(SYM_Extern, "extern") \
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
    /* varargs */ \
    T(SYM_Parenthesis, "...") \
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
    /* list styles */ \
    T(SYM_SquareList, "square-list") \
    T(SYM_CurlyList, "curly-list") \
    \
    /* compile flags */ \
    T(SYM_DumpDisassembly, "compile-flag-dump-disassembly") \
    T(SYM_DumpModule, "compile-flag-dump-module") \
    T(SYM_DumpFunction, "compile-flag-dump-function") \
    T(SYM_DumpTime, "compile-flag-dump-time") \
    T(SYM_NoOpts, "compile-flag-no-opts") \
    \
    /* function flags */ \
    T(SYM_Variadic, "variadic") \
    T(SYM_Pure, "pure") \
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

    StyledStream() :
        _ssf(stream_default_style),
        _ost(std::cerr)
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
    struct Hash {
        std::size_t operator()(const String *s) const {
            return CityHash64(s->data, s->count);
        }
    };

    size_t count;
    char data[1];

    bool operator ==(const String &other) const {
        if (count == other.count) {
            return !memcmp(data, other.data, count);
        }
        return false;
    }

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
        std::size_t operator()(const scopes::Symbol & s) const {
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
        auto it = file_cache.find(path);
        if (it != file_cache.end()) {
            file_cache.erase(it);
        }
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

    size_t size() const {
        return length;
    }

    StyledStream &stream(StyledStream &ost, int offset,
        const char *indent = "    ") {
        auto str = strptr();
        if (offset >= length) {
            ost << "<cannot display location in source file (offset " 
                << offset << " is beyond length " << length << ")>" << std::endl;
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

#define B_TYPE_KIND() \
    T(TK_Integer, "type-kind-integer") \
    T(TK_Real, "type-kind-real") \
    T(TK_Pointer, "type-kind-pointer") \
    T(TK_Array, "type-kind-array") \
    T(TK_Vector, "type-kind-vector") \
    T(TK_Tuple, "type-kind-tuple") \
    T(TK_Union, "type-kind-union") \
    T(TK_Typename, "type-kind-typename") \
    T(TK_TypedLabel, "type-kind-label") \
    T(TK_TypeSet, "type-kind-typeset") \
    T(TK_Function, "type-kind-function") \
    T(TK_Constant, "type-kind-constant") \
    T(TK_Extern, "type-kind-extern")

enum TypeKind {
#define T(NAME, BNAME) \
    NAME,
    B_TYPE_KIND()
#undef T
};

struct Type;

static bool is_opaque(const Type *T);
static size_t size_of(const Type *T);
static size_t align_of(const Type *T);
static const Type *storage_type(const Type *T);
static StyledStream& operator<<(StyledStream& ost, const Type *type);

#define B_TYPES() \
    /* types */ \
    T(TYPE_Void, "void") \
    T(TYPE_Nothing, "Nothing") \
    T(TYPE_Any, "Any") \
    \
    T(TYPE_Type, "type") \
    T(TYPE_Unknown, "unknown") \
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
    T(TYPE_F16, "f16") \
    T(TYPE_F32, "f32") \
    T(TYPE_F64, "f64") \
    T(TYPE_F80, "f80") \
    \
    T(TYPE_List, "list") \
    T(TYPE_Syntax, "Syntax") \
    T(TYPE_Anchor, "Anchor") \
    T(TYPE_String, "string") \
    T(TYPE_Ref, "ref") \
    \
    T(TYPE_Scope, "Scope") \
    T(TYPE_SourceFile, "SourceFile") \
    T(TYPE_Exception, "Exception") \
    \
    T(TYPE_Parameter, "Parameter") \
    T(TYPE_Label, "Label") \
    \
    T(TYPE_USize, "usize") \
    \
    /* supertypes */ \
    T(TYPE_Integer, "integer") \
    T(TYPE_Real, "real") \
    T(TYPE_Pointer, "pointer") \
    T(TYPE_Array, "array") \
    T(TYPE_Vector, "vector") \
    T(TYPE_Tuple, "tuple") \
    T(TYPE_Union, "union") \
    T(TYPE_Typename, "typename") \
    T(TYPE_TypedLabel, "typed-label") \
    T(TYPE_TypeSet, "typeset") \
    T(TYPE_Function, "function") \
    T(TYPE_Constant, "constant") \
    T(TYPE_Extern, "extern") \
    T(TYPE_CStruct, "CStruct") \
    T(TYPE_CUnion, "CUnion") \
    T(TYPE_CEnum, "CEnum")

#define T(TYPE, TYPENAME) \
    static const Type *TYPE = nullptr;
B_TYPES()
#undef T

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
struct Exception;

struct Any {
    struct Hash {
        std::size_t operator()(const Any & s) const {
            return s.hash();
        }
    };

    const Type *type;
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
        const Type *typeref;
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
        const Exception *exception;
    };

    Any(Nothing x) : type(TYPE_Nothing), u64(0) {}
    Any(const Type *x) : type(TYPE_Type), typeref(x) {}
    Any(bool x) : type(TYPE_Bool), u64(0) { i1 = x; }
    Any(int8_t x) : type(TYPE_I8), u64(0) { i8 = x; }
    Any(int16_t x) : type(TYPE_I16), u64(0) { i16 = x; }
    Any(int32_t x) : type(TYPE_I32), u64(0) { i32 = x; }
    Any(int64_t x) : type(TYPE_I64), i64(x) {}
    Any(uint8_t x) : type(TYPE_U8), u64(0) { u8 = x; }
    Any(uint16_t x) : type(TYPE_U16), u64(0) { u16 = x; }
    Any(uint32_t x) : type(TYPE_U32), u64(0) { u32 = x; }
    Any(uint64_t x) : type(TYPE_U64), u64(x) {}
    Any(float x) : type(TYPE_F32), u64(0) { f32 = x; }
    Any(double x) : type(TYPE_F64), f64(x) {}
    Any(const String *x) : type(TYPE_String), string(x) {}
    Any(Symbol x) : type(TYPE_Symbol), symbol(x) {}
    Any(const Syntax *x) : type(TYPE_Syntax), syntax(x) {}
    Any(const Anchor *x) : type(TYPE_Anchor), anchor(x) {}
    Any(const List *x) : type(TYPE_List), list(x) {}
    Any(const Exception *x) : type(TYPE_Exception), exception(x) {}
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

    static Any from_opaque(const Type *type) {
        Any val = none;
        val.type = type;
        return val;
    }

    static Any from_pointer(const Type *type, void *ptr) {
        Any val = none;
        val.type = type;
        val.pointer = ptr;
        return val;
    }

    void verify(const Type *T) const;
    void verify_indirect(const Type *T) const;
    const Type *indirect_type() const;

    operator const Type *() const { verify(TYPE_Type); return typeref; }
    operator const List *() const { verify(TYPE_List); return list; }
    operator const Syntax *() const { verify(TYPE_Syntax); return syntax; }
    operator const Anchor *() const { verify(TYPE_Anchor); return anchor; }
    operator const String *() const { verify(TYPE_String); return string; }
    operator const Exception *() const { verify(TYPE_Exception); return exception; }
    operator Label *() const { verify(TYPE_Label); return label; }
    operator Scope *() const { verify(TYPE_Scope); return scope; }
    operator Parameter *() const { verify(TYPE_Parameter); return parameter; }

    struct AnyStreamer {
        StyledStream& ost;
        const Type *type;
        bool annotate_type;
        AnyStreamer(StyledStream& _ost, const Type *_type, bool _annotate_type) :
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

    StyledStream& stream(StyledStream& ost, bool annotate_type = true) const;

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

static const Type *superof(const Type *T);

struct Type {
    TypeKind kind() const { return _kind; } // for this codebase

    Type(TypeKind kind) : _kind(kind), _name(Symbol(SYM_Unnamed).name()) {}
    Type(const Type &other) = delete;

    const String *name() const {
        return _name;
    }

    StyledStream& stream(StyledStream& ost) const {
        ost << Style_Type;
        ost << name()->data;
        ost << Style_None;
        return ost;
    }

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
        const Type *self = this;
        do {
            auto it = self->symbols.find(name);
            if (it != self->symbols.end()) {
                dest = it->second;
                return true;
            }
            if (self == TYPE_Typename)
                break;
            self = superof(self);
        } while (self);
        return false;
    }

    bool lookup_local(Symbol name, Any &dest) const {
        auto it = symbols.find(name);
        if (it != symbols.end()) {
            dest = it->second;
            return true;
        }
        return false;
    }
    
    bool lookup_call_handler(Any &dest) const {
        return lookup(KW_Call, dest);
    }

private:
    const TypeKind _kind;

protected:
    const String *_name;

    std::unordered_map<Symbol, Any, Symbol::Hash> symbols;
};

static StyledStream& operator<<(StyledStream& ost, const Type *type) {
    return type->stream(ost);
}

static Any wrap_pointer(const Type *type, void *ptr);

//------------------------------------------------------------------------------
// TYPE CHECK PREDICATES
//------------------------------------------------------------------------------

static void verify(const Type *typea, const Type *typeb) {
    if (typea != typeb) {
        StyledString ss;
        ss.out << "type " << typea << " expected, got " << typeb;
        location_error(ss.str());
    }
}

static void verify_integer(const Type *type) {
    if (type->kind() != TK_Integer) {
        StyledString ss;
        ss.out << "integer or bool type expected, got " << type;
        location_error(ss.str());
    }
}

static void verify_real(const Type *type) {
    if (type->kind() != TK_Real) {
        StyledString ss;
        ss.out << "real type expected, got " << type;
        location_error(ss.str());
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

void Any::verify(const Type *T) const {
    scopes::verify(T, type);
}

//------------------------------------------------------------------------------
// TYPE FACTORIES
//------------------------------------------------------------------------------

template<typename T>
struct TypeFactory {
    struct TypeArgs {
        std::vector<Any> args;

        TypeArgs() {}
        TypeArgs(const std::vector<Any> &_args) : args(_args) {}

        bool operator==(const TypeArgs &other) const {
            if (args.size() != other.args.size()) return false;
            for (size_t i = 0; i < args.size(); ++i) {
                auto &&a = args[i];
                auto &&b = other.args[i];
                if (a != b)
                    return false;
            }
            return true;
        }

        struct Hash {
            std::size_t operator()(const TypeArgs& s) const {
                std::size_t h = 0;
                for (auto &&arg : s.args) {
                    h = HashLen16(h, arg.hash());
                }
                return h;
            }
        };
    };

    typedef std::unordered_map<TypeArgs, T *, typename TypeArgs::Hash> ArgMap;

    ArgMap map;

    const Type *insert(const std::vector<Any> &args) {
        TypeArgs ta(args);
        typename ArgMap::iterator it = map.find(ta);
        if (it == map.end()) {
            T *t = new T(args);
            map.insert({ta, t});
            return t;
        } else {
            return it->second;
        }
    }

    template <typename... Args>
    const Type *insert(Args... args) {
        TypeArgs ta({ args... });
        typename ArgMap::iterator it = map.find(ta);
        if (it == map.end()) {
            T *t = new T(args...);
            map.insert({ta, t});
            return t;
        } else {
            return it->second;
        }
    }
};

//------------------------------------------------------------------------------
// INTEGER TYPE
//------------------------------------------------------------------------------

struct IntegerType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_Integer;
    }

    IntegerType(size_t _width, bool _issigned)
        : Type(TK_Integer), width(_width), issigned(_issigned) {
        std::stringstream ss;
        if (_width == 1) {
            assert(!_issigned);
            ss << "bool";
        } else {
            if (issigned) {
                ss << "i";
            } else {
                ss << "u";
            }
            ss << width;
        }
        _name = String::from_stdstring(ss.str());
    }

    size_t width;
    bool issigned;
};

const Type *_Integer(size_t _width, bool _issigned) {
    return new IntegerType(_width, _issigned);
}
static auto Integer = memoize(_Integer);

//------------------------------------------------------------------------------
// INTEGER TYPE
//------------------------------------------------------------------------------

struct RealType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_Real;
    }

    RealType(size_t _width)
        : Type(TK_Real), width(_width) {
        std::stringstream ss;
        ss << "f" << width;
        _name = String::from_stdstring(ss.str());
    }

    size_t width;
};

const Type *_Real(size_t _width) {
    return new RealType(_width);
}
static auto Real = memoize(_Real);

//------------------------------------------------------------------------------
// POINTER TYPE
//------------------------------------------------------------------------------

struct PointerType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_Pointer;
    }

    PointerType(const Type *_element_type)
        : Type(TK_Pointer), element_type(_element_type) {
        std::stringstream ss;
        ss << element_type->name()->data << "*";
        _name = String::from_stdstring(ss.str());
    }

    void *getelementptr(void *src, size_t i) const {
        size_t stride = size_of(element_type);
        return (void *)((char *)src + stride * i);
    }

    Any unpack(void *src) const {
        return wrap_pointer(element_type, src);
    }
    static size_t size() {
        return sizeof(uint64_t);
    }

    const Type *element_type;
};

static const Type *Pointer(const Type *element_type) {
    static TypeFactory<PointerType> pointers;
    return pointers.insert(element_type);
}

//------------------------------------------------------------------------------
// ARRAY TYPE
//------------------------------------------------------------------------------

struct StorageType : Type {

    StorageType(TypeKind kind) : Type(kind) {}

    size_t size;
    size_t align;
};

struct SizedStorageType : StorageType {

    SizedStorageType(TypeKind kind, const Type *_element_type, size_t _count)
        : StorageType(kind), element_type(_element_type), count(_count) {
        stride = size_of(element_type);
        size = stride * count;
        align = align_of(element_type);
    }

    void *getelementptr(void *src, size_t i) const {
        verify_range(i, count);
        return (void *)((char *)src + stride * i);
    }

    Any unpack(void *src, size_t i) const {
        return wrap_pointer(type_at_index(i), getelementptr(src, i));
    }

    const Type *type_at_index(size_t i) const {
        verify_range(i, count);
        return element_type;
    }

    const Type *element_type;
    size_t count;
    size_t stride;
};

struct ArrayType : SizedStorageType {
    static bool classof(const Type *T) {
        return T->kind() == TK_Array;
    }

    ArrayType(const Type *_element_type, size_t _count)
        : SizedStorageType(TK_Array, _element_type, _count) {
        std::stringstream ss;
        ss << "[" << element_type->name()->data << " x " << count << "]";
        _name = String::from_stdstring(ss.str());
    }
};

static const Type *Array(const Type *element_type, size_t count) {
    static TypeFactory<ArrayType> arrays;
    return arrays.insert(element_type, count);
}

//------------------------------------------------------------------------------
// VECTOR TYPE
//------------------------------------------------------------------------------

struct VectorType : SizedStorageType {
    static bool classof(const Type *T) {
        return T->kind() == TK_Vector;
    }

    VectorType(const Type *_element_type, size_t _count)
        : SizedStorageType(TK_Vector, _element_type, _count) {
        std::stringstream ss;
        ss << "<" << element_type->name()->data << " x " << count << ">";
        _name = String::from_stdstring(ss.str());
    }
};

static const Type *Vector(const Type *element_type, size_t count) {
    static TypeFactory<VectorType> vectors;
    return vectors.insert(element_type, count);
}

//------------------------------------------------------------------------------
// TUPLE TYPE
//------------------------------------------------------------------------------

struct TupleType : StorageType {
    static bool classof(const Type *T) {
        return T->kind() == TK_Tuple;
    }

    TupleType(const std::vector<Any> &_types)
        : StorageType(TK_Tuple) {
        types.reserve(_types.size());
        for (auto &&arg : _types) {
            types.push_back(arg);
        }
        std::stringstream ss;
        ss << "{";
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) {
                ss << " ";
            }
            ss << types[i]->name()->data;
        }
        ss << "}";
        _name = String::from_stdstring(ss.str());

        offsets.resize(types.size());
        size_t sz = 0;
        size_t al = 1;
        for (size_t i = 0; i < types.size(); ++i) {
            const Type *ET = types[i];
            size_t etal = align_of(ET);
            sz = ::align(sz, etal);
            offsets[i] = sz;
            al = std::max(al, etal);
            sz += size_of(ET);
        }
        size = ::align(sz, al);
        align = al;
    }

    void *getelementptr(void *src, size_t i) const {
        verify_range(i, offsets.size());
        return (void *)((char *)src + offsets[i]);
    }

    Any unpack(void *src, size_t i) const {
        return wrap_pointer(type_at_index(i), getelementptr(src, i));
    }

    const Type *type_at_index(size_t i) const {
        verify_range(i, types.size());
        return types[i];
    }

    std::vector<const Type *> types;
    std::vector<size_t> offsets;
};

static const Type *Tuple(const std::vector<const Type *> &types) {
    static TypeFactory<TupleType> tuples;
    std::vector<Any> atypes;
    atypes.reserve(types.size());
    for (auto &&arg : types) {
        atypes.push_back(arg);
    }
    return tuples.insert(atypes);
}

//------------------------------------------------------------------------------
// UNION TYPE
//------------------------------------------------------------------------------

struct UnionType : StorageType {
    static bool classof(const Type *T) {
        return T->kind() == TK_Union;
    }

    UnionType(const std::vector<Any> &_types)
        : StorageType(TK_Union) {
        types.reserve(_types.size());
        for (auto &&arg : _types) {
            types.push_back(arg);
        }
        std::stringstream ss;
        ss << "{";
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) {
                ss << " | ";
            }
            ss << types[i]->name()->data;
        }
        ss << "}";
        _name = String::from_stdstring(ss.str());

        size_t sz = 0;
        size_t al = 1;
        largest_field = 0;
        for (size_t i = 0; i < types.size(); ++i) {
            const Type *ET = types[i];
            auto newsz = size_of(ET);
            if (newsz > sz) {
                largest_field = i;
                sz = newsz;
            }
            al = std::max(al, align_of(ET));
        }
        size = ::align(sz, al);
        align = al;
    }

    Any unpack(void *src, size_t i) const {
        return wrap_pointer(type_at_index(i), src);
    }

    const Type *type_at_index(size_t i) const {
        verify_range(i, types.size());
        return types[i];
    }

    std::vector<const Type *> types;
    size_t largest_field;
};

static const Type *Union(const std::vector<const Type *> &types) {
    static TypeFactory<UnionType> unions;
    std::vector<Any> atypes;
    atypes.reserve(types.size());
    for (auto &&arg : types) {
        atypes.push_back(arg);
    }
    return unions.insert(atypes);
}

//------------------------------------------------------------------------------
// EXTERN TYPE
//------------------------------------------------------------------------------

struct ExternType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_Extern;
    }

    ExternType(const Type *_type) :
        Type(TK_Extern),
        type(_type) {
        std::stringstream ss;
        ss << "<extern " <<  _type->name()->data << ">";
        _name = String::from_stdstring(ss.str());
    }

    const Type *type;
};

static const Type *Extern(const Type *type) {
    static TypeFactory<ExternType> externs;
    return externs.insert(type);
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

struct FunctionType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_Function;
    }

    FunctionType(
        const Type *_return_type, const Type *_argument_types, uint32_t _flags) :
        Type(TK_Function),
        return_type(_return_type),
        argument_types(llvm::cast<TupleType>(_argument_types)->types),
        flags(_flags) {

        std::stringstream ss;
        ss <<  return_type->name()->data;
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
            ss << argument_types[i]->name()->data;
        }
        if (flags & FF_Variadic) {
            ss << " ...";
        }
        ss << ")";
        _name = String::from_stdstring(ss.str());
    }

    bool vararg() const {
        return flags & FF_Variadic;
    }
    bool pure() const {
        return flags & FF_Pure;
    }

    const Type *type_at_index(size_t i) const {
        verify_range(i, argument_types.size() + 1);
        if (i == 0)
            return return_type;
        else
            return argument_types[i - 1];
    }

    const Type *return_type;
    std::vector<const Type *> argument_types;
    uint32_t flags;
};

static const Type *Function(const Type *return_type,
    const std::vector<const Type *> &argument_types, uint32_t flags = 0) {
    static TypeFactory<FunctionType> functions;
    return functions.insert(return_type, Tuple(argument_types), flags);
}

static bool is_function_pointer(const Type *type) {
    switch (type->kind()) {
    case TK_Pointer: {
        const PointerType *ptype = cast<PointerType>(type);
        return isa<FunctionType>(ptype->element_type);
    } break;
    case TK_Extern: {
        const ExternType *etype = cast<ExternType>(type);
        return isa<FunctionType>(etype->type);
    } break;
    default: return false;
    }
}

static bool is_pure_function_pointer(const Type *type) {
    const PointerType *ptype = dyn_cast<PointerType>(type);
    if (!ptype) return false;
    const FunctionType *ftype = dyn_cast<FunctionType>(ptype->element_type);
    if (!ftype) return false;
    return ftype->flags & FF_Pure;
}

static const FunctionType *extract_function_type(const Type *T) {
    switch(T->kind()) {
    case TK_Extern: {
        auto et = cast<ExternType>(T);
        return cast<FunctionType>(et->type);
    } break;
    case TK_Pointer: {
        auto pi = cast<PointerType>(T);
        return cast<FunctionType>(pi->element_type);
    } break;
    default: assert(false && "unexpected function type");
        return nullptr;
    }
}

//------------------------------------------------------------------------------
// CONSTANT TYPE
//------------------------------------------------------------------------------

struct ConstantType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_Constant;
    }

    ConstantType(const Any &_value) :
        Type(TK_Constant), value(_value) {
        StyledString ss;
        ss.out << "!" << value;
        _name = ss.str();
    }

    Any value;
};

static const Type *Constant(const Any &value) {
    static TypeFactory<ConstantType> constants;
    return constants.insert(value);
}

//------------------------------------------------------------------------------
// TYPENAME
//------------------------------------------------------------------------------

struct TypenameType : Type {
    static std::unordered_set<Symbol, Symbol::Hash> used_names;

    static bool classof(const Type *T) {
        return T->kind() == TK_Typename;
    }

    size_t field_index(Symbol name) const {
        for (size_t i = 0; i < field_names.size(); ++i) {
            if (name == field_names[i])
                return i;
        }
        return (size_t)-1;
    }

    Symbol field_name(size_t i) const {
        verify_range(i, field_names.size());
        return field_names[i];
    }

    TypenameType(const String *name)
        : Type(TK_Typename), storage_type(nullptr), super_type(nullptr) {
        auto ss = StyledString::plain();
        name->stream(ss.out, " *");
        const String *newstr = ss.str();

        auto newname = Symbol(newstr);
        size_t idx = 2;
        while (used_names.count(newname)) {
            // keep testing until we hit a name that's free
            auto ss = StyledString::plain();
            ss.out << newstr->data << "$" << idx++;
            newname = Symbol(ss.str());
        }
        used_names.insert(newname);
        _name = newname.name();
    }

    void finalize(const Type *_type) {
        if (finalized()) {
            location_error(String::from("typename is already final"));
        }
        if (isa<TypenameType>(_type)) {
            location_error(String::from("cannot use typename as storage type"));
        } else if (isa<ConstantType>(_type)) {
            location_error(String::from("cannot use constant as storage type"));
        }
        storage_type = _type;
    }

    bool finalized() const { return storage_type != nullptr; }

    const Type *super() const {
        if (!super_type) return TYPE_Typename;
        return super_type;
    }

    const Type *storage_type;
    const Type *super_type;
    std::vector<Symbol> field_names;
};

std::unordered_set<Symbol, Symbol::Hash> TypenameType::used_names;

// always generates a new type
static const Type *Typename(const String *name) {
    return new TypenameType(name);
}

static const Type *storage_type(const Type *T) {
    const TypenameType *tt = dyn_cast<TypenameType>(T);
    if (!tt) return T;
    if (!tt->finalized()) {
        StyledString ss;
        ss.out << "type " << T << " is opaque";
        location_error(ss.str());
    }
    return tt->storage_type;
}

//------------------------------------------------------------------------------
// TYPED LABEL TYPE
//------------------------------------------------------------------------------

static const Type *TypedLabel(const std::vector<const Type *> &types);

struct TypedLabelType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_TypedLabel;
    }

    TypedLabelType(const std::vector<Any> &_types)
        : Type(TK_TypedLabel) {
        types.reserve(_types.size());
        for (auto &&arg : _types) {
            types.push_back(arg);
        }

        std::stringstream ss;
        ss << "λ(";
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) {
                ss << " ";
            }
            ss << types[i]->name()->data;
        }
        ss << ")";
        _name = String::from_stdstring(ss.str());

    }

    bool has_constants() const {
        for (size_t i = 0; i < types.size(); ++i) {
            if (isa<ConstantType>(types[i]))
                return true;
        }
        return false;
    }

    const Type *strip_constants() const {
        std::vector<const Type *> dest_types;
        dest_types.reserve(types.size());
        for (size_t i = 0; i < types.size(); ++i) {
            const Type *T = types[i];
            const ConstantType *ct = dyn_cast<ConstantType>(T);
            if (ct) {
                T = ct->value.type;
            }
            dest_types.push_back(T);
        }
        return TypedLabel(dest_types);
    }

    std::vector<const Type *> types;
};

static const Type *TypedLabel(const std::vector<const Type *> &types) {
    static TypeFactory<TypedLabelType> typed_labels;
    assert(!types.empty());
    std::vector<Any> atypes;
    atypes.reserve(types.size());
    for (auto &&arg : types) {
        atypes.push_back(arg);
    }
    return typed_labels.insert(atypes);
}

//------------------------------------------------------------------------------
// TYPESET TYPE
//------------------------------------------------------------------------------

struct TypeSetType : Type {
    static bool classof(const Type *T) {
        return T->kind() == TK_TypeSet;
    }

    TypeSetType(const std::vector<Any> &_types)
        : Type(TK_TypeSet) {
        types.reserve(_types.size());
        for (auto &&arg : _types) {
            types.push_back(arg);
        }

        std::stringstream ss;
        ss << "<";
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) {
                ss << " | ";
            }
            ss << types[i]->name()->data;
        }
        ss << ">";
        _name = String::from_stdstring(ss.str());
    }

    std::vector<const Type *> types;
};

static const Type *TypeSet(const std::vector<const Type *> &types) {
    static TypeFactory<TypeSetType> typesets;

    std::unordered_set<const Type *> typeset;

    for (auto &&entry : types) {
        const TypeSetType *ts = dyn_cast<TypeSetType>(entry);
        if (ts) {
            for (auto &&t : ts->types) {
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

    std::vector<const Type *> type_array;
    type_array.reserve(typeset.size());
    for (auto &&entry : typeset) {
        type_array.push_back(entry);
    }

    std::sort(type_array.begin(), type_array.end());

    std::vector<Any> atypes;
    atypes.reserve(type_array.size());
    for (auto &&arg : type_array) {
        atypes.push_back(arg);
    }
    return typesets.insert(atypes);
}

//------------------------------------------------------------------------------
// TYPE INQUIRIES
//------------------------------------------------------------------------------

template<TypeKind tk>
static void verify_kind(const Type *T) {
    if (T->kind() != tk) {
        StyledString ss;
        switch(tk) {
        case TK_Integer: ss.out << "integer"; break;
        case TK_Real: ss.out << "real"; break;
        case TK_Pointer: ss.out << "pointer"; break;
        case TK_Array: ss.out << "array"; break;
        case TK_Vector: ss.out << "vector"; break;
        case TK_Tuple: ss.out << "tuple"; break;
        case TK_Union: ss.out << "union"; break;
        case TK_Typename: ss.out << "typename"; break;
        case TK_TypedLabel: ss.out << "typed label"; break;
        case TK_TypeSet: ss.out << "typeset"; break;
        case TK_Function: ss.out << "function"; break;
        case TK_Constant: ss.out << "constant"; break;
        case TK_Extern: ss.out << "extern"; break;
        }
        ss.out << " expected, got " << T;
        location_error(ss.str());
    }
}

static void verify_function_pointer(const Type *type) {
    if (!is_function_pointer(type)) {
        StyledString ss;
        ss.out << "function pointer expected, got " << type;
        location_error(ss.str());
    }
}

static bool is_opaque(const Type *T) {
    const TypenameType *tt = dyn_cast<TypenameType>(T);
    if (tt) {
        if (!tt->finalized()) {
            return true;
        } else {
            T = tt->storage_type;
        }
    }
    switch(T->kind()) {
    case TK_TypedLabel:
    case TK_TypeSet:
    case TK_Constant:
    case TK_Function: return true;
    default: break;
    }
    return false;
}

static size_t size_of(const Type *T) {
    switch(T->kind()) {
    case TK_Integer: {
        const IntegerType *it = cast<IntegerType>(T);
        return (it->width + 7) / 8;
    }
    case TK_Real: {
        const RealType *rt = cast<RealType>(T);
        return (rt->width + 7) / 8;
    }
    case TK_Extern:
    case TK_Pointer: return PointerType::size();
    case TK_Array: return cast<ArrayType>(T)->size;
    case TK_Vector: return cast<VectorType>(T)->size;
    case TK_Tuple: return cast<TupleType>(T)->size;
    case TK_Union: return cast<UnionType>(T)->size;
    case TK_Typename: return size_of(storage_type(cast<TypenameType>(T)));
    case TK_Constant: return size_of(cast<ConstantType>(T)->value.type);
    default: break;
    }

    StyledString ss;
    ss.out << "opaque type " << T << " has no size";
    location_error(ss.str());
    return -1;
}

static size_t align_of(const Type *T) {
    switch(T->kind()) {
    case TK_Integer: {
        const IntegerType *it = cast<IntegerType>(T);
        return (it->width + 7) / 8;
    }
    case TK_Real: {
        const RealType *rt = cast<RealType>(T);
        return (rt->width + 7) / 8;
    }
    case TK_Extern:
    case TK_Pointer: return PointerType::size();
    case TK_Array: return cast<ArrayType>(T)->align;
    case TK_Vector: return cast<VectorType>(T)->align;
    case TK_Tuple: return cast<TupleType>(T)->align;
    case TK_Union: return cast<UnionType>(T)->align;
    case TK_Typename: return align_of(storage_type(cast<TypenameType>(T)));
    case TK_Constant: return align_of(cast<ConstantType>(T)->value.type);
    default: break;
    }

    StyledString ss;
    ss.out << "opaque type " << T << " has no alignment";
    location_error(ss.str());
    return 1;
}

static Any wrap_pointer(const Type *type, void *ptr) {
    Any result = none;
    result.type = type;

    type = storage_type(type);
    switch(type->kind()) {
    case TK_Integer:
    case TK_Real:
    case TK_Pointer:
        memcpy(result.content, ptr, size_of(type));
        return result;
    case TK_Array:
    case TK_Vector:
    case TK_Tuple:
    case TK_Union:
        result.pointer = ptr;
        return result;
    default: break;
    }

    StyledString ss;
    ss.out << "cannot wrap data of type " << type;
    location_error(ss.str());
    return none;
}


void *get_pointer(const Type *type, Any &value, bool create = false) {
    if (type == TYPE_Void) {
        return value.content;
    }
    switch(type->kind()) {
    case TK_Integer: {
        auto it = cast<IntegerType>(type);
        switch(it->width) {
        case 1: return (void *)&value.i1;
        case 8: return (void *)&value.u8;
        case 16: return (void *)&value.u16;
        case 32: return (void *)&value.u32;
        case 64: return (void *)&value.u64;
        default: break;
        }
    } break;
    case TK_Real: {
        auto rt = cast<RealType>(type);
        switch(rt->width) {
        case 32: return (void *)&value.f32;
        case 64: return (void *)&value.f64;
        default: break;
        }
    } break;
    case TK_Pointer: return (void *)&value.pointer;
    case TK_Typename: {
        return get_pointer(storage_type(type), value, create);
    } break;
    case TK_Array:
    case TK_Vector:
    case TK_Tuple:
    case TK_Union:
        if (create) {
            value.pointer = malloc(size_of(type));
        }
        return value.pointer;
    default: break;
    };

    StyledString ss;
    ss.out << "cannot extract pointer from type " << type;
    location_error(ss.str());
    return nullptr;
}

static const Type *superof(const Type *T) {
    switch(T->kind()) {
    case TK_Integer: return TYPE_Integer;
    case TK_Real: return TYPE_Real;
    case TK_Pointer: return TYPE_Pointer;
    case TK_Array: return TYPE_Array;
    case TK_Vector: return TYPE_Vector;
    case TK_Tuple: return TYPE_Tuple;
    case TK_Union: return TYPE_Union;
    case TK_Typename: return cast<TypenameType>(T)->super();
    case TK_TypedLabel: return TYPE_TypedLabel;
    case TK_TypeSet: return TYPE_TypeSet;
    case TK_Function: return TYPE_Function;
    case TK_Constant: return TYPE_Constant;
    case TK_Extern: return TYPE_Extern;
    }
    assert(false && "unhandled type kind");    
    return nullptr;
}

//------------------------------------------------------------------------------
// ANY METHODS
//------------------------------------------------------------------------------

StyledStream& Any::stream(StyledStream& ost, bool annotate_type) const {
    AnyStreamer as(ost, type, annotate_type);
    if (type == TYPE_Nothing) { as.naked(none); }
    else if (type == TYPE_Type) { as.naked(typeref); }
    else if (type == TYPE_Bool) { as.naked(i1); }
    else if (type == TYPE_I8) { as.typed(i8); }
    else if (type == TYPE_I16) { as.typed(i16); }
    else if (type == TYPE_I32) { as.naked(i32); }
    else if (type == TYPE_I64) { as.typed(i64); }
    else if (type == TYPE_U8) { as.typed(u8); }
    else if (type == TYPE_U16) { as.typed(u16); }
    else if (type == TYPE_U32) { as.typed(u32); }
    else if (type == TYPE_U64) { as.typed(u64); }
    else if (type == TYPE_USize) { as.typed(u64); }
    else if (type == TYPE_F32) { as.naked(f32); }
    else if (type == TYPE_F64) { as.typed(f64); }
    else if (type == TYPE_String) { as.naked(string); }
    else if (type == TYPE_Symbol) { as.naked(symbol); }
    else if (type == TYPE_Syntax) { as.naked(syntax); }
    else if (type == TYPE_Anchor) { as.typed(anchor); }
    else if (type == TYPE_List) { as.naked(list); }
    else if (type == TYPE_Builtin) { as.typed(builtin); }
    else if (type == TYPE_Label) { as.typed(label); }
    else if (type == TYPE_Parameter) { as.typed(parameter); }
    else if (type == TYPE_Scope) { as.typed(scope); }
    else if (type == TYPE_Ref) {
        ost << Style_Operator << "[" << Style_None;
        ref->stream(ost);
        ost << Style_Operator << "]" << Style_None;
        as.stream_type_suffix();
    } else if (type->kind() == TK_Extern) {
        ost << symbol;
        as.stream_type_suffix();                
    } else { as.typed(pointer); }
    return ost;
}

size_t Any::hash() const {
    if (type == TYPE_String) {
        return CityHash64(string->data, string->count);
    }
    if (is_opaque(type))
        return 0;
    const Type *T = storage_type(type);
    switch(T->kind()) {
    case TK_Integer: {
        switch(cast<IntegerType>(T)->width) {
        case 1: return std::hash<bool>{}(i1);
        case 8: return std::hash<uint8_t>{}(u8);
        case 16: return std::hash<uint16_t>{}(u16);
        case 32: return std::hash<uint32_t>{}(u32);
        case 64: return std::hash<uint64_t>{}(u64);
        default: break;
        }
    } break;
    case TK_Real: {
        switch(cast<RealType>(T)->width) {
        case 32: return std::hash<float>{}(f32);
        case 64: return std::hash<double>{}(f64);
        default: break;
        }
    } break;
    case TK_Extern: {
        return std::hash<uint64_t>{}(u64);
    } break;
    case TK_Pointer: return std::hash<void *>{}(pointer);
    case TK_Array: {
        auto ai = cast<ArrayType>(T);
        size_t h = 0;
        for (size_t i = 0; i < ai->count; ++i) {
            h = HashLen16(h, ai->unpack(pointer, i).hash());
        }
        return h;
    } break;
    case TK_Vector: {
        auto vi = cast<VectorType>(T);
        size_t h = 0;
        for (size_t i = 0; i < vi->count; ++i) {
            h = HashLen16(h, vi->unpack(pointer, i).hash());
        }
        return h;
    } break;
    case TK_Tuple: {
        auto ti = cast<TupleType>(T);
        size_t h = 0;
        for (size_t i = 0; i < ti->types.size(); ++i) {
            h = HashLen16(h, ti->unpack(pointer, i).hash());
        }
        return h;
    } break;
    case TK_Union:
        return CityHash64((const char *)pointer, size_of(T));
    default: break;
    }

    StyledStream ss(std::cout);
    ss << "unhashable value: " << T << std::endl;
    assert(false && "unhashable value");
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
    const Type *T = storage_type(type);
    switch(T->kind()) {
    case TK_Integer: {
        switch(cast<IntegerType>(T)->width) {
        case 1: return (i1 == other.i1);
        case 8: return (u8 == other.u8);
        case 16: return (u16 == other.u16);
        case 32: return (u32 == other.u32);
        case 64: return (u64 == other.u64);
        default: break;
        }
    } break;
    case TK_Real: {
        switch(cast<RealType>(T)->width) {
        case 32: return (f32 == other.f32);
        case 64: return (f64 == other.f64);
        default: break;
        }
    } break;
    case TK_Extern: return symbol == other.symbol;
    case TK_Pointer: return pointer == other.pointer;
    case TK_Array: {
        auto ai = cast<ArrayType>(T);
        for (size_t i = 0; i < ai->count; ++i) {
            if (ai->unpack(pointer, i) != ai->unpack(other.pointer, i))
                return false;
        }
        return true;
    } break;
    case TK_Vector: {
        auto vi = cast<VectorType>(T);
        for (size_t i = 0; i < vi->count; ++i) {
            if (vi->unpack(pointer, i) != vi->unpack(other.pointer, i))
                return false;
        }
        return true;
    } break;
    case TK_Tuple: {
        auto ti = cast<TupleType>(T);
        for (size_t i = 0; i < ti->types.size(); ++i) {
            if (ti->unpack(pointer, i) != ti->unpack(other.pointer, i))
                return false;
        }
        return true;
    } break;
    case TK_Union:
        return !memcmp(pointer, other.pointer, size_of(T));
    default: break;
    }

    StyledStream ss(std::cout);
    ss << "incomparable value: " << T << std::endl;
    assert(false && "incomparable value");
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

    Exception() : 
        anchor(nullptr),
        msg(nullptr) {}

    Exception(const Anchor *_anchor, const String *_msg) :
        anchor(_anchor),
        msg(_msg) {}
};

struct ExceptionPad {
    jmp_buf retaddr;
    Any value;

    ExceptionPad() : value(none) {
    }

    void invoke(const Any &value) {
        this->value = value;
        longjmp(retaddr, 1);
    }
};

#ifdef SCOPES_WIN32
#define SCOPES_TRY() \
    ExceptionPad exc_pad; \
    ExceptionPad *_last_exc_pad = _exc_pad; \
    _exc_pad = &exc_pad; \
    if (!_setjmpex(exc_pad.retaddr, nullptr)) {
#else
#define SCOPES_TRY() \
    ExceptionPad exc_pad; \
    ExceptionPad *_last_exc_pad = _exc_pad; \
    _exc_pad = &exc_pad; \
    if (!setjmp(exc_pad.retaddr)) {
#endif

#define SCOPES_CATCH(EXCNAME) \
        _exc_pad = _last_exc_pad; \
    } else { \
        _exc_pad = _last_exc_pad; \
        auto &&EXCNAME = exc_pad.value;

#define SCOPES_TRY_END() \
    }

static ExceptionPad *_exc_pad = nullptr;

static void default_exception_handler(const Any &value);

static void error(const Any &value) {
    if (!_exc_pad) {
        default_exception_handler(value);
    } else {
        _exc_pad->invoke(value);
    }
}

static void location_error(const String *msg) {
    const Exception *exc = new Exception(_active_anchor, msg);
    error(exc);
}

//------------------------------------------------------------------------------
// SCOPE
//------------------------------------------------------------------------------

struct Scope {
protected:
    Scope(Scope *_parent = nullptr) : parent(_parent) {}

public:
    std::unordered_map<Any, Any, Any::Hash> map;
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

    void bind(KnownSymbol name, const Any &value) {
        bind(Symbol(name), value);
    }

    void bind(const Any &name, const Any &value) {
        auto ret = map.insert(std::pair<Any, Any>(name, value));
        if (!ret.second) {
            ret.first->second = value;
        }
    }

    void del(const Any &name) {
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
                if (k.first.type != TYPE_Symbol)
                    continue;
                Symbol sym = k.first.symbol;
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

    bool lookup(const Any &name, Any &dest) const {
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

    bool lookup_local(const Any &name, Any &dest) const {
        auto it = map.find(name);
        if (it != map.end()) {
            dest = it->second;
            return true;
        }
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

    static const Syntax *from(const Anchor *_anchor, const Any &_datum, bool quoted = false) {
        assert(_anchor);
        return new Syntax(_anchor, _datum, quoted);
    }

    static const Syntax *from_quoted(const Anchor *_anchor, const Any &_datum) {
        assert(_anchor);
        return new Syntax(_anchor, _datum, true);
    }
};

static Any unsyntax(const Any &e) {
    e.verify(TYPE_Syntax);
    return e.syntax->datum;
}

static Any maybe_unsyntax(const Any &e) {
    if (e.type == TYPE_Syntax) {
        return e.syntax->datum;
    } else {
        return e;
    }
}

static Any strip_syntax(Any e) {
    e = maybe_unsyntax(e);
    if (e.type == TYPE_List) {
        auto src = e.list;
        auto l = src;
        bool needs_unwrap = false;
        while (l != EOL) {
            if (l->at.type == TYPE_Syntax) {
                needs_unwrap = true;
                break;
            }
            l = l->next;
        }
        if (needs_unwrap) {
            l = src;
            const List *dst = EOL;
            while (l != EOL) {
                dst = List::from(strip_syntax(l->at), dst);
                l = l->next;
            }
            return reverse_list_inplace(dst);
        }
    }
    return e;
}

static Any wrap_syntax(const Anchor *anchor, Any e, bool quoted = false) {
    if (e.type == TYPE_List) {
        auto src = e.list;
        auto l = src;
        bool needs_wrap = false;
        while (l != EOL) {
            if (l->at.type != TYPE_Syntax) {
                needs_wrap = true;
                break;
            }
            l = l->next;
        }
        if (needs_wrap) {
            l = src;
            const List *dst = EOL;
            while (l != EOL) {
                dst = List::from(wrap_syntax(anchor, l->at, quoted), dst);
                l = l->next;
            }
            l = reverse_list_inplace(dst);
        }
        return Syntax::from(anchor, l, quoted);
    } else if (e.type != TYPE_Syntax) {
        return Syntax::from(anchor, e, quoted);
    }
    return e;
}

static StyledStream& operator<<(StyledStream& ost, const Syntax *value) {
    ost << value->anchor << value->datum;
    return ost;
}

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

    LexerParser(SourceFile *_file, size_t offset = 0, size_t length = 0) :
            value(none) {
        file = _file;
        input_stream = file->strptr() + offset;
        token = tok_eof;
        base_offset = (int)offset;
        if (length) {
            eof = input_stream + length;
        } else {
            eof = file->strptr() + file->length;
        }
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
            auto _lineno = lineno; auto _line = line; auto _cursor = cursor;
            next_token();
            read_symbol();
            lineno = _lineno; line = _line; cursor = _cursor;
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
            auto _lineno = lineno; auto _line = line; auto _cursor = cursor;
            next_token();
            read_symbol();
            lineno = _lineno; line = _line; cursor = _cursor;
            return RN_Typed;
        } else {
            return RN_Untyped;
        }
    }

    bool has_suffix() const {
        return (string_len >= 1) && (string[0] == ':');
    }

    bool select_integer_suffix() {
        if (!has_suffix())
            return false;
        if (is_suffix(":i8")) { value = Any(value.i8); return true; }
        else if (is_suffix(":i16")) { value = Any(value.i16); return true; }
        else if (is_suffix(":i32")) { value = Any(value.i32); return true; }
        else if (is_suffix(":i64")) { value = Any(value.i64); return true; }
        else if (is_suffix(":u8")) { value = Any(value.u8); return true; }
        else if (is_suffix(":u16")) { value = Any(value.u16); return true; }
        else if (is_suffix(":u32")) { value = Any(value.u32); return true; }
        else if (is_suffix(":u64")) { value = Any(value.u64); return true; }
        //else if (is_suffix(":isize")) { value = Any(value.i64); return true; }
        else if (is_suffix(":usize")) { value = Any(value.u64); value.type = TYPE_USize; return true; }
        else {
            StyledString ss;
            ss.out << "invalid suffix for integer literal: "
                << String::from(string, string_len);
            location_error(ss.str());
            return false;
        }
    }

    bool select_real_suffix() {
        if (!has_suffix())
            return false;
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
        switch(read_integer(scopes_strtoll)) {
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
        switch(read_integer(scopes_strtoull)) {
        case RN_Invalid: return false;
        case RN_Untyped:
            return true;
        case RN_Typed:
            return select_integer_suffix();
        default: assert(false); return false;
        }
    }
    bool read_real64() {
        switch(read_real(scopes_strtod)) {
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
    Any parse_any() {
        assert(this->token != tok_eof);
        const Anchor *anchor = this->anchor();
        if (this->token == tok_open) {
            return Syntax::from(anchor, parse_list(tok_close));
        } else if (this->token == tok_square_open) {
            return Syntax::from(anchor,
                List::from(Symbol(SYM_SquareList),
                    parse_list(tok_square_close)));
        } else if (this->token == tok_curly_open) {
            return Syntax::from(anchor,
                List::from(Symbol(SYM_CurlyList),
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
        } else if (this->token == tok_quote) {
            this->read_token();
            return Syntax::from(anchor,
                List::from({ 
                    Syntax::from(anchor, Symbol(KW_Quote)), 
                    parse_any() }));
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

        bool unwrap_single = true;
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
                unwrap_single = false;
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

        auto result = builder.get_result();
        if (unwrap_single && result && result->count == 1) {
            return result->at;
        } else {
            return Syntax::from(anchor, result);
        }
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
    /*
    // TODO
    else if ((val >= TYPE_FIRST) && (val <= TYPE_LAST))
        return Style_Type;*/
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

    static StreamExprFormat debug() {
        auto fmt = StreamExprFormat();
        fmt.naked = true;
        fmt.anchors = All;
        return fmt;
    }

    static StreamExprFormat debug_digest() {
        auto fmt = StreamExprFormat();
        fmt.naked = true;
        fmt.anchors = Line;
        fmt.maxdepth = 5;
        fmt.maxlength = 5;
        return fmt;
    }

    static StreamExprFormat debug_singleline() {
        auto fmt = StreamExprFormat();
        fmt.naked = false;
        fmt.anchors = All;
        return fmt;
    }

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
                if ((q.type == TYPE_Symbol)
                    ||(q.type == TYPE_String)
                    ||(q.type == TYPE_I32)
                    ||(q.type == TYPE_F32)) {
                    return true;
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

enum ParameterKind {
    PK_Regular = 0,
    PK_Variadic = 1,
};

struct Parameter : ILNode {
protected:
    Parameter(const Anchor *_anchor, Symbol _name, const Type *_type, ParameterKind _kind) :
        anchor(_anchor), name(_name), type(_type), label(nullptr), index(-1),
        kind(_kind) {}

public:
    const Anchor *anchor;
    Symbol name;
    const Type *type;
    Label *label;
    int index;
    ParameterKind kind;

    bool is_vararg() const {
        return (kind == PK_Variadic);
    }

    bool is_typed() const {
        return type != TYPE_Void;
    }

    StyledStream &stream_local(StyledStream &ss) const {
        if ((name != SYM_Unnamed) || !label) {
            ss << Style_Symbol;
            name.name()->stream(ss, SYMBOL_ESCAPE_CHARS);
            ss << Style_None;
        } else {
            ss << Style_Operator << "@" << Style_None << index;
        }
        if (is_vararg()) {
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
            _param->anchor, _param->name, _param->type, _param->kind);
    }

    static Parameter *from(const Anchor *_anchor, Symbol _name, const Type *_type) {
        return new Parameter(_anchor, _name, _type, PK_Regular);
    }

    static Parameter *vararg_from(const Anchor *_anchor, Symbol _name, const Type *_type) {
        return new Parameter(_anchor, _name, _type, PK_Variadic);
    }
};

void Any::verify_indirect(const Type *T) const {
    scopes::verify(T, indirect_type());
}

const Type *Any::indirect_type() const {
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

enum LabelBodyFlags {
    LBF_RawCall = (1 << 0)
};

struct KeyAny {
    Symbol key;
    Any value;

    KeyAny() : key(SYM_Unnamed), value(none) {}
    KeyAny(Any _value) : key(SYM_Unnamed), value(_value) {}
    KeyAny(Symbol _key, Any _value) : key(_key), value(_value) {}
    template<typename T>
    KeyAny(const T &x) : key(SYM_Unnamed), value(x) {}

    bool operator ==(const KeyAny &other) const {
        return (key == other.key) && (value == other.value);
    }

    bool operator !=(const KeyAny &other) const {
        return (key != other.key) || (value != other.value);
    }

    uint64_t hash() const {
        return HashLen16(std::hash<uint64_t>{}(key.value()), value.hash());
    }
};

typedef std::vector<KeyAny> Args;

struct Body {
    const Anchor *anchor;
    Any enter;
    Args args;
    uint64_t flags;    

    Body() : anchor(nullptr), enter(none), flags(0) {}

    bool is_rawcall() {
        return (flags & LBF_RawCall) == LBF_RawCall;
    }
    
    void set_rawcall(bool enable = true) {
        if (enable) {
            flags |= LBF_RawCall;
        } else {
            flags &= ~LBF_RawCall;
        }
    }
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
        uid(++next_uid), original(nullptr), anchor(_anchor), name(_name),
        paired(nullptr), num_instances(0)
        {}

public:
    size_t uid;
    Label *original;
    const Anchor *anchor;
    Symbol name;
    std::vector<Parameter *> params;
    Body body;
    LabelTag tag;
    Label *paired;
    uint64_t num_instances;
    // if return_constants are specified, the continuation must be inlined
    // with these arguments
    std::vector<Any> return_constants;

    Parameter *get_param_by_name(Symbol name) {
        size_t count = params.size();
        for (size_t i = 1; i < count; ++i) {
            if (params[i]->name == name) {
                return params[i];
            }
        }
        return nullptr;
    }

    bool is_complete() {
        return !params.empty() && body.anchor && !body.args.empty();
    }

    void verify_complete () {
        if (!is_complete()) {
            set_active_anchor(anchor);
            location_error(String::from("incomplete function/label"));
        }
    }

    struct Args {
        scopes::Args args;

        bool operator==(const Args &other) const {
            if (args.size() != other.args.size()) return false;
            for (size_t i = 0; i < args.size(); ++i) {
                auto &&a = args[i];
                auto &&b = other.args[i];
                if (a != b)
                    return false;
            }
            return true;
        }

        struct Hash {
            std::size_t operator()(const Args& s) const {
                std::size_t h = 0;
                for (auto &&arg : s.args) {
                    h = HashLen16(h, arg.hash());
                }
                return h;
            }
        };

    };

    // inlined instances of this label
    std::unordered_map<Args, Label *, Args::Hash> instances;


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
        assert(body.args[0].value.type == TYPE_Label);
        return body.args[0].value.label;
    }

    const Type *get_return_type() const {
        if (params[0]->type == TYPE_Void)
            return TYPE_Void;

        std::vector<const Type *> rettypes;
        auto tl = cast<TypedLabelType>(params[0]->type);
        for (size_t i = 1; i < tl->types.size(); ++i) {
            rettypes.push_back(tl->types[i]);
        }

        const Type *rtype = TYPE_Void;
        if (rettypes.size() == 1) {
            rtype = rettypes[0];
        } else if (!rettypes.empty()) {
            rtype = Tuple(rettypes);
        }
        return rtype;
    }

    void verify_compilable() const {
        if ((params[0]->type != TYPE_Void)
            && (params[0]->type != TYPE_Nothing)) {
            auto tl = dyn_cast<TypedLabelType>(params[0]->type);
            if (!tl) {
                set_active_anchor(anchor);
                StyledString ss;
                ss.out << "cannot compile function with complex continuation type " 
                    << params[0]->type;
                location_error(ss.str());
            }
            for (size_t i = 1; i < tl->types.size(); ++i) {
                auto T = tl->types[i];
                if (is_opaque(T)) {
                    set_active_anchor(anchor);
                    StyledString ss;
                    ss.out << "cannot compile function with opaque return argument of type " 
                        << T;
                    location_error(ss.str());
                }
            }
        }

        std::vector<const Type *> argtypes;
        for (size_t i = 1; i < params.size(); ++i) {
            auto T = params[i]->type;
            if (T == TYPE_Void) {
                set_active_anchor(anchor);
                location_error(String::from("cannot compile function with untyped argument"));
            } else if (is_opaque(T)) {
                set_active_anchor(anchor);
                StyledString ss;
                ss.out << "cannot compile function with opaque argument of type " 
                    << T;
                location_error(ss.str());
            }
        }
    }

    const Type *get_function_type() const {

        std::vector<const Type *> argtypes;
        for (size_t i = 1; i < params.size(); ++i) {
            argtypes.push_back(params[i]->type);
        }

        return Function(get_return_type(), argtypes);
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
            use(body.args[i].value, i);
        }
    }

    void unlink_backrefs() {
        unuse(body.enter, -1);
        size_t count = body.args.size();
        for (size_t i = 0; i < count; ++i) {
            unuse(body.args[i].value, i);
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
                    arg = parent->body.args[i].value;
                }

                if (arg.type == TYPE_Label) {
                    Label *label = arg.label;
                    if (!visited.count(label)) {
                        visited.insert(label);
                        stack.push_back(label);
                    }
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
                    arg = parent->body.args[i].value;
                }

                if (arg.type == TYPE_Label) {
                    Label *label = arg.label;
                    if (!labels.count(label)) {
                        labels.insert(label);
                        stack.push_back(label);
                    }
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
        if (name == SYM_Unnamed) {
            ss << Style_Keyword << "λ" << Style_Symbol << uid;
        } else {
            ss << Style_Symbol;
            name.name()->stream(ss, SYMBOL_ESCAPE_CHARS);
        }
        ss << Style_None;
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
        if (count) {
            const Type *rtype = params[0]->type;
            if (rtype != TYPE_Nothing) {
                ss << Style_Comment << CONT_SEP << Style_None;
                if (rtype == TYPE_Void) {
                    ss << Style_Comment << "?" << Style_None;
                } else {
                    params[0]->stream_local(ss);
                }
            }
            if (users) {
                params[0]->stream_users(ss);
            }
        }
        return ss;
    }

    static Label *from(const Anchor *_anchor, Symbol _name) {
        assert(_anchor);
        return new Label(_anchor, _name);
    }
    // only inherits name and anchor
    static Label *from(Label *label) {
        Label *result = new Label(label->anchor, label->name);
        label->num_instances++;
        result->original = label->original?label->original:label;
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
    ss << Style_Comment << "." << Style_None;
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

    void stream_argument(KeyAny arg, Label *alabel) {
        if (arg.key != SYM_Unnamed) {
            ss << arg.key << Style_Operator << "=" << Style_None;
        }
        if (arg.value.type == TYPE_Parameter) {
            stream_param_label(arg.value.parameter, alabel);
        } else if (arg.value.type == TYPE_Label) {
            stream_label_label(arg.value.label);
        } else if (arg.value.type == TYPE_List) {
            stream_expr(ss, arg.value, StreamExprFormat::singleline_digest());
        } else {
            ss << arg.value;
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
        if (alabel->body.is_rawcall()) {
            ss << Style_Keyword << "rawcall " << Style_None;
        }
        stream_argument(alabel->body.enter, alabel);
        for (size_t i=1; i < alabel->body.args.size(); ++i) {
            ss << " ";
            stream_argument(alabel->body.args[i], alabel);
        }
        if (!alabel->body.args.empty()) {
            auto &&cont = alabel->body.args[0];
            if (cont.value.type != TYPE_Nothing) {
                ss << Style_Comment << CONT_SEP << Style_None;
                stream_argument(cont.value, alabel);
            }
        }
        ss << std::endl;

        if (follow_labels) {
            for (size_t i=0; i < alabel->body.args.size(); ++i) {
                stream_any(alabel->body.args[i].value);
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
                arg = obj->body.args[i].value;
            }

            if (arg.type == TYPE_Label) {
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

typedef std::unordered_map<ILNode *, Args > MangleMap;

static KeyAny first(const Args &values) {
    return values.empty()?KeyAny():values.front();
}

static void mangle_remap_body(Label *ll, Label *entry, MangleMap &map) {
    Any enter = entry->body.enter;
    Args &args = entry->body.args;
    Args &body = ll->body.args;
    if (enter.type == TYPE_Label) {
        auto it = map.find(enter.label);
        if (it != map.end()) {
            enter = first(it->second).value;
        }
    } else if (enter.type == TYPE_Parameter) {
        auto it = map.find(enter.parameter);
        if (it != map.end()) {
            enter = first(it->second).value;
        }
    }
    ll->body.flags = entry->body.flags;
    ll->body.anchor = entry->body.anchor;
    ll->body.enter = enter;

    StyledStream ss(std::cout);
    size_t lasti = (args.size() - 1);
    for (size_t i = 0; i < args.size(); ++i) {
        KeyAny arg = args[i];
        if (arg.value.type == TYPE_Label) {
            auto it = map.find(arg.value.label);
            if (it != map.end()) {
                arg.value = first(it->second).value;
            }
        } else if (arg.value.type == TYPE_Parameter) {
            auto it = map.find(arg.value.parameter);
            if (it != map.end()) {
                if ((i == lasti) && arg.value.parameter->is_vararg()) {
                    for (auto subit = it->second.begin(); subit != it->second.end(); ++subit) {
                        body.push_back(*subit);
                    }
                    continue;
                } else {
                    arg.value = first(it->second).value;
                }
            }
        }
        body.push_back(arg);
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
        map.insert(MangleMap::value_type(l, {KeyAny(Any(ll))}));
        ll->params.reserve(l->params.size());
        for (auto &&param : l->params) {
            Parameter *pparam = Parameter::from(param);
            map.insert(MangleMap::value_type(param, {KeyAny(Any(pparam))}));
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
        stream_label(ss, it->second.front().value, StreamLabelFormat::debug_single());
    }
    ss << "]OUT\n";
    }

    return le;
}

static void verify_instance_count(Label *label) {
    if (label->num_instances < SCOPES_MAX_LABEL_INSTANCES)
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

static Any unknown_of(const Type *T) {
    Any result(T);
    result.type = TYPE_Unknown;
    return result;
}

// inlining the arguments of an untyped scope (including continuation)
// folds arguments and types parameters
// arguments are treated as follows:
// TYPE_Void = leave the parameter as-is
// TYPE_Unknown = type the parameter
// any other = inline the argument and remove the parameter
static Label *fold_type_label(Label *label, const Args &args) {
#if 0
    ss_cout << "inline-arguments " << label << ":";
    for (size_t i = 0; i < args.size(); ++i) {
        ss_cout << " " << args[i];
    }
    ss_cout << std::endl;
#endif

    Label::Args la;
    la.args = args;
    auto &&instances = label->instances;
    auto it = instances.find(la);
    if (it != instances.end())
        return it->second;
    assert(!label->params.empty());

    verify_instance_count(label);

    MangleMap map;
    std::vector<Parameter *> newparams;
    size_t lasti = label->params.size() - 1;
    size_t srci = 0;
    for (size_t i = 0; i < label->params.size(); ++i) {
        Parameter *param = label->params[i];
        if (param->is_vararg()) {
            assert(i == lasti);
            size_t ncount = args.size();
            if (srci < ncount) {
                ncount -= srci;
                Args vargs;
                for (size_t k = 0; k < ncount; ++k) {
                    KeyAny value = args[srci + k];
                    if ((value.value.type == TYPE_Void)
                        || (value.value.type == TYPE_Unknown)) {
                        Parameter *newparam = Parameter::from(param);
                        newparam->kind = PK_Regular;
                        newparam->type =
                            (value.value.type == TYPE_Unknown)?value.value.typeref:TYPE_Void;
                        newparam->name = Symbol(SYM_Unnamed);
                        newparams.push_back(newparam);
                        vargs.push_back(KeyAny(value.key, newparam));
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
            KeyAny value = args[srci];
            if ((value.value.type == TYPE_Void)
                || (value.value.type == TYPE_Unknown)) {
                Parameter *newparam = Parameter::from(param);
                if (value.value.type == TYPE_Unknown) {
                    if (newparam->is_typed()
                        && (newparam->type != value.value.typeref)) {
                        StyledString ss;
                        ss.out << "attempting to retype parameter of type "
                            << newparam->type << " as " << value.value.typeref;
                        location_error(ss.str());
                    } else {
                        newparam->type = value.value.typeref;
                    }
                }
                newparams.push_back(newparam);
                map[param] = {KeyAny(value.key, newparam)};
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
            map[param] = {KeyAny()};
            srci++;
        }
    }
    Label *newlabel = mangle(label, newparams, map);//, Mangle_Verbose);
    instances.insert({la, newlabel});
    return newlabel;
}

typedef std::vector<const Type *> ArgTypes;

static Label *typify(Label *label, const ArgTypes &argtypes) {
    assert(!argtypes.empty());
    assert(!label->params.empty());

    Any voidval = none;
    voidval.type = TYPE_Void;

    Args args;
    args.reserve(argtypes.size());
    args = { KeyAny(voidval) };
    for (size_t i = 1; i < argtypes.size(); ++i) {
        args.push_back(KeyAny(unknown_of(argtypes[i])));
    }

    return fold_type_label(label, args);
}

//------------------------------------------------------------------------------
// C BRIDGE (CLANG)
//------------------------------------------------------------------------------

class CVisitor : public clang::RecursiveASTVisitor<CVisitor> {
public:

    typedef std::unordered_map<Symbol, const Type *, Symbol::Hash> NamespaceMap;

    Scope *dest;
    clang::ASTContext *Context;
    std::unordered_map<clang::RecordDecl *, bool> record_defined;
    std::unordered_map<clang::EnumDecl *, bool> enum_defined;
    NamespaceMap named_structs;
    NamespaceMap named_unions;
    NamespaceMap named_enums;
    NamespaceMap typedefs;

    CVisitor() : dest(nullptr), Context(NULL) {
        typedefs.insert({Symbol("__builtin_va_list"), 
            Typename(String::from("__builtin_va_list")) });
    }

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

    void GetFields(TypenameType *tni, clang::RecordDecl * rd) {
        //auto &rl = Context->getASTRecordLayout(rd);

        if (rd->isStruct()) {
            tni->super_type = TYPE_CStruct;
        } else if (rd->isUnion()) {
            tni->super_type = TYPE_CUnion;
        }

        std::vector<Symbol> names;
        std::vector<const Type *> types;
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
            const Type *fieldtype = TranslateType(FT);

            // todo: work offset into structure
            names.push_back(
                it->isAnonymousStructOrUnion()?
                    Symbol("") : Symbol(
                        String::from_stdstring(declname.getAsString())));
            types.push_back(fieldtype);
        }

        tni->finalize(rd->isUnion()?Union(types):Tuple(types));
        tni->field_names = names;
    }

    const Type *get_typename(Symbol name, NamespaceMap &map) {
        if (name != SYM_Unnamed) {
            auto it = map.find(name);
            if (it != map.end()) {
                return it->second;
            }
            const Type *T = Typename(name.name());
            auto ok = map.insert({name, T});
            assert(ok.second);
            return T;
        }
        return Typename(name.name());
    }

    const Type *TranslateRecord(clang::RecordDecl *rd) {
        if (!rd->isStruct() && !rd->isUnion())
            location_error(String::from("can not translate record: is neither struct nor union"));

        Symbol name(String::from_stdstring(rd->getName().data()));

        const Type *struct_type = get_typename(name,
            rd->isUnion()?named_unions:named_structs);

        //const Anchor *anchor = anchorFromLocation(rd->getSourceRange().getBegin());

        clang::RecordDecl * defn = rd->getDefinition();
        if (defn && !record_defined[rd]) {
            record_defined[rd] = true;

            GetFields(
                cast<TypenameType>(const_cast<Type *>(struct_type)),
                defn);

            #if 0
            auto &rl = Context->getASTRecordLayout(rd);
            auto align = (size_t)rl.getAlignment().getQuantity();
            auto size = (size_t)rl.getSize().getQuantity();
            #endif
        }

        return struct_type;
    }

    Any make_integer(const Type *T, int64_t v) {
        auto it = cast<IntegerType>(T);
        if (it->issigned) {
            switch(it->width) {
            case 8: return Any((int8_t)v);
            case 16: return Any((int16_t)v);
            case 32: return Any((int32_t)v);
            case 64: return Any((int64_t)v);
            default: assert(false); return none;
            }
        } else {
            switch(it->width) {
            case 8: return Any((uint8_t)v);
            case 16: return Any((uint16_t)v);
            case 32: return Any((uint32_t)v);
            case 64: return Any((uint64_t)v);
            default: assert(false); return none;
            }
        }
    }

    const Type *TranslateEnum(clang::EnumDecl *ed) {

        Symbol name(String::from_stdstring(ed->getName()));

        const Type *enum_type = get_typename(name, named_enums);

        //const Anchor *anchor = anchorFromLocation(ed->getIntegerTypeRange().getBegin());

        clang::EnumDecl * defn = ed->getDefinition();
        if (defn && !enum_defined[ed]) {
            enum_defined[ed] = true;

            const Type *tag_type = TranslateType(ed->getIntegerType());

            auto tni = cast<TypenameType>(const_cast<Type *>(enum_type));
            tni->super_type = TYPE_CEnum;
            tni->finalize(tag_type);

            for (auto it : ed->enumerators()) {
                //const Anchor *anchor = anchorFromLocation(it->getSourceRange().getBegin());
                auto &val = it->getInitVal();

                auto name = Symbol(String::from_stdstring(it->getName().data()));
                auto value = make_integer(tag_type, val.getExtValue());
                value.type = enum_type;

                tni->bind(name, value);
                dest->bind(name, value);
            }
        }

        return enum_type;
    }

    const Type *TranslateType(clang::QualType T) {
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
                return TYPE_Void;
            case clang::BuiltinType::Bool:
                return TYPE_Bool;
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
                return Integer(sz, !Ty->isUnsignedIntegerType());
            } break;
            case clang::BuiltinType::Half: return TYPE_F16;
            case clang::BuiltinType::Float:
                return TYPE_F32;
            case clang::BuiltinType::Double:
                return TYPE_F64;
            case clang::BuiltinType::LongDouble: return TYPE_F80;
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
            break;
        case clang::Type::IncompleteArray: {
            const IncompleteArrayType *ATy = cast<IncompleteArrayType>(Ty);
            return Pointer(TranslateType(ATy->getElementType()));
        } break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            const Type *at = TranslateType(ATy->getElementType());
            uint64_t sz = ATy->getSize().getZExtValue();
            return Array(at, sz);
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
            const clang::VectorType *VT = cast<clang::VectorType>(T);
            const Type *at = TranslateType(VT->getElementType());
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

    const Type *TranslateFuncType(const clang::FunctionType * f) {

        clang::QualType RT = f->getReturnType();

        const Type *returntype = TranslateType(RT);

        uint64_t flags = 0;

        std::vector<const Type *> argtypes;

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

    void exportType(Symbol name, const Type *type) {
        dest->bind(name, type);
    }

    void exportExtern(Symbol name, const Type *type,
        const Anchor *anchor) {
        Any value(name);
        value.type = Extern(type);
        dest->bind(name, value);
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

            exportExtern(
                String::from_stdstring(vd->getName().data()),
                TranslateType(vd->getType()),
                anchor);
        }

        return true;
    }

    bool TraverseTypedefDecl(clang::TypedefDecl *td) {

        //const Anchor *anchor = anchorFromLocation(td->getSourceRange().getBegin());

        const Type *type = TranslateType(td->getUnderlyingType());

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

        const Type *functype = TranslateFuncType(fntyp);

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

        exportExtern(Symbol(String::from_stdstring(FuncName)),
            functype, anchor);

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
}

static std::vector<LLVMModuleRef> llvm_c_modules;

static void add_c_macro(clang::Preprocessor & PP, 
    const clang::IdentifierInfo * II, 
    clang::MacroDirective * MD, Scope *scope) {
    if(!II->hasMacroDefinition())
        return;
    clang::MacroInfo * MI = MD->getMacroInfo();
    if(MI->isFunctionLike())
        return;
    bool negate = false;
    const clang::Token * Tok;
    if(MI->getNumTokens() == 2 && MI->getReplacementToken(0).is(clang::tok::minus)) {
        negate = true;
        Tok = &MI->getReplacementToken(1);
    } else if(MI->getNumTokens() == 1) {
        Tok = &MI->getReplacementToken(0);
    } else {
        return;
    }
    
    if(Tok->isNot(clang::tok::numeric_constant))
        return;
    
    clang::SmallString<64> IntegerBuffer;
    bool NumberInvalid = false;
    clang::StringRef Spelling = PP.getSpelling(*Tok, IntegerBuffer, &NumberInvalid);
    clang::NumericLiteralParser Literal(Spelling, Tok->getLocation(), PP);
    if(Literal.hadError)
        return;
    const String *name = String::from_cstr(II->getName().str().c_str());
    if(Literal.isFloatingLiteral()) {
        llvm::APFloat Result(0.0);
        Literal.GetFloatValue(Result);
        double V = Result.convertToDouble();
        if (negate)
            V = -V;
        scope->bind(Symbol(name), V);
    } else {
        llvm::APInt Result(64,0);
        Literal.GetIntegerValue(Result);
        int64_t i = Result.getSExtValue();
        if (negate)
            i = -i;
        scope->bind(Symbol(name), i);
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
            //~ CompilerInvocation::GetResourcesPath(scopes_argv[0], MainAddr);

    LLVMModuleRef M = NULL;


    Scope *result = Scope::from();

    // Create and execute the frontend to generate an LLVM bitcode module.
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction(result));
    if (compiler.ExecuteAction(*Act)) {

        clang::Preprocessor & PP = compiler.getPreprocessor();
        PP.getDiagnostics().setClient(new IgnoringDiagConsumer(), true);

        for(Preprocessor::macro_iterator it = PP.macro_begin(false),end = PP.macro_end(false); 
            it != end; ++it) {
            const IdentifierInfo * II = it->first;
            MacroDirective * MD = it->second.getLatest();

            add_c_macro(PP, II, MD, result);
        }

        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        llvm_c_modules.push_back(M);
        assert(ee);
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

static bool signal_abort = false;
void f_abort() {
    if (signal_abort) {
        std::abort();
    } else {
        exit(1);
    }
}

static void default_exception_handler(const Any &value) {
    auto cerr = StyledStream(std::cerr);
    if (value.type == TYPE_Exception) {        
        const Exception *exc = value;
        if (exc->anchor) {
            cerr << exc->anchor << " ";
        }
        cerr << Style_Error << "error:" << Style_None << " "
            << exc->msg->data << std::endl;
        if (exc->anchor) {
            exc->anchor->stream_source_line(cerr);
        }
    } else {
        cerr << "exception raised: " << value << std::endl;
    }
    f_abort();
}

static int integer_type_bit_size(const Type *T) {
    return (int)cast<IntegerType>(T)->width;
}

template<typename T>
static T cast_number(const Any &value) {
    auto ST = storage_type(value.type);
    auto it = dyn_cast<IntegerType>(ST);
    if (it) {
        if (it->issigned) {
            switch(it->width) {
            case 8: return (T)value.i8;
            case 16: return (T)value.i16;
            case 32: return (T)value.i32;
            case 64: return (T)value.i64;
            default: break;
            }
        } else {
            switch(it->width) {
            case 1: return (T)value.i1;
            case 8: return (T)value.u8;
            case 16: return (T)value.u16;
            case 32: return (T)value.u32;
            case 64: return (T)value.u64;
            default: break;
            }
        }
    }
    auto ft = dyn_cast<RealType>(ST);
    if (ft) {
        switch(ft->width) {
        case 32: return (T)value.f32;
        case 64: return (T)value.f64;
        default: break;
        }
    }
    StyledString ss;
    ss.out << "type " << value.type << " can not be converted to numerical type";
    location_error(ss.str());
    return 0;
}

//------------------------------------------------------------------------------
// PLATFORM ABI
//------------------------------------------------------------------------------

// life is unfair, which is why we need to implement the remaining platform ABI
// support in the front-end, particularly whether an argument is passed by
// value or not.

#ifdef SCOPES_WIN32
#else
// x86-64 PS ABI based on https://www.uclibc.org/docs/psABI-x86_64.pdf

enum ABIClass {
    // This class consists of integral types that fit into one of the general
    // purpose registers.
    ABI_CLASS_INTEGER,
    ABI_CLASS_INTEGERSI,
    // The class consists of types that fit into a vector register.
    ABI_CLASS_SSE,
    ABI_CLASS_SSESF,
    ABI_CLASS_SSEDF,
    // The class consists of types that fit into a vector register and can be 
    // passed and returned in the upper bytes of it.
    ABI_CLASS_SSEUP,
    // These classes consists of types that will be returned via the x87 FPU
    ABI_CLASS_X87,
    ABI_CLASS_X87UP,
    // This class consists of types that will be returned via the x87 FPU
    ABI_CLASS_COMPLEX_X87,
    // This class is used as initializer in the algorithms. It will be used for
    // padding and empty structures and unions.
    ABI_CLASS_NO_CLASS,
    // This class consists of types that will be passed and returned in memory
    // via the stack.
    ABI_CLASS_MEMORY,
};

static ABIClass merge_abi_classes(ABIClass class1, ABIClass class2) {
    if (class1 == class2)
        return class1;

    if (class1 == ABI_CLASS_NO_CLASS)
        return class2;
    if (class2 == ABI_CLASS_NO_CLASS)
        return class1;

    if (class1 == ABI_CLASS_MEMORY || class2 == ABI_CLASS_MEMORY)
        return ABI_CLASS_MEMORY;

    if ((class1 == ABI_CLASS_INTEGERSI && class2 == ABI_CLASS_SSESF)
        || (class2 == ABI_CLASS_INTEGERSI && class1 == ABI_CLASS_SSESF))
        return ABI_CLASS_INTEGERSI;
    if (class1 == ABI_CLASS_INTEGER || class1 == ABI_CLASS_INTEGERSI
        || class2 == ABI_CLASS_INTEGER || class2 == ABI_CLASS_INTEGERSI)
        return ABI_CLASS_INTEGER;

    if (class1 == ABI_CLASS_X87
        || class1 == ABI_CLASS_X87UP
        || class1 == ABI_CLASS_COMPLEX_X87
        || class2 == ABI_CLASS_X87
        || class2 == ABI_CLASS_X87UP
        || class2 == ABI_CLASS_COMPLEX_X87)
        return ABI_CLASS_MEMORY;

    return ABI_CLASS_SSE;
}

const size_t MAX_ABI_CLASSES = 4;
static size_t classify(const Type *T, ABIClass *classes, size_t offset) {
    switch(T->kind()) {
    case TK_Integer: 
    case TK_Extern:
    case TK_Pointer: {
        size_t size = size_of(T) + offset;
        if (size <= 4) {
            classes[0] = ABI_CLASS_INTEGERSI;
            return 1;
        } else if (size <= 8) {
            classes[0] = ABI_CLASS_INTEGER;
            return 1;
        } else if (size <= 12) {
            classes[0] = ABI_CLASS_INTEGER;
            classes[1] = ABI_CLASS_INTEGERSI;
            return 2;
        } else if (size <= 16) {
            classes[0] = ABI_CLASS_INTEGER;
            classes[1] = ABI_CLASS_INTEGER;
            return 2;
        } else {
            assert(false && "illegal type");
        }
    } break;
    case TK_Real: {
        size_t size = size_of(T);
        if (size == 4) {
            if (!(offset % 8))
                classes[0] = ABI_CLASS_SSESF;
            else
                classes[0] = ABI_CLASS_SSE;
            return 1;
        } else if (size == 8) {
            classes[0] = ABI_CLASS_SSEDF;
            return 1;
        } else {
            assert(false && "illegal type");
        }
    } break;
    case TK_Typename: {
        if (is_opaque(T)) {
            classes[0] = ABI_CLASS_NO_CLASS;
            return 1;
        } else {
            return classify(storage_type(T), classes, offset);
        }
    } break;
    case TK_Array: {
        const size_t UNITS_PER_WORD = 8;
        size_t size = size_of(T);
	    size_t words = (size + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
        if (size > 32)
            return 0;
        for (size_t i = 0; i < MAX_ABI_CLASSES; i++)
	        classes[i] = ABI_CLASS_NO_CLASS;
        if (!words) {
            classes[0] = ABI_CLASS_NO_CLASS;
            return 1;
        }
        auto tt = cast<ArrayType>(T);
        auto ET = tt->element_type;
        ABIClass subclasses[MAX_ABI_CLASSES];
        size_t alignment = align_of(ET);
        size_t esize = size_of(ET);
        for (size_t i = 0; i < tt->count; ++i) {
            offset = align(offset, alignment);
            size_t num = classify(ET, subclasses, offset % 8);
            if (!num) return 0;
            for (size_t k = 0; k < num; ++k) {
                size_t pos = offset / 8;
		        classes[k + pos] =
		            merge_abi_classes (subclasses[k], classes[k + pos]);
            }
            offset += esize;
        }
        if (words > 2) {
            if (classes[0] != ABI_CLASS_SSE)
                return 0;
            for (size_t i = 1; i < words; ++i) {
                if (classes[i] != ABI_CLASS_SSEUP)
                    return 0;
            }
        }
        for (size_t i = 0; i < words; i++) {
            if (classes[i] == ABI_CLASS_MEMORY)
                return 0;

            if (classes[i] == ABI_CLASS_SSEUP) {
                assert(i > 0);
                if (classes[i - 1] != ABI_CLASS_SSE
                    && classes[i - 1] != ABI_CLASS_SSEUP) {
                    classes[i] = ABI_CLASS_SSE;
                }
            }

            if (classes[i] == ABI_CLASS_X87UP) {
                assert(i > 0);
                if(classes[i - 1] != ABI_CLASS_X87) {
                    return 0;
                }
            }
        }
        return words;        
    } break;
    case TK_Union: {
        auto ut = cast<UnionType>(T);
        return classify(ut->types[ut->largest_field], classes, offset);
    } break;
    case TK_Tuple: {
        const size_t UNITS_PER_WORD = 8;
        size_t size = size_of(T);
	    size_t words = (size + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
        if (size > 32)
            return 0;
        for (size_t i = 0; i < MAX_ABI_CLASSES; i++)
	        classes[i] = ABI_CLASS_NO_CLASS;
        if (!words) {
            classes[0] = ABI_CLASS_NO_CLASS;
            return 1;
        }
        auto tt = cast<TupleType>(T);
        ABIClass subclasses[MAX_ABI_CLASSES];
        for (size_t i = 0; i < tt->types.size(); ++i) {
            auto ET = tt->types[i];
            offset = align(offset, align_of(ET));
            size_t num = classify (ET, subclasses, offset % 8);
            if (!num) return 0;
            for (size_t k = 0; k < num; ++k) {
                size_t pos = offset / 8;
		        classes[k + pos] =
		            merge_abi_classes (subclasses[k], classes[k + pos]);
            }
            offset += size_of(ET);
        }
        if (words > 2) {
            if (classes[0] != ABI_CLASS_SSE)
                return 0;
            for (size_t i = 1; i < words; ++i) {
                if (classes[i] != ABI_CLASS_SSEUP)
                    return 0;
            }
        }
        for (size_t i = 0; i < words; i++) {
            if (classes[i] == ABI_CLASS_MEMORY)
                return 0;

            if (classes[i] == ABI_CLASS_SSEUP) {
                assert(i > 0);
                if (classes[i - 1] != ABI_CLASS_SSE
                    && classes[i - 1] != ABI_CLASS_SSEUP) {
                    classes[i] = ABI_CLASS_SSE;
                }
            }

            if (classes[i] == ABI_CLASS_X87UP) {
                assert(i > 0);
                if(classes[i - 1] != ABI_CLASS_X87) {
                    return 0;
                }
            }
        }
        return words;
    } break;
    default: {
        assert(false && "not supported in ABI");
        return 0;
    } break;
    }
    return 0;
}
#endif // SCOPES_WIN32

static bool is_memory_class(const Type *T) {
#ifdef SCOPES_WIN32
    if (T == TYPE_Void)
        return false;
    if (size_of(T) > 8)
        return true;
    else
        return false;
#else
    ABIClass subclasses[MAX_ABI_CLASSES];
    return !classify(T, subclasses, 0);
#endif
}

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
            if (arg.value.type == TYPE_Parameter && !labels.count(arg.value.parameter->label))
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
    //LLVMAddAnalysisPasses(LLVMGetExecutionEngineTargetMachine(ee), functionPasses);

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

typedef llvm::DIBuilder *LLVMDIBuilderRef;

static LLVMDIBuilderRef LLVMCreateDIBuilder(LLVMModuleRef M) {
  return new llvm::DIBuilder(*llvm::unwrap(M));
}

static void LLVMDisposeDIBuilder(LLVMDIBuilderRef Builder) {
  Builder->finalize();
  delete Builder;
}

static llvm::MDNode *value_to_mdnode(LLVMValueRef value) {
    return value ? cast<llvm::MDNode>(
        llvm::unwrap<llvm::MetadataAsValue>(value)->getMetadata()) : nullptr;
}

template<typename T>
static T *value_to_DI(LLVMValueRef value) {
    return value ? cast<T>(
        llvm::unwrap<llvm::MetadataAsValue>(value)->getMetadata()) : nullptr;
}

static LLVMValueRef mdnode_to_value(llvm::MDNode *node) {
  return llvm::wrap(
    llvm::MetadataAsValue::get(*llvm::unwrap(LLVMGetGlobalContext()), node));
}

typedef llvm::DINode::DIFlags LLVMDIFlags;

static LLVMValueRef LLVMDIBuilderCreateSubroutineType(
    LLVMDIBuilderRef Builder, LLVMValueRef ParameterTypes) {
    return mdnode_to_value(
        Builder->createSubroutineType(value_to_DI<llvm::MDTuple>(ParameterTypes)));
}

static LLVMValueRef LLVMDIBuilderCreateCompileUnit(LLVMDIBuilderRef Builder,
    unsigned Lang,
    const char *File, const char *Dir, const char *Producer, bool isOptimized,
    const char *Flags, unsigned RV, const char *SplitName,
    //DICompileUnit::DebugEmissionKind Kind,
    uint64_t DWOId) {
    auto ctx = (llvm::LLVMContext *)LLVMGetGlobalContext();    
    auto file = llvm::DIFile::get(*ctx, File, Dir);
    return mdnode_to_value(
        Builder->createCompileUnit(Lang, file,
                      Producer, isOptimized, Flags,
                      RV, SplitName,
                      llvm::DICompileUnit::DebugEmissionKind::FullDebug,
                      //llvm::DICompileUnit::DebugEmissionKind::LineTablesOnly,
                      DWOId));
}

static LLVMValueRef LLVMDIBuilderCreateFunction(
    LLVMDIBuilderRef Builder, LLVMValueRef Scope, const char *Name,
    const char *LinkageName, LLVMValueRef File, unsigned LineNo,
    LLVMValueRef Ty, bool IsLocalToUnit, bool IsDefinition,
    unsigned ScopeLine) {
  return mdnode_to_value(Builder->createFunction(
        cast<llvm::DIScope>(value_to_mdnode(Scope)), Name, LinkageName,
        cast<llvm::DIFile>(value_to_mdnode(File)),
        LineNo, cast<llvm::DISubroutineType>(value_to_mdnode(Ty)),
        IsLocalToUnit, IsDefinition, ScopeLine));
}

static LLVMValueRef LLVMGetFunctionSubprogram(LLVMValueRef func) {
    return mdnode_to_value(
        llvm::cast<llvm::Function>(llvm::unwrap(func))->getSubprogram());
}

static void LLVMSetFunctionSubprogram(LLVMValueRef func, LLVMValueRef subprogram) {
    llvm::cast<llvm::Function>(llvm::unwrap(func))->setSubprogram(
        value_to_DI<llvm::DISubprogram>(subprogram));
}

static LLVMValueRef LLVMDIBuilderCreateLexicalBlock(LLVMDIBuilderRef Builder,
    LLVMValueRef Scope, LLVMValueRef File, unsigned Line, unsigned Col) {
    return mdnode_to_value(Builder->createLexicalBlock(
        value_to_DI<llvm::DIScope>(Scope),
        value_to_DI<llvm::DIFile>(File), Line, Col));
}

static LLVMValueRef LLVMCreateDebugLocation(unsigned Line,
                                     unsigned Col, const LLVMValueRef Scope,
                                     const LLVMValueRef InlinedAt) {
  llvm::MDNode *SNode = value_to_mdnode(Scope);
  llvm::MDNode *INode = value_to_mdnode(InlinedAt);
  return mdnode_to_value(llvm::DebugLoc::get(Line, Col, SNode, INode).get());
}

static LLVMValueRef LLVMDIBuilderCreateFile(
    LLVMDIBuilderRef Builder, const char *Filename,
                            const char *Directory) {
  return mdnode_to_value(Builder->createFile(Filename, Directory));
}

static std::vector<void *> loaded_libs;
static void *local_aware_dlsym(const char *name) {
    size_t i = loaded_libs.size();
    while (i--) {
        void *ptr = dlsym(loaded_libs[i], name);
        if (ptr) {
            LLVMAddSymbol(name, ptr);
            return ptr;
        }
    }
    return dlsym(global_c_namespace, name);
}

struct GenerateCtx {
    struct HashFuncLabelPair {
        size_t operator ()(const std::pair<LLVMValueRef, Label *> &value) const {
            return
                HashLen16(std::hash<LLVMValueRef>()(value.first),
                    std::hash<Label *>()(value.second));
        }
    };

    std::unordered_map<Label *, LLVMValueRef> label2func;
    std::unordered_map< std::pair<LLVMValueRef, Label *>,
        LLVMBasicBlockRef, HashFuncLabelPair> label2bb;
    std::vector< std::pair<Label *, Label *> > bb_label_todo;

    std::unordered_map<Label *, LLVMValueRef> label2md;
    std::unordered_map<SourceFile *, LLVMValueRef> file2value;
    std::unordered_map<Parameter *, LLVMValueRef> param2value;
    std::unordered_map<const Type *, LLVMTypeRef> type_cache;
    std::vector<const Type *> type_todo;

    std::unordered_map<Any, LLVMValueRef, Any::Hash> extern2global;

    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMDIBuilderRef di_builder;

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

    LLVMAttributeRef attr_byval;
    LLVMAttributeRef attr_sret;
    LLVMAttributeRef attr_nonnull;

    bool use_debug_info;

    template<unsigned N>
    static LLVMAttributeRef get_attribute(const char (&s)[N]) {
        unsigned kind = LLVMGetEnumAttributeKindForName(s, N - 1);
        assert(kind);
        return LLVMCreateEnumAttribute(LLVMGetGlobalContext(), kind, 0);
    }

    GenerateCtx() :
        active_function(nullptr),
        use_debug_info(true) {
        attr_byval = get_attribute("byval");
        attr_sret = get_attribute("sret");
        attr_nonnull = get_attribute("nonnull");
    }

    LLVMValueRef source_file_to_scope(SourceFile *sf) {
        assert(use_debug_info);

        auto it = file2value.find(sf);
        if (it != file2value.end())
            return it->second;

        char *dn = strdup(sf->path.name()->data);
        char *bn = strdup(dn);

        LLVMValueRef result = LLVMDIBuilderCreateFile(di_builder,
            basename(bn), dirname(dn));
        free(dn);
        free(bn);

        file2value.insert({ sf, result });

        return result;
    }

    LLVMValueRef label_to_subprogram(Label *l) {
        assert(use_debug_info);

        auto it = label2md.find(l);
        if (it != label2md.end())
            return it->second;

        const Anchor *anchor = l->anchor;

        LLVMValueRef difile = source_file_to_scope(anchor->file);

        LLVMValueRef subroutinevalues[] = {
            nullptr
        };
        LLVMValueRef disrt = LLVMDIBuilderCreateSubroutineType(di_builder,
            LLVMMDNode(subroutinevalues, 1));

        LLVMValueRef difunc = LLVMDIBuilderCreateFunction(
            di_builder, difile, l->name.name()->data, l->name.name()->data,
            difile, anchor->lineno, disrt, false, true,
            anchor->lineno);

        label2md.insert({ l, difunc });
        return difunc;
    }

    LLVMValueRef anchor_to_location(const Anchor *anchor) {
        assert(use_debug_info);

        auto old_bb = LLVMGetInsertBlock(builder);
        LLVMValueRef func = LLVMGetBasicBlockParent(old_bb);
        LLVMValueRef disp = LLVMGetFunctionSubprogram(func);

        LLVMValueRef result = LLVMCreateDebugLocation(
            anchor->lineno, anchor->column, disp, nullptr);

        return result;
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
            if (param->kind != PK_Regular)
                return false;
            if ((param->type == TYPE_Type) || (param->type == TYPE_Label))
                return false;
            if (isa<TypeSetType>(param->type))
                return false;
            if (isa<TypedLabelType>(param->type) && (param->index != 0))
                return false;
        }
        return true;
    }

    LLVMTypeRef create_llvm_type(const Type *type) {
        switch(type->kind()) {
        case TK_Integer:
            return LLVMIntType(cast<IntegerType>(type)->width);
        case TK_Real:
            switch(cast<RealType>(type)->width) {
            case 32: return f32T;
            case 64: return f64T;
            default: break;
            }
            break;
        case TK_Extern: {
            return LLVMPointerType(
                _type_to_llvm_type(cast<ExternType>(type)->type), 0);
        } break;
        case TK_Pointer:
            return LLVMPointerType(
                _type_to_llvm_type(cast<PointerType>(type)->element_type), 0);
        case TK_Array: {
            auto ai = cast<ArrayType>(type);
            return LLVMArrayType(_type_to_llvm_type(ai->element_type), ai->count);
        } break;
        case TK_Vector: {
            auto vi = cast<VectorType>(type);
            return LLVMVectorType(_type_to_llvm_type(vi->element_type), vi->count);
        } break;
        case TK_Tuple: {
            auto ti = cast<TupleType>(type);
            size_t count = ti->types.size();
            LLVMTypeRef elements[count];
            for (size_t i = 0; i < count; ++i) {
                elements[i] = _type_to_llvm_type(ti->types[i]);
            }
            return LLVMStructType(elements, count, false);
        } break;
        case TK_Union: {
            auto ui = cast<UnionType>(type);
            size_t count = ui->types.size();
            size_t sz = ui->size;
            size_t al = ui->align;
            // find member with the same alignment
            for (size_t i = 0; i < count; ++i) {
                const Type *ET = ui->types[i];
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
        case TK_Typename: {
            if (type == TYPE_Void)
                return LLVMVoidType();
            auto tn = cast<TypenameType>(type);
            if (tn->finalized()) {
                switch(tn->storage_type->kind()) {
                case TK_Tuple:
                case TK_Union: {
                    type_todo.push_back(type);
                } break;
                default: {
                    return create_llvm_type(tn->storage_type);
                } break;
                }
            }
            return LLVMStructCreateNamed(
                LLVMGetGlobalContext(), type->name()->data);
        } break;
        case TK_Function: {
            auto fi = cast<FunctionType>(type);
            size_t count = fi->argument_types.size();
            size_t offset = 0;
            bool use_sret = is_memory_class(fi->return_type);
            if (use_sret) {
                offset = 1;
            }
            LLVMTypeRef elements[count + offset];
            LLVMTypeRef rettype;
            if (use_sret) {
                elements[0] = _type_to_llvm_type(Pointer(fi->return_type));
                rettype = voidT;
            } else {
                rettype = _type_to_llvm_type(fi->return_type);
            }
            for (size_t i = 0; i < count; ++i) {
                auto AT = fi->argument_types[i];
                if (is_memory_class(AT)) {
                    AT = Pointer(AT);
                }
                elements[i + offset] = _type_to_llvm_type(AT);
            }
            return LLVMFunctionType(rettype,
                elements, count + offset, fi->vararg());
        } break;
        default: break;
        };

        StyledString ss;
        ss.out << "IL->IR: cannot convert type " << type;
        location_error(ss.str());
        return nullptr;
    }

    void finalize_types() {
        while (!type_todo.empty()) {
            const Type *T = type_todo.back();
            type_todo.pop_back();
            auto tn = cast<TypenameType>(T);
            if (!tn->finalized())
                continue;
            LLVMTypeRef LLT = _type_to_llvm_type(T);
            const Type *ST = tn->storage_type;
            switch(ST->kind()) {
            case TK_Tuple: {
                auto ti = cast<TupleType>(ST);
                size_t count = ti->types.size();
                LLVMTypeRef elements[count];
                for (size_t i = 0; i < count; ++i) {
                    elements[i] = _type_to_llvm_type(ti->types[i]);
                }
                LLVMStructSetBody(LLT, elements, count, false);
            } break;
            case TK_Union: {
                auto ui = cast<UnionType>(ST);
                size_t count = ui->types.size();
                size_t sz = ui->size;
                size_t al = ui->align;
                // find member with the same alignment
                for (size_t i = 0; i < count; ++i) {
                    const Type *ET = ui->types[i];
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

    LLVMTypeRef _type_to_llvm_type(const Type *type) {
        auto it = type_cache.find(type);
        if (it == type_cache.end()) {
            LLVMTypeRef result = create_llvm_type(type);
            type_cache.insert({type, result});
            return result;
        } else {
            return it->second;
        }
    }

    LLVMTypeRef type_to_llvm_type(const Type *type) {
        auto typeref = _type_to_llvm_type(type);
        finalize_types();
        return typeref;
    }

    LLVMTypeRef return_type_to_llvm_type(const Type *type) {
        if (type == TYPE_Void) {
            StyledString ss;
            ss.out << "IL->IR: untyped continuation encountered";
            location_error(ss.str());
        } else if (!isa<TypedLabelType>(type)) {
            StyledString ss;
            ss.out << "IL->IR: invalid continuation type: " << type;
            location_error(ss.str());
        }
        auto tli = cast<TypedLabelType>(type);
        assert(tli->types[0] == TYPE_Nothing);
        size_t count = tli->types.size() - 1;
        if (!count) {
            return LLVMVoidType();
        }
        LLVMTypeRef element_types[count];
        for (size_t i = 0; i < count; ++i) {
            const Type *arg = tli->types[i + 1];
            element_types[i] = type_to_llvm_type(arg);
        }
        if (count == 1) {
            return element_types[0];
        } else {
            return LLVMStructType(element_types, count, false);
        }
    }

    LLVMValueRef label_to_value(Label *label) {
        if (is_basic_block_like(label)) {
            return LLVMBasicBlockAsValue(label_to_basic_block(label));
        } else {
            return label_to_function(label);
        }
    }

    LLVMValueRef argument_to_value(Any value) {
        if (value.type == TYPE_Parameter) {
            auto it = param2value.find(value.parameter);
            if (it == param2value.end()) {
                StyledString ss;
                ss.out << "can't translate free variable " << value.parameter;
                location_error(ss.str());
            }
            return it->second;
        }

        switch(value.type->kind()) {
        case TK_Integer: {
            auto it = cast<IntegerType>(value.type);
            if (it->issigned) {
                switch(it->width) {
                case 8: return LLVMConstInt(i8T, value.i8, true);
                case 16: return LLVMConstInt(i16T, value.i16, true);
                case 32: return LLVMConstInt(i32T, value.i32, true);
                case 64: return LLVMConstInt(i64T, value.i64, true);
                default: break;
                }
            } else {
                switch(it->width) {
                case 1: return LLVMConstInt(i1T, value.i1, false);
                case 8: return LLVMConstInt(i8T, value.u8, false);
                case 16: return LLVMConstInt(i16T, value.u16, false);
                case 32: return LLVMConstInt(i32T, value.u32, false);
                case 64: return LLVMConstInt(i64T, value.u64, false);
                default: break;
                }
            }
        } break;
        case TK_Real: {
            auto rt = cast<RealType>(value.type);
            switch(rt->width) {
            case 32: return LLVMConstReal(f32T, value.f32);
            case 64: return LLVMConstReal(f64T, value.f64);
            default: break;
            }
        } break;
        case TK_Extern: {
            auto it = extern2global.find(value);
            if (it == extern2global.end()) {
                const String *namestr = value.symbol.name();
                const char *name = namestr->data;
                assert(name);
                auto et = cast<ExternType>(value.type);
                LLVMTypeRef LLT = type_to_llvm_type(et->type);
                LLVMValueRef result = nullptr;
                if ((namestr->count > 5) && !strncmp(name, "llvm.", 5)) {
                    result = LLVMAddFunction(module, name, LLT);
                } else {
                    uint64_t ptr = LLVMGetGlobalValueAddress(ee, name);
                    if (!ptr) {
                        void *pptr = local_aware_dlsym(name);
                        ptr = *(uint64_t*)&pptr;
                    }
                    if (!ptr) {
                        StyledString ss;
                        ss.out << "could not resolve " << value;
                        location_error(ss.str());
                    }
                    result = LLVMAddGlobal(module, LLT, name);
                }
                extern2global.insert({ value, result });
                return result;
            } else {
                return it->second;
            }
        } break;
        case TK_Pointer: {
            LLVMTypeRef LLT = type_to_llvm_type(value.type);
            if (!value.pointer) {
                return LLVMConstPointerNull(LLT);
            } else {
                return LLVMConstIntToPtr(
                    LLVMConstInt(i64T, *(uint64_t*)&value.pointer, false),
                    LLT);
            }
        } break;
        case TK_Typename: {
            LLVMTypeRef LLT = type_to_llvm_type(value.type);
            auto tn = cast<TypenameType>(value.type);
            switch(tn->storage_type->kind()) {
            case TK_Tuple: {
                auto ti = cast<TupleType>(tn->storage_type);
                size_t count = ti->types.size();
                LLVMValueRef values[count];
                for (size_t i = 0; i < count; ++i) {
                    values[i] = argument_to_value(ti->unpack(value.pointer, i));
                }
                return LLVMConstNamedStruct(LLT, values, count);
            } break;
            default: {
                Any storage_value = value;
                storage_value.type = tn->storage_type;
                LLVMValueRef val = argument_to_value(storage_value);
                return LLVMConstBitCast(val, LLT);
            } break;
            }
        } break;
        case TK_Tuple: {
            auto ti = cast<TupleType>(value.type);
            size_t count = ti->types.size();
            LLVMValueRef values[count];
            for (size_t i = 0; i < count; ++i) {
                values[i] = argument_to_value(ti->unpack(value.pointer, i));
            }
            return LLVMConstStruct(values, count, false);
        } break;
        default: break;
        };

        StyledString ss;
        ss.out << "IL->IR: cannot convert argument of type " << value.type;
        location_error(ss.str());
        return nullptr;
    }

    LLVMValueRef build_call(const Type *functype, LLVMValueRef func, Args &args) {
        size_t argcount = args.size() - 1;

        auto fi = cast<FunctionType>(functype);

        bool use_sret = is_memory_class(fi->return_type);

        size_t valuecount = argcount;
        size_t offset = 0;
        if (use_sret) {
            valuecount++;
            offset = 1;
        }
        LLVMValueRef values[valuecount];
        if (use_sret) {
            values[0] = LLVMBuildAlloca(builder, 
                _type_to_llvm_type(fi->return_type), "");
        }
        std::vector<size_t> memptrs;
        for (size_t i = 0; i < argcount; ++i) {
            auto &&arg = args[i + 1];
            LLVMValueRef val = argument_to_value(arg.value);
            auto AT = arg.value.indirect_type();
            if (is_memory_class(AT)) {
                LLVMValueRef ptrval = LLVMBuildAlloca(builder, 
                    _type_to_llvm_type(AT), "");
                LLVMBuildStore(builder, val, ptrval);
                val = ptrval;
                memptrs.push_back(i + offset + 1);
            }
            values[i + offset] = val;
        }

        size_t fargcount = fi->argument_types.size();
        assert(argcount >= fargcount);
        // make variadic calls C compatible
        if (fi->flags & FF_Variadic) {
            for (size_t i = fargcount; i < argcount; ++i) {
                auto value = values[i];
                // floats need to be widened to doubles
                if (LLVMTypeOf(value) == f32T) {
                    values[i] = LLVMBuildFPExt(builder, value, f64T, "");
                }
            }
        }

        auto ret = LLVMBuildCall(builder, func, values, valuecount, "");
        for (auto idx : memptrs) {
            LLVMAddCallSiteAttribute(ret, idx, attr_byval);
            LLVMAddCallSiteAttribute(ret, idx, attr_nonnull);
        }
        if (use_sret) {
            LLVMAddCallSiteAttribute(ret, 1, attr_sret);
            return LLVMBuildLoad(builder, values[0], "");
        } else if (fi->return_type != TYPE_Void) {
            return ret;
        } else {
            return nullptr;
        }
    }

    void write_label_body(Label *label) {
        auto &&body = label->body;
        auto &&enter = body.enter;
        auto &&args = body.args;

        set_active_anchor(label->body.anchor);

        LLVMValueRef diloc = nullptr;
        if (use_debug_info) {
            diloc = anchor_to_location(label->body.anchor);
            LLVMSetCurrentDebugLocation(builder, diloc);
        }

        assert(!args.empty());
        size_t argcount = args.size() - 1;
        size_t argn = 1;
#define READ_ANY(NAME) \
        assert(argn <= argcount); \
        Any &NAME = args[argn++].value;
#define READ_VALUE(NAME) \
        assert(argn <= argcount); \
        LLVMValueRef NAME = argument_to_value(args[argn++].value);
#define READ_LABEL_VALUE(NAME) \
        assert(argn <= argcount); \
        LLVMValueRef NAME = label_to_value(args[argn++].value);
#define READ_TYPE(NAME) \
        assert(argn <= argcount); \
        assert(args[argn].value.type == TYPE_Type); \
        LLVMTypeRef NAME = type_to_llvm_type(args[argn++].value.typeref);

        LLVMValueRef retvalue = nullptr;
        if (enter.type == TYPE_Builtin) {
            switch(enter.builtin.value()) {
            case FN_Branch: {
                READ_VALUE(cond);
                READ_LABEL_VALUE(then_block);
                READ_LABEL_VALUE(else_block);
                assert(LLVMValueIsBasicBlock(then_block));
                assert(LLVMValueIsBasicBlock(else_block));
                LLVMBuildCondBr(builder, cond,
                    LLVMValueAsBasicBlock(then_block),
                    LLVMValueAsBasicBlock(else_block));
            } break;
            case OP_Tertiary: {
                READ_VALUE(cond);
                READ_VALUE(then_value);
                READ_VALUE(else_value);
                retvalue = LLVMBuildSelect(
                    builder, cond, then_value, else_value, "");
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
            case FN_InsertValue: {
                READ_VALUE(val);
                READ_VALUE(eltval);
                READ_ANY(index);
                retvalue = LLVMBuildInsertValue(
                    builder, val, eltval, cast_number<int32_t>(index), "");
            } break;
            case FN_Undef: { READ_TYPE(ty);
                retvalue = LLVMGetUndef(ty); } break;
            case FN_NullOf: { READ_TYPE(ty);
                retvalue = LLVMConstNull(ty); } break;
            case FN_Alloca: { READ_TYPE(ty);
                retvalue = LLVMBuildAlloca(builder, ty, ""); } break;
            case FN_AllocaArray: { READ_TYPE(ty); READ_VALUE(val);
                retvalue = LLVMBuildArrayAlloca(builder, ty, val, ""); } break;
            case FN_Malloc: { READ_TYPE(ty);
                retvalue = LLVMBuildMalloc(builder, ty, ""); } break;
            case FN_MallocArray: { READ_TYPE(ty); READ_VALUE(val);
                retvalue = LLVMBuildArrayMalloc(builder, ty, val, ""); } break;
            case FN_Free: { READ_VALUE(val);
                retvalue = LLVMBuildFree(builder, val); } break;
            case FN_GetElementPtr: {
                READ_VALUE(pointer);
                assert(argcount > 1);
                size_t count = argcount - 1;
                LLVMValueRef indices[count];
                for (size_t i = 0; i < count; ++i) {
                    indices[i] = argument_to_value(args[argn + i].value);
                }
                retvalue = LLVMBuildGEP(builder, pointer, indices, count, "");
            } break;
            case FN_Bitcast: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildBitCast(builder, val, ty, ""); 
            } break;
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
            case FN_FPTrunc: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildFPTrunc(builder, val, ty, ""); } break;
            case FN_FPExt: { READ_VALUE(val); READ_TYPE(ty);
                retvalue = LLVMBuildFPExt(builder, val, ty, ""); } break;
            case FN_VolatileLoad:
            case FN_Load: { READ_VALUE(ptr);
                retvalue = LLVMBuildLoad(builder, ptr, ""); 
                if (enter.builtin.value() == FN_VolatileLoad) { LLVMSetVolatile(retvalue, true); }
            } break;
            case FN_VolatileStore:
            case FN_Store: { READ_VALUE(val); READ_VALUE(ptr);
                retvalue = LLVMBuildStore(builder, val, ptr); 
                if (enter.builtin.value() == FN_VolatileStore) { LLVMSetVolatile(retvalue, true); }
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
                READ_VALUE(a); READ_VALUE(b);
                LLVMIntPredicate pred = LLVMIntEQ;
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
            case OP_FCmpOEQ:
            case OP_FCmpONE:
            case OP_FCmpORD:
            case OP_FCmpOGT:
            case OP_FCmpOGE:
            case OP_FCmpOLT:
            case OP_FCmpOLE:
            case OP_FCmpUEQ:
            case OP_FCmpUNE:
            case OP_FCmpUNO:
            case OP_FCmpUGT:
            case OP_FCmpUGE:
            case OP_FCmpULT:
            case OP_FCmpULE: {
                READ_VALUE(a); READ_VALUE(b);
                LLVMRealPredicate pred = LLVMRealOEQ;
                switch(enter.builtin.value()) {
                    case OP_FCmpOEQ: pred = LLVMRealOEQ; break;
                    case OP_FCmpONE: pred = LLVMRealONE; break;
                    case OP_FCmpORD: pred = LLVMRealORD; break;
                    case OP_FCmpOGT: pred = LLVMRealOGT; break;
                    case OP_FCmpOGE: pred = LLVMRealOGE; break;
                    case OP_FCmpOLT: pred = LLVMRealOLT; break;
                    case OP_FCmpOLE: pred = LLVMRealOLE; break;
                    case OP_FCmpUEQ: pred = LLVMRealUEQ; break;
                    case OP_FCmpUNE: pred = LLVMRealUNE; break;
                    case OP_FCmpUNO: pred = LLVMRealUNO; break;
                    case OP_FCmpUGT: pred = LLVMRealUGT; break;
                    case OP_FCmpUGE: pred = LLVMRealUGE; break;
                    case OP_FCmpULT: pred = LLVMRealULT; break;
                    case OP_FCmpULE: pred = LLVMRealULE; break;
                    default: assert(false); break;
                }
                retvalue = LLVMBuildFCmp(builder, pred, a, b, "");
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
            case SFXFN_Unreachable:
                retvalue = LLVMBuildUnreachable(builder); break;
            default: {
                StyledString ss;
                ss.out << "IL->IR: unsupported builtin " << enter.builtin << " encountered";
                location_error(ss.str());
            } break;
            }
        } else if (enter.type == TYPE_Label) {
            LLVMValueRef value = label_to_value(enter);
            if (LLVMValueIsBasicBlock(value)) {
                LLVMValueRef values[argcount];
                for (size_t i = 0; i < argcount; ++i) {
                    values[i] = argument_to_value(args[i + 1].value);
                }
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
                if (use_debug_info) {
                    LLVMSetCurrentDebugLocation(builder, diloc);
                }
                retvalue = build_call(
                    enter.label->get_function_type(), 
                    value, args);
            }
        } else if (is_function_pointer(enter.indirect_type())) {
            retvalue = build_call(extract_function_type(enter.indirect_type()), 
                argument_to_value(enter), args);
        } else if (enter.type == TYPE_Parameter) {
            LLVMValueRef values[argcount];
            for (size_t i = 0; i < argcount; ++i) {
                values[i] = argument_to_value(args[i + 1].value);
            }
            // must be a return
            assert(enter.parameter->index == 0);
            // must be returning from this function
            assert(enter.parameter->label == active_function);

            Label *label = enter.parameter->label;
            bool use_sret = is_memory_class(label->get_return_type());
            if (use_sret) {
                auto it = param2value.find(enter.parameter);
                assert (it != param2value.end());                   
                if (argcount > 1) {
                    LLVMTypeRef types[argcount];
                    for (size_t i = 0; i < argcount; ++i) {
                        types[i] = LLVMTypeOf(values[i]);
                    }

                    LLVMValueRef val = LLVMGetUndef(LLVMStructType(types, argcount, false));
                    for (size_t i = 0; i < argcount; ++i) {
                        val = LLVMBuildInsertValue(builder, val, values[i], i, "");
                    }
                    LLVMBuildStore(builder, val, it->second);
                } else if (argcount == 1) {
                    LLVMBuildStore(builder, values[0], it->second);
                }
                LLVMBuildRetVoid(builder);
            } else {
                if (argcount > 1) {
                    LLVMBuildAggregateRet(builder, values, argcount);
                } else if (argcount == 1) {
                    LLVMBuildRet(builder, values[0]);
                } else {
                    LLVMBuildRetVoid(builder);
                }
            }
        } else {
            assert(false && "todo: translate non-builtin call");
        }

        Any contarg = args[0].value;
        if (contarg.type == TYPE_Parameter) {
            assert(contarg.parameter->index == 0);
            assert(contarg.parameter->label == active_function);
            Label *label = contarg.parameter->label;
            bool use_sret = is_memory_class(label->get_return_type());
            if (use_sret) {
                auto it = param2value.find(contarg.parameter);
                assert (it != param2value.end());
                if (retvalue) {
                    LLVMBuildStore(builder, retvalue, it->second);
                }
                LLVMBuildRetVoid(builder);
            } else {
                if (retvalue) {
                    LLVMBuildRet(builder, retvalue);
                } else {
                    LLVMBuildRetVoid(builder);
                }
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

        LLVMSetCurrentDebugLocation(builder, nullptr);

    }
#undef READ_ANY
#undef READ_VALUE
#undef READ_TYPE
#undef READ_LABEL_VALUE

    void process_labels() {
        while (!bb_label_todo.empty()) {
            auto it = bb_label_todo.back();
            active_function = it.first;
            Label *label = it.second;
            bb_label_todo.pop_back();

            auto it3 = label2func.find(active_function);
            assert(it3 != label2func.end());
            LLVMValueRef func = it3->second;

            auto it2 = label2bb.find({func, label});
            assert(it2 != label2bb.end());
            LLVMBasicBlockRef bb = it2->second;
            LLVMPositionBuilderAtEnd(builder, bb);

            write_label_body(label);
        }
    }

    LLVMBasicBlockRef label_to_basic_block(Label *label) {
        auto old_bb = LLVMGetInsertBlock(builder);
        LLVMValueRef func = LLVMGetBasicBlockParent(old_bb);
        auto it = label2bb.find({func, label});
        if (it == label2bb.end()) {
            const char *name = label->name.name()->data;
            auto bb = LLVMAppendBasicBlock(func, name);
            label2bb.insert({{func, label}, bb});
            bb_label_todo.push_back({active_function, label});
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
            
            LLVMPositionBuilderAtEnd(builder, old_bb);
            return bb;
        } else {
            return it->second;
        }
    }

    LLVMValueRef label_to_function(Label *label, bool root_function = false) {
        auto it = label2func.find(label);
        if (it == label2func.end()) {

            const Anchor *old_anchor = get_active_anchor();
            set_active_anchor(label->anchor);
            Label *last_function = active_function;
            active_function = label;

            auto old_bb = LLVMGetInsertBlock(builder);
            
            const char *name;
            if (root_function && (label->name == SYM_Unnamed)) {
                name = "unnamed";
            } else {
                name = label->name.name()->data;
            }

            auto &&params = label->params;
            //auto &&contparam = params[0];

            label->verify_compilable();
            auto ilfunctype = label->get_function_type();
            auto fi = cast<FunctionType>(ilfunctype);
            bool use_sret = is_memory_class(fi->return_type);

            auto functype = type_to_llvm_type(ilfunctype);

            size_t paramcount = label->params.size() - 1;

            auto func = LLVMAddFunction(module, name, functype);
            LLVMSetLinkage(func, LLVMPrivateLinkage);

            if (use_debug_info) {
                LLVMSetFunctionSubprogram(func, label_to_subprogram(label));
            }

            auto bb = LLVMAppendBasicBlock(func, "");
            LLVMPositionBuilderAtEnd(builder, bb);

            size_t offset = 0;
            if (use_sret) {
                offset++;         
                Parameter *param = params[0];
                param2value[param] = LLVMGetParam(func, 0);
            }

            for (size_t i = 0; i < paramcount; ++i) {
                Parameter *param = params[i + 1];
                LLVMValueRef val = LLVMGetParam(func, i + offset);
                if (is_memory_class(param->type)) {
                    val = LLVMBuildLoad(builder, val, "");
                }
                param2value[param] = val;
            }

            label2func[label] = func;

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
        di_builder = LLVMCreateDIBuilder(module);
        define_builtin_functions();

        if (use_debug_info) {
            const char *DebugStr = "Debug Info Version";
            LLVMValueRef DbgVer[3];
            DbgVer[0] = LLVMConstInt(i32T, 1, 0);
            DbgVer[1] = LLVMMDString(DebugStr, strlen(DebugStr));
            DbgVer[2] = LLVMConstInt(i32T, 3, 0);
            LLVMAddNamedMetadataOperand(module, "llvm.module.flags",
                LLVMMDNode(DbgVer, 3));

            LLVMDIBuilderCreateCompileUnit(di_builder,
                llvm::dwarf::DW_LANG_C99, "file", "directory", "scopes",
                false, "", 0, "", 0);
            //LLVMAddNamedMetadataOperand(module, "llvm.dbg.cu", dicu);
        }

        auto func = label_to_function(entry, true);
        LLVMSetLinkage(func, LLVMExternalLinkage);
        process_labels();

        finalize_types();

        LLVMDisposeBuilder(builder);
        LLVMDisposeDIBuilder(di_builder);

#if SCOPES_DEBUG_CODEGEN
        LLVMDumpModule(module);
#endif
        char *errmsg = NULL;
        if (LLVMVerifyModule(module, LLVMReturnStatusAction, &errmsg)) {
            LLVMDumpModule(module);
            location_error(
                String::join(
                    String::from("LLVM: "),
                    String::from_cstr(errmsg)));
        }
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
    CF_NoOpts           = (1 << 2),
    CF_DumpFunction     = (1 << 3),
    CF_DumpTime         = (1 << 4),
    CF_NoDebugInfo      = (1 << 5),
};

static DisassemblyListener *disassembly_listener = nullptr;
static Any compile(Label *fn, uint64_t flags) {
//#ifdef SCOPES_WIN32
    flags |= CF_NoDebugInfo;
//#endif

    fn->verify_compilable();
    const Type *functype = Pointer(fn->get_function_type());

    GenerateCtx ctx;
    if (flags & CF_NoDebugInfo) {
        ctx.use_debug_info = false;
    }
    auto result = ctx.generate(fn);

    auto module = result.first;
    auto func = result.second;
    assert(func);

    if (!ee) {
        char *errormsg = nullptr;

        LLVMMCJITCompilerOptions opts;
        LLVMInitializeMCJITCompilerOptions(&opts, sizeof(opts));
        opts.OptLevel = 0;
        opts.NoFramePointerElim = true;

        if (LLVMCreateMCJITCompilerForModule(&ee, module, &opts,
            sizeof(opts), &errormsg)) {
            location_error(String::from_cstr(errormsg));
        }

        llvm::ExecutionEngine *pEE = reinterpret_cast<llvm::ExecutionEngine*>(ee);
        disassembly_listener = new DisassemblyListener(pEE);
        pEE->RegisterJITEventListener(disassembly_listener);
    } else {
        LLVMAddModule(ee, module);
    }

#if SCOPES_OPTIMIZE_ASSEMBLY
    if (!(flags & CF_NoOpts)) {
        build_and_run_opt_passes(module);
    }
#endif
    if (flags & CF_DumpModule) {
        LLVMDumpModule(module);
    } else if (flags & CF_DumpFunction) {
        LLVMDumpValue(func);
    }

    void *pfunc = LLVMGetPointerToGlobal(ee, func);
    if (flags & CF_DumpDisassembly) {
        assert(disassembly_listener);
        //auto td = LLVMGetExecutionEngineTargetData(ee);
        auto tm = LLVMGetExecutionEngineTargetMachine(ee);
        auto it = disassembly_listener->sizes.find(pfunc);
        if (it != disassembly_listener->sizes.end()) {
            std::cout << "disassembly:\n";
            do_disassemble(tm, pfunc, it->second);
        } else {
            std::cout << "no disassembly available\n";
        }
    }

#if 0
        if (flags & CF_DumpTime) {
            auto tt = compile_timer.getTotalTime();
            std::cout << "compile time: " << (tt.getUserTime() * 1000.0) << "ms" << std::endl;
        }
#endif

    return Any::from_pointer(functype, pfunc);
}

//------------------------------------------------------------------------------
// COMMON ERRORS
//------------------------------------------------------------------------------

void invalid_op2_types_error(const Type *A, const Type *B) {
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
#if SCOPES_DEBUG_CODEGEN
    StyledStream ss_cout;
#endif

    Label *start_entry;

    NormalizeCtx() :
#if SCOPES_DEBUG_CODEGEN
        ss_cout(std::cout),
#endif
        start_entry(nullptr)
    {}

    ILNode *node_from_continuation(Any cont) {
        if (cont.type == TYPE_Nothing) {
            return nullptr;
        } else if (cont.type == TYPE_Label) {
            return cont.label;
        } else if (cont.type == TYPE_Parameter) {
            return cont.parameter;
        } else {
            StyledString ss;
            ss.out << "don't know how to apply continuation of type " << cont.type;
            location_error(ss.str());
        }
        return nullptr;
    }

    // inlining the continuation of a branch label without arguments
    Label *inline_branch_continuation(Label *label, Any cont) {
        if (!is_basic_block_like(label)) {
            StyledString ss;
            ss.out << "branch destination must be label, not function" << std::endl;
            location_error(ss.str());
        }
        return label;
    }

    Any type_continuation(Any dest, const ArgTypes &argtypes) {
        //ss_cout << "type_continuation: " << dest << std::endl;

        if (dest.type == TYPE_Parameter) {
            Parameter *param = dest.parameter;
            if (param->type == TYPE_Nothing) {
                location_error(String::from("attempting to call none continuation"));
            } else if (param->type == TYPE_Void) {
                param->type = TypedLabel(argtypes);
            } else {
                param->type = TypeSet({param->type, TypedLabel(argtypes)});
            }
        } else if (dest.type == TYPE_Label) {
            dest = typify(dest.label, argtypes);
        } else {
            apply_type_error(dest);
        }
        return dest;
    }

    static void verify_integer_ops(Any a, Any b) {
        verify_integer(storage_type(a.indirect_type()));
        verify(a.indirect_type(), b.indirect_type());
    }

    static void verify_real_ops(Any a, Any b) {
        verify_real(storage_type(a.indirect_type()));
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

    static bool has_params(Label *l) {
        return l->params.size() > 1;
    }

    static bool has_keyed_args(Label *l) {
        auto &&args = l->body.args;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i].key != SYM_Unnamed)
                return true;
        }
        return false;
    }

    static void verify_no_keyed_args(Label *l) {
        auto &&args = l->body.args;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i].key != SYM_Unnamed) {
                location_error(String::from("unexpected keyed argument"));
            }
        }

    }

    static bool is_jumping(Label *l) {
        auto &&args = l->body.args;
        assert(!args.empty());
        return args[0].value.type == TYPE_Nothing;
    }

    static bool is_continuing_to_label(Label *l) {
        auto &&args = l->body.args;
        assert(!args.empty());
        return args[0].value.type == TYPE_Label;
    }

    static bool is_calling_label(Label *l) {
        auto &&enter = l->body.enter;
        return enter.type == TYPE_Label;
    }

    static bool is_return_parameter(Any val) {
        return (val.type == TYPE_Parameter) && (val.parameter->index == 0);
    }

    static bool is_calling_continuation(Label *l) {
        auto &&enter = l->body.enter;
        return (enter.type == TYPE_Parameter) && (enter.parameter->index == 0);
    }

    static bool is_calling_builtin(Label *l) {
        auto &&enter = l->body.enter;
        return enter.type == TYPE_Builtin;
    }

    static bool is_calling_callable(Label *l) {
        if (l->body.is_rawcall())
            return false;
        auto &&enter = l->body.enter;
        const Type *T = enter.indirect_type();
        Any value = none;
        return T->lookup_call_handler(value);
    }

    static bool is_calling_function(Label *l) {
        auto &&enter = l->body.enter;
        return is_function_pointer(enter.indirect_type());
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
            if ((args[i].value.type == TYPE_Parameter)
                && (args[i].value.parameter->index != 0)
                && (!args[i].value.parameter->is_typed()))
                return false;
        }
        return true;
    }

    static bool all_args_constant(Label *l) {
        auto &&args = l->body.args;
        for (size_t i = 1; i < args.size(); ++i) {
            if (!is_const(args[i].value))
                return false;
        }
        return true;
    }

    static bool has_foldable_args(Label *l) {
        auto &&args = l->body.args;
        for (size_t i = 1; i < args.size(); ++i) {
            if (is_const(args[i].value))
                return true;
            else if (is_return_parameter(args[i].value))
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
        return (args[0].value.type == TYPE_Label) && (args[0].value.label == callee);
    }

    static bool is_continuing_from(Parameter *callee, Label *caller) {
        auto &&args = caller->body.args;
        assert(!args.empty());
        return (args[0].value.type == TYPE_Parameter) && (args[0].value.parameter == callee);
    }

    void verify_function_argument_signature(const FunctionType *fi, Label *l) {
        auto &&args = l->body.args;
        verify_function_argument_count(fi, args.size() - 1);

        size_t fargcount = fi->argument_types.size();
        for (size_t i = 1; i < args.size(); ++i) {
            KeyAny &arg = args[i];
            size_t k = i - 1;
            const Type *argT = arg.value.indirect_type();
            if (k < fargcount) {
                const Type *ft = fi->argument_types[k];
                if (storage_type(ft) != storage_type(argT)) {
                    StyledString ss;
                    ss.out << "argument of type " << ft << " expected, got " << argT;
                    location_error(ss.str());
                }
            }
        }
    }

    void argtypes_from_function_call(Label *l, std::vector<const Type *> &retargtypes) {

        auto &&enter = l->body.enter;
        //auto &&args = l->body.args;

        const FunctionType *fi = extract_function_type(enter.indirect_type());

        verify_function_argument_signature(fi, l);

        retargtypes = { TYPE_Nothing };
        if (fi->return_type != TYPE_Void) {
            if (isa<TupleType>(fi->return_type)) {
                auto ti = cast<TupleType>(fi->return_type);
                for (size_t i = 0; i < ti->types.size(); ++i) {
                    retargtypes.push_back(ti->types[i]);
                }
            } else {
                retargtypes.push_back(fi->return_type);
            }
        }
    }

    void fold_pure_function_call(Label *l) {
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "folding pure function call in " << l << std::endl;
#endif

        auto &&enter = l->body.enter;
        auto &&args = l->body.args;

        auto pi = cast<PointerType>(enter.type);
        auto fi = cast<FunctionType>(pi->element_type);

        verify_function_argument_signature(fi, l);

        assert(!args.empty());
        Any result = none;

        if (fi->flags & FF_Variadic) {
            // convert C types
            size_t argcount = args.size() - 1;
            Args cargs;
            cargs.reserve(argcount);
            for (size_t i = 0; i < argcount; ++i) {
                KeyAny &srcarg = args[i + 1];
                if (i >= fi->argument_types.size()) {
                    if (srcarg.value.type == TYPE_F32) {
                        cargs.push_back(KeyAny(srcarg.key, (double)srcarg.value.f32));
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
        enter = args[0].value;
        args = { KeyAny() };
        if (fi->return_type != TYPE_Void) {
            if (isa<TupleType>(fi->return_type)) {
                // unpack
                auto ti = cast<TupleType>(fi->return_type);
                size_t count = ti->types.size();
                for (size_t i = 0; i < count; ++i) {
                    args.push_back(KeyAny(ti->unpack(result.pointer, i)));
                }
            } else {
                args.push_back(KeyAny(result));
            }
        }
        l->link_backrefs();
    }

    void solve_keyed_args(Label *l) {
        Label *enter = l->get_label_enter();

        auto &&args = l->body.args;
        assert(!args.empty());
        Args newargs;
        newargs.reserve(args.size());
        newargs.push_back(args[0]);
        Parameter *vaparam = nullptr;
        if (!enter->params.empty() && enter->params.back()->is_vararg()) {
            vaparam = enter->params.back();
        }
        std::vector<bool> mapped;
        mapped.reserve(args.size());
        mapped.push_back(true);
        size_t next_index = 1;
        for (size_t i = 1; i < args.size(); ++i) {
            auto &&arg = args[i];
            if (arg.key == SYM_Unnamed) {
                while ((next_index < mapped.size()) && mapped[next_index])
                    next_index++;
                while (mapped.size() <= next_index) {
                    mapped.push_back(false);
                    newargs.push_back(none);
                }
                mapped[next_index] = true;
                newargs[next_index] = arg;
                next_index++;
            } else {
                auto param = enter->get_param_by_name(arg.key);
                size_t index = -1;
                if (param && (param != vaparam)) {
                    while (mapped.size() <= (size_t)param->index) {
                        mapped.push_back(false);
                        newargs.push_back(none);
                    }
                    if (mapped[param->index]) {
                        StyledString ss;
                        ss.out << "duplicate binding to parameter " << arg.key;
                        location_error(ss.str());
                    }
                    index = param->index;
                } else if (vaparam) {
                    while (mapped.size() < (size_t)vaparam->index) {
                        mapped.push_back(false);
                        newargs.push_back(none);
                    }
                    index = newargs.size();
                    mapped.push_back(false);
                    newargs.push_back(none);
                    newargs[index].key = arg.key;
                } else {
                    // no such parameter, map like regular parameter
                    while ((next_index < mapped.size()) && mapped[next_index])
                        next_index++;
                    while (mapped.size() <= next_index) {
                        mapped.push_back(false);
                        newargs.push_back(none);
                    }
                    index = next_index;
                    newargs[index].key = SYM_Unnamed;
                    next_index++;
                }
                mapped[index] = true;
                newargs[index].value = arg.value;
            }
        }
        l->unlink_backrefs();
        args = newargs;
        l->link_backrefs();
    }

    bool fold_type_label_arguments(Label *l) {
        if (!has_foldable_args(l)
            && all_params_typed(l->get_label_enter())) {
            return false;
        }

#if SCOPES_DEBUG_CODEGEN
        ss_cout << "folding & typing arguments in " << l << std::endl;
#endif

        auto &&enter = l->body.enter;
        assert(enter.type == TYPE_Label);

        // inline constant arguments
        Args callargs;

        Any anyval = none;
        anyval.type = TYPE_Void;

        Args keys;
        auto &&args = l->body.args;
        callargs.push_back(args[0]);
        keys.push_back(KeyAny(anyval));
        for (size_t i = 1; i < args.size(); ++i) {
            auto &&arg = args[i];
            if (is_const(arg.value)) {
                keys.push_back(arg);
            } else if (is_return_parameter(arg.value)) {
                keys.push_back(arg);
            } else {
                keys.push_back(KeyAny(arg.key, unknown_of(arg.value.indirect_type())));
                callargs.push_back(arg);
            }
        }

        Label *newl = fold_type_label(enter.label, keys);
        l->unlink_backrefs();
        enter = newl;
        args = callargs;
        l->link_backrefs();

        return true;
    }

    // returns true if the builtin folds regardless of whether the arguments are
    // constant
    bool builtin_always_folds(Builtin builtin) {
        switch(builtin.value()) {
        case FN_TypeOf:
        case FN_IsConstant:
        case FN_VaCountOf:
        case FN_VaKeys:
        case FN_VaAt:
        case FN_Location:
        case FN_Dump:
        case FN_ExternNew:
        case FN_ExternSymbol:
            return true;
        default: return false;
        }
    }

    bool builtin_has_keyed_args(Builtin builtin) {
        switch(builtin.value()) {
        case FN_VaCountOf:
        case FN_VaKeys:
        case FN_VaAt:
        case FN_Dump:
            return true;
        default: return false;
        }
    }

    bool builtin_never_folds(Builtin builtin) {
        switch(builtin.value()) {
        case FN_Unconst:
        case FN_Undef:
        case FN_NullOf:
        case FN_Alloca:
        case FN_AllocaArray:
        case FN_Malloc:
        case FN_MallocArray:
        case SFXFN_Unreachable:
            return true;
        default: return false;
        }
    }

#define CHECKARGS(MINARGS, MAXARGS) \
    checkargs<MINARGS, MAXARGS>(args.size())

#define RETARGTYPES(...) \
    retargtypes = { TYPE_Nothing, __VA_ARGS__ }

    void argtypes_from_builtin_call(Label *l, std::vector<const Type *> &retargtypes) {
        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        assert(enter.type == TYPE_Builtin);
        switch(enter.builtin.value()) {
        case OP_Tertiary: {
            CHECKARGS(3, 3);
            verify(TYPE_Bool, args[1].value.indirect_type());
            verify(args[2].value.indirect_type(), args[3].value.indirect_type());
            RETARGTYPES(args[2].value.indirect_type());
        } break;
        case FN_Unconst: {
            CHECKARGS(1, 1);
            auto T = args[1].value.indirect_type();
            auto et = dyn_cast<ExternType>(T);
            if (et) {
                RETARGTYPES(Pointer(et->type));
            } else {
                RETARGTYPES(T);
            }
        } break;
        case FN_Bitcast: {
            CHECKARGS(2, 2);
            // todo: verify source and dest type are non-aggregate
            // also, both must be of same category
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            RETARGTYPES(DestT);
        } break;
        case FN_IntToPtr: {
            CHECKARGS(2, 2);
            verify_integer(storage_type(args[1].value.indirect_type()));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_kind<TK_Pointer>(storage_type(DestT));
            RETARGTYPES(DestT);
        } break;
        case FN_PtrToInt: {
            CHECKARGS(2, 2);
            verify_kind<TK_Pointer>(
                storage_type(args[1].value.indirect_type()));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(storage_type(DestT));
            RETARGTYPES(DestT);
        } break;
        case FN_Trunc: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.indirect_type();
            verify_integer(storage_type(T));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(storage_type(DestT));
            RETARGTYPES(DestT);
        } break;
        case FN_FPTrunc: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.indirect_type();
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            if (cast<RealType>(T)->width >= cast<RealType>(DestT)->width) {
            } else { invalid_op2_types_error(T, DestT); }
            RETARGTYPES(DestT);
        } break;
        case FN_FPExt: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.indirect_type();
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            if (cast<RealType>(T)->width <= cast<RealType>(DestT)->width) {
            } else { invalid_op2_types_error(T, DestT); }
            RETARGTYPES(DestT);
        } break;
        case FN_FPToUI: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(DestT);
            if ((T == TYPE_F32) || (T == TYPE_F64)) {
            } else {
                invalid_op2_types_error(T, DestT);
            }
            RETARGTYPES(DestT);
        } break;
        case FN_FPToSI: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(DestT);
            if ((T == TYPE_F32) || (T == TYPE_F64)) {
            } else {
                invalid_op2_types_error(T, DestT);
            }
            RETARGTYPES(DestT);
        } break;
        case FN_UIToFP: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_integer(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            if ((DestT == TYPE_F32) || (DestT == TYPE_F64)) {
            } else {
                invalid_op2_types_error(T, DestT);
            }
            RETARGTYPES(DestT);
        } break;
        case FN_SIToFP: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_integer(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            if ((DestT == TYPE_F32) || (DestT == TYPE_F64)) {
            } else {
                invalid_op2_types_error(T, DestT);
            }
            RETARGTYPES(DestT);
        } break;
        case FN_ZExt: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.indirect_type();
            verify_integer(storage_type(T));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(storage_type(DestT));
            RETARGTYPES(DestT);
        } break;
        case FN_SExt: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.indirect_type();
            verify_integer(storage_type(T));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(storage_type(DestT));
            RETARGTYPES(DestT);
        } break;
        case FN_ExtractValue: {
            CHECKARGS(2, 2);
            size_t idx = cast_number<size_t>(args[2].value);
            const Type *T = storage_type(args[1].value.indirect_type());
            switch(T->kind()) {
            case TK_Array: {
                auto ai = cast<ArrayType>(T);
                RETARGTYPES(ai->type_at_index(idx));
            } break;
            case TK_Tuple: {
                auto ti = cast<TupleType>(T);
                RETARGTYPES(ti->type_at_index(idx));
            } break;
            case TK_Union: {
                auto ui = cast<UnionType>(T);
                RETARGTYPES(ui->type_at_index(idx));
            } break;
            default: {
                StyledString ss;
                ss.out << "can not extract value from type " << T;
                location_error(ss.str());
            } break;
            }
        } break;
        case FN_InsertValue: {
            CHECKARGS(3, 3);
            const Type *T = storage_type(args[1].value.indirect_type());
            const Type *ET = storage_type(args[2].value.indirect_type());
            size_t idx = cast_number<size_t>(args[3].value);
            switch(T->kind()) {
            case TK_Array: {
                auto ai = cast<ArrayType>(T);
                verify(storage_type(ai->type_at_index(idx)), ET);
            } break;
            case TK_Tuple: {
                auto ti = cast<TupleType>(T);
                verify(storage_type(ti->type_at_index(idx)), ET);
            } break;
            case TK_Union: {
                auto ui = cast<UnionType>(T);
                verify(storage_type(ui->type_at_index(idx)), ET);
            } break;
            default: {
                StyledString ss;
                ss.out << "can not insert value into type " << T;
                location_error(ss.str());
            } break;
            }
            RETARGTYPES(args[1].value.indirect_type());
        } break;
        case FN_Undef: {
            CHECKARGS(1, 1);
            args[1].value.verify(TYPE_Type);
            RETARGTYPES(args[1].value.typeref);
        } break;
        case FN_NullOf: {
            CHECKARGS(1, 1);
            args[1].value.verify(TYPE_Type);
            RETARGTYPES(args[1].value.typeref);
        } break;
        case FN_Malloc:
        case FN_Alloca: {
            CHECKARGS(1, 1);
            args[1].value.verify(TYPE_Type);
            RETARGTYPES(Pointer(args[1].value.typeref));
        } break;
        case FN_MallocArray:
        case FN_AllocaArray: {
            CHECKARGS(2, 2);
            args[1].value.verify(TYPE_Type);
            verify_integer(storage_type(args[2].value.indirect_type()));
            RETARGTYPES(Pointer(args[1].value.typeref));
        } break;
        case FN_Free: {
            CHECKARGS(1, 1);
            verify_kind<TK_Pointer>(args[1].value.indirect_type());
            RETARGTYPES();
        } break;
        case FN_GetElementPtr: {
            CHECKARGS(2, -1);
            const Type *T = storage_type(args[1].value.indirect_type());
            verify_kind<TK_Pointer>(T);
            auto pi = cast<PointerType>(T);
            T = pi->element_type;
            verify_integer(storage_type(args[2].value.indirect_type()));
            for (size_t i = 3; i < args.size(); ++i) {
                
                const Type *ST = storage_type(T);
                auto &&arg = args[i];
                switch(ST->kind()) {
                case TK_Array: {
                    auto ai = cast<ArrayType>(ST);
                    T = ai->element_type;
                    verify_integer(storage_type(arg.value.indirect_type()));
                } break;
                case TK_Tuple: {
                    auto ti = cast<TupleType>(ST);
                    size_t idx = 0;
                    if ((T->kind() == TK_Typename) && (arg.value.type == TYPE_Symbol)) {
                        idx = cast<TypenameType>(T)->field_index(arg.value.symbol);
                        if (idx == (size_t)-1) {
                            StyledString ss;
                            ss.out << "no such field " << arg.value.symbol << " in typename " << T;
                            location_error(ss.str());
                        }
                        // rewrite field
                        arg = KeyAny(arg.key, Any((int)idx));
                    } else {
                        idx = cast_number<size_t>(arg.value);
                    }
                    T = ti->type_at_index(idx);
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
        case FN_VolatileLoad:
        case FN_Load: {
            CHECKARGS(1, 1);
            const Type *T = storage_type(args[1].value.indirect_type());
            verify_kind<TK_Pointer>(T);
            auto pi = cast<PointerType>(T);
            RETARGTYPES(pi->element_type);
        } break;
        case FN_VolatileStore:
        case FN_Store: {
            CHECKARGS(2, 2);
            const Type *T = storage_type(args[2].value.indirect_type());
            verify_kind<TK_Pointer>(T);
            auto pi = cast<PointerType>(T);
            verify(storage_type(pi->element_type),
                storage_type(args[1].value.indirect_type()));
            RETARGTYPES();
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
            verify_integer_ops(args[1].value, args[2].value);
            RETARGTYPES(TYPE_Bool);
        } break;
        case OP_FCmpOEQ:
        case OP_FCmpONE:
        case OP_FCmpORD:
        case OP_FCmpOGT:
        case OP_FCmpOGE:
        case OP_FCmpOLT:
        case OP_FCmpOLE:
        case OP_FCmpUEQ:
        case OP_FCmpUNE:
        case OP_FCmpUNO:
        case OP_FCmpUGT:
        case OP_FCmpUGE:
        case OP_FCmpULT:
        case OP_FCmpULE: {
            CHECKARGS(2, 2);
            verify_real_ops(args[1].value, args[2].value);
            RETARGTYPES(TYPE_Bool);
        } break;
#define IARITH_NUW_NSW_OPS(NAME, OP) \
    case OP_ ## NAME: \
    case OP_ ## NAME ## NUW: \
    case OP_ ## NAME ## NSW: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1].value, args[2].value); \
        RETARGTYPES(args[1].value.indirect_type()); \
    } break;
#define IARITH_OP(NAME, OP, PFX) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1].value, args[2].value); \
        RETARGTYPES(args[1].value.indirect_type()); \
    } break;
#define FARITH_OP(NAME, OP) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_real_ops(args[1].value, args[2].value); \
        RETARGTYPES(args[1].value.indirect_type()); \
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
    enter = args[0].value; \
    args = { none, __VA_ARGS__ }; \
    l->link_backrefs();

    void print_traceback() {
        StyledStream ss(std::cerr);
        for (size_t i = 0; i < todo.size(); ++i) {
            Label *l = todo[i];
            ss << l->body.anchor << " in ";
            if (l->name == SYM_Unnamed) {
                ss << "anonymous function";
            } else {
                ss << l->name.name()->data;
            }
            ss << std::endl;
            l->body.anchor->stream_source_line(ss);
        }
    }

    void *aligned_alloc(size_t sz, size_t al) {
        assert(sz);
        assert(al);
        return reinterpret_cast<void *>(
            ::align(reinterpret_cast<uintptr_t>(malloc(sz + al - 1)), al));
    }

    void *copy_storage(const Type *T, void *ptr) {
        size_t sz = size_of(T);
        size_t al = align_of(T);
        void *destptr = aligned_alloc(sz, al);
        memcpy(destptr, ptr, sz);
        return destptr;
    }

    void fold_callable_call(Label *l) {
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "folding callable call in " << l << std::endl;
#endif

        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        const Type *T = enter.indirect_type();

        Any value = none;
        auto result = T->lookup_call_handler(value);
        assert(result);
        l->unlink_backrefs();
        args.insert(args.begin() + 1, KeyAny(enter));
        enter = value;
        l->link_backrefs();
    }

    bool isnan(float f) {
        return f != f;
    }
    bool isnan(double f) {
        return f != f;
    }

    bool fold_builtin_call(Label *l) {
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "folding builtin call in " << l << std::endl;
#endif

        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        assert(enter.type == TYPE_Builtin);
        switch(enter.builtin.value()) {
        case FN_ExternSymbol: {
            CHECKARGS(1, 1);
            verify_kind<TK_Extern>(args[1].value);
            RETARGS(args[1].value.symbol);
        } break;
        case FN_ExternNew: {
            CHECKARGS(2, 2);
            args[1].value.verify(TYPE_Symbol);
            const Type *T = args[2].value;
            Any value(args[1].value.symbol);
            value.type = Extern(T);
            RETARGS(value);
        } break;
        case FN_FunctionType: {
            CHECKARGS(1, -1);
            std::vector<const Type *> types;
            size_t k = 2;
            while (k < args.size()) {
                if (args[k].value.type != TYPE_Type)
                    break;
                types.push_back(args[k].value);
                k++;
            }
            uint32_t flags = 0;
            
            while (k < args.size()) {
                args[k].value.verify(TYPE_Symbol);
                Symbol sym = args[k].value.symbol;
                uint64_t flag = 0;
                switch(sym.value()) {
                case SYM_Variadic: flag = FF_Variadic; break;
                case SYM_Pure: flag = FF_Pure; break;
                default: {
                    StyledString ss;
                    ss.out << "illegal option: " << sym;
                    location_error(ss.str());
                } break;
                }
                flags |= flag;
                k++;
            }
            RETARGS(Function(args[1].value, types, flags));
        } break;
        case FN_TupleType: {
            CHECKARGS(0, -1);
            std::vector<const Type *> types;
            for (size_t i = 1; i < args.size(); ++i) {
                types.push_back(args[i].value);
            }
            RETARGS(Tuple(types));
        } break;
        case FN_Location: {
            CHECKARGS(0, 0);
            RETARGS(l->body.anchor);
        } break;
        case SFXFN_SetTypenameStorage: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value;
            const Type *T2 = args[2].value;
            verify_kind<TK_Typename>(T);
            cast<TypenameType>(const_cast<Type *>(T))->finalize(T2);
            RETARGS();
        } break;
        case SFXFN_SetTypeSymbol: {
            CHECKARGS(3, 3);
            const Type *T = args[1].value;
            args[2].value.verify(TYPE_Symbol);
            const_cast<Type *>(T)->bind(args[2].value.symbol, args[3].value);
            RETARGS();
        } break;
        case SFXFN_DelTypeSymbol: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value;
            args[2].value.verify(TYPE_Symbol);
            const_cast<Type *>(T)->del(args[2].value.symbol);
            RETARGS();
        } break;
        case FN_TypeAt: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value;
            args[2].value.verify(TYPE_Symbol);
            Any result = none;
            if (!T->lookup(args[2].value.symbol, result)) {
                RETARGS(none, false);
            } else {
                RETARGS(result, true);
            }
        } break;
        case FN_IsConstant: {
            CHECKARGS(1, 1);
            RETARGS(is_const(args[1].value));
        } break;
        case FN_VaCountOf: {
            RETARGS((int)(args.size()-1));
        } break;
        case FN_VaKeys: {
            CHECKARGS(0, -1);
            Args result = { none };
            for (size_t i = 1; i < args.size(); ++i) {
                result.push_back(args[i].key);
            }
            l->unlink_backrefs();
            enter = args[0].value;
            args = result;
            l->link_backrefs();
        } break;
        case FN_VaAt: {
            CHECKARGS(1, -1);
            Args result = { none };
            if (args[1].value.type == TYPE_Symbol) {
                auto key = args[1].value.symbol;
                for (size_t i = 2; i < args.size(); ++i) {
                    if (args[i].key == key) {
                        result.push_back(args[i]);
                    }
                }
            } else {
                size_t idx = cast_number<size_t>(args[1].value);
                for (size_t i = (idx + 2); i < args.size(); ++i) {
                    result.push_back(args[i]);
                }
            }
            l->unlink_backrefs();
            enter = args[0].value;
            args = result;
            l->link_backrefs();
        } break;
        case FN_Branch: {
            CHECKARGS(3, 3);
            args[1].value.verify(TYPE_Bool);
            // either branch label is typed and binds no parameters,
            // so we can directly inline it
            Label *newl = nullptr;
            if (args[1].value.i1) {
                newl = inline_branch_continuation(args[2].value, args[0].value);
            } else {
                newl = inline_branch_continuation(args[3].value, args[0].value);
            }
            copy_body(l, newl);
        } break;
        case OP_Tertiary: {
            CHECKARGS(3, 3);
            args[1].value.verify(TYPE_Bool);
            verify(args[2].value.type, args[3].value.type);
            if (args[1].value.i1) {
                RETARGS(args[2]);
            } else {
                RETARGS(args[3]);
            }
        } break;
        case FN_Bitcast: {
            CHECKARGS(2, 2);
            // todo: verify source and dest type are non-aggregate
            // also, both must be of same category
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            Any result = args[1].value;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_IntToPtr: {
            CHECKARGS(2, 2);
            verify_integer(storage_type(args[1].value.type));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_kind<TK_Pointer>(storage_type(DestT));
            Any result = args[1].value;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_PtrToInt: {
            CHECKARGS(2, 2);
            verify_kind<TK_Pointer>(storage_type(args[1].value.type));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(storage_type(DestT));
            Any result = args[1].value;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_Trunc: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_integer(storage_type(T));
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(storage_type(DestT));
            Any result = args[1].value;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_FPTrunc: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            if ((T == TYPE_F64) && (DestT == TYPE_F32)) {
                RETARGS((float)args[1].value.f64);
            } else { invalid_op2_types_error(T, DestT); }
        } break;
        case FN_FPExt: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            if ((T == TYPE_F32) && (DestT == TYPE_F64)) {
                RETARGS((double)args[1].value.f32);
            } else { invalid_op2_types_error(T, DestT); }
        } break;
        case FN_FPToUI: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(DestT);
            uint64_t val = 0;
            if (T == TYPE_F32) {
                val = (uint64_t)args[1].value.f32;
            } else if (T == TYPE_F64) {
                val = (uint64_t)args[1].value.f64;
            } else {
                invalid_op2_types_error(T, DestT);
            }
            Any result = val;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_FPToSI: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_real(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_integer(DestT);
            int64_t val = 0;
            if (T == TYPE_F32) {
                val = (int64_t)args[1].value.f32;
            } else if (T == TYPE_F64) {
                val = (int64_t)args[1].value.f64;
            } else {
                invalid_op2_types_error(T, DestT);
            }
            Any result = val;
            result.type = DestT;
            RETARGS(result);
        } break;
        case FN_UIToFP: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_integer(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            uint64_t src = cast_number<uint64_t>(args[1].value);
            Any result = none;
            if (DestT == TYPE_F32) {
                result = (float)src;
            } else if (DestT == TYPE_F64) {
                result = (double)src;
            } else {
                invalid_op2_types_error(T, DestT);
            }
            RETARGS(result);
        } break;
        case FN_SIToFP: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            verify_integer(T);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            verify_real(DestT);
            int64_t src = cast_number<int64_t>(args[1].value);
            Any result = none;
            if (DestT == TYPE_F32) {
                result = (float)src;
            } else if (DestT == TYPE_F64) {
                result = (double)src;
            } else {
                invalid_op2_types_error(T, DestT);
            }
            RETARGS(result);
        } break;
        case FN_ZExt: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            auto ST = storage_type(T);
            verify_integer(ST);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            auto DestST = storage_type(DestT);
            verify_integer(DestST);
            Any result = args[1].value;
            result.type = DestT;
            int oldbitnum = integer_type_bit_size(ST);
            int newbitnum = integer_type_bit_size(DestST);
            for (int i = oldbitnum; i < newbitnum; ++i) {
                result.u64 &= ~(1ull << i);
            }
            RETARGS(result);
        } break;
        case FN_SExt: {
            CHECKARGS(2, 2);
            const Type *T = args[1].value.type;
            auto ST = storage_type(T);
            verify_integer(ST);
            args[2].value.verify(TYPE_Type);
            const Type *DestT = args[2].value.typeref;
            auto DestST = storage_type(DestT);
            verify_integer(DestST);
            Any result = args[1].value;
            result.type = DestT;
            int oldbitnum = integer_type_bit_size(ST);
            int newbitnum = integer_type_bit_size(DestST);
            uint64_t bit = (result.u64 >> (oldbitnum - 1)) & 1ull;
            for (int i = oldbitnum; i < newbitnum; ++i) {
                result.u64 &= ~(1ull << i);
                result.u64 |= bit << i;
            }
            RETARGS(result);
        } break;
        case FN_TypeOf: {
            CHECKARGS(1, 1);
            RETARGS(args[1].value.indirect_type());
        } break;
        case FN_ExtractValue: {
            CHECKARGS(2, 2);
            size_t idx = cast_number<size_t>(args[2].value);
            const Type *T = storage_type(args[1].value.type);
            switch(T->kind()) {
            case TK_Array: {
                auto ai = cast<ArrayType>(T);
                RETARGS(ai->unpack(args[1].value.pointer, idx));
            } break;
            case TK_Tuple: {
                auto ti = cast<TupleType>(T);
                RETARGS(ti->unpack(args[1].value.pointer, idx));
            } break;
            case TK_Union: {
                auto ui = cast<UnionType>(T);
                RETARGS(ui->unpack(args[1].value.pointer, idx));
            } break;
            default: {
                StyledString ss;
                ss.out << "can not extract value from type " << T;
                location_error(ss.str());
            } break;
            }
        } break;
        case FN_InsertValue: {
            CHECKARGS(3, 3);
            const Type *T = storage_type(args[1].value.type);
            const Type *ET = storage_type(args[2].value.type);
            size_t idx = cast_number<size_t>(args[3].value);

            void *destptr = args[1].value.pointer;
            void *offsetptr = nullptr;
            switch(T->kind()) {
            case TK_Array: {
                destptr = copy_storage(T, destptr);
                auto ai = cast<ArrayType>(T);
                verify(storage_type(ai->type_at_index(idx)), ET);
                offsetptr = ai->getelementptr(destptr, idx);
            } break;
            case TK_Tuple: {
                destptr = copy_storage(T, destptr);
                auto ti = cast<TupleType>(T);
                verify(storage_type(ti->type_at_index(idx)), ET);
                offsetptr = ti->getelementptr(destptr, idx);
            } break;
            case TK_Union: {
                destptr = copy_storage(T, destptr);
                auto ui = cast<UnionType>(T);
                verify(storage_type(ui->type_at_index(idx)), ET);
                offsetptr = destptr;
            } break;
            default: {
                StyledString ss;
                ss.out << "can not extract value from type " << T;
                location_error(ss.str());
            } break;
            }
            void *srcptr = get_pointer(ET, args[1].value);
            memcpy(offsetptr, srcptr, size_of(ET));
            RETARGS(Any::from_pointer(args[1].value.type, destptr));
        } break;
        case FN_VolatileLoad:
        case FN_Load: {
            CHECKARGS(1, 1);
            const Type *T = storage_type(args[1].value.type);
            verify_kind<TK_Pointer>(T);
            auto pi = cast<PointerType>(T);
            RETARGS(pi->unpack(args[1].value.pointer));
        } break;
        case FN_VolatileStore:
        case FN_Store: {
            CHECKARGS(2, 2);
            const Type *T = storage_type(args[2].value.type);
            verify_kind<TK_Pointer>(T);
            auto pi = cast<PointerType>(T);
            verify(storage_type(pi->element_type), storage_type(args[1].value.type));
            void *destptr = args[2].value.pointer;
            auto ET = args[1].value.type;
            void *srcptr = get_pointer(ET, args[1].value);
            memcpy(destptr, srcptr, size_of(ET));
            RETARGS();
        } break;
        case FN_GetElementPtr: {
            CHECKARGS(2, -1);
            const Type *T = storage_type(args[1].value.type);
            verify_kind<TK_Pointer>(T);
            auto pi = cast<PointerType>(T);
            T = pi->element_type;
            void *ptr = args[1].value.pointer;
            size_t idx = cast_number<size_t>(args[2].value);
            ptr = pi->getelementptr(ptr, idx);

            for (size_t i = 3; i < args.size(); ++i) {
                const Type *ST = storage_type(T);
                auto &&arg = args[i].value;
                switch(ST->kind()) {
                case TK_Array: {
                    auto ai = cast<ArrayType>(ST);
                    T = ai->element_type;
                    size_t idx = cast_number<size_t>(arg);
                    ptr = ai->getelementptr(ptr, idx);
                } break;
                case TK_Tuple: {
                    auto ti = cast<TupleType>(ST);
                    size_t idx = 0;
                    if ((T->kind() == TK_Typename) && (arg.type == TYPE_Symbol)) {
                        idx = cast<TypenameType>(T)->field_index(arg.symbol);
                        if (idx == (size_t)-1) {
                            StyledString ss;
                            ss.out << "no such field " << arg.symbol << " in typename " << T;
                            location_error(ss.str());
                        }
                        // rewrite field
                        arg = (int)idx;
                    } else {
                        idx = cast_number<size_t>(arg);
                    }
                    T = ti->type_at_index(idx);
                    ptr = ti->getelementptr(ptr, idx);
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
            args[1].value.verify(TYPE_Any);
            Any arg = *args[1].value.ref;
            RETARGS(arg);
        } break;
        case FN_AnyWrap: {
            CHECKARGS(1, 1);
            Any arg = args[1].value.toref();
            arg.type = TYPE_Any;
            RETARGS(arg);
        } break;
        case FN_Purify: {
            CHECKARGS(1, 1);
            Any arg = args[1].value;
            verify_function_pointer(arg.type);
            auto pi = cast<PointerType>(arg.type);
            auto fi = cast<FunctionType>(pi->element_type);
            if (fi->flags & FF_Pure) {
                RETARGS(args[1]);
            } else {
                arg.type = Pointer(Function(
                    fi->return_type, fi->argument_types, fi->flags | FF_Pure));
                RETARGS(arg);
            }
        } break;
        case SFXFN_CompilerError: {
            CHECKARGS(1, 1);
            location_error(args[1].value);
            RETARGS();
        } break;
        case FN_CompilerMessage: {
            CHECKARGS(1, 1);
            args[1].value.verify(TYPE_String);
            StyledString ss;
            ss.out << l->body.anchor << " message: " << args[1].value.string->data << std::endl;
            std::cout << ss.str()->data;
            RETARGS();
        } break;
        case FN_Dump: {
            CHECKARGS(1, -1);
            StyledStream ss(std::cerr);
            ss << l->body.anchor << " dump: ";
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i].key != SYM_Unnamed) {
                    ss << args[i].key << " " << Style_Operator << "=" << Style_None << " ";
                }
                if (is_const(args[i].value)) {
                    stream_expr(ss, args[i].value, StreamExprFormat());
                } else {
                    ss << "<unknown>" 
                        << Style_Operator << ":" << Style_None
                        << args[i].value.indirect_type() << std::endl;
                }
            }
            l->unlink_backrefs();
            enter = args[0].value;
            args[0].value = none;
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
            verify_integer_ops(args[1].value, args[2].value);
#define B_INT_OP2(OP, N) \
    switch(cast<IntegerType>(storage_type(args[1].value.type))->width) { \
    case 1: result = (args[1].value.i1 OP args[2].value.i1); break; \
    case 8: result = (args[1].value.N ## 8 OP args[2].value.N ## 8); break; \
    case 16: result = (args[1].value.N ## 16 OP args[2].value.N ## 16); break; \
    case 32: result = (args[1].value.N ## 32 OP args[2].value.N ## 32); break; \
    case 64: result = (args[1].value.N ## 64 OP args[2].value.N ## 64); break; \
    default: assert(false); break; \
    }
            bool result = false;
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
        case OP_FCmpOEQ:
        case OP_FCmpONE:
        case OP_FCmpORD:
        case OP_FCmpOGT:
        case OP_FCmpOGE:
        case OP_FCmpOLT:
        case OP_FCmpOLE:
        case OP_FCmpUEQ:
        case OP_FCmpUNE:
        case OP_FCmpUNO:
        case OP_FCmpUGT:
        case OP_FCmpUGE:
        case OP_FCmpULT:
        case OP_FCmpULE: {
#define B_FLOAT_OP2(OP) \
    switch(cast<RealType>(storage_type(args[1].value.type))->width) { \
    case 32: result = (args[1].value.f32 OP args[2].value.f32); break; \
    case 64: result = (args[1].value.f64 OP args[2].value.f64); break; \
    default: assert(false); break; \
    }
#define B_FLOAT_OPF2(OP) \
    switch(cast<RealType>(storage_type(args[1].value.type))->width) { \
    case 32: result = OP(args[1].value.f32, args[2].value.f32); break; \
    case 64: result = OP(args[1].value.f64, args[2].value.f64); break; \
    default: assert(false); break; \
    }
            CHECKARGS(2, 2);
            verify_real_ops(args[1].value, args[2].value);
            bool result;
            bool failed = false;
            bool nan;
            switch(cast<RealType>(storage_type(args[1].value.type))->width) {
            case 32: nan = isnan(args[1].value.f32) || isnan(args[2].value.f32); break;
            case 64: nan = isnan(args[1].value.f64) || isnan(args[2].value.f64); break;
            default: assert(false); break;
            }
            switch(enter.builtin.value()) {
            case OP_FCmpOEQ:
            case OP_FCmpONE:
            case OP_FCmpORD:
            case OP_FCmpOGT:
            case OP_FCmpOGE:
            case OP_FCmpOLT:
            case OP_FCmpOLE:
                if (nan) {
                    result = false;
                    failed = true;
                } break;
            case OP_FCmpUEQ:
            case OP_FCmpUNE:
            case OP_FCmpUNO:
            case OP_FCmpUGT:
            case OP_FCmpUGE:
            case OP_FCmpULT:
            case OP_FCmpULE:
                if (nan) {
                    result = true;
                    failed = true;
                } break;
            default: assert(false); break;
            }

            if (!failed) {
                switch(enter.builtin.value()) {
                case OP_FCmpOEQ: B_FLOAT_OP2(==); break;
                case OP_FCmpONE: B_FLOAT_OP2(!=); break;
                case OP_FCmpORD: break;
                case OP_FCmpOGT: B_FLOAT_OP2(>); break;
                case OP_FCmpOGE: B_FLOAT_OP2(>=); break;
                case OP_FCmpOLT: B_FLOAT_OP2(<); break;
                case OP_FCmpOLE: B_FLOAT_OP2(<=); break;
                case OP_FCmpUEQ: B_FLOAT_OP2(==); break;
                case OP_FCmpUNE: B_FLOAT_OP2(!=); break;
                case OP_FCmpUNO: break;
                case OP_FCmpUGT: B_FLOAT_OP2(>); break;
                case OP_FCmpUGE: B_FLOAT_OP2(>=); break;
                case OP_FCmpULT: B_FLOAT_OP2(<); break;
                case OP_FCmpULE: B_FLOAT_OP2(<=); break;
                default: assert(false); break;
                }
            }
            RETARGS(result);
        } break;
#define IARITH_NUW_NSW_OPS(NAME, OP) \
    case OP_ ## NAME: \
    case OP_ ## NAME ## NUW: \
    case OP_ ## NAME ## NSW: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1].value, args[2].value); \
        Any result = none; \
        switch(enter.builtin.value()) { \
        case OP_ ## NAME: B_INT_OP2(OP, u); break; \
        case OP_ ## NAME ## NUW: B_INT_OP2(OP, u); break; \
        case OP_ ## NAME ## NSW: B_INT_OP2(OP, i); break; \
        default: assert(false); break; \
        } \
        result.type = args[1].value.type; \
        RETARGS(result); \
    } break;
#define IARITH_OP(NAME, OP, PFX) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_integer_ops(args[1].value, args[2].value); \
        Any result = none; \
        B_INT_OP2(OP, PFX); \
        result.type = args[1].value.type; \
        RETARGS(result); \
    } break;
#define FARITH_OP(NAME, OP) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_real_ops(args[1].value, args[2].value); \
        Any result = none; \
        B_FLOAT_OP2(OP); \
        RETARGS(result); \
    } break;
#define FARITH_OPF(NAME, OP) \
    case OP_ ## NAME: { \
        CHECKARGS(2, 2); \
        verify_real_ops(args[1].value, args[2].value); \
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
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "inlining branch continuations in " << l << std::endl;
#endif

        auto &&args = l->body.args;
        CHECKARGS(3, 3);
        args[1].value.verify_indirect(TYPE_Bool);
        Label *then_br = inline_branch_continuation(args[2].value, args[0].value);
        Label *else_br = inline_branch_continuation(args[3].value, args[0].value);
        l->unlink_backrefs();
        args[0].value = none;
        args[2].value = then_br;
        args[3].value = else_br;
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
        const Type *T = l->params[0]->type;
        if (isa<TypeSetType>(T)) return true;
        if (!isa<TypedLabelType>(T)) return false;
        auto tli = cast<TypedLabelType>(T);
        for (size_t i = 1; i < tli->types.size(); ++i) {
            if (tli->types[i] == TYPE_Label) return true;
            if (tli->types[i] == TYPE_Type) return true;
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
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "deleting continuation of " << owner << std::endl;
#endif

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
                if (is_called_by(param, user)) {
                    set_active_anchor(user->body.anchor);
                    {
                        StyledStream ss;
                        stream_label(ss, user, StreamLabelFormat());
                        ss << owner->anchor 
                            << " while lowering function to label" << std::endl;
                        owner->anchor->stream_source_line(ss);
                    }
                    StyledString ss;
                    ss.out << "attempting to return from lowered function";
                    location_error(ss.str());
                }
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
                    /*
                    if (is_continuing_to_label(user)) {
                        set_active_anchor(user->body.anchor);
                        location_error(String::from("return call must be last expression in function"));
                    }*/
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
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "inlining label call to " << enter_label << " in " << l << std::endl;
#endif

        Any voidarg = none;
        voidarg.type = TYPE_Void;

        Args newargs = { args[0] };
        for (size_t i = 1; i < enter_label->params.size(); ++i) {
            newargs.push_back(voidarg);
        }
        Label *newl = fold_type_label(enter_label, newargs);
        l->unlink_backrefs();
        args[0] = none;
        enter = newl;
        l->link_backrefs();
    }

    void type_continuation_from_label_return_type(Label *l) {
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "typing continuation from label return type in " << l << std::endl;
#endif
        auto &&enter = l->body.enter;
        auto &&args = l->body.args;
        assert(enter.type == TYPE_Label);
        Label *enter_label = enter.label;
        assert(!args.empty());
        assert(!enter_label->params.empty());
        Parameter *cont_param = enter_label->params[0];
        const Type *cont_type = cont_param->type;

        if (isa<TypedLabelType>(cont_type)) {
            auto tli = cast<TypedLabelType>(cont_type);
            Any newarg = type_continuation(args[0].value, tli->types);
            l->unlink_backrefs();
            args[0] = newarg;
            l->link_backrefs();
        } else {
#if SCOPES_DEBUG_CODEGEN
            ss_cout << "unexpected return type: " << cont_type << std::endl;
#endif
            assert(false && "todo: unexpected return type");
        }
    }

    void type_continuation_from_builtin_call(Label *l) {
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "typing continuation from builtin call in " << l << std::endl;
#endif
        auto &&enter = l->body.enter;
        assert(enter.type == TYPE_Builtin);
        auto &&args = l->body.args;
        if (enter.builtin == SFXFN_Unreachable) {
            l->unlink_backrefs();
            args[0] = none;
            l->link_backrefs();
        } else {
            std::vector<const Type *> argtypes;
            argtypes_from_builtin_call(l, argtypes);
            Any newarg = type_continuation(args[0].value, argtypes);
            l->unlink_backrefs();
            args[0] = newarg;
            l->link_backrefs();
        }
    }

    void type_continuation_from_function_call(Label *l) {
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "typing continuation from function call in " << l << std::endl;
#endif
        std::vector<const Type *> argtypes;
        argtypes_from_function_call(l, argtypes);
        auto &&args = l->body.args;
        Any newarg = type_continuation(args[0].value, argtypes);
        l->unlink_backrefs();
        args[0] = newarg;
        l->link_backrefs();
    }

    void type_continuation_call(Label *l) {
#if SCOPES_DEBUG_CODEGEN
        ss_cout << "typing continuation call in " << l << std::endl;
#endif
        auto &&args = l->body.args;
        if (args[0].value.type != TYPE_Nothing) {
            args[0].value.type = TYPE_Nothing;
        }
        std::vector<const Type *> argtypes = {};
        for (auto &&arg : args) {
            argtypes.push_back(arg.value.indirect_type());
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

        SCOPES_TRY()

        done.clear();
        todo = { entry };

        while (!todo.empty()) {
            Label *l = pop_label();
#if SCOPES_DEBUG_CODEGEN
            ss_cout << "processing " << l << std::endl;
#endif
            l->verify_complete();

        process_body:
#if SCOPES_DEBUG_CODEGEN
            stream_label(ss_cout, l, StreamLabelFormat::debug_single());
#endif
            assert(all_params_typed(l));
            //assert(is_basic_block_like(l) || !is_return_param_typed(l));
            assert(all_args_typed(l));

            set_active_anchor(l->body.anchor);

            if (is_calling_callable(l)) {
                fold_callable_call(l);
                goto process_body;
            } else if (is_calling_function(l)) {
                verify_no_keyed_args(l);
                if (is_calling_pure_function(l)
                    && all_args_constant(l)) {
                    fold_pure_function_call(l);
                    goto process_body;
                } else {
                    type_continuation_from_function_call(l);
                }
            } else if (is_calling_builtin(l)) {
                if (!builtin_has_keyed_args(l->get_builtin_enter()))
                    verify_no_keyed_args(l);
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
                    push_label(args[3].value);
                    push_label(args[2].value);
                } else {
                    type_continuation_from_builtin_call(l);
                }
            } else if (is_calling_label(l)) {
                #if SCOPES_DEBUG_CODEGEN
                if (!is_done(l->get_label_enter())) {
                    Label *dest = l->get_label_enter();
                    SCCBuilder scc(dest);
                    if (scc.is_recursive(dest)) {
                        ss_cout << std::endl;
                        scc.stream_group(ss_cout, scc.group(dest));
                        ss_cout << std::endl;
                    }
                }
                #endif

                if (has_keyed_args(l)) {
                    solve_keyed_args(l);
                }

                fold_type_label_arguments(l);

                Label *enter_label = l->get_label_enter();
                if (is_basic_block_like(enter_label)) {
                    if (!has_params(enter_label)) {
#if SCOPES_DEBUG_CODEGEN
                        ss_cout << "folding jump to label in " << l << std::endl;
#endif
                        copy_body(l, enter_label);
                        goto process_body;
                    } else {
                        if (!is_jumping(l)) {
                            clear_continuation_arg(l);
                        }
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
                    ss.out << "unable to call constant of type " << enter.type;
                }
                location_error(ss.str());
            }

#if SCOPES_DEBUG_CODEGEN
            ss_cout << "done: ";
            stream_label(ss_cout, l, StreamLabelFormat::debug_single());
#endif

            set_done(l);

            if (is_continuing_to_label(l)) {
                push_label(l->body.args[0].value.label);
            }
        }

        SCOPES_CATCH(exc)
            print_traceback();
            error(exc);
        SCOPES_TRY_END()
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
#if SCOPES_DEBUG_CODEGEN
                ss_cout << "invalid: ";
                stream_label(ss_cout, l, StreamLabelFormat::debug_single());
#endif
                auto users_copy = l->users;
                // continuation must be eliminated
                for (auto kv = users_copy.begin(); kv != users_copy.end(); ++kv) {
                    Label *user = kv->first;
                    if (!visited.count(user)) {
#if SCOPES_DEBUG_CODEGEN
                        ss_cout << "warning: unreachable user encountered" << std::endl;
#endif
                        continue;
                    }
                    auto &&enter = user->body.enter;
                    auto &&args = user->body.args;
                    #if 0
                    // labels can be passed to API functions, so this is legal
                    for (size_t i = 0; i < args.size(); ++i) {
                        auto &&arg = args[i];
                        if ((arg.type == TYPE_Label) && (arg.label == l)) {                            
                            assert(false && "unexpected use of label as argument");
                        }
                    }
                    #endif
                    if ((enter.type == TYPE_Label) && (enter.label == l)) {
                        assert(!args.empty());

                        auto &&cont = args[0];
                        if ((cont.value.type == TYPE_Parameter)
                            && (cont.value.parameter->label == l)) {
#if SCOPES_DEBUG_CODEGEN
                            ss_cout << "skipping recursive call" << std::endl;
#endif
                        } else {
                            Args newargs = { cont };
                            for (size_t i = 1; i < l->params.size(); ++i) {
                                newargs.push_back(voidarg);
                            }
                            Label *newl = fold_type_label(l, newargs);

#if SCOPES_DEBUG_CODEGEN
                            ss_cout << l << "(" << cont << ") -> " << newl << std::endl;
#endif

                            user->unlink_backrefs();
                            cont = none;
                            enter = newl;
                            user->link_backrefs();
                            numchanges++;
                        }
                    } else {
#if SCOPES_DEBUG_CODEGEN
                        ss_cout << "warning: invalidated user encountered" << std::endl;
#endif
                    }
                }
            }
        } while (numchanges);

#if SCOPES_DEBUG_CODEGEN
        ss_cout << "lowered to CFF in " << iterations << " steps" << std::endl;
#endif

        return entry;
    }

    std::unordered_map<const Type *, ffi_type *> ffi_types;

    ffi_type *new_type() {
        ffi_type *result = (ffi_type *)malloc(sizeof(ffi_type));
        memset(result, 0, sizeof(ffi_type));
        return result;
    }

    ffi_type *create_ffi_type(const Type *type) {
        if (type == TYPE_Void) return &ffi_type_void;
        if (type == TYPE_Nothing) return &ffi_type_void;

        switch(type->kind()) {
        case TK_Integer: {
            auto it = cast<IntegerType>(type);
            if (it->issigned) {
                switch (it->width) {
                case 8: return &ffi_type_sint8;
                case 16: return &ffi_type_sint16;
                case 32: return &ffi_type_sint32;
                case 64: return &ffi_type_sint64;
                default: break;
                }
            } else {
                switch (it->width) {
                case 1: return &ffi_type_uint8;
                case 8: return &ffi_type_uint8;
                case 16: return &ffi_type_uint16;
                case 32: return &ffi_type_uint32;
                case 64: return &ffi_type_uint64;
                default: break;
                }
            }
        } break;
        case TK_Real: {
            switch(cast<RealType>(type)->width) {
            case 32: return &ffi_type_float;
            case 64: return &ffi_type_double;
            default: break;
            }
        } break;
        case TK_Pointer: return &ffi_type_pointer;
        case TK_Typename: {
            return get_ffi_type(storage_type(type));
        } break;
        case TK_Array: {
            auto ai = cast<ArrayType>(type);
            size_t count = ai->count;
            ffi_type *ty = (ffi_type *)malloc(sizeof(ffi_type));
            ty->size = 0;
            ty->alignment = 0;
            ty->type = FFI_TYPE_STRUCT;
            ty->elements = (ffi_type **)malloc(sizeof(ffi_type*) * (count + 1));
            ffi_type *element_type = get_ffi_type(ai->element_type);
            for (size_t i = 0; i < count; ++i) {
                ty->elements[i] = element_type;
            }
            ty->elements[count] = nullptr;
            return ty;
        } break;
        case TK_Tuple: {
            auto ti = cast<TupleType>(type);
            size_t count = ti->types.size();
            ffi_type *ty = (ffi_type *)malloc(sizeof(ffi_type));
            ty->size = 0;
            ty->alignment = 0;
            ty->type = FFI_TYPE_STRUCT;
            ty->elements = (ffi_type **)malloc(sizeof(ffi_type*) * (count + 1));
            for (size_t i = 0; i < count; ++i) {
                ty->elements[i] = get_ffi_type(ti->types[i]);
            }
            ty->elements[count] = nullptr;
            return ty;
        } break;
        case TK_Union: {
            auto ui = cast<UnionType>(type);
            size_t count = ui->types.size();
            size_t sz = ui->size;
            size_t al = ui->align;
            ffi_type *ty = (ffi_type *)malloc(sizeof(ffi_type));
            ty->size = 0;
            ty->alignment = 0;
            ty->type = FFI_TYPE_STRUCT;
            // find member with the same alignment
            for (size_t i = 0; i < count; ++i) {
                const Type *ET = ui->types[i];
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

    ffi_type *get_ffi_type(const Type *type) {
        auto it = ffi_types.find(type);
        if (it == ffi_types.end()) {
            auto result = create_ffi_type(type);
            ffi_types[type] = result;
            return result;
        } else {
            return it->second;
        }
    }

    void verify_function_argument_count(const FunctionType *fi, size_t argcount) {

        size_t fargcount = fi->argument_types.size();
        if (fi->flags & FF_Variadic) {
            if (argcount < fargcount) {
                StyledString ss;
                ss.out << "argument count mismatch (need at least "
                    << fargcount << ", got " << argcount << ")";
                location_error(ss.str());
            }
        } else {
            if (argcount != fargcount) {
                StyledString ss;
                ss.out << "argument count mismatch (need "
                    << fargcount << ", got " << argcount << ")";
                location_error(ss.str());
            }
        }
    }

    Any run_ffi_function(Any enter, KeyAny *args, size_t argcount) {
        auto pi = cast<PointerType>(enter.type);
        auto fi = cast<FunctionType>(pi->element_type);

        size_t fargcount = fi->argument_types.size();

        const Type *rettype = fi->return_type;

        ffi_cif cif;
        ffi_type *argtypes[argcount];
        void *avalues[argcount];
        for (size_t i = 0; i < argcount; ++i) {
            KeyAny &arg = args[i];
            argtypes[i] = get_ffi_type(arg.value.type);
            avalues[i] = get_pointer(arg.value.type, arg.value);
        }
        ffi_status prep_result;
        if (fi->flags & FF_Variadic) {
            prep_result = ffi_prep_cif_var(
                &cif, FFI_DEFAULT_ABI, fargcount, argcount, get_ffi_type(rettype), argtypes);
        } else {
            prep_result = ffi_prep_cif(
                &cif, FFI_DEFAULT_ABI, argcount, get_ffi_type(rettype), argtypes);
        }
        assert(prep_result == FFI_OK);

        Any result = Any::from_pointer(rettype, nullptr);
        ffi_call(&cif, FFI_FN(enter.pointer),
            get_pointer(result.type, result, true), avalues);
        return result;
    }

};

static Label *normalize(Label *entry) {
#if 0
    StyledStream ss;
    ss << entry << std::endl;
#endif

    NormalizeCtx ctx;
    ctx.start_entry = entry;
    ctx.normalize(entry);
    ctx.lower2cff(entry);

#if 0
    {
        auto tt = normalize_timer.getTotalTime();
        std::cout << "normalize time: " << (tt.getUserTime() * 1000.0) << "ms" << std::endl;
    }
#endif
    
    return entry;
}

//------------------------------------------------------------------------------
// MACRO EXPANDER
//------------------------------------------------------------------------------
// expands macros and generates the IL

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

struct Expander {
    Label *state;
    Scope *env;
    const List *next;
    Any voidval;
    static bool verbose;

    const Type *list_expander_func_type;

    Expander(Label *_state, Scope *_env, const List *_next = EOL) :
        state(_state),
        env(_env),
        next(_next),
        voidval(none),
        list_expander_func_type(nullptr) {
        voidval.type = TYPE_Void;
        list_expander_func_type = Pointer(Function(
            Tuple({TYPE_List, TYPE_Scope}),
            {TYPE_List, TYPE_Scope}));
    }

    ~Expander() {}

    bool is_goto_label(Any enter) {
        return (enter.type == TYPE_Label)            
            && (enter.label->params[0]->type == TYPE_Nothing);
    }

    // arguments must include continuation
    // enter and args must be passed with syntax object removed
    void br(Any enter, const Args &args, uint64_t flags = 0) {
        assert(!args.empty());
        const Anchor *anchor = get_active_anchor();
        assert(anchor);
        if (!state) {
            set_active_anchor(anchor);
            location_error(String::from("can not define body: continuation already exited."));
            return;
        }
        assert(!is_goto_label(enter) || (args[0].value.type == TYPE_Nothing));
        assert(state->body.enter.type == TYPE_Nothing);
        assert(state->body.args.empty());
        state->body.flags = flags;
        state->body.enter = enter;
        state->body.args = args;
        state->body.anchor = anchor;
        state->link_backrefs();
        state = nullptr;
    }

    bool is_parameter_or_label(Any val) {
        return (val.type == TYPE_Parameter) || (val.type == TYPE_Label);
    }

    Any write_dest(const Any &value, const Any &dest) {
        if (dest.type == TYPE_Symbol) {
            return value;
        } else if (is_parameter_or_label(dest)) {
            if (last_expression()) {
                br(dest, { none, value });
            }
            return value;
        } else {
            assert(false && "illegal dest type");
        }
        return none;
    }

    Any expand_syntax_extend(const List *it, const Any &dest, const Any &longdest) {
        auto _anchor = get_active_anchor();

        verify_list_parameter_count(it, 1, -1);

        // skip head
        it = it->next;

        Label *func = Label::from(_anchor, Symbol(KW_SyntaxExtend));

        auto retparam = Parameter::from(_anchor, Symbol(SYM_Unnamed), TYPE_Void);
        auto scopeparam = Parameter::from(_anchor, Symbol(SYM_SyntaxScope), TYPE_Void);

        func->append(retparam);
        func->append(scopeparam);

        Scope *subenv = Scope::from(env);
        subenv->bind(Symbol(SYM_SyntaxScope), scopeparam);

        Expander subexpr(func, subenv);

        subexpr.expand_function_body(it, retparam);

        set_active_anchor(_anchor);

        func = fold_type_label(func, { Any::from_opaque(TYPE_Void), env });
        func = normalize(func);

        // expected type
        const Type *expected_functype = Function( TYPE_Scope, {} );
        const Type *functype = func->get_function_type();
        if (functype != expected_functype) {
            set_active_anchor(_anchor);
            location_error(String::from("syntax-extend must return a scope"));
        }

        typedef Scope *(*FuncType)();

        FuncType fptr = (FuncType)compile(func, 0).pointer;
        env = fptr();

        return write_dest(none, dest);
    }

    void expand_function_body(const List *it, const Any &longdest) {
        if (it == EOL) {
            br(longdest, { none });
        } else {
            while (it) {
                next = it->next;
                expand(it->at, longdest, longdest);
                it = next;
            }
        }
    }

    bool ends_with_parenthesis(Symbol sym) {
        if (sym == SYM_Parenthesis)
            return true;
        const String *str = sym.name();
        if (str->count < 3)
            return false;
        const char *dot = str->data + str->count - 3;
        return !strcmp(dot, "...");
    }

    Parameter *expand_parameter(Any value) {
        const Syntax *sxvalue = value;
        const Anchor *anchor = sxvalue->anchor;
        Any _value = sxvalue->datum;
        if (_value.type == TYPE_Parameter) {
            return _value.parameter;
        } else if (_value.type == TYPE_List && _value.list == EOL) {
            return Parameter::from(anchor, Symbol(SYM_Unnamed), TYPE_Nothing);
        } else {
            _value.verify(TYPE_Symbol);
            Parameter *param = nullptr;
            if (ends_with_parenthesis(_value.symbol)) {
                param = Parameter::vararg_from(anchor, _value.symbol, TYPE_Void);
            } else {
                param = Parameter::from(anchor, _value.symbol, TYPE_Void);
            }
            env->bind(_value.symbol, param);
            return param;
        }
    }

    Any expand_fn(const List *it, const Any &dest, const Any &longdest, bool label) {
        auto _anchor = get_active_anchor();

        verify_list_parameter_count(it, 1, -1);

        // skip head
        it = it->next;

        assert(it != EOL);

        bool continuing = false;
        Label *func = nullptr;
        Any tryfunc_name = unsyntax(it->at);
        if (tryfunc_name.type == TYPE_Symbol) {
            // named self-binding
            // see if we can find a forward declaration in the local scope
            Any result = none;
            if (env->lookup_local(tryfunc_name.symbol, result)
                && (result.type == TYPE_Label)
                && !result.label->is_complete()) {
                func = result.label;
                continuing = true;
            } else {
                func = Label::from(_anchor, tryfunc_name.symbol);
                env->bind(tryfunc_name.symbol, func);
            }
            it = it->next;
        } else if (tryfunc_name.type == TYPE_String) {
            // named lambda
            func = Label::from(_anchor, Symbol(tryfunc_name.string));
            it = it->next;
        } else {
            // unnamed lambda
            func = Label::from(_anchor, Symbol(SYM_Unnamed));
        }

        Parameter *retparam = nullptr;
        if (continuing) {
            assert(!func->params.empty());
            retparam = func->params[0];
        } else {
            retparam = Parameter::from(_anchor, Symbol(SYM_Unnamed), label?TYPE_Nothing:TYPE_Void);
            func->append(retparam);
        }

        if (it == EOL) {
            // forward declaration
            if (tryfunc_name.type != TYPE_Symbol) {
                location_error(label?
                    String::from("forward declared label must be named")
                    :String::from("forward declared function must be named"));
            }

            return write_dest(none, dest);
        }

        const Syntax *sxplist = it->at;
        const List *params = sxplist->datum;

        it = it->next;

        Scope *subenv = Scope::from(env);
        // hidden self-binding for subsequent macros
        subenv->bind(SYM_ThisFnCC, func);
        if (!label) {
            subenv->bind(KW_Recur, func);
            subenv->bind(KW_Return, retparam);
        }

        Expander subexpr(func, subenv);

        while (params != EOL) {
            func->append(subexpr.expand_parameter(params->at));
            params = params->next;
        }

        subexpr.expand_function_body(it, label?longdest:Any(func->params[0]));

        set_active_anchor(_anchor);
        return write_dest(func, dest);
    }

    bool is_return_parameter(Any val) {
        return (val.type == TYPE_Parameter) && (val.parameter->index == 0);
    }

    bool last_expression() {
        return next == EOL;
    }

    Any expand_do(const List *it, const Any &dest, Any longdest) {
        auto _anchor = get_active_anchor();

        it = it->next;

        Label *nextstate = nullptr;
        Any result = none;
        if (dest.type == TYPE_Symbol) {
            nextstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
            Parameter *param = Parameter::vararg_from(_anchor, 
                Symbol(SYM_Unnamed), TYPE_Void);
            nextstate->append(param);
            longdest = nextstate;
            result = param;
        } else if (is_parameter_or_label(dest)) {
            if (dest.type == TYPE_Parameter) {
                assert(dest.parameter->type != TYPE_Nothing);
            }
            if (!last_expression()) {
                nextstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
                longdest = nextstate;
            }
        } else {
            assert(false && "illegal dest type");
        }

        Label *func = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
        Scope *subenv = Scope::from(env);
        Expander subexpr(func, subenv);
        subexpr.expand_function_body(it, longdest);

        set_active_anchor(_anchor);
        br(func, { none });
        state = nextstate;
        return result;
    }

    // (let x ... = args ...)
    // ...
    Any expand_let(const List *it, const Any &dest, const Any &longdest) {

        verify_list_parameter_count(it, 3, -1);
        it = it->next;

        auto _anchor = get_active_anchor();

        Symbol labelname = Symbol(SYM_Unnamed);

        if (it) {
            auto name = unsyntax(it->at);
            if (name.type == TYPE_List) {
                const List *val = name.list;
                if (val != EOL) {
                    auto head = val->at;
                    if (head == Symbol(SYM_SquareList)) {
                        val = val->next;
                        if (val != EOL) {
                            auto name = unsyntax(val->at);
                            name.verify(TYPE_Symbol);
                            labelname = name.symbol;
                            it = it->next;
                        }
                    }
                }
            }
        }

        Label *nextstate = Label::continuation_from(_anchor, labelname);
        if (labelname != SYM_Unnamed) {
            env->bind(labelname, nextstate);
        }

        Scope *orig_env = env;
        env = Scope::from(env);
        // read parameter names
        while (it) {
            auto name = unsyntax(it->at);
            if ((name.type == TYPE_Symbol)
                && (name.symbol == OP_Set))
                break;
            nextstate->append(expand_parameter(it->at));
            it = it->next;
        }

        if (it == EOL) {
            location_error(String::from("= expected"));
        }

        Args args;
        args.reserve(it->count);
        args.push_back(none);

        it = it->next;

        // read init values
        Expander subexp(state, orig_env);
        while (it) {
            subexp.next = it->next;
            args.push_back(subexp.expand(it->at, Symbol(SYM_Unnamed), longdest));
            it = subexp.next;
        }
        set_active_anchor(_anchor);
        state = subexp.state;
        br(nextstate, args);
        state = nextstate;

        Any result = none;
        if (nextstate->params.size() > 1) {
            return nextstate->params[1];
        } else {
            return result;
        }
    }

    // quote <value> ...
    Any expand_quote(const List *it, const Any &dest, Any longdest) {
        //auto _anchor = get_active_anchor();

        verify_list_parameter_count(it, 1, -1);
        it = it->next;
        
        Any result = none;
        if (it->count == 1) {
            result = it->at;
        } else {
            result = it;
        }
        return write_dest(strip_syntax(result), dest);
    }

    Any expand_syntax_log(const List *it, const Any &dest, Any longdest) {
        //auto _anchor = get_active_anchor();

        verify_list_parameter_count(it, 1, 1);
        it = it->next;

        Any val = unsyntax(it->at);
        val.verify(TYPE_Symbol);

        auto sym = val.symbol;
        if (sym == KW_True) {
            this->verbose = true;
        } else if (sym == KW_False) {
            this->verbose = false;
        } else {
            // ignore
        }

        return write_dest(none, dest);
    }

    // (if cond body ...)
    // [(elseif cond body ...)]
    // [(else body ...)]
    Any expand_if(const List *it, const Any &dest, Any longdest) {
        auto _anchor = get_active_anchor();

        std::vector<const List *> branches;

    collect_branch:
        verify_list_parameter_count(it, 1, -1);
        branches.push_back(it);

        it = next;
        if (it != EOL) {
            auto itnext = it->next;
            const Syntax *sx = it->at;
            if (sx->datum.type == TYPE_List) {
                it = sx->datum;
                if (it != EOL) {
                    auto head = unsyntax(it->at);
                    if (head == Symbol(KW_ElseIf)) {
                        next = itnext;
                        goto collect_branch;
                    } else if (head == Symbol(KW_Else)) {
                        next = itnext;
                        branches.push_back(it);
                    } else {
                        branches.push_back(EOL);
                    }
                } else {
                    branches.push_back(EOL);
                }
            } else {
                branches.push_back(EOL);
            }
        } else {
            branches.push_back(EOL);
        }

        Label *nextstate = nullptr;
        Any result = none;
        if (dest.type == TYPE_Symbol) {
            nextstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
            Parameter *param = Parameter::vararg_from(_anchor, Symbol(SYM_Unnamed), TYPE_Void);
            nextstate->append(param);
            longdest = nextstate;
            result = param;
        } else if (is_parameter_or_label(dest)) {
            if (dest.type == TYPE_Parameter) {
                assert(dest.parameter->type != TYPE_Nothing);
            }
            if (!last_expression()) {
                nextstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
                longdest = nextstate;
            }
        } else {
            assert(false && "illegal dest type");
        }

        int lastidx = (int)branches.size() - 1;
        for (int idx = 0; idx < lastidx; ++idx) {
            it = branches[idx];
            it = it->next;

            Expander subexp(state, env);
            subexp.next = it->next;
            Any cond = subexp.expand(it->at, Symbol(SYM_Unnamed), longdest);
            it = subexp.next;

            Label *thenstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
            Label *elsestate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));

            set_active_anchor(_anchor);
            state = subexp.state;
            br(Builtin(FN_Branch), { none, cond, thenstate, elsestate });

            subexp.env = Scope::from(env);
            subexp.state = thenstate;
            subexp.expand_function_body(it, longdest);

            state = elsestate;
        }

        it = branches[lastidx];
        if (it != EOL) {
            it = it->next;
            Expander subexp(state, Scope::from(env));
            subexp.expand_function_body(it, longdest);
        } else {
            br(longdest, { none });
        }

        state = nextstate;
        return result;
    }
    
    static bool get_kwargs(Any it, KeyAny &value) {
        it = unsyntax(it);
        if (it.type != TYPE_List) return false;
        auto l = it.list;
        if (l == EOL) return false;
        if (l->count != 3) return false;
        it = unsyntax(l->at);
        if (it.type != TYPE_Symbol) return false;
        value.key = it.symbol;
        l = l->next;
        it = unsyntax(l->at);
        if (it.type != TYPE_Symbol) return false;
        if (it.symbol != OP_Set) return false;
        l = l->next;
        value.value = l->at;
        return true;
    }

    Any expand_call(const List *it, const Any &dest, Any longdest, bool rawcall = false) {
        if (it == EOL)
            return write_dest(it, dest);
        auto _anchor = get_active_anchor();
        Expander subexp(state, env, it->next);

        Args args;
        args.reserve(it->count);

        Label *nextstate = nullptr;
        Any result = none;
        if (dest.type == TYPE_Symbol) {
            nextstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
            Parameter *param = Parameter::vararg_from(_anchor, Symbol(SYM_Unnamed), TYPE_Void);
            nextstate->append(param);
            args.push_back(nextstate);
            longdest = nextstate;
            result = param;
        } else if (is_parameter_or_label(dest)) {
            if (dest.type == TYPE_Parameter) {
                assert(dest.parameter->type != TYPE_Nothing);
            }
            if (last_expression()) {
                args.push_back(dest);
            } else {
                nextstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
                args.push_back(nextstate);
                longdest = nextstate;
            }
        } else {
            assert(false && "illegal dest type");
        }

        Any enter = subexp.expand(it->at, Symbol(SYM_Unnamed), longdest);
        if (is_return_parameter(enter)) {
            assert(enter.parameter->type != TYPE_Nothing);
            args[0] = none;
            if (!last_expression()) {
                location_error(
                    String::from("return call must be last in statement list"));
            }
        } else if (is_goto_label(enter)) {
            args[0] = none;
        }

        it = subexp.next;
        while (it) {
            subexp.next = it->next;
            KeyAny value;
            set_active_anchor(((const Syntax *)it->at)->anchor);
            if (get_kwargs(it->at, value)) {
                value.value = subexp.expand(
                    value.value, Symbol(SYM_Unnamed), longdest);
            } else {
                value = subexp.expand(it->at, Symbol(SYM_Unnamed), longdest);
            }
            args.push_back(value);
            it = subexp.next;
        }

        state = subexp.state;
        set_active_anchor(_anchor);
        br(enter, args, rawcall?LBF_RawCall:0);
        state = nextstate;
        return result;
    }

    Any expand_syntax_apply_block(const List *it, const Any &dest, const Any &longdest) {
        auto _anchor = get_active_anchor();
        verify_list_parameter_count(it, 1, 1);

        it = it->next;

        Expander subexp(state, env, it->next);
        Label *func = subexp.expand(it->at, Symbol(SYM_Unnamed), longdest);
        it = subexp.next;

        Args args;
        args.reserve(((it == EOL)?0:(it->count)) + 1);
        Label *nextstate = nullptr;
        Any result = none;
        if (dest.type == TYPE_Symbol) {
            nextstate = Label::continuation_from(_anchor, Symbol(SYM_Unnamed));
            Parameter *param = Parameter::vararg_from(_anchor, Symbol(SYM_Unnamed), TYPE_Void);
            nextstate->append(param);
            args.push_back(nextstate);
            result = param;
        } else if (dest.type == TYPE_Parameter) {
            args.push_back(dest);
        } else {
            assert(false && "illegal dest type");
        }
        args.push_back(_anchor);
        args.push_back(next);
        args.push_back(env);
        state = subexp.state;
        set_active_anchor(_anchor);
        br(func, args);
        state = nextstate;
        next = EOL;
        return result;
    }

    Any expand(const Syntax *sx, const Any &dest, const Any &longdest) {
    expand_again:
        set_active_anchor(sx->anchor);
        if (sx->quoted) {
            if (verbose) {
                StyledStream ss(std::cerr);
                ss << "quoting ";
                stream_expr(ss, sx, StreamExprFormat::debug_digest());
            }
            // return as-is
            return write_dest(sx->datum, dest);
        }
        Any expr = sx->datum;
        if (expr.type == TYPE_List) {
            if (verbose) {
                StyledStream ss(std::cerr);
                ss << "expanding list ";
                stream_expr(ss, sx, StreamExprFormat::debug_digest());
            }
            
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
                case KW_SyntaxLog: return expand_syntax_log(list, dest, longdest);
                case KW_Fn: return expand_fn(list, dest, longdest, false);
                case KW_Label: return expand_fn(list, dest, longdest, true);
                case KW_SyntaxApplyBlock: return expand_syntax_apply_block(list, dest, longdest);
                case KW_SyntaxExtend: return expand_syntax_extend(list, dest, longdest);
                case KW_Let: return expand_let(list, dest, longdest);
                case KW_If: return expand_if(list, dest, longdest);
                case KW_Quote: return expand_quote(list, dest, longdest);
                case KW_Do: return expand_do(list, dest, longdest);
                case KW_RawCall:
                case KW_Call: {
                    verify_list_parameter_count(list, 1, -1);
                    list = list->next;
                    assert(list != EOL);
                    return expand_call(list, dest, longdest, func.value() == KW_RawCall);
                } break;
                default: break;
                }
            }

            Any list_handler = none;
            if (env->lookup(Symbol(SYM_ListWildcard), list_handler)) {
                if (list_handler.type != list_expander_func_type) {
                    StyledString ss;
                    ss.out << "custom list expander has wrong type "
                        << list_handler.type << ", must be "
                        << list_expander_func_type;
                    location_error(ss.str());
                }
                struct ListScopePair { const List *topit; Scope *env; };
                typedef ListScopePair (*HandlerFuncType)(const List *, Scope *);
                HandlerFuncType f = (HandlerFuncType)list_handler.pointer;
                auto result = f(List::from(sx, next), env);
                const Syntax *newsx = result.topit->at;
                if (newsx != sx) {
                    sx = newsx;
                    next = result.topit->next;
                    env = result.env;
                    goto expand_again;
                } else if (verbose) {
                    StyledStream ss(std::cerr);
                    ss << "ignored by list handler" << std::endl;
                }
            }
            return expand_call(list, dest, longdest);
        } else if (expr.type == TYPE_Symbol) {
            if (verbose) {
                StyledStream ss(std::cerr);
                ss << "expanding symbol ";
                stream_expr(ss, sx, StreamExprFormat::debug_digest());
            }
            
            Symbol name = expr.symbol;

            Any result = none;
            if (!env->lookup(name, result)) {
                Any symbol_handler = none;
                if (env->lookup(Symbol(SYM_SymbolWildcard), symbol_handler)) {
                    if (symbol_handler.type != list_expander_func_type) {
                        StyledString ss;
                        ss.out << "custom symbol expander has wrong type "
                            << symbol_handler.type << ", must be "
                            << list_expander_func_type;
                        location_error(ss.str());
                    }
                    struct ListScopePair { const List *topit; Scope *env; };
                    typedef ListScopePair (*HandlerFuncType)(const List *, Scope *);
                    HandlerFuncType f = (HandlerFuncType)symbol_handler.pointer;
                    auto result = f(List::from(sx, next), env);
                    const Syntax *newsx = result.topit->at;
                    if (newsx != sx) {
                        sx = newsx;
                        next = result.topit->next;
                        env = result.env;
                        goto expand_again;
                    }
                }
                
                StyledString ss;
                ss.out << "use of undeclared identifier '" << name.name()->data << "'.";
                auto syms = env->find_closest_match(name);
                if (!syms.empty()) {
                    ss.out << " Did you mean '" << syms[0].name()->data << "'";
                    for (size_t i = 1; i < syms.size(); ++i) {
                        if ((i + 1) == syms.size()) {
                            ss.out << " or ";
                        } else {
                            ss.out << ", ";
                        }
                        ss.out << "'" << syms[i].name()->data << "'";
                    }
                    ss.out << "?";
                }
                location_error(ss.str());
            }
            return write_dest(result, dest);
        } else {
            if (verbose) {
                StyledStream ss(std::cerr);
                ss << "ignoring ";
                stream_expr(ss, sx, StreamExprFormat::debug_digest());
            }            
            return write_dest(expr, dest);
        }
    }

};

bool Expander::verbose = false;

static Label *expand_module(Any expr, Scope *scope = nullptr) {
    const Anchor *anchor = get_active_anchor();
    if (expr.type == TYPE_Syntax) {
        anchor = expr.syntax->anchor;
        set_active_anchor(anchor);
        expr = expr.syntax->datum;
    }
    expr.verify(TYPE_List);
    assert(anchor);
    Label *mainfunc = Label::function_from(anchor, anchor->path());

    Expander subexpr(mainfunc, scope?scope:globals);
    subexpr.expand_function_body(expr, mainfunc->params[0]);

    return mainfunc;
}

//------------------------------------------------------------------------------
// GLOBALS
//------------------------------------------------------------------------------

#define DEFINE_TYPENAME(NAME, T) \
    T = Typename(String::from(NAME));

#define DEFINE_BASIC_TYPE(NAME, CT, T, BODY) { \
        T = Typename(String::from(NAME)); \
        auto tn = cast<TypenameType>(const_cast<Type *>(T)); \
        tn->finalize(BODY); \
        assert(sizeof(CT) == size_of(T)); \
    }

#define DEFINE_STRUCT_TYPE(NAME, CT, T, ...) { \
        T = Typename(String::from(NAME)); \
        auto tn = cast<TypenameType>(const_cast<Type *>(T)); \
        tn->finalize(Tuple({ __VA_ARGS__ })); \
        assert(sizeof(CT) == size_of(T)); \
    }

#define DEFINE_STRUCT_HANDLE_TYPE(NAME, CT, T, ...) { \
        T = Typename(String::from(NAME)); \
        auto tn = cast<TypenameType>(const_cast<Type *>(T)); \
        auto ET = Tuple({ __VA_ARGS__ }); \
        assert(sizeof(CT) == size_of(ET)); \
        tn->finalize(Pointer(ET)); \
    }

#define DEFINE_OPAQUE_HANDLE_TYPE(NAME, CT, T) { \
        T = Typename(String::from(NAME)); \
        auto tn = cast<TypenameType>(const_cast<Type *>(T)); \
        tn->finalize(Pointer(Typename(String::from("_" NAME)))); \
    }

static void init_types() {
    DEFINE_TYPENAME("typename", TYPE_Typename);

    DEFINE_TYPENAME("void", TYPE_Void);
    DEFINE_TYPENAME("Nothing", TYPE_Nothing);

    DEFINE_TYPENAME("integer", TYPE_Integer);
    DEFINE_TYPENAME("real", TYPE_Real);
    DEFINE_TYPENAME("pointer", TYPE_Pointer);
    DEFINE_TYPENAME("array", TYPE_Array);
    DEFINE_TYPENAME("vector", TYPE_Vector);
    DEFINE_TYPENAME("tuple", TYPE_Tuple);
    DEFINE_TYPENAME("union", TYPE_Union);
    DEFINE_TYPENAME("typed-label", TYPE_TypedLabel);
    DEFINE_TYPENAME("typeset", TYPE_TypeSet);
    DEFINE_TYPENAME("constant", TYPE_Constant);
    DEFINE_TYPENAME("function", TYPE_Function);
    DEFINE_TYPENAME("extern", TYPE_Extern);
    DEFINE_TYPENAME("CStruct", TYPE_CStruct);
    DEFINE_TYPENAME("CUnion", TYPE_CUnion);
    DEFINE_TYPENAME("CEnum", TYPE_CEnum);

    TYPE_Bool = Integer(1, false);

    TYPE_I8 = Integer(8, true);
    TYPE_I16 = Integer(16, true);
    TYPE_I32 = Integer(32, true);
    TYPE_I64 = Integer(64, true);

    TYPE_U8 = Integer(8, false);
    TYPE_U16 = Integer(16, false);
    TYPE_U32 = Integer(32, false);
    TYPE_U64 = Integer(64, false);

    TYPE_F16 = Real(16);
    TYPE_F32 = Real(32);
    TYPE_F64 = Real(64);
    TYPE_F80 = Real(80);

    TYPE_Ref = Typename(String::from("ref"));

    DEFINE_BASIC_TYPE("usize", size_t, TYPE_USize, TYPE_U64);

    TYPE_Type = Typename(String::from("type"));
    TYPE_Unknown = Typename(String::from("Unknown"));
    const Type *_TypePtr = Pointer(Typename(String::from("_type")));
    cast<TypenameType>(const_cast<Type *>(TYPE_Type))->finalize(_TypePtr);
    cast<TypenameType>(const_cast<Type *>(TYPE_Unknown))->finalize(_TypePtr);

    cast<TypenameType>(const_cast<Type *>(TYPE_Nothing))->finalize(Tuple({}));

    DEFINE_BASIC_TYPE("Symbol", Symbol, TYPE_Symbol, TYPE_U64);
    DEFINE_BASIC_TYPE("Builtin", Builtin, TYPE_Builtin, TYPE_U64);

    DEFINE_STRUCT_TYPE("Any", Any, TYPE_Any,
        TYPE_Type,
        TYPE_U64
    );

    DEFINE_OPAQUE_HANDLE_TYPE("SourceFile", SourceFile, TYPE_SourceFile);
    DEFINE_OPAQUE_HANDLE_TYPE("Label", Label, TYPE_Label);
    DEFINE_OPAQUE_HANDLE_TYPE("Parameter", Parameter, TYPE_Parameter);
    DEFINE_OPAQUE_HANDLE_TYPE("Scope", Scope, TYPE_Scope);

    DEFINE_STRUCT_HANDLE_TYPE("Anchor", Anchor, TYPE_Anchor,
        Pointer(TYPE_SourceFile),
        TYPE_I32,
        TYPE_I32,
        TYPE_I32
    );

    {
        TYPE_List = Typename(String::from("list"));

        const Type *cellT = Typename(String::from("_list"));
        auto tn = cast<TypenameType>(const_cast<Type *>(cellT));
        auto ET = Tuple({ TYPE_Any, Pointer(cellT), TYPE_USize });
        assert(sizeof(List) == size_of(ET));
        tn->finalize(ET);

        cast<TypenameType>(const_cast<Type *>(TYPE_List))
            ->finalize(Pointer(cellT));
    }

    DEFINE_STRUCT_HANDLE_TYPE("Syntax", Syntax, TYPE_Syntax,
        TYPE_Anchor,
        TYPE_Any,
        TYPE_Bool);

    DEFINE_STRUCT_HANDLE_TYPE("string", String, TYPE_String,
        TYPE_USize,
        Array(TYPE_I8, 1)
    );

    DEFINE_STRUCT_HANDLE_TYPE("Exception", Exception, TYPE_Exception,
        TYPE_Anchor,
        TYPE_String);

#define T(TYPE, TYPENAME) \
    assert(TYPE);
    B_TYPES()
#undef T
}

#undef DEFINE_TYPENAME
#undef DEFINE_BASIC_TYPE
#undef DEFINE_STRUCT_TYPE
#undef DEFINE_STRUCT_HANDLE_TYPE
#undef DEFINE_OPAQUE_HANDLE_TYPE
#undef DEFINE_STRUCT_TYPE

typedef struct { int x,y; } I2;
typedef struct { int x,y,z; } I3;

static const String *f_repr(Any value) {
    StyledString ss;
    value.stream(ss.out, false);
    return ss.str();
}

static const String *f_any_string(Any value) {
    auto ss = StyledString::plain();
    ss.out << value;
    return ss.str();
}

static void f_write(const String *value) {
    fputs(value->data, stdout);
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

typedef struct { Any result; bool ok; } AnyBoolPair;
static AnyBoolPair f_scope_at(Scope *scope, Symbol key) {
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

static size_t f_sizeof(const Type *T) {
    return size_of(T);
}

size_t f_type_countof(const Type *T) {
    T = storage_type(T);
    switch(T->kind()) {
    case TK_Array: return cast<ArrayType>(T)->count;
    case TK_Vector: return cast<VectorType>(T)->count;
    case TK_Tuple: return cast<TupleType>(T)->types.size();
    case TK_Union: return cast<UnionType>(T)->types.size();
    case TK_Function:  return cast<FunctionType>(T)->argument_types.size() + 1;
    default: {
        StyledString ss;
        ss.out << "type " << T << " has no count" << std::endl;
        location_error(ss.str());
    } break;
    }
    return 0;
}

static const Type *f_elementtype(const Type *T, int i) {
    T = storage_type(T);
    switch(T->kind()) {
    case TK_Pointer: return cast<PointerType>(T)->element_type;
    case TK_Array: return cast<ArrayType>(T)->element_type;
    case TK_Vector: return cast<VectorType>(T)->element_type;
    case TK_Tuple: return cast<TupleType>(T)->type_at_index(i);
    case TK_Union: return cast<UnionType>(T)->type_at_index(i);
    case TK_Function:  return cast<FunctionType>(T)->type_at_index(i);
    case TK_Extern: return cast<ExternType>(T)->type;
    default: {
        StyledString ss;
        ss.out << "type " << T << " has no elements" << std::endl;
        location_error(ss.str());
    } break;
    }
    return nullptr;
}

static const Type *f_pointertype(const Type *T) {
    return Pointer(T);
}

static const List *f_list_cons(Any at, const List *next) {
    return List::from(at, next);
}

static int32_t f_type_kind(const Type *T) {
    return T->kind();
}

static int32_t f_bitcountof(const Type *T) {
    T = storage_type(T);
    switch(T->kind()) {
    case TK_Integer:
        return cast<IntegerType>(T)->width;
    case TK_Real:
        return cast<RealType>(T)->width;
    default: {
        StyledString ss;
        ss.out << "type " << T << " has no bitcount" << std::endl;
        location_error(ss.str());
    } break;
    }
    return 0;
}

static bool f_issigned(const Type *T) {
    T = storage_type(T);
    verify_kind<TK_Integer>(T);
    return cast<IntegerType>(T)->issigned;
}

static const Type *f_type_storage(const Type *T) {
    return storage_type(T);
}

static void f_error(const String *msg) {
    location_error(msg);
}

static void f_raise(Any value) {
    error(value);
}

static void f_set_anchor(const Anchor *anchor) {
    set_active_anchor(anchor);
}

static const Type *f_integer_type(int width, bool issigned) {
    return Integer(width, issigned);
}

static const Type *f_typename_type(const String *str) {
    return Typename(str);
}

static I3 f_compiler_version() {
    return { 
        SCOPES_VERSION_MAJOR,
        SCOPES_VERSION_MINOR,
        SCOPES_VERSION_PATCH };
}

static const Syntax *f_syntax_new(const Anchor *anchor, Any value, bool quoted) {
    return Syntax::from(anchor, value, quoted);
}

static Parameter *f_parameter_new(const Anchor *anchor, Symbol symbol, const Type *type) {
    return Parameter::from(anchor, symbol, type);
}

static const String *f_string_new(const char *ptr, size_t count) {
    return String::from(ptr, count);
}

static bool f_is_file(const String *path) {
    auto sf = SourceFile::from_file(path);
    return sf != nullptr;
}

static const Syntax *f_list_load(const String *path) {
    auto sf = SourceFile::from_file(path);
    if (!sf) {
        StyledString ss;
        ss.out << "no such file: " << path;
        location_error(ss.str());
    }
    LexerParser parser(sf);
    return parser.parse();    
}

static const Syntax *f_list_parse(const String *str) {
    auto sf = SourceFile::from_string(Symbol("<string>"), str);
    assert(sf);
    LexerParser parser(sf);
    return parser.parse();   
}

static Scope *f_scope_new() {
    return Scope::from();
}
static Scope *f_scope_new_subscope(Scope *scope) {
    return Scope::from(scope);
}

static Scope *f_globals() {
    return globals;
}

static void f_set_globals(Scope *s) {
    globals = s;
}

static Label *f_eval(const Syntax *expr, Scope *scope) {
    return normalize(expand_module(expr, scope));
}

static void f_set_scope_symbol(Scope *scope, Symbol sym, Any value) {
    scope->bind(sym, value);
}

static void f_del_scope_symbol(Scope *scope, Symbol sym) {
    scope->del(sym);
}

static Label *f_typify(Label *srcl, int numtypes, const Type **typeargs) {
    std::vector<const Type *> types = { TYPE_Void };
    for (int i = 0; i < numtypes; ++i) {
        types.push_back(typeargs[i]);

    }
    return normalize(typify(srcl, types)); 
}

static Any f_compile(Label *srcl, uint64_t flags) {
    return compile(srcl, flags);
}

static const Type *f_array_type(const Type *element_type, size_t count) {
    return Array(element_type, count);
}

static const String *f_default_styler(Symbol style, const String *str) {
    StyledString ss;
    if (!style.is_known()) {
        location_error(String::from("illegal style"));
    }
    ss.out << Style(style.known_value()) << str->data << Style_None;
    return ss.str();
}

typedef struct { const String *_0; bool _1; } StringBoolPair;
static StringBoolPair f_prompt(const String *s, const String *pre) {
    if (pre->count) {
        linenoisePreloadBuffer(pre->data);
    }
    char *r = linenoise(s->data);
    if (!r) {
        return { Symbol(SYM_Unnamed).name(), false };
    }
    linenoiseHistoryAdd(r);
    return { String::from_cstr(r), true };
}

static const String *f_format_message(const Anchor *anchor, const String *message) {
    StyledString ss;
    ss.out << anchor << " " << message->data << std::endl;
    anchor->stream_source_line(ss.out);
    return ss.str();
}

static const String *f_symbol_to_string(Symbol sym) {
    return sym.name();
}

static void f_set_signal_abort(bool value) {
    signal_abort = value;
}

ExceptionPad *f_set_exception_pad(ExceptionPad *pad) {
    ExceptionPad *last_exc_pad = _exc_pad;
    _exc_pad = pad;
    return last_exc_pad;
}

Any f_exception_value(ExceptionPad *pad) {
    return pad->value;
}

static bool f_any_eq(Any a, Any b) {
    return a == b;
}

static const List *f_list_join(List *a, List *b) {
    return List::join(a, b);
}

static int f_typename_field_index(const Type *type, Symbol name) {
    verify_kind<TK_Typename>(type);
    auto tn = cast<TypenameType>(type);
    return tn->field_index(name);
}

static Symbol f_typename_field_name(const Type *type, int index) {
    verify_kind<TK_Typename>(type);
    auto tn = cast<TypenameType>(type);
    return tn->field_name(index);
}

typedef struct { Any _0; Any _1; } AnyAnyPair;
static AnyAnyPair f_scope_next(Scope *scope, Any key) {
    auto &&map = scope->map;
    if (key.type == TYPE_Nothing) {
        if (map.empty()) {
            return { none, none };
        } else {
            auto it = map.begin();
            return { it->first, it->second };
        }
    } else {
        auto it = map.find(key);
        if (it == map.end()) {
            return { none, none };
        } else {
            it++;
            if (it == map.end()) {
                return { none, none };
            } else {
                return { it->first, it->second };
            }
        }
    }
}

static bool f_string_match(const String *pattern, const String *text) {
    const char *error = nullptr;
    Reprog *m = regcomp(pattern->data, 0, &error);
    if (error) {
        const String *err = String::from_cstr(error);
        regfree(m);
        location_error(err);
    }
    bool matches = (regexec(m, text->data, nullptr, 0) == 0);
    regfree(m);
    return matches;
}

static void f_load_library(const String *name) {
    dlerror();
    void *handle = dlopen(name->data, RTLD_LAZY|RTLD_DEEPBIND);
    if (!handle) {
        location_error(String::from_cstr(dlerror()));
    }
    loaded_libs.push_back(handle);
}

static const String *f_type_name(const Type *T) {
    return T->name();
}

static void f_set_typename_super(const Type *T, const Type *ST) {
    verify_kind<TK_Typename>(T);
    verify_kind<TK_Typename>(ST);
    // if T <=: ST, the operation is illegal
    const Type *S = ST;
    while (S) {
        if (S == T) {
            StyledString ss;
            ss.out << "typename " << ST << " can not be a supertype of " << T;
            location_error(ss.str());
        }
        if (S == TYPE_Typename)
            break;
        S = superof(S);
    }
    auto tn = cast<TypenameType>(T);
    const_cast<TypenameType *>(tn)->super_type = ST;
}

static void init_globals(int argc, char *argv[]) {

#define DEFINE_C_FUNCTION(SYMBOL, FUNC, RETTYPE, ...) \
    globals->bind(SYMBOL, \
        Any::from_pointer(Pointer(Function(RETTYPE, { __VA_ARGS__ })), \
            (void *)FUNC));
#define DEFINE_C_VARARG_FUNCTION(SYMBOL, FUNC, RETTYPE, ...) \
    globals->bind(SYMBOL, \
        Any::from_pointer(Pointer(Function(RETTYPE, { __VA_ARGS__ }, FF_Variadic)), \
            (void *)FUNC));
#define DEFINE_PURE_C_FUNCTION(SYMBOL, FUNC, RETTYPE, ...) \
    globals->bind(SYMBOL, \
        Any::from_pointer(Pointer(Function(RETTYPE, { __VA_ARGS__ }, FF_Pure)), \
            (void *)FUNC));

    //const Type *rawstring = Pointer(TYPE_I8);

    DEFINE_PURE_C_FUNCTION(FN_ImportC, f_import_c, TYPE_Scope, TYPE_String, TYPE_String, TYPE_List);
    DEFINE_PURE_C_FUNCTION(FN_ScopeAt, f_scope_at, Tuple({TYPE_Any,TYPE_Bool}), TYPE_Scope, TYPE_Symbol);
    DEFINE_PURE_C_FUNCTION(FN_SymbolNew, f_symbol_new, TYPE_Symbol, TYPE_String);
    DEFINE_PURE_C_FUNCTION(FN_Repr, f_repr, TYPE_String, TYPE_Any);
    DEFINE_PURE_C_FUNCTION(FN_AnyString, f_any_string, TYPE_String, TYPE_Any);    
    DEFINE_PURE_C_FUNCTION(FN_StringJoin, f_string_join, TYPE_String, TYPE_String, TYPE_String);
    DEFINE_PURE_C_FUNCTION(FN_ElementType, f_elementtype, TYPE_Type, TYPE_Type, TYPE_I32);
    DEFINE_PURE_C_FUNCTION(FN_SizeOf, f_sizeof, TYPE_USize, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_PointerType, f_pointertype, TYPE_Type, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_ListCons, f_list_cons, TYPE_List, TYPE_Any, TYPE_List);
    DEFINE_PURE_C_FUNCTION(FN_TypeKind, f_type_kind, TYPE_I32, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_BitCountOf, f_bitcountof, TYPE_I32, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_IsSigned, f_issigned, TYPE_Bool, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_TypeStorage, f_type_storage, TYPE_Type, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_IntegerType, f_integer_type, TYPE_Type, TYPE_I32, TYPE_Bool);
    DEFINE_PURE_C_FUNCTION(FN_CompilerVersion, f_compiler_version, Tuple({TYPE_I32, TYPE_I32, TYPE_I32}));
    DEFINE_PURE_C_FUNCTION(FN_TypeName, f_type_name, TYPE_String, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_TypenameType, f_typename_type, TYPE_Type, TYPE_String);
    DEFINE_PURE_C_FUNCTION(FN_SyntaxNew, f_syntax_new, TYPE_Syntax, TYPE_Anchor, TYPE_Any, TYPE_Bool); 
    DEFINE_PURE_C_FUNCTION(FN_SyntaxWrap, wrap_syntax, TYPE_Any, TYPE_Anchor, TYPE_Any, TYPE_Bool); 
    DEFINE_PURE_C_FUNCTION(FN_SyntaxStrip, strip_syntax, TYPE_Any, TYPE_Any);
    DEFINE_PURE_C_FUNCTION(FN_ParameterNew, f_parameter_new, TYPE_Parameter, TYPE_Anchor, TYPE_Symbol, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_StringNew, f_string_new, TYPE_String, Pointer(TYPE_I8), TYPE_USize);
    DEFINE_PURE_C_FUNCTION(FN_DumpLabel, f_dump_label, TYPE_Void, TYPE_Label);
    DEFINE_PURE_C_FUNCTION(FN_Eval, f_eval, TYPE_Label, TYPE_Syntax, TYPE_Scope);
    DEFINE_PURE_C_FUNCTION(FN_Typify, f_typify, TYPE_Label, TYPE_Label, TYPE_I32, Pointer(TYPE_Type));
    DEFINE_PURE_C_FUNCTION(FN_ArrayType, f_array_type, TYPE_Type, TYPE_Type, TYPE_USize);
    DEFINE_PURE_C_FUNCTION(FN_TypeCountOf, f_type_countof, TYPE_USize, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_SymbolToString, f_symbol_to_string, TYPE_String, TYPE_Symbol);
    DEFINE_PURE_C_FUNCTION(Symbol("Any=="), f_any_eq, TYPE_Bool, TYPE_Any, TYPE_Any);
    DEFINE_PURE_C_FUNCTION(FN_ListJoin, f_list_join, TYPE_List, TYPE_List, TYPE_List);    
    DEFINE_PURE_C_FUNCTION(FN_ScopeNext, f_scope_next, Tuple({TYPE_Any, TYPE_Any}), TYPE_Scope, TYPE_Any);
    DEFINE_PURE_C_FUNCTION(FN_TypenameFieldIndex, f_typename_field_index, TYPE_I32, TYPE_Type, TYPE_Symbol);
    DEFINE_PURE_C_FUNCTION(FN_TypenameFieldName, f_typename_field_name, TYPE_Symbol, TYPE_Type, TYPE_I32);
    DEFINE_PURE_C_FUNCTION(FN_StringMatch, f_string_match, TYPE_Bool, TYPE_String, TYPE_String);
    DEFINE_PURE_C_FUNCTION(SFXFN_SetTypenameSuper, f_set_typename_super, TYPE_Void, TYPE_Type, TYPE_Type);
    DEFINE_PURE_C_FUNCTION(FN_SuperOf, superof, TYPE_Type, TYPE_Type);

    DEFINE_PURE_C_FUNCTION(FN_DefaultStyler, f_default_styler, TYPE_String, TYPE_Symbol, TYPE_String);    

    DEFINE_C_FUNCTION(FN_Compile, f_compile, TYPE_Any, TYPE_Label, TYPE_U64);
    DEFINE_C_FUNCTION(FN_Prompt, f_prompt, Tuple({TYPE_String, TYPE_Bool}), TYPE_String, TYPE_String);
    DEFINE_C_FUNCTION(FN_LoadLibrary, f_load_library, TYPE_Void, TYPE_String);

    DEFINE_C_FUNCTION(FN_IsFile, f_is_file, TYPE_Bool, TYPE_String);
    DEFINE_C_FUNCTION(FN_ListLoad, f_list_load, TYPE_Syntax, TYPE_String);
    DEFINE_C_FUNCTION(FN_ListParse, f_list_parse, TYPE_Syntax, TYPE_String);
    DEFINE_C_FUNCTION(FN_ScopeNew, f_scope_new, TYPE_Scope);
    DEFINE_C_FUNCTION(FN_ScopeNewSubscope, f_scope_new_subscope, TYPE_Scope, TYPE_Scope);
    DEFINE_C_FUNCTION(KW_Globals, f_globals, TYPE_Scope);    
    DEFINE_C_FUNCTION(SFXFN_SetGlobals, f_set_globals, TYPE_Void, TYPE_Scope);
    DEFINE_C_FUNCTION(SFXFN_SetScopeSymbol, f_set_scope_symbol, TYPE_Void, TYPE_Scope, TYPE_Symbol, TYPE_Any);
    DEFINE_C_FUNCTION(SFXFN_DelScopeSymbol, f_del_scope_symbol, TYPE_Void, TYPE_Scope, TYPE_Symbol);

    DEFINE_C_FUNCTION(FN_FormatMessage, f_format_message, TYPE_String, TYPE_Anchor, TYPE_String);
    DEFINE_C_FUNCTION(FN_ActiveAnchor, get_active_anchor, TYPE_Anchor);
    DEFINE_C_FUNCTION(FN_Write, f_write, TYPE_Void, TYPE_String);
    DEFINE_C_FUNCTION(SFXFN_SetAnchor, f_set_anchor, TYPE_Void, TYPE_Anchor);
    DEFINE_C_FUNCTION(SFXFN_Error, f_error, TYPE_Void, TYPE_String);
    DEFINE_C_FUNCTION(SFXFN_Raise, f_raise, TYPE_Void, TYPE_Any);
    DEFINE_C_FUNCTION(SFXFN_Abort, f_abort, TYPE_Void);
    DEFINE_C_FUNCTION(FN_Exit, exit, TYPE_Void, TYPE_I32);    
    //DEFINE_C_FUNCTION(FN_Malloc, malloc, Pointer(TYPE_I8), TYPE_USize);

    const Type *exception_pad_type = Array(TYPE_U8, sizeof(ExceptionPad));
    const Type *p_exception_pad_type = Pointer(exception_pad_type);

    DEFINE_C_FUNCTION(Symbol("set-exception-pad"), f_set_exception_pad, 
        p_exception_pad_type, p_exception_pad_type);
    #if SCOPES_WIN32
    DEFINE_C_FUNCTION(Symbol("catch-exception"), _setjmpex, TYPE_I32, 
        Pointer(exception_pad_type), Pointer(TYPE_I8));
    #else
    DEFINE_C_FUNCTION(Symbol("catch-exception"), setjmp, TYPE_I32, 
        Pointer(exception_pad_type));
    #endif
    DEFINE_C_FUNCTION(Symbol("exception-value"), f_exception_value,
        TYPE_Any, p_exception_pad_type);
    DEFINE_C_FUNCTION(Symbol("set-signal-abort!"), f_set_signal_abort, 
        TYPE_Void, TYPE_Bool);

    

#undef DEFINE_C_FUNCTION

    auto stub_file = SourceFile::from_string(Symbol("<internal>"), String::from_cstr(""));
    auto stub_anchor = Anchor::from(stub_file, 1, 1);

    {
        // launch arguments
        // this is a function returning vararg constants
        Label *fn = Label::function_from(stub_anchor, FN_Args);
        fn->body.anchor = stub_anchor;
        fn->body.enter = fn->params[0];
        globals->bind(FN_Args, fn);
        if (argv && argc) {
            auto &&args = fn->body.args;
            args.push_back(none);
            for (int i = 0; i < argc; ++i) {
                char *s = argv[i];
                if (!s)
                    break;
                args.push_back(String::from_cstr(s));
            }
        }
    }

#if SCOPES_WIN32
    globals->bind(Symbol("operating-system"), Symbol("windows"));
#else
    globals->bind(Symbol("operating-system"), Symbol("unix"));
#endif

    globals->bind(KW_True, true);
    globals->bind(KW_False, false);
    globals->bind(KW_ListEmpty, EOL);
    globals->bind(KW_None, none);
    globals->bind(Symbol("unnamed"), Symbol(SYM_Unnamed));
    globals->bind(SYM_CompilerDir,
        String::from(scopes_compiler_dir, strlen(scopes_compiler_dir)));
    globals->bind(SYM_CompilerPath,
        String::from(scopes_compiler_path, strlen(scopes_compiler_path)));
    globals->bind(SYM_DebugBuild, scopes_is_debug());
    globals->bind(SYM_CompilerTimestamp,
        String::from_cstr(scopes_compile_time_date()));

    for (uint64_t i = STYLE_FIRST; i <= STYLE_LAST; ++i) {
        Symbol sym = Symbol((KnownSymbol)i);
        globals->bind(sym, sym);
    }

    globals->bind(Symbol("exception-pad-type"), exception_pad_type);

#define T(TYPE, NAME) \
    globals->bind(Symbol(NAME), TYPE);
B_TYPES()
#undef T

#define T(NAME, BNAME) \
    globals->bind(Symbol(BNAME), (int32_t)NAME);
    B_TYPE_KIND()
#undef T

    globals->bind(Symbol(SYM_DumpDisassembly), (uint64_t)CF_DumpDisassembly);
    globals->bind(Symbol(SYM_DumpModule), (uint64_t)CF_DumpModule);
    globals->bind(Symbol(SYM_DumpFunction), (uint64_t)CF_DumpFunction);
    globals->bind(Symbol(SYM_DumpTime), (uint64_t)CF_DumpTime);
    globals->bind(Symbol(SYM_NoOpts), (uint64_t)CF_NoOpts);

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
// SCOPES CORE
//------------------------------------------------------------------------------

/* this function looks for a header at the end of the compiler executable
   that indicates a scopes core.
   
   the header has the format (core-size <size>), where size is a i32 value
   holding the size of the core source file in bytes.

   the compiler uses this function to override the default scopes core 'core.sc'
   located in the compiler's directory. 

   to later override the default core file and load your own, cat the new core
   file behind the executable and append the header, like this:

   $ cp scopes myscopes
   $ cat mycore.sc >> myscopes
   $ echo "(core-size " >> myscopes
   $ wc -c < mycore.sc >> myscopes
   $ echo ")" >> myscopes

   */

static Any load_custom_core(const char *executable_path) {
    // attempt to read bootstrap expression from end of binary
    auto file = SourceFile::from_file(
        Symbol(String::from_cstr(executable_path)));
    if (!file) {
        stb_fprintf(stderr, "could not open binary\n");
        return none;
    }
    auto ptr = file->strptr();
    auto size = file->size();
    auto cursor = ptr + size - 1;
    while ((*cursor == '\n')
        || (*cursor == '\r')
        || (*cursor == ' ')) {
        // skip the trailing text formatting garbage
        // that win32 echo produces
        cursor--;
        if (cursor < ptr) return none;
    }
    if (*cursor != ')') return none;
    cursor--;
    // seek backwards to find beginning of expression
    while ((cursor >= ptr) && (*cursor != '('))
        cursor--;

    LexerParser footerParser(file, cursor - ptr);
    auto expr = footerParser.parse();
    if (expr.type == TYPE_Nothing) {
        stb_fprintf(stderr, "could not parse footer expression\n");
        return none;
    }
    expr = strip_syntax(expr);
    if ((expr.type != TYPE_List) || (expr.list == EOL)) {
        stb_fprintf(stderr, "footer parser returned illegal structure\n");
        return none;
    }
    expr = ((const List *)expr)->at;
    if (expr.type != TYPE_List)  {
        stb_fprintf(stderr, "footer expression is not a symbolic list\n");
        return none;
    }
    auto symlist = expr.list;
    auto it = symlist;
    if (it == EOL) {
        stb_fprintf(stderr, "footer expression is empty\n");
        return none;
    }
    auto head = it->at;
    it = it->next;
    if (head.type != TYPE_Symbol)  {
        stb_fprintf(stderr, "footer expression does not begin with symbol\n");
        return none;
    }
    if (head != Any(Symbol("core-size")))  {
        stb_fprintf(stderr, "footer expression does not begin with 'core-size'\n");
        return none;
    }
    if (it == EOL) {
        stb_fprintf(stderr, "footer expression needs two arguments\n");
        return none;
    }
    auto arg = it->at;
    it = it->next;
    if (arg.type != TYPE_I32)  {
        stb_fprintf(stderr, "script-size argument is not of type i32\n");
        return none;
    }
    auto script_size = arg.i32;
    if (script_size <= 0) {
        stb_fprintf(stderr, "script-size must be larger than zero\n");
        return none;
    }
    LexerParser parser(file, cursor - script_size - ptr, script_size);
    return parser.parse();
}

//------------------------------------------------------------------------------
// MAIN
//------------------------------------------------------------------------------

static bool terminal_supports_ansi() {
#ifdef SCOPES_WIN32
    if (isatty(STDOUT_FILENO))
        return true;
    return getenv("TERM") != nullptr;
#else
    //return isatty(fileno(stdout));
    return isatty(STDOUT_FILENO);
#endif
}

static void setup_stdio() {
    if (terminal_supports_ansi()) {
        stream_default_style = stream_ansi_style;
        #ifdef SCOPES_WIN32
        #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
        #endif

        // turn on ANSI code processing
        auto hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        auto hStdErr = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode;
        GetConsoleMode(hStdOut, &mode);
        SetConsoleMode(hStdOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        GetConsoleMode(hStdErr, &mode);
        SetConsoleMode(hStdErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        setbuf(stdout, 0);
        setbuf(stderr, 0);
        SetConsoleOutputCP(65001);
        #endif
    }
}

} // namespace scopes

#ifndef SCOPES_WIN32
static void crash_handler(int sig) {
  void *array[20];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 20);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}
#endif

int main(int argc, char *argv[]) {
    using namespace scopes;
    Symbol::_init_symbols();
    init_llvm();

    setup_stdio();
    scopes_argc = argc;
    scopes_argv = argv;

    scopes::global_c_namespace = dlopen(NULL, RTLD_LAZY);

    scopes_compiler_path = nullptr;
    scopes_compiler_dir = nullptr;
    if (argv) {
        if (argv[0]) {
            std::string loader = GetExecutablePath(argv[0]);
            // string must be kept resident
            scopes_compiler_path = strdup(loader.c_str());
        } else {
            scopes_compiler_path = strdup("");
        }

        scopes_compiler_dir = dirname(strdup(scopes_compiler_path));
    }

#if 0
#ifndef SCOPES_WIN32
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
#endif
#endif

    init_types();
    init_globals(argc, argv);

    Any expr = load_custom_core(scopes_compiler_path);
    if (expr != none) {
        goto skip_regular_load;
    }

    {
        SourceFile *sf = nullptr;
        char sourcepath[1024];
        strncpy(sourcepath, scopes_compiler_dir, 1024);
        strncat(sourcepath, "/core.sc", 1024);
        Symbol name = String::from_cstr(sourcepath);
        sf = SourceFile::from_file(name);
        if (!sf) {
            location_error(String::from("core missing\n"));
        }
        LexerParser parser(sf);
        expr = parser.parse();
    }

skip_regular_load:
    Label *fn = expand_module(expr);

#if SCOPES_DEBUG_CODEGEN
    StyledStream ss(std::cout);
    std::cout << "non-normalized:" << std::endl;
    stream_label(ss, fn, StreamLabelFormat::debug_all());
    std::cout << std::endl;
#endif

    fn = normalize(fn);
#if SCOPES_DEBUG_CODEGEN
    std::cout << "normalized:" << std::endl;
    stream_label(ss, fn, StreamLabelFormat::debug_all());
    std::cout << std::endl;
#endif

    typedef void (*MainFuncType)();
    MainFuncType fptr = (MainFuncType)compile(fn, CF_NoOpts).pointer;
    fptr();

    return 0;
}

#endif // SCOPES_CPP_IMPL
