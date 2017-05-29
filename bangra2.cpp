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
#include "external/cityhash/city.cpp"

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

void bangra_strtof(float *v, const char *str, char **str_end, int base );
void bangra_strtoll(int64_t *v, const char* str, char** endptr, int base);
void bangra_strtoull(uint64_t *v, const char* str, char** endptr, int base);

void bangra_r32_mod(float *out, float a, float b);
void bangra_r64_mod(double *out, double a, double b);

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
    T(r32, float, T2) \
    T(r64, double, T2)
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

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>

#include "llvm/IR/Module.h"

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
// #pragma GCC diagnostic ignored "-Wc99-extensions"
// #pragma GCC diagnostic ignored "-Wmissing-braces"
// this one is only enabled for code cleanup
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wdate-time"

//------------------------------------------------------------------------------
// UTILITIES
//------------------------------------------------------------------------------

void bangra_strtof(float *v, const char *str, char **str_end, int base ) {
    *v = std::strtof(str, str_end);
}
void bangra_strtoll(int64_t *v, const char* str, char** endptr, int base) {
    *v = std::strtoll(str, endptr, base);
}
void bangra_strtoull(uint64_t *v, const char* str, char** endptr, int base) {
    *v = std::strtoull(str, endptr, base);
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

void bangra_r32_mod(float *out, float a, float b) { *out = std::fmod(a,b); }
void bangra_r64_mod(double *out, double a, double b) { *out = std::fmod(a,b); }

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

enum Style {
    Style_None,
    Style_Symbol,
    Style_String,
    Style_Number,
    Style_Keyword,
    Style_Function,
    Style_SfxFunction,
    Style_Operator,
    Style_Instruction,
    Style_Type,
    Style_Comment,
    Style_Error,
    Style_Location,
};

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

//------------------------------------------------------------------------------
// SYMBOL
//------------------------------------------------------------------------------

static const char SYMBOL_ESCAPE_CHARS[] = " []{}()\"";

//------------------------------------------------------------------------------
// SYMBOL TYPE
//------------------------------------------------------------------------------

#define B_MAP_SYMBOLS() \
    T(SYM_Unnamed, "") \
    \
    /* types */ \
    T(TYPE_Nothing, "Nothing") \
    T(TYPE_Any, "Any") \
    T(TYPE_Type, "type") \
    T(TYPE_Callable, "Callable") \
    \
    T(TYPE_Bool, "bool") \
    \
    T(TYPE_Integer, "Integer") \
    T(TYPE_Real, "Real") \
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
    T(TYPE_R32, "r32") \
    T(TYPE_R64, "r64") \
    \
    T(TYPE_Builtin, "Builtin") \
    \
    T(TYPE_Scope, "Scope") \
    \
    T(TYPE_Symbol, "Symbol") \
    T(TYPE_List, "list") \
    T(TYPE_String, "string") \
    \
    T(TYPE_Form, "Form") \
    T(TYPE_Parameter, "Parameter") \
    T(TYPE_Label, "Label") \
    T(TYPE_VarArgs, "va-list") \
    T(TYPE_TypeSet, "TypeSet") \
    \
    T(TYPE_Ref, "ref") \
    \
    T(TYPE_Anchor, "Anchor") \
    \
    T(TYPE_BuiltinMacro, "BuiltinMacro") \
    T(TYPE_Macro, "Macro") \
    \
    T(TYPE_Syntax, "Syntax") \
    \
    T(TYPE_Boxed, "Boxed") \
    \
    T(TYPE_Constant, "Constant") \
    \
    /* keywords and macros */ \
    T(KW_CatRest, "::*") T(KW_CatOne, "::@") T(KW_Assert, "assert") T(KW_Break, "break") \
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
    T(KW_Return, "return") T(KW_Splice, "splice") \
    T(KW_SyntaxExtend, "syntax-extend") T(KW_True, "true") T(KW_Try, "try") \
    T(KW_Unquote, "unquote") T(KW_UnquoteSplice, "unquote-splice") \
    T(KW_With, "with") T(KW_XFn, "xfn") T(KW_XLet, "xlet") T(KW_Yield, "yield") \
    \
    /* builtin and global functions */ \
    T(FN_Alignof, "alignof") T(FN_Alloc, "alloc") T(FN_Arrayof, "arrayof") \
    T(FN_Bitcast, "bitcast") T(FN_BlockMacro, "block-macro") \
    T(FN_BlockScopeMacro, "block-scope-macro") T(FN_Box, "box") \
    T(FN_Branch, "branch") T(FN_IsCallable, "callable?") T(FN_Cast, "cast") \
    T(FN_Concat, "concat") T(FN_Cons, "cons") T(FN_Countof, "countof") \
    T(FN_CStr, "cstr") T(FN_DatumToSyntax, "datum->syntax") \
    T(FN_Disqualify, "disqualify") T(FN_Dump, "dump") \
    T(FN_ElementType, "element-type") T(FN_IsEmpty, "empty?") \
    T(FN_Enumerate, "enumerate") T(FN_Error, "error") T(FN_Eval, "eval") \
    T(FN_Exit, "exit") T(FN_Expand, "expand") \
    T(FN_ExternLibrary, "extern-library") T(FN_External, "external") \
    T(FN_ExtractMemory, "extract-memory") \
    T(FN_GetExceptionHandler, "get-exception-handler") \
    T(FN_GetScopeSymbol, "get-scope-symbol") T(FN_Hash, "hash") \
    T(FN_ImportC, "import-c") T(FN_IsInteger, "integer?") T(FN_Iter, "iter") \
    T(FN_IsIterator, "iterator?") T(FN_IsLabel, "label?") \
    T(FN_ListAtom, "list-atom?") T(FN_ListLoad, "list-load") \
    T(FN_ListParse, "list-parse") T(FN_IsList, "list?") T(FN_Load, "load") \
    T(FN_Macro, "macro") T(FN_Max, "max") T(FN_Min, "min") T(FN_IsNone, "none?") \
    T(FN_IsNull, "null?") T(FN_OrderedBranch, "ordered-branch") \
    T(FN_ParseC, "parse-c") T(FN_PointerOf, "pointerof") T(FN_Print, "print") \
    T(FN_Product, "product") T(FN_Prompt, "prompt") T(FN_Qualify, "qualify") \
    T(FN_Range, "range") T(FN_Repeat, "repeat") T(FN_Repr, "repr") \
    T(FN_Require, "require") T(FN_ScopeOf, "scopeof") T(FN_SizeOf, "sizeof") \
    T(FN_Slice, "slice") T(FN_StructOf, "structof") T(FN_IsSymbol, "symbol?") \
    T(FN_SyntaxAnchor, "syntax->anchor") T(FN_SyntaxDatum, "syntax->datum") \
    T(FN_SyntaxCons, "syntax-cons") T(FN_SyntaxDo, "syntax-do") \
    T(FN_SyntaxError, "syntax-error") T(FN_IsSyntaxHead, "syntax-head?") \
    T(FN_SyntaxList, "syntax-list") T(FN_SyntaxQuote, "syntax-quote") \
    T(FN_IsSyntaxQuoted, "syntax-quoted?") \
    T(FN_SyntaxUnquote, "syntax-unquote") T(FN_TupleOf, "tupleof") \
    T(FN_IsType, "type?") T(FN_TypeOf, "typeof") T(FN_Unbox, "unbox") \
    T(FN_VaCountOf, "va-countof") T(FN_VaAter, "va-iter") T(FN_VaAt, "va@") \
    T(FN_VectorOf, "vectorof") T(FN_XPCall, "xpcall") T(FN_Zip, "zip") \
    T(FN_ZipFill, "zip-fill") \
    \
    /* builtin and global functions with side effects */ \
    T(SFXFN_CopyMemory, "copy-memory!") T(SFXFN_RefSet, "ref-set!") \
    T(SFXFN_SetExceptionHandler, "set-exception-handler!") \
    T(SFXFN_SetGlobals, "set-globals!") \
    T(SFXFN_SetScopeSymbol, "set-scope-symbol!") \
    T(SFXFN_SetTypeSymbol, "set-type-symbol!") \
    \
    /* builtin operator functions that can also be used as infix */ \
    T(OP_NotEq, "!=") T(OP_Mod, "%") T(OP_InMod, "%=") T(OP_BitAnd, "&") T(OP_InBitAnd, "&=") \
    T(OP_Mul, "*") T(OP_Pow, "**") T(OP_InMul, "*=") T(OP_Add, "+") T(OP_Incr, "++") \
    T(OP_InAdd, "+=") T(OP_Comma, ",") T(OP_Sub, "-") T(OP_Decr, "--") T(OP_InSub, "-=") \
    T(OP_Dot, ".") T(OP_Join, "..") T(OP_Div, "/") T(OP_InDiv, "/=") \
    T(OP_Colon, ":") T(OP_Let, ":=") T(OP_Less, "<") T(OP_LeftArrow, "<-") T(OP_Subtype, "<:") \
    T(OP_ShiftL, "<<") T(OP_LessThan, "<=") T(OP_Set, "=") T(OP_Eq, "==") \
    T(OP_Greater, ">") T(OP_GreaterThan, ">=") T(OP_ShiftR, ">>") T(OP_Tertiary, "?") \
    T(OP_At, "@") T(OP_Xor, "^") T(OP_InXor, "^=") T(OP_And, "and") T(OP_Not, "not") \
    T(OP_Or, "or") T(OP_BitOr, "|") T(OP_InBitOr, "|=") T(OP_BitNot, "~") \
    T(OP_InBitNot, "~=") \
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
    B_MAP_SYMBOLS()
#undef T
    SYM_Count,
};

