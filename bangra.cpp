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
#else
#include <sys/mman.h>
#include <unistd.h>
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
    _ ## NAME = NAME,
EXPORT_DEFINES
#undef T
#undef EXPORT_DEFINES
};

enum {
    // semver style versioning
    BANGRA_VERSION_MAJOR = 0,
    BANGRA_VERSION_MINOR = 6,
    BANGRA_VERSION_PATCH = 0,
};

// C namespace exports
int unescape_string(char *buf);
int escape_string(char *buf, const char *str, int strcount, const char *quote_chars);

void bangra_strtof(float *v, const char *str, char **str_end );
void bangra_strtoll(long long int *v, const char* str, char** endptr, int base);
void bangra_strtoull(unsigned long long int *v, const char* str, char** endptr, int base);

bool bangra_r32_eq(float a, float b);
bool bangra_r32_ne(float a, float b);
bool bangra_r32_lt(float a, float b);
bool bangra_r32_gt(float a, float b);

bool bangra_r64_eq(double a, double b);
bool bangra_r64_ne(double a, double b);
bool bangra_r64_lt(double a, double b);
bool bangra_r64_gt(double a, double b);

#define DEF_BINOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, ctype b);
#define IMPL_BINOP_FUNC(tag, ctype, name, op) \
    void bangra_ ## tag ## _ ## name(ctype *out, ctype a, ctype b) { *out = a op b; }
#define WALK_ARITHMETIC_OPS(tag, ctype, T) \
    T(tag, ctype, add, +) \
    T(tag, ctype, sub, -) \
    T(tag, ctype, mul, *) \
    T(tag, ctype, div, /)
#define WALK_PRIMITIVE_TYPES(T, T2) \
    T(i8, int8_t, T2) \
    T(i16, int16_t, T2) \
    T(i32, int32_t, T2) \
    T(i64, int64_t, T2) \
    T(u8, uint8_t, T2) \
    T(u16, uint16_t, T2) \
    T(u32, uint32_t, T2) \
    T(u64, uint64_t, T2) \
    T(r32, float, T2) \
    T(r64, double, T2)
WALK_PRIMITIVE_TYPES(WALK_ARITHMETIC_OPS, DEF_BINOP_FUNC)

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
#include "external/linenoise-ng/include/linenoise.h"
#else
// for backtrace
#include <execinfo.h>
#include <dlfcn.h>
//#include "external/linenoise/linenoise.h"
#include "external/linenoise-ng/include/linenoise.h"
#endif
#include <assert.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdlib.h>
#include <libgen.h>

#include <cstdlib>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "bangra.bin.h"

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

// #pragma GCC diagnostic ignored "-Wvla-extension"
// #pragma GCC diagnostic ignored "-Wzero-length-array"
// #pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
// #pragma GCC diagnostic ignored "-Wembedded-directive"
// #pragma GCC diagnostic ignored "-Wgnu-statement-expression"
// #pragma GCC diagnostic ignored "-Wc99-extensions"
// #pragma GCC diagnostic ignored "-Wmissing-braces"
// this one is only enabled for code cleanup
// #pragma GCC diagnostic ignored "-Wunused-function"

//------------------------------------------------------------------------------
// UTILITIES
//------------------------------------------------------------------------------

void bangra_strtof(float *v, const char *str, char **str_end ) {
    *v = std::strtof(str, str_end);
}
void bangra_strtoll(long long int *v, const char* str, char** endptr, int base) {
    *v = std::strtoll(str, endptr, base);
}
void bangra_strtoull(unsigned long long int *v, const char* str, char** endptr, int base) {
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

bool bangra_r32_eq(float a, float b) { return a == b; }
bool bangra_r32_ne(float a, float b) { return a != b; }
bool bangra_r32_lt(float a, float b) { return a <  b; }
bool bangra_r32_gt(float a, float b) { return a >  b; }

bool bangra_r64_eq(double a, double b) { return a == b; }
bool bangra_r64_ne(double a, double b) { return a != b; }
bool bangra_r64_lt(double a, double b) { return a <  b; }
bool bangra_r64_gt(double a, double b) { return a >  b; }

WALK_PRIMITIVE_TYPES(WALK_ARITHMETIC_OPS, IMPL_BINOP_FUNC)

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
}