enum {
    TYPE_FIRST = TYPE_Nothing,
    TYPE_LAST = TYPE_Constant,

    KEYWORD_FIRST = KW_CatRest,
    KEYWORD_LAST = KW_Yield,

    FUNCTION_FIRST = FN_Alignof,
    FUNCTION_LAST = FN_ZipFill,

    SFXFUNCTION_FIRST = SFXFN_CopyMemory,
    SFXFUNCTION_LAST = SFXFN_SetTypeSymbol,

    OPERATOR_FIRST = OP_NotEq,
    OPERATOR_LAST = OP_InBitNot,
};

static const char *get_known_symbol_name(KnownSymbol sym) {
    switch(sym) {
#define T(SYM, NAME) case SYM: return #SYM;
    B_MAP_SYMBOLS()
#undef T
    case SYM_Count: return "SYM_Count";
    }
}

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
        map_symbol(id, name);
        return id;
    }

    static const String *get_symbol_name(Symbol id) {
        return map_symbol_name[id];
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
        B_MAP_SYMBOLS()
    #undef T
    }

    StyledStream& stream(StyledStream& ost) const {
        auto s = name();
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

    static SourceFile *open(Symbol _path, const String *str = nullptr) {
        auto it = file_cache.find(_path);
        if (it != file_cache.end()) {
            return it->second;
        }
        SourceFile *file = new SourceFile(_path);
        if (str) {
            // loading from string buffer rather than file
            file->ptr = (void *)str->data;
            file->length = str->count;
            file->_str = str;
            file_cache[_path] = file;
        } else {
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
        }
        return nullptr;
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
    Anchor(Symbol _path, int _lineno, int _column, int _offset) :
        path(_path),
        lineno(_lineno),
        column(_column),
        offset(_offset) {}

public:
    Symbol path;
    int lineno;
    int column;
    int offset;

    static const Anchor *from(
        Symbol _path, int _lineno, int _column, int _offset = 0) {
        return new Anchor(_path, _lineno, _column, _offset);
    }

    StyledStream& stream(StyledStream& ost) const {
        ost << Style_Location;
        auto ss = StyledStream::plain(ost);
        ss << path.name()->data << ":" << lineno << ":" << column << ":";
        ost << Style_None;
        return ost;
    }

    StyledStream &stream_source_line(StyledStream &ost, const char *indent = "    ") const {
        SourceFile *sf = SourceFile::open(path);
        if (sf) {
            sf->stream(ost, offset, indent);
        }
        return ost;
    }
};

static StyledStream& operator<<(StyledStream& ost, const Anchor *anchor) {
    return anchor->stream(ost);
}

//------------------------------------------------------------------------------
// TYPE
//------------------------------------------------------------------------------

struct Type {
    typedef KnownSymbol EnumT;
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
        name().name()->stream(ost, "");
        ost << Style_None;
        return ost;
    }
};

static StyledStream& operator<<(StyledStream& ost, Type type) {
    return type.stream(ost);
}

//------------------------------------------------------------------------------
// BUILTIN
//------------------------------------------------------------------------------