static const char *progname = "<bangra>";

static void l_message(const char *pname, const char *msg) {
  if (pname) fprintf(stderr, "%s: ", pname);
  fprintf(stderr, "%s\n", msg);
  fflush(stderr);
}

static int report(lua_State *L, int status) {
  if (status && !lua_isnil(L, -1)) {
    const char *msg = lua_tostring(L, -1);
    if (msg == NULL) msg = "(error object is not a string)";
    l_message(progname, msg);
    lua_pop(L, 1);
  }
  return status;
}

static struct Smain {
  char **argv;
  int argc;
  int status;
} smain;

static int getargs(lua_State *L, char **argv, int n) {
    int narg;
    int i;
    int argc = 0;
    while (argv[argc]) argc++;  /* count total number of arguments */
    narg = argc - (n + 1);  /* number of arguments to the script */
    luaL_checkstack(L, narg + 3, "too many arguments to script");
    for (i = n+1; i < argc; i++)
        lua_pushstring(L, argv[i]);
    lua_createtable(L, narg, n + 1);
    for (i = 0; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - n);
    }
    return narg;
}

static int traceback(lua_State *L) {
    if (!lua_isstring(L, 1)) { /* Non-string error object? Try metamethod. */
        if (lua_isnoneornil(L, 1) ||
            !luaL_callmeta(L, 1, "__tostring") ||
            !lua_isstring(L, -1))
            return 1;  /* Return non-string error object. */
        lua_remove(L, 1);  /* Replace object by result of __tostring metamethod. */
    }
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

static int docall(lua_State *L, int narg, int clear) {
    int status;
    int base = lua_gettop(L) - narg;  /* function index */
    lua_pushcfunction(L, traceback);  /* push traceback function */
    lua_insert(L, base);  /* put it under chunk and args */
    //signal(SIGINT, laction);
    status = lua_pcall(L, narg, (clear ? 0 : LUA_MULTRET), base);
    //signal(SIGINT, SIG_DFL);
    lua_remove(L, base);  /* remove traceback function */
    /* force a complete garbage collection in case of errors */
    if (status != 0) lua_gc(L, LUA_GCCOLLECT, 0);
    return status;
}

static int handle_script(lua_State *L, char **argv, int n) {
    int status;
    int narg = getargs(L, argv, n);  /* collect arguments */
    lua_setglobal(L, "arg");
    //status = luaL_loadbuffer(L, (const char *)blobs::luaJIT_BC_bangra, luaJIT_BC_bangra_SIZE, "main");
    status = luaL_loadfile(L, "bangra.lua");
    lua_insert(L, -(narg+1));
    if (status == 0) {
        status = docall(L, narg, 0);
    } else {
        lua_pop(L, narg);
    }
    return report(L, status);
}

static int pmain(lua_State *L) {
    struct Smain *s = &smain;
    char **argv = s->argv;
    if (argv[0] && argv[0][0]) progname = argv[0];
    lua_gc(L, LUA_GCSTOP, 0);
    luaL_openlibs(L);
    lua_gc(L, LUA_GCRESTART, -1);
    s->status = handle_script(L, argv, 0);
    return 0;
}

int main(int argc, char *argv[]) {
    setup_stdio();

    // Create VM state
    lua_State *L = lua_open();
    if (!L)
        return 1;
    smain.argc = argc;
    smain.argv = argv;
    int status = lua_cpcall(L, pmain, NULL);
    report(L, status);
    lua_close(L);
    return (status || smain.status) ? EXIT_FAILURE : EXIT_SUCCESS;

    return 0;
}

#endif // BANGRA_CPP_IMPL