struct Builtin {
    typedef KnownSymbol EnumT;
protected:
    Symbol _name;

public:
    Builtin(Symbol name) :
        _name(name) {
        assert(_name.is_known());
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
// FORM
//------------------------------------------------------------------------------

struct Form {
    typedef KnownSymbol EnumT;
protected:
    Symbol _name;

public:
    Form(Symbol name) :
        _name(name) {
        assert(_name.is_known());
    }

    EnumT value() const {
        return _name.known_value();
    }

    bool operator < (Form b) const { return _name < b._name; }
    bool operator ==(Form b) const { return _name == b._name; }
    bool operator !=(Form b) const { return _name != b._name; }
    bool operator ==(EnumT b) const { return _name == b; }
    bool operator !=(EnumT b) const { return _name != b; }
    std::size_t hash() const { return _name.hash(); }
    Symbol name() const { return _name; }

    StyledStream& stream(StyledStream& ost) const {
        ost << Style_Function; name().name()->stream(ost, ""); ost << Style_None;
        return ost;
    }
};

static StyledStream& operator<<(StyledStream& ost, Form form) {
    return form.stream(ost);
}

//------------------------------------------------------------------------------
// ANY
//------------------------------------------------------------------------------

struct Syntax;
struct List;
struct Label;
struct Parameter;
struct VarArgs;

static void location_error(const String *msg);

struct Any {
    Type type;
    union {
        bool i1;
        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        float r32;
        double r64;
        Type typeref;
        const String *string;
        Symbol symbol;
        const Syntax *syntax;
        const List *list;
        Label *label;
        Parameter *parameter;
        const VarArgs *varargs;
        Builtin builtin;
    };

    Any(Nothing x) : type(TYPE_Nothing) {}
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
    Any(float x) : type(TYPE_R32), r32(x) {}
    Any(double x) : type(TYPE_R64), r64(x) {}
    Any(const String *x) : type(TYPE_String), string(x) {}
    Any(Symbol x) : type(TYPE_Symbol), symbol(x) {}
    Any(const Syntax *x) : type(TYPE_Syntax), syntax(x) {}
    Any(const List *x) : type(TYPE_List), list(x) {}
    //Any(const VarArgs *x) : type(TYPE_VarArgs), varargs(x) {}
    Any(VarArgs *x) : type(TYPE_VarArgs), varargs(x) {}
    Any(Label *x) : type(TYPE_Label), label(x) {}
    Any(Parameter *x) : type(TYPE_Parameter), parameter(x) {}
    Any(Builtin x) : type(TYPE_Builtin), builtin(x) {}
    template<unsigned N>
    Any(const char (&str)[N]) : type(TYPE_String), string(String::from(str)) {}
    // a catch-all for unsupported types
    template<typename T>
    Any(const T &x);

    template<typename T>
    void dispatch(const T &dest) const {
        switch(type.known_value()) {
            case TYPE_Nothing: return dest(none);
            case TYPE_Type: return dest(typeref);
            case TYPE_Bool: return dest(i1);
            case TYPE_I8: return dest(i8);
            case TYPE_I16: return dest(i16);
            case TYPE_I32: return dest(i32);
            case TYPE_I64: return dest(i64);
            case TYPE_U8: return dest(u8);
            case TYPE_U16: return dest(u16);
            case TYPE_U32: return dest(u32);
            case TYPE_U64: return dest(u64);
            case TYPE_R32: return dest(r32);
            case TYPE_R64: return dest(r64);
            case TYPE_String: return dest(string);
            case TYPE_Symbol: return dest(symbol);
            case TYPE_Syntax: return dest(syntax);
            case TYPE_List: return dest(list);
            default: assert(false && "cannot dispatch type"); break;
        }
    }

    struct AnyStreamer {
        StyledStream& ost;
        const Type &type;
        AnyStreamer(StyledStream& _ost, const Type &_type) :
            ost(_ost), type(_type) {}
        template<typename T>
        void operator ()(const T &x) const {
            ost << x;
            ost << Style_Operator << ":" << Style_None;
            ost << type;
        }
        template<typename T>
        void naked(const T &x) const {
            ost << x;
        }
        // these types are streamed without type tag
        void operator ()(const Nothing &x) const { naked(x); }
        void operator ()(bool x) const { naked(x); }
        void operator ()(int32_t x) const { naked(x); }
        void operator ()(const String *x) const { naked(x); }
        void operator ()(const Syntax *x) const { naked(x); }
        void operator ()(const List *x) const { naked(x); }
        void operator ()(Symbol x) const { naked(x); }
        void operator ()(Type x) const { naked(x); }
    };

    template<KnownSymbol T>
    void verify() const {
        StyledString ss;
        ss.out << "type " << Type(T) << " expected, got " << type << std::endl;
        location_error(ss.str());
    }

    operator const List *() const { verify<TYPE_List>(); return list; }
    operator const Syntax *() const { verify<TYPE_Syntax>(); return syntax; }

    StyledStream& stream(StyledStream& ost) const {
        dispatch(AnyStreamer(ost, type));
        return ost;
    }
};

static StyledStream& operator<<(StyledStream& ost, Any value) {
    return value.stream(ost);
}

//------------------------------------------------------------------------------
// VARARGS
//------------------------------------------------------------------------------

struct VarArgs {
    std::vector<Any> values;

    size_t size() const {
        return values.size();
    }

    bool empty() const {
        return values.empty();
    }

    Any first() const {
        if (values.empty()) {
            return none;
        } else {
            return values[0];
        }
    }

    Any at(size_t i) const {
        return values[i];
    }

    static VarArgs *from(size_t capacity) {
        VarArgs *va = new VarArgs();
        va->values.reserve(capacity);
        return va;
    }
};

//------------------------------------------------------------------------------
// ERROR HANDLING
//------------------------------------------------------------------------------

static const Anchor *_active_anchor = nullptr;

static void set_active_anchor(const Anchor *anchor) {
    _active_anchor = anchor;
}

static const Anchor *get_active_anchor() {
    return _active_anchor;
}

struct Exception {
    const Anchor *anchor;
    const String *msg;

    Exception(const Anchor *_anchor, const String *_msg) :
        anchor(_anchor),
        msg(_msg) {}
};

static void location_error(const String *msg) {
    throw Exception(_active_anchor, msg);
}

//------------------------------------------------------------------------------
// SCOPE
//------------------------------------------------------------------------------

struct Scope {
protected:
    std::unordered_map<Symbol, Any, Symbol::Hash> map;
    Scope *parent;

    Scope(Scope *_parent = nullptr) : parent(_parent) {}

public:
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
        map.insert(std::pair<Symbol, Any>(name, value));
    }

    void del(Symbol name) {
        auto it = map.find(name);
        if (it != map.end()) {
            map.erase(it);
        }
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
            << format("%i+%i symbols", count, totalcount - count)
            << Style_Comment << ">" << Style_None;
        return ss;
    }

    Scope *from(Scope *_parent = nullptr) {
        return new Scope(_parent);
    }
};


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

static StyledStream& operator<<(StyledStream& ost, const List *list) {
    ost << Style_Operator << "(" << Style_None;
    int i = 0;
    while (list != EOL) {
        if (i > 0) {
            ost << " ";
        }
        ost << list->at;
        list = list->next;
        ++i;
    }
    ost << Style_Operator << ")" << Style_None;
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
    Symbol path;
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

    LexerParser(Symbol _path, const char *_input_stream,
        const char *_eof = nullptr, int offset = 0) :
            value(none) {
        if (!_eof) {
            _eof = _input_stream + strlen(_input_stream);
        }
        token = tok_eof;
        base_offset = offset;
        path = _path;
        input_stream = _input_stream;
        eof = _eof;
        cursor = next_cursor = _input_stream;
        lineno = next_lineno = 1;
        line = next_line = _input_stream;
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
        return Anchor::from(path, lineno, column(), offset());
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

    template<typename T>
    bool read_number(void (*strton)(T *, const char*, char**, int)) {
        char *cend;
        errno = 0;
        T srcval;
        strton(&srcval, cursor, &cend, 0);
        if ((cend == cursor)
            || (errno == ERANGE)
            || (cend > eof)
            || ((!isspace(*cend)) && (!strchr(TOKEN_TERMINATORS, *cend)))) {
            return false;
        }
        value = Any(srcval);
        next_cursor = cend;
        return true;
    }

    bool read_int64() {
        return read_number(bangra_strtoll);
    }
    bool read_uint64() {
        return read_number(bangra_strtoull);
    }
    bool read_real32() {
        return read_number(bangra_strtof);
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
        else if (c == ',') { token = tok_symbol; read_single_symbol(); }
        else if (read_int64() || read_uint64() || read_real32()) { token = tok_number; }
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
        if ((value.type == TYPE_I64)
            && (value.i64 <= 0x7fffffffll)
            && (value.i64 >= -0x80000000ll)) {
            return int32_t(value.i64);
        } else if ((value.type == TYPE_U64)
            && (value.u64 <= 0xffffffffull)) {
            return uint32_t(value.u64);
        }
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
};

struct StreamExpr {
    StyledStream &ss;
    StreamExprFormat fmt;
    const Anchor *last_anchor;
    bool line_anchors;
    bool atom_anchors;

    StreamExpr(StyledStream &_ss, const StreamExprFormat &_fmt) :
        ss(_ss), fmt(_fmt), last_anchor(nullptr) {
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

    void stream_anchor(const Anchor *anchor, bool quoted = false) {
        if (anchor) {
            ss << Style_Location;
            auto rss = StyledStream::plain(ss);
            // ss << path.name()->data << ":" << lineno << ":" << column << ":";
            if (!last_anchor || (last_anchor->path != anchor->path)) {
                rss << anchor->path.name()->data
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
                    case TYPE_R32:
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
            users[label] = 1;
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
    const Label *label;
    int index;
    bool vararg;

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
        if (type != TYPE_Any) {
            ss << Style_Operator << ":" << Style_None << type;
        }
        return ss;
    }
    StyledStream &stream(StyledStream &ss) const;

    static Parameter *from(const Parameter *_param) {
        auto param = new Parameter(*_param);
        param->label = nullptr;
        param->index = -1;
        return param;
    }

    static Parameter *from(const Anchor *_anchor, Symbol _name, Type _type) {
        return new Parameter(_anchor, _name, _type, false);
    }

    static Parameter *vararg_from(const Anchor *_anchor, Symbol _name, Type _type) {
        return new Parameter(_anchor, _name, _type, true);
    }
};

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

static const char CONT_SEP[] = "→";

template<typename T>
struct Tag {
    static int active_gen;
    int gen;

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
int Tag<T>::active_gen = 0;

typedef Tag<Label> LabelTag;

struct Label : ILNode {
protected:
    static uint64_t next_uid;

    Label(const Anchor *_anchor, Symbol _name) :
        uid(++next_uid), anchor(_anchor), name(_name), paired(nullptr)
        {}

public:
    uint64_t uid;
    const Anchor *anchor;
    Symbol name;
    std::vector<Parameter *> params;
    Body body;
    LabelTag tag;
    Label *paired;

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

    void set_enter(const Any &enter) {
        unuse(body.enter, -1);
        body.enter = enter;
        use(body.enter, -1);
    }

    void set_args(const Any *args, size_t count) {
        {
            size_t count = body.args.size();
            for (size_t i = 0; i < count; ++i) {
                unuse(body.args[i], i);
            }
        }
        body.args.clear();
        body.args.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            body.args.push_back(args[i]);
        }
        for (size_t i = 0; i < count; ++i) {
            use(body.args[i], i);
        }
    }

    template<unsigned N>
    void set_args(const Any (&args)[N]) {
        set_args(args, N);
    }

    std::vector<Label *> &build_scope() {
        static std::vector<Label *> tempscope;

        LabelTag::clear();
        tag.visit();

        tempscope.clear();
        for (auto &&param : params) {
            // every label using one of our parameters is live in scope
            for (auto &&kv : param->users) {
                Label *live_label = kv.first;
                if (!live_label->tag.visited()) {
                    live_label->tag.visit();
                    tempscope.push_back(live_label);
                }
            }
        }

        size_t index = 0;
        while (index < tempscope.size()) {
            Label *scope_label = tempscope[index++];
            // users of scope_label are indirectly live in scope
            for (auto &&kv : scope_label->users) {
                Label *live_label = kv.first;
                if (!live_label->tag.visited()) {
                    live_label->tag.visit();
                    tempscope.push_back(live_label);
                }
            }

            // every label using one of our parameters is live in scope
            for (auto &&param : scope_label->params) {
                for (auto &&kv : param->users) {
                    Label *live_label = kv.first;
                    if (!live_label->tag.visited()) {
                        live_label->tag.visit();
                        tempscope.push_back(live_label);
                    }
                }
            }
        }

        return tempscope;
    }

    StyledStream &stream_short(StyledStream &ss) const {
        ss << Style_Keyword << "λ" << Style_Symbol;
        name.name()->stream(ss, SYMBOL_ESCAPE_CHARS);
        ss << Style_Operator << "#" << Style_None << uid;
        return ss;
    }

    StyledStream &stream(StyledStream &ss) const {
        stream_short(ss);
        ss << Style_Operator << "(" << Style_None;
        size_t count = params.size();
        for (size_t i = 1; i < count; ++i) {
            if (i > 1) {
                ss << " ";
            }
            params[i]->stream_local(ss);
        }
        ss << Style_Operator << ")" << Style_None;
        if (count && (params[0]->type != TYPE_Nothing)) {
            ss << Style_Comment << CONT_SEP << Style_None;
            params[0]->stream_local(ss);
        }
        return ss;
    }

    static Label *from(const Anchor *_anchor, Symbol _name) {
        return new Label(_anchor, _name);
    }
    // only inherits name and anchor
    static Label *from(const Label *label) {
        return new Label(label->anchor, label->name);
    }

};

uint64_t Label::next_uid = 0;

static StyledStream& operator<<(StyledStream& ss, Label *label) {
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

typedef std::map<ILNode *, Any> MangleMap;

void mangle_remap_body(Label *ll, Label *entry, MangleMap &map) {
    Any enter = entry->body.enter;
    std::vector<Any> &args = entry->body.args;
    std::vector<Any> &body = ll->body.args;
    if (enter.type == TYPE_Label) {
        auto it = map.find(enter.label);
        if (it != map.end()) {
            enter = it->second;
        }
    } else if (enter.type == TYPE_Parameter) {
        auto it = map.find(enter.parameter);
        if (it != map.end()) {
            enter = it->second;
        }
    } else {
        goto skip;
    }
    if (enter.type == TYPE_VarArgs) {
        enter = enter.varargs->first();
    }
skip:
    ll->body.enter = enter;

    entry->link_backrefs();
    for (size_t i = 0; i < args.size(); ++i) {
        Any arg = args[i];
        if (arg.type == TYPE_Label) {
            auto it = map.find(arg.label);
            if (it != map.end()) {
                arg = it->second;
            }
        } else if (arg.type == TYPE_Parameter) {
            auto it = map.find(arg.parameter);
            if (it != map.end()) {
                arg = it->second;
            }
        } else {
            goto skip2;
        }
        if (arg.type == TYPE_VarArgs) {
            // if at tail, append
            if (i == (args.size() - 1)) {
                for (size_t j = 0; j < arg.varargs->size(); ++j) {
                    body.push_back(arg.varargs->at(j));
                }
                continue;
            } else {
                arg = arg.varargs->first();
            }
        }
    skip2:
        body.push_back(arg);
    }

    ll->link_backrefs();
}

static Label *mangle(Label *entry, std::vector<Parameter *> params, MangleMap &map) {
    std::vector<Label *> &entry_scope = entry->build_scope();
    // remap entry point
    Label *le = Label::from(entry);
    le->set_parameters(params);

    // create new labels and map new parameters
    for (auto &&l : entry_scope) {
        Label *ll = Label::from(l);
        l->paired = ll;
        map.insert(std::pair<ILNode *, Any>(l, Any(ll)));
        ll->params.reserve(l->params.size());
        for (auto &&param : l->params) {
            Parameter *pparam = Parameter::from(param);
            map.insert(std::pair<ILNode *, Any>(param, Any(pparam)));
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

    return le;
}

#if 0
    function cls.__newindex(self, name, value)
        error("can't directly set attribute: " .. name)
    end

    function cls:set_typed()
        rawset(self, 'typed', true)
    end

    function cls:set_mangled(signature, label)
        local cache = self.cache
        if not cache then
            cache = {}
            rawset(self, 'cache', cache)
        end
        set_array_key(cache, signature, label)
    end

    function cls:inputs_typed(self)
        local params = self.parameters
        for i=2,#params do
            local arg = params[i]
            if arg.type == Type.Any then
                return false
            end
        end
        return true
    end

    function cls:get_mangled(signature)
        local cache = self.cache
        if cache then
            return get_array_key(cache, signature)
        end
    end

    function cls:set_body_anchor(anchor)
        assert_anchor(anchor)
        rawset(self, 'body_anchor', anchor)
    end

    function cls:__tostring()
        return self:repr(default_styler)
    end

    function cls.create_from_syntax(name)
        local name,anchor = unsyntax(name)
        name = unwrap(Type.Symbol, name)
        return Label(name, anchor)
    end

    -- an empty function
    -- you have to add the continuation argument manually
    function cls.create_empty_function(name)
        return cls.create_from_syntax(name)
    end

    -- a function that eventually returns
    function cls.create_function(name)
        local value = cls.create_from_syntax(name)
        local sym, anchor = maybe_unsyntax(name)
        sym = unwrap(Type.Symbol, sym)
        -- continuation is always first argument
        -- this argument will be called when the function is done
        value:append_parameter(
            Parameter(Symbol("return-" .. sym.name), anchor, Type.Any))
        return value
    end

    -- a continuation that never returns
    function cls.create_continuation(name)
        local value = cls.create_from_syntax(name)
        -- first argument is present, but unused
        value:append_parameter(
            Parameter(value.name, value.anchor, Type.Nothing))
        return value
    end
end
#endif

//------------------------------------------------------------------------------
// INTERPRETER
//------------------------------------------------------------------------------

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
};

static void apply_type_error(const Any &enter) {
    StyledString ss;
    ss.out << "don't know how to apply value of type " << enter.type;
    location_error(ss.str());
}

void interpreter_loop(Label *entry) {
    Instruction _in;
    Instruction _out;

    Instruction *in = &_in;
    Instruction *out = &_out;

    MangleMap map;

    _in.enter = Any(entry);
    set_active_anchor(entry->body.anchor);

    try {
loop:
    out->clear();
    const Any &enter = in->enter;
    Any &next_enter = out->enter;
    const std::vector<Any> &args = in->args;
    std::vector<Any> &next_args = out->args;
    if (enter.type.is_known()) {
        switch(enter.type.known_value()) {
        case TYPE_Label: {
            //debugger.enter_call(dest, cont, ...)
            map.clear();

            Label *label = enter.label;
            // map arguments
            size_t srci = 0;
            size_t rcount = args.size();
            size_t pcount = label->params.size();
            for (size_t i = 0; i < pcount; ++i) {
                Parameter *param = label->params[i];
                if (param->vararg) {
                    if (i == 0) {
                        location_error(
                            String::from(
                            "continuation parameter can't be vararg"));
                    }
                    // how many parameters after this one
                    size_t remparams = pcount - i - 1;

                    // how many varargs to capture
                    size_t vargsize = 0;
                    size_t r = rcount;
                    if (remparams <= r) {
                        r = r - remparams;
                    }
                    if (srci < r) {
                        vargsize = r - srci;
                    }
                    VarArgs *va = VarArgs::from(vargsize);

                    size_t endi = srci + vargsize;
                    for (size_t k = srci; k < endi; ++k) {
                        va->values.push_back(args[k]);
                    }
                    srci = srci + vargsize;
                    map.insert(std::pair<ILNode*,Any>(param, va));
                } else if (srci < rcount) {
                    map.insert(std::pair<ILNode*,Any>(param, args[srci]));
                    srci = srci + 1;
                } else {
                    map.insert(std::pair<ILNode*,Any>(param, none));
                }
            }

            label = mangle(label, {}, map);
            next_enter = label->body.enter;
            next_args = label->body.args;
            set_active_anchor(label->body.anchor);
        } break;
        case TYPE_Builtin: {
            //debugger.enter_call(dest, cont, ...)
            auto func = enter.builtin.value();
            switch(func) {
            case FN_Exit: {
                return;
                } break;
            default: {
                StyledString ss;
                ss.out << "don't know how to apply builtin " << enter.builtin;
                location_error(ss.str());
                } break;
            }
        } break;
        /*
        case TYPE_Type: {
            //local ty = dest.value
            //local func = ty:lookup(Symbol.ApplyType)
            //if func ~= null then
            //    return call(func, cont, ...)
            //else
            //    location_error("can not apply type "
            //        .. tostring(ty))
            //end
        } break;
        */
        default: {
            apply_type_error(enter);
        } break;
        }
    } else {
        apply_type_error(enter);
    }

    // flip
    Instruction *tmp = in;
    in = out;
    out = tmp;
    goto loop;
    } catch (Exception &exc) {
        auto cerr = StyledStream(std::cerr);
        if (exc.anchor) {
            cerr << exc.anchor << " ";
        }
        cerr << Style_Error << "error:" << Style_None << " "
            << exc.msg->data << std::endl;
        exit(1);
    }

}

//------------------------------------------------------------------------------
// IL PRINTER
//------------------------------------------------------------------------------

#if 0
local stream_il
-- local CONT_SEP = " ⮕ " -- not compatible with Consolas
local CONT_SEP = " → "
do
    stream_il = function(writer, afunc, opts)
        assert_label(afunc)

        opts = opts or {}
        local follow = opts.follow or "scope"
        local follow_labels = (follow == "all")
        local follow_scope = (follow == "scope")
        local styler = opts.styler or default_styler
        local line_anchors = (opts.anchors == "line")
        local atom_anchors = (opts.anchors == "all")
        local users = opts.users or {}
        local scopes = opts.scopes or {}

        local last_anchor
        local function stream_anchor(anchor)
            if anchor then
                local str
                if not last_anchor or last_anchor.path ~= anchor.path then
                    str = anchor.path
                        .. ":" .. tostring(anchor.lineno)
                        .. ":" .. tostring(anchor.column) .. ANCHOR_SEP
                elseif not last_anchor or last_anchor.lineno ~= anchor.lineno then
                    str = ":" .. tostring(anchor.lineno)
                        .. ":" .. tostring(anchor.column) .. ANCHOR_SEP
                elseif not last_anchor or last_anchor.column ~= anchor.column then
                    str = "::" .. tostring(anchor.column) .. ANCHOR_SEP
                else
                    str = "::" .. ANCHOR_SEP
                end

                writer(styler(Style.Comment, str))
                last_anchor = anchor
            else
                --writer(styler(Style.Error, "?"))
            end
        end

        local visited = {}
        local stream_any
        local function stream_label_label(alabel)
            writer(styler(Style.Keyword, "λ"))
            writer(styler(Style.Symbol, alabel.name.name))
            writer(styler(Style.Operator, "#"))
            writer(styler(Style.Number, tostring(alabel.uid)))
        end
        local function stream_label_label_user(alabel)
            writer(styler(Style.Comment, "λ"))
            writer(styler(Style.Comment, alabel.name.name))
            writer(styler(Style.Comment, "#"))
            writer(styler(Style.Comment, tostring(alabel.uid)))
        end

        local function stream_param_label(param, alabel)
            if param.label ~= alabel then
                stream_label_label(param.label)
            end
            if param.name == Symbol.Unnamed then
                writer(styler(Style.Operator, "@"))
                writer(styler(Style.Number, tostring(param.index - 1)))
            else
                writer(styler(Style.Comment, "%"))
                writer(styler(Style.Symbol, param.name.name))
            end
            if param.type ~= Type.Any then
                writer(styler(Style.Comment, ":"))
                writer(param.type:repr(styler))
            end
            if param.vararg then
                writer(styler(Style.Keyword, "…"))
            end
        end

        local function stream_argument(arg, alabel)
            if arg.type == Type.Syntax then
                local anchor
                arg,anchor = unsyntax(arg)
                if atom_anchors then
                    stream_anchor(anchor)
                end
            end

            if arg.type == Type.Parameter then
                stream_param_label(arg.value, alabel)
            elseif arg.type == Type.Label then
                stream_label_label(arg.value)
            else
                writer(tostring(arg))
            end
        end

        local function stream_users(_users)
            if _users then
                writer(styler(Style.Comment, "{"))
                local k = 0
                for dest,_ in pairs(_users) do
                    if k > 0 then
                        writer(" ")
                    end
                    stream_label_label_user(dest)
                    k = k + 1
                end
                writer(styler(Style.Comment, "}"))
            end
        end

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

        local function stream_label (alabel)
            if visited[alabel] then
                return
            end
            visited[alabel] = true
            if line_anchors then
                stream_anchor(alabel.anchor)
            end
            writer(styler(Style.Symbol, alabel.name.name))
            writer(styler(Style.Operator, "#"))
            writer(styler(Style.Number, tostring(alabel.uid)))
            stream_users(users[alabel])
            writer(" ")
            writer(styler(Style.Operator, "("))
            for i=2,#alabel.parameters do
                local param = alabel.parameters[i]
                if i > 2 then
                    writer(" ")
                end
                stream_param_label(param, alabel)
                stream_users(users[param])
            end
            writer(styler(Style.Operator, ")"))
            if alabel.parameters[1] and alabel.parameters[1].type ~= Type.Nothing then
                writer(styler(Style.Comment,CONT_SEP))
                local param = alabel.parameters[1]
                stream_param_label(param, alabel)
                stream_users(users[param])
            end
            writer(styler(Style.Operator, ":"))
            stream_scope(scopes[alabel])
            writer("\n    ")
            if not alabel.enter then
                writer(styler(Style.Error, "empty"))
            else
                if line_anchors and alabel.body_anchor then
                    stream_anchor(alabel.body_anchor)
                    writer(' ')
                end
                stream_argument(alabel.enter, alabel)
                for i=2,#alabel.arguments do
                    writer(" ")
                    local arg = alabel.arguments[i]
                    stream_argument(arg, alabel)
                end
                local cont = alabel.arguments[1]
                if cont and not is_none(maybe_unsyntax(cont)) then
                    writer(styler(Style.Comment,CONT_SEP))
                    stream_argument(cont, alabel)
                end
            end
            writer("\n")

            if follow_labels then
                for i,arg in ipairs(alabel.arguments) do
                    arg = maybe_unsyntax(arg)
                    stream_any(arg)
                end
                if alabel.enter then
                    stream_any(maybe_unsyntax(alabel.enter))
                end
            end
        end
        stream_any = function(afunc)
            if afunc.type == Type.Label then
                stream_label(afunc.value)
            end
        end
        stream_label(afunc)
        if follow_scope then
            local scope = afunc:build_scope()
            for k=#scope,1,-1 do
                stream_label(scope[k])
            end
        end
    end
end
#endif

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

static const List *expand_expr_list(Scope *env, const List *list);

static const List *expand(Scope *env, const List *topit) {
process:
    assert(env);
    assert(topit != EOL);
    Any expr = topit->at;
    const Syntax *sx = expr;
    if (sx->quoted) {
        // return as-is
        return expr;
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
            default: {
            } break;
            }
        }

        list = expand_expr_list(env, list);
        return List::from(Syntax::from_quoted(anchor, list), topit->next);
    } else if (expr.type == TYPE_Symbol) {
        Symbol name = expr.symbol;

        Any result = none;
        if (!env->lookup(name, result)) {
            location_error(
                format("no value bound to name '%s' in scope", name.name()->data));
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
    goto process;
}

//------------------------------------------------------------------------------

#if 0


local globals

local expand
local translate

local expand_fn_cc
local expand_syntax_extend

local expand_root

local function wrap_expand_builtin(f)
    return function(dest, cont, topit, env)
        return f(unwrap(Type.Scope, env), unwrap(Type.List, topit),
            function (cur_list, cur_env)
                assert(cur_env)
                return retcall(cont, Any(cur_list), Any(cur_env))
            end)
    end
end

do

local function expand_expr_list(env, it, cont)
    assert_scope(env)
    assert_list(it)

    local function process(env, it, l)
        if it == EOL then
            return cont(reverse_list_inplace(l))
        end
        return expand(env, it, function(nextlist,nextscope)
            assert_list(nextlist)
            if (nextlist == EOL) then
                return cont(reverse_list_inplace(l))
            end
            return process(
                nextscope,
                nextlist.next,
                List(nextlist.at, l))
        end)
    end
    return process(env, it, EOL)
end

expand_fn_cc = function(env, topit, cont)
    assert_scope(env)
    assert_list(topit)
    verify_at_parameter_count(topit, 1, -1)

    local it = topit.at

    local nit,anchor = unsyntax(it)
    it = unwrap(Type.List, nit)

    local _,anchor_kw = unsyntax(it.at)

    it = it.next

    local func_name
    assert(it ~= EOL)

    local scopekey

    local tryfunc_name = unsyntax(it.at)
    if (tryfunc_name.type == Type.Symbol) then
        func_name = it.at
        it = it.next
        scopekey = tryfunc_name
    elseif (tryfunc_name.type == Type.String) then
        func_name = Any(Syntax(Any(Symbol(tryfunc_name.value)), anchor_kw))
        it = it.next
    else
        func_name = Any(Syntax(Any(Symbol.Unnamed), anchor_kw))
    end

    local expr_parameters = it.at
    local params_anchor
    expr_parameters, params_anchor = unsyntax(expr_parameters)

    it = it.next

    local func = Label.create_empty_function(func_name, anchor)
    if scopekey then
        -- named self-binding
        env:bind(scopekey, Any(func))
    end
    -- hidden self-binding for subsequent macros
    env:bind(Any(Symbol.ThisFnCC), Any(func))

    local subenv = Scope(env)

    local function toparameter(env, value)
        assert_scope(env)
        local _value, anchor = unsyntax(value)
        if _value.type == Type.Parameter then
            return _value.value
        else
            local param = Parameter.create_from_syntax(value, Type.Any)
            env:bind(value, Any(param))
            return param
        end
    end

    local params = unwrap(Type.List, expr_parameters)
    while (params ~= EOL) do
        func:append_parameter(toparameter(subenv, params.at))
        params = params.next
    end
    if (#func.parameters == 0) then
        set_active_anchor(params_anchor)
        location_error("explicit continuation parameter missing")
    end

    return expand_expr_list(subenv, it, function(result)
        result = List(Any(Syntax(Any(func), anchor, true)), result)
        result = List(Any(Syntax(globals:lookup(Symbol.FnCCForm), anchor, true)), result)
        return cont(List(Any(Syntax(Any(result), anchor, true)), topit.next), env)
    end)
end

expand_syntax_extend = function(env, topit, cont)
    assert_scope(env)
    assert_list(topit)
    verify_at_parameter_count(topit, 1, -1)

    local it = topit.at

    local nit,anchor = unsyntax(it)
    it = unwrap(Type.List, nit)

    local _,anchor_kw = unsyntax(it.at)
    it = it.next

    local func_name = Any(Syntax(Any(Symbol.Unnamed), anchor))
    local func = Label.create_empty_function(func_name)
    func:append_parameter(Parameter.create_from_syntax(func_name, Type.Any))

    local subenv = Scope(env)
    subenv:bind(Any(Symbol.SyntaxScope), Any(env))

    return expand_expr_list(subenv, it, function(expr)
        expr = List(Any(Syntax(Any(func), anchor, true)), expr)
        expr = List(Any(Syntax(globals:lookup(Symbol.FnCCForm), anchor, true)), expr)
        expr = Any(Syntax(Any(expr), anchor, true))
        return translate(null, expr,
            function(_state, _anchor, enter, fun)
                assert(not enter)
                fun = maybe_unsyntax(fun)
                return execute(fun,
                    function(expr_env)
                        if expr_env == null or expr_env.type ~= Type.Scope then
                            set_active_anchor(anchor)
                            location_error("syntax-extend did not evaluate to scope")
                        end
                        return cont(topit.next, unwrap(Type.Scope, expr_env))
                    end)
            end)
    end)
end

local function expand_wildcard(label, env, handler, topit, cont)
    assert_string(label)
    assert_scope(env)
    assert_any(handler)
    assert_list(topit)
    assert(cont)
    return xpcallcc(function(cont)
        return execute(handler, function(result)
            if result == null or is_none(result) then
                return cont(EOL)
            end
            if result.type ~= Type.List then
                location_error(label
                    .. " macro returned unexpected value of type "
                    .. tostring(result.type))
            end
            return cont(unwrap(Type.List, result))
        end, Any(topit), Any(env))
    end,
    function (exc, cont)
        exc = exception(exc)
        local w = string_writer()
        local _,anchor = unsyntax(topit.at)
        anchor:stream_message_with_source(w,
            'while expanding ' .. label .. ' macro')
        local fmt = StreamValueFormat()
        fmt.naked = true
        fmt.maxdepth = 3
        fmt.maxlength = 5
        stream_expr(w, topit.at, fmt)
        exc.macros = w() .. (exc.macros or "")
        error(exc)
    end,
    cont)
end

local function expand_macro(env, handler, topit, cont)
    assert_scope(env)
    assert_any(handler)
    assert_list(topit)
    assert(cont)
    return xpcallcc(function(cont)
        return execute(handler, function(result_list, result_scope)
            --print(handler, result_list, result_scope)
            if (is_none(result_list)) then
                return cont(EOL)
            end
            result_list = unwrap(Type.List, result_list)
            result_scope = result_scope and unwrap(Type.Scope, result_scope)
            if result_list ~= EOL and result_scope == null then
                location_error(tostring(handler) .. " did not return a scope")
            end
            if global_opts.validate_macros then
                -- validate result completely wrapped in syntax
                local todo = {result_list.at}
                local k = 1
                while k <= #todo do
                    local elem = todo[k]
                    if elem.type ~= Type.Syntax then
                        location_error("syntax objects missing in expanded macro")
                    end
                    if not elem.value.quoted then
                        elem = unsyntax(elem)
                        if elem.type == Type.List then
                            elem = elem.value
                            while elem ~= EOL do
                                table.insert(todo, elem.at)
                                elem = elem.next
                            end
                        end
                    end
                    k = k + 1
                    assert(k < global_opts.stack_limit, "possible circular reference encountered")
                end
            end
            return cont(result_list, result_scope)
        end, Any(topit), Any(env))
    end,
    function (exc, cont)
        exc = exception(exc)
        local w = string_writer()
        local _, anchor = unsyntax(topit.at)
        anchor:stream_message_with_source(w, 'while expanding macro')
        local fmt = StreamValueFormat()
        fmt.naked = true
        fmt.maxdepth = 3
        fmt.maxlength = 5
        stream_expr(w, topit.at, fmt)
        exc.macros = w() .. (exc.macros or "")
        error(exc)
    end,
    cont)
end

expand = function(env, topit, cont)
    assert_scope(env)
    assert_list(topit)
    local result = none
    assert(topit ~= EOL)

    local function process(env, topit)
        local expr = topit.at
        local sx = unwrap(Type.Syntax, expr)
        if sx.quoted then
            -- return as-is
            return cont(List(expr, topit.next), env)
        end
        local anchor
        expr,anchor = unsyntax(expr)
        set_active_anchor(anchor)
        if (expr.type == Type.List) then
            local list = expr.value
            if (list == EOL) then
                location_error("expression is empty")
            end

            local head = list.at
            local head_anchor
            head, head_anchor = unsyntax(head)

            -- resolve symbol
            if (head.type == Type.Symbol) then
                head = env:lookup(head.value) or none
            end

            local function expand_list()
                return expand_expr_list(env,
                    unwrap(Type.List, expr),
                    function (result)
                        return cont(List(Any(Syntax(Any(result), anchor, true)),
                            topit.next), env)
                    end)
            end

            local function expand_wildcard_list()
                local default_handler = env:lookup(Symbol.ListWildcard)
                if default_handler then
                    return expand_wildcard("wildcard list",
                        env, default_handler, topit,
                        function (result)
                            if result ~= EOL then
                                return process(env, result)
                            end
                            return expand_list()
                        end)
                end
                return expand_list()
            end

            if (is_macro_type(head.type)) then
                return expand_macro(env, unmacro(head), topit,
                    function (result_list,result_env)
                        if (result_list ~= EOL) then
                            assert_scope(result_env)
                            assert(result_list ~= EOL)
                            return process(result_env, result_list)
                        elseif result_env then
                            return cont(EOL, env)
                        else
                            return expand_wildcard_list()
                        end
                    end)
            end

            return expand_wildcard_list()
        elseif expr.type == Type.Symbol then
            local value = expr.value
            local result = env:lookup(value)
            if result == null then
                local function missing_symbol_error()
                    set_active_anchor(anchor)
                    location_error(
                        format("no value bound to name '%s' in scope", value.name))
                end
                local default_handler = env:lookup(Symbol.SymbolWildcard)
                if default_handler then
                    return expand_wildcard("wildcard symbol",
                        env, default_handler, topit, function(result)
                        if result ~= EOL then
                            return process(env, result)
                        end
                        return missing_symbol_error()
                    end)
                else
                    return missing_symbol_error()
                end
            end
            if result.type == Type.List then
                -- quote lists
                result = List(Any(Syntax(result, anchor, true)), EOL)
                result = List(Any(Syntax(globals:lookup(Symbol.QuoteForm), anchor, true)), result)
                result = Any(result)
            end
            result = Any(Syntax(result, anchor, true))
            return cont(List(result, topit.next), env)
        else
            return cont(List(Any(Syntax(expr, anchor, true)), topit.next), env)
        end
    end
    return process(env, topit)
end

expand_root = function(expr, scope, cont)
    local anchor
    if expr.type == Type.Syntax then
        expr, anchor = unsyntax(expr)
    end
    expr = unwrap(Type.List, expr)
    return expand_expr_list(scope or globals, expr, function(result)
        result = Any(result)
        if anchor then
            result = Any(Syntax(result, anchor))
        end
        return cont(result)
    end)
end

end -- do
#endif

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

    setup_stdio();
    bangra_argc = argc;
    bangra_argv = argv;

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

    auto cout = StyledStream(std::cout);

    cout << 1 << "," << 2.0 << "," << false << std::endl;

    cout << Any(Syntax::from(Anchor::from("test.txt",2,5), Any(String::from("yo yo yo")))) << std::endl;

    Anchor::from("bangra2.cpp",1,1,10)->stream_source_line(cout);

    cout << List::from({1,2.0,List::from({1,2,3}),Symbol("my dude"), false,"hi"}) << std::endl;

    cout << Symbol("test") << std::endl;
    cout << Symbol("tost") << std::endl;
    cout << Symbol("test") << std::endl;
    cout << Type(TYPE_U32) << std::endl;

    cout << Any(String::from("string"))
        << " " << Any(none)
        << " " << Any(Symbol("symbol"))
        << " " << Any(123)
        << " " << Any((int8_t)123)
        << " " << Any((uint8_t)123)
        << " " << Any((int16_t)123)
        << " " << Any((uint16_t)123)
        << " " << Any(false)
        << std::endl;

    auto p0 = Parameter::from(Anchor::from("test.txt",2,5), "test", TYPE_Any);
    auto fn = Label::from(Anchor::from("test.txt",2,5), "xyz");
    fn->append(p0);

    cout << fn << std::endl;

    SourceFile *sf = SourceFile::open("bangra.b");
    LexerParser parser(sf->path, sf->strptr(), sf->strptr() + sf->length);
    auto expr = parser.parse();
    //stream_expr(cout, expr, StreamExprFormat());
    interpreter_loop(fn);

    return 0;
}

#endif // BANGRA_CPP_IMPL
