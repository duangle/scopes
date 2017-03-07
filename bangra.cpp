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

//#define BANGRA_DEBUG_IL

// maximum number of arguments to a function
#define BANGRA_MAX_FUNCARGS 256

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
// UTILITIES
//------------------------------------------------------------------------------

static std::string vformat( const char *fmt, va_list va ) {
    va_list va2;
    va_copy(va2, va);
    size_t size = stb_vsnprintf( nullptr, 0, fmt, va2 );
    va_end(va2);
    std::string str;
    str.resize(size);
    stb_vsnprintf( &str[0], size + 1, fmt, va );
    return str;
}

static std::string format( const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    std::string result = vformat(fmt, va);
    va_end(va);
    return result;
}

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

template<typename T>
static void streamString(T &stream, const std::string &a, const char *quote_chars = nullptr) {
    for (size_t i = 0; i < a.size(); i++) {
        char c = a[i];
        switch(c) {
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if ((c < 32) || (c >= 127)) {
                unsigned char uc = c;
                stream << format("\\x%02x", uc);
            } else {
                if ((c == '\\') || (quote_chars && strchr(quote_chars, c)))
                    stream << '\\';
                stream << c;
            }
            break;
        }
    }
}

static std::string quoteString(const std::string &a, const char *quote_chars = nullptr) {
    std::stringstream ss;
    streamString(ss, a, quote_chars);
    return ss.str();
}

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

static bool support_ansi = false;
static std::string ansi(const std::string &code, const std::string &content) {
    if (support_ansi) {
        return code + content + ANSI_RESET;
    } else {
        return content;
    }
}

static std::string quoteReprString(const std::string &value) {
    return ansi(ANSI_STYLE_STRING,
        format("\"%s\"",
            quoteString(value, "\"").c_str()));
}

/*
static std::string quoteRepr
bol(const std::string &value) {
    return quoteString(value, "[]{}()\"\'");
}

static std::string quoteReprInteger(int64_t value) {
    return ansi(ANSI_STYLE_NUMBER, format("%" PRIi64, value));
}

static size_t padding(size_t offset, size_t align) {
    return (-offset) & (align - 1);
}
*/

static size_t align(size_t offset, size_t align) {
    return (offset + align - 1) & ~(align - 1);
}

bool endswith(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// return a resident copy
static char *resident_c_str(const std::string &path) {
    return strdup(path.c_str());
}

//------------------------------------------------------------------------------
// FILE I/O
//------------------------------------------------------------------------------

struct MappedFile {
protected:
    int fd;
    off_t length;
    void *ptr;

    MappedFile() :
        fd(-1),
        length(0),
        ptr(NULL)
        {}

public:
    ~MappedFile() {
        if (ptr)
            munmap(ptr, length);
        if (fd >= 0)
            close(fd);
    }

    const char *strptr() const {
        return (const char *)ptr;
    }

    size_t size() const {
        return length;
    }

    static std::unique_ptr<MappedFile> open(const char *path) {
        std::unique_ptr<MappedFile> file(new MappedFile());
        file->fd = ::open(path, O_RDONLY);
        if (file->fd < 0)
            return nullptr;
        file->length = lseek(file->fd, 0, SEEK_END);
        file->ptr = mmap(NULL, file->length, PROT_READ, MAP_PRIVATE, file->fd, 0);
        if (file->ptr == MAP_FAILED) {
            return nullptr;
        }
        return file;
    }

    void dumpLine(size_t offset) {
        if (offset >= (size_t)length) {
            return;
        }
        const char *str = strptr();
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
        stb_printf("%.*s\n", (int)(end - start), str + start);
        size_t column = offset - start;
        for (size_t i = 0; i < column; ++i) {
            putchar(' ');
        }
        puts(ansi(ANSI_STYLE_OPERATOR, "^").c_str());
    }
};

static void dumpFileLine(const char *path, int offset) {
    auto file = MappedFile::open(path);
    if (file) {
        file->dumpLine((size_t)offset);
    }
}

//------------------------------------------------------------------------------
// ANCHORS
//------------------------------------------------------------------------------

struct RuntimeException {};

struct Anchor {
    const char *path;
    int lineno;
    int column;
    int offset;

    Anchor() {
        path = NULL;
        lineno = 0;
        column = 0;
        offset = 0;
    }

    bool isValid() const {
        return (path != NULL) && (lineno != 0) && (column != 0);
    }

    bool operator ==(const Anchor &other) const {
        return
            path == other.path
                && lineno == other.lineno
                && column == other.column
                && offset == other.offset;
    }

    void printMessage (const std::string &msg) const {
        stb_printf("%s:%i:%i: %s\n", path, lineno, column, msg.c_str());
        dumpFileLine(path, offset);
    }

    static void printMessageV (const Anchor *anchor, const char *fmt, va_list args) {
        if (anchor) {
            std::cout
                << ansi(ANSI_STYLE_LOCATION, anchor->path)
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->lineno))
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->column))
                << " ";
        }
        stb_vprintf (fmt, args);
        putchar('\n');
        if (anchor) {
            dumpFileLine(anchor->path, anchor->offset);
        }
    }

    static void printMessage (const Anchor *anchor, const char *fmt, ...) {
        va_list args;
        va_start (args, fmt);
        Anchor::printMessageV(anchor, fmt, args);
        va_end (args);
    }

    static void printErrorV (const Anchor *anchor, const char *fmt, va_list args) {
        if (anchor && !anchor->isValid())
            anchor = nullptr;
        if (anchor) {
            std::cout
                << ansi(ANSI_STYLE_LOCATION, anchor->path)
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->lineno))
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->column))
                << " "
                << ansi(ANSI_STYLE_ERROR, "error:")
                << " ";
        } else {
            std::cout << ansi(ANSI_STYLE_ERROR, "error:") << " ";
        }
        stb_vprintf (fmt, args);
        putchar('\n');
        if (anchor) {
            dumpFileLine(anchor->path, anchor->offset);
        }
    }

    static void printError (const Anchor *anchor, const char *fmt, ...) {
        va_list args;
        va_start (args, fmt);
        Anchor::printErrorV(anchor, fmt, args);
        va_end (args);
    }

};

//------------------------------------------------------------------------------
// MID-LEVEL IL
//------------------------------------------------------------------------------

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
    T(Flow) \
    T(SpecialForm) \
    T(BuiltinMacro) \
    T(Frame) \
    T(Closure)

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

static Type TYPE_Rawstring;
static Type TYPE_PVoid;
static Type TYPE_ListTableTuple;


struct Any {
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

        const BuiltinFlowFunction builtin_flow;
        const SpecialFormFunction special_form;

        const char *c_str;
        const List *list;
        Type typeref;
        const Parameter *parameter;
        const Flow *flow;
        const Closure *closure;
        const Frame *frame;
        const Table *table;
        const Anchor *anchorref;
    };
    Type type;

    Any() {}
    Any &operator =(const Any &other) {
        ptr = other.ptr;
        type = other.type;
        return *this;
    }
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

//------------------------------------------------------------------------------

static Any const_none;

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
// SYMBOL CACHE
//------------------------------------------------------------------------------

/*
    // short string representation
    std::string (*tostring)(const Type *self, const Any &value);
    // hash representation
    uint64_t (*hash)(const Type *self, const Any &value);
    // return range
    Any (*slice)(const Type *self, const Any &value, size_t i0, size_t i1);
    // apply the type
    B_FUNC((*apply_type));
    // splice value into argument list
    size_t (*splice)(const Type *self, const Any &value, Any *ret, size_t retsize);
    // count of value, if any
    size_t (*countof)(const Type *self, const Any &value);

    // if set, specifies the supertype of this type
    const Type *supertype;

    size_t size;
    size_t alignment;
    // value is directly stored in Any struct, rather than in a pointer
    // the value is then by definition immutable.
    bool is_embedded;

    union {
        // integer: is type signed?
        bool is_signed;
        // function: is vararg?
        bool is_vararg;
    };
    union {
        // integer, real: width in bits
        size_t width;
        // array, vector: number of elements
        size_t count;
    };

    union {
        // array, vector, pointer: type of element
        const Type *element_type;
        // enum: type of tag
        const Type *tag_type;
        // function: type of result
        const Type *result_type;
    };
    // tuple, struct, union: field types
    // function: parameter types
    // enum: payload types
    std::vector<const Type *> types;
    // tuple, struct: field offsets
    std::vector<size_t> offsets;
    // enum: tag values
    std::vector<int64_t> tags;
    // struct, union: name to types index lookup table
    // enum: name to types & tag index lookup table
    std::unordered_map<Symbol, size_t> name_index_map;
*/

#define B_MAP_SYMBOLS() \
    T(SYM_Unnamed, "") \
    T(SYM_Name, "name") \
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

enum {
#define T(sym, name) sym,
    B_MAP_SYMBOLS()
#undef T
    SYM_Count,
};

static Symbol next_symbol_id = SYM_Count;
static std::map<Symbol, std::string> map_symbol_name;
static std::map<std::string, Symbol> map_name_symbol;

static void map_symbol(Symbol id, const std::string &name) {
    map_name_symbol[name] = id;
    map_symbol_name[id] = name;
}

static Symbol get_symbol(const std::string &name) {
    auto it = map_name_symbol.find(name);
    if (it != map_name_symbol.end()) {
        return it->second;
    } else {
        Symbol id = ++next_symbol_id;
        map_symbol(id, name);
        return id;
    }
}

static std::string get_symbol_name(Symbol id) {
    return map_symbol_name[id];
}

static void initSymbols() {
#define T(sym, name) map_symbol(sym, name);
    B_MAP_SYMBOLS()
#undef T
}

//------------------------------------------------------------------------------
// ANCHORED POINTERS
//------------------------------------------------------------------------------

typedef std::unordered_map<const void *, const Anchor *> PtrAnchorMap;

static PtrAnchorMap anchor_map;

static const Anchor *get_anchor(const void *ptr) {
    auto it = anchor_map.find(ptr);
    if (it != anchor_map.end()) {
        return it->second;
    }
    return nullptr;
}

static const Anchor *get_anchor(const Any &value) {
    return get_anchor(value.ptr);
}

static void set_anchor(const void *ptr, const Anchor *anchor) {
    if (!ptr) return;
    if (anchor) {
        anchor_map[ptr] = anchor;
    } else {
        auto it = anchor_map.find(ptr);
        if (it != anchor_map.end()) {
            anchor_map.erase(it);
        }
    }
}

static void set_anchor(const Any &value, const Anchor *anchor) {
    set_anchor(value.ptr, anchor);
}

//------------------------------------------------------------------------------
// NAMED POINTERS
//------------------------------------------------------------------------------

typedef std::unordered_map<const void *, Symbol> PtrSymbolMap;

static PtrSymbolMap ptr_symbol_map;

static Symbol get_ptr_symbol(const void *ptr) {
    auto it = ptr_symbol_map.find(ptr);
    if (it != ptr_symbol_map.end()) {
        return it->second;
    }
    return SYM_Unnamed;
}

static void set_ptr_symbol(const void *ptr, const Symbol &sym) {
    if (sym) {
        ptr_symbol_map[ptr] = sym;
    } else {
        auto it = ptr_symbol_map.find(ptr);
        if (it != ptr_symbol_map.end()) {
            ptr_symbol_map.erase(it);
        }
    }
}

static void set_ptr_symbol(const void *ptr, const std::string &name) {
    set_ptr_symbol(ptr, get_symbol(name));
}

//------------------------------------------------------------------------------
// TYPE ATTRIBUTES
//------------------------------------------------------------------------------

static std::unordered_map<TypeAttribute, Any> map_type_attributes;

static Any get_type_attrib(Type type, Symbol sym, const Any &defvalue) {
    auto it = map_type_attributes.find(type.attrib(sym));
    if (it != map_type_attributes.end())
        return it->second;
    else
        return defvalue;
}

static std::string get_name(Type self) {
    Any value = get_type_attrib(self, SYM_Name, const_none);
    if (value.type != TYPE_String) {
        return format("%s(%zu)",
            get_builtin_name(self), self);
    }
    return std::string(value.str->ptr, value.str->count);
}

static Type new_type() {
    return Type::create();
}

static void set_type_attrib(Type type, Symbol sym, const Any &value) {
    auto key = type.attrib(sym);
    if (value.type == TYPE_Void) {
        auto it = map_type_attributes.find(key);
        if (it != map_type_attributes.end()) {
            map_type_attributes.erase(it);
        }
    } else {
        if (value.type == TYPE_BuiltinFlow) {
            if (get_ptr_symbol(value.ptr) == SYM_Unnamed) {
                set_ptr_symbol(value.ptr,
                    get_symbol(format("<type>.%s",
                        get_symbol_name(sym).c_str())));
            }
        }
        map_type_attributes[key] = value;
    }
}

static Type _new_array_type(Type element, size_t size);
static Type _new_vector_type(Type element, size_t size);
static Type _new_tuple_type(std::vector<Type> types);
static Type _new_cfunction_type(
    Type result, Type parameters, bool vararg);
static Type _new_pointer_type(Type element);
static Type _new_integer_type(size_t width, bool signed_);
static Type _new_real_type(size_t width);
static Type _new_qualifier_type(Type tag, Type element);

namespace Types {
    static auto Array = memo(_new_array_type);
    static auto Vector = memo(_new_vector_type);
    static auto Tuple = memo(_new_tuple_type);
    static auto CFunction = memo(_new_cfunction_type);
    static auto Pointer = memo(_new_pointer_type);
    static auto Integer = memo(_new_integer_type);
    static auto Real = memo(_new_real_type);
    static auto Qualifier = memo(_new_qualifier_type);

    static Type Quote(Type element);
    static Type Splice(Type element);
    static Type Macro(Type element);
    static Type Struct(const std::string &name, bool builtin = false);
    static Type Tag(const Symbol &name);
} // namespace Types

//------------------------------------------------------------------------------

static const Anchor *find_valid_anchor(const Any &expr);

static void throw_any(const Any &any) {
    throw any;
}

static void error (const char *format, ...);

//------------------------------------------------------------------------------

static uint64_t hash(const Any &value);
struct AnyHash {
    std::size_t operator()(const Any & s) const {
        return hash(s);
    }
};

static bool eq(const Any &a, const Any &b);
struct AnyEqualTo {
    bool operator()(const Any &a, const Any &b) const {
        try {
            return eq(a, b);
        } catch (Any &v) {
            return false;
        }
    }
};

struct Table {
    typedef std::unordered_map<Any, Any, AnyHash, AnyEqualTo> map_type;

    map_type _;
    Table *meta;
};

//------------------------------------------------------------------------------

struct ExecuteOpts {
    bool trace;
    bool dump_error;

    ExecuteOpts() :
        trace(false),
        dump_error(true)
    {}
};

static Any execute(const Any *args, size_t argcount);
static Any execute(const Any *args, size_t argcount, const ExecuteOpts &opts);

enum {
    ARG_Cont = 0,
    ARG_Func = 1,
    ARG_Arg0 = 2,

    PARAM_Cont = 0,
    PARAM_Arg0 = 1,
};


enum Ordering {
    Less = -1,
    Equal = 0,
    Greater = 1,
    Unordered = 0x7f,
};

static void verify_exact_type(Type have, Type want) {
    if (have != want) {
        error("type mismatch: %s expected, %s provided",
            get_name(want).c_str(), get_name(have).c_str());
    }
}

template<KnownType atype>
static Any verify_type_attrib(Type type, Symbol sym) {
    Any result = get_type_attrib(type, sym, const_none);
    if (result.type != atype) {
        if (isnone(result)) {
            error("type %s is missing '%s' attribute",
                get_name(type).c_str(), get_symbol_name(sym).c_str());
        }
        error("type %s attribute '%s' should be of type %s, but is of type %s",
            get_name(type).c_str(), get_symbol_name(sym).c_str(),
            get_name(atype).c_str(), get_name(result.type).c_str());
    }
    return result;
}

template<KnownType atype>
static Any try_type_attrib(Type type, Symbol sym) {
    Any result = get_type_attrib(type, sym, const_none);
    if (!isnone(result) && (result.type != atype)) {
        error("type %s attribute '%s' should be of type %s, but is of type %s",
            get_name(type).c_str(), get_symbol_name(sym).c_str(),
            get_name(atype).c_str(), get_name(result.type).c_str());
    }
    return result;
}

static Type get_super(Type self) {
    return verify_type_attrib<TYPE_Type>(self, SYM_SuperType).typeref;
}

static Any try_super(Type self) {
    return try_type_attrib<TYPE_Type>(self, SYM_SuperType);
}

static bool is_callable(const Any &x) {
    if (x.type.is_builtin()) {
        switch (x.type.builtin_value()) {
            case TYPE_BuiltinFlow:
            case TYPE_Closure:
            case TYPE_Flow:
            case TYPE_Type:
                return true;
            default: return false;
        }
    } else {
        auto stype = try_super(x.type);
        if (isnone(stype)) return false;
        return stype.typeref == TYPE_CFunction;
    }
}

static Any verify_callable_type_attrib(Type type, Symbol sym) {
    Any result = get_type_attrib(type, sym, const_none);
    if (isnone(result)) {
        error("type %s is missing '%s' attribute",
            get_name(type).c_str(), get_symbol_name(sym).c_str());
    }
    if (!is_callable(result)) {
        error("type %s attribute '%s' has type %s, which is not a callable",
            get_name(type).c_str(), get_symbol_name(sym).c_str(),
            get_name(result.type).c_str());
    }
    return result;
}

static size_t get_size(Type self) {
    return verify_type_attrib<TYPE_SizeT>(self, SYM_Size).size;
}

static size_t get_alignment(Type self) {
    return verify_type_attrib<TYPE_SizeT>(self, SYM_Alignment).size;
}

static size_t get_width(Type self) {
    return verify_type_attrib<TYPE_SizeT>(self, SYM_Width).size;
}

static bool is_signed(Type self) {
    return verify_type_attrib<TYPE_Bool>(self, SYM_IsSigned).i1;
}

static bool is_vararg(Type self) {
    return verify_type_attrib<TYPE_Bool>(self, SYM_IsVarArg).i1;
}

static bool is_embedded(Type self) {
    return verify_type_attrib<TYPE_Bool>(self, SYM_IsEmbedded).i1;
}

static Type get_element_type(Type self) {
    return verify_type_attrib<TYPE_Type>(self, SYM_ElementType).typeref;
}

static size_t get_element_count(Type self) {
    return verify_type_attrib<TYPE_SizeT>(self, SYM_ElementCount).size;
}

static size_t get_field_count(Type self) {
    Any value = get_type_attrib(self, SYM_FieldTypes, const_none);
    if (isnone(value)) return 0;
    if (get_super(value.type) != TYPE_Array)
        error("type %s attribute '%s' is not an array",
            get_name(self).c_str(), get_symbol_name(SYM_FieldTypes).c_str());
    return get_element_count(value.type);
}

static Type get_type(Type self, size_t i) {
    auto atype = Types::Array(TYPE_Type, get_field_count(self));
    Any value = get_type_attrib(self, SYM_FieldTypes, const_none);
    verify_exact_type(value.type, atype);
    return ((Type *)value.ptr)[i];
}

static size_t get_field_offset(Type self, size_t i) {
    auto atype = Types::Array(TYPE_SizeT, get_field_count(self));
    Any value = get_type_attrib(self, SYM_FieldOffsets, const_none);
    verify_exact_type(value.type, atype);
    return ((size_t *)value.ptr)[i];
}

static Symbol get_field_name(Type self, size_t i) {
    auto atype = Types::Array(TYPE_Symbol, get_field_count(self));
    Any value = get_type_attrib(self, SYM_FieldNames, const_none);
    verify_exact_type(value.type, atype);
    return ((Symbol *)value.ptr)[i];
}

static Type get_return_type(Type self) {
    return verify_type_attrib<TYPE_Type>(self, SYM_ReturnType).typeref;
}

static Type get_parameter_types(Type self) {
    return verify_type_attrib<TYPE_Type>(self, SYM_ParameterTypes).typeref;
}

template<Symbol OP>
static Any op1(const Any &x) {
    Any args[] = { get_type_attrib(x.type, OP, const_none), x };
    return execute(args, 2);
}

template<Symbol OP, Symbol ROP>
static Any op2(const Any &a, const Any &b) {
    try {
        Any args[] = { get_type_attrib(a.type, OP, const_none), a, b };
        return execute(args, 3);
    } catch (const Any &any1) {
        try {
            Any args[] = { get_type_attrib(b.type, ROP, const_none), b, a };
            return execute(args, 3);
        } catch (const Any &any2) {
            // raise original error
            throw any1;
        }
    }
}

static Ordering extract_ordering(const Any &o) {
    verify_exact_type(o.type, { TYPE_I32 });
    return (Ordering)o.i32;
}

static Any wrap (Ordering o) {
    Any out = make_any(TYPE_I32);
    out.i32 = (int32_t)o;
    return out;
}

static Ordering compare(const Any &a, const Any &b) {
    ExecuteOpts opts;
    opts.dump_error = false;
    try {
        Any args[] = { verify_callable_type_attrib(a.type, SYM_Cmp), a, b };
        return extract_ordering(execute(args, 3, opts));
    } catch (const Any &any1) {
        Any args[] = { verify_callable_type_attrib(b.type, SYM_Cmp), b, a };
        auto result = extract_ordering(execute(args, 3, opts));
        switch(result) {
            case Less: return Greater;
            case Greater: return Less;
            default: return result;
        }
    }
}

static bool eq(const Any &a, const Any &b) {
    return compare(a, b) == Equal;
}

static bool ne(const Any &a, const Any &b) {
    //try {
        return compare(a, b) != Equal;
    //} catch (const Any &any) {
    //    return true;
    //}
}

static bool gt(const Any &a, const Any &b) {
    //try {
        return compare(a, b) == Greater;
    //} catch (const Any &any) {
    //    return false;
    //}
}

static bool ge(const Any &a, const Any &b) {
    //try {
        auto result = compare(a, b);
        return (result == Equal) || (result == Greater);
    //} catch (const Any &any) {
    //    return false;
    //}
}

static bool lt(const Any &a, const Any &b) {
    //try {
        return compare(a, b) == Less;
    //} catch (const Any &any) {
    //    return false;
    //}
}

static bool le(const Any &a, const Any &b) {
    //try {
        auto result = compare(a, b);
        return (result == Equal) || (result == Less);
    //} catch (const Any &any) {
    //    return false;
    //}
}

template <typename T>
static T **alloc_ptr(T *value) {
    T **ptr = (T **)malloc(sizeof(T *));
    *ptr = value;
    return ptr;
}

// undefined catch-all that produces diagnostic messages when it is selected,
// which should never happen.
template<typename T>
static Any wrap(const T &arg);

static Any wrap_ptr(Type type, const void *ptr) {
    Any any = make_any(type);
    if (is_embedded(type)) {
        any.ptr = ptr;
    } else {
        any.ptr = alloc_ptr(ptr);
    }
    return any;
}

static Any wrap(size_t value) {
    Any any = make_any(TYPE_SizeT);
    any.size = value;
    return any;
}

static Any wrap(Type type) {
    Any any = make_any(TYPE_Type);
    any.typeref = type;
    return any;
}

static Any wrap(KnownType type) {
    return wrap(Type(type));
}

static bool is_subtype(Type subtype, Type supertype) {
    auto super = try_super(subtype);
    if (isnone(super))
        return false;
    else
        return super.typeref == supertype;
}

static bool is_subtype_or_type(Type subtype, Type supertype) {
    if (subtype == supertype) return true;
    return is_subtype(subtype, supertype);
}

static Any add(const Any &a, const Any &b) {
    return op2<SYM_OP2_Add, SYM_OP2_RAdd>(a, b);
}

static Any sub(const Any &a, const Any &b) {
    return op2<SYM_OP2_Sub, SYM_OP2_RSub>(a, b);
}

static Any mul(const Any &a, const Any &b) {
    return op2<SYM_OP2_Mul, SYM_OP2_RMul>(a, b);
}

static Any div(const Any &a, const Any &b) {
    return op2<SYM_OP2_Div, SYM_OP2_RDiv>(a, b);
}

static Any floordiv(const Any &a, const Any &b) {
    return op2<SYM_OP2_FloorDiv, SYM_OP2_RFloorDiv>(a, b);
}

static Any mod(const Any &a, const Any &b) {
    return op2<SYM_OP2_Mod, SYM_OP2_RMod>(a, b);
}

static Any pow(const Any &a, const Any &b) {
    return op2<SYM_OP2_Pow, SYM_OP2_RPow>(a, b);
}

static Any bit_and(const Any &a, const Any &b) {
    return op2<SYM_OP2_And, SYM_OP2_RAnd>(a, b);
}

static Any bit_or(const Any &a, const Any &b) {
    return op2<SYM_OP2_Or, SYM_OP2_ROr>(a, b);
}

static Any bit_xor(const Any &a, const Any &b) {
    return op2<SYM_OP2_Xor, SYM_OP2_RXor>(a, b);
}

static Any join(const Any &a, const Any &b) {
    return op2<SYM_OP2_Join, SYM_OP2_RJoin>(a, b);
}

static Any at(const Any &value, const Any &index) {
    return op2<SYM_OP2_At, SYM_OP2_RAt>(value, index);
}

static Any lshift(const Any &a, const Any &b) {
    return op2<SYM_OP2_LShift, SYM_OP2_RLShift>(a, b);
}

static Any rshift(const Any &a, const Any &b) {
    return op2<SYM_OP2_RShift, SYM_OP2_RRShift>(a, b);
}

static Any neg(const Any &value) {
    return op1<SYM_OP1_Neg>(value);
}

static Any bit_not(const Any &value) {
    return op1<SYM_OP1_Not>(value);
}

static Any bool_not(const Any &value) {
    return op1<SYM_OP1_BoolNot>(value);
}

static Any rcp(const Any &value) {
    return op1<SYM_OP1_Rcp>(value);
}

static uint64_t hash(const Any &x) {
    Any args[] = { verify_callable_type_attrib(x.type, SYM_Hash), x };
    Any result = execute(args, 2);
    verify_exact_type(result.type, TYPE_U64);
    return result.u64;
}

static std::string get_string(const Any &x) {
    Any result;
    if (x.type != TYPE_String) {
        Any func = get_type_attrib(x.type, SYM_ToString, const_none);
        if (is_callable(func)) {
            Any args[] = { func, x };
            ExecuteOpts opts;
            opts.trace = false;
            opts.dump_error = false;
            result = execute(args, 2, opts);
            verify_exact_type(result.type, TYPE_String);
        } else {
            return format("<value of %s>", get_name(x.type).c_str());
        }
    } else {
        result = x;
    }
    return std::string(result.str->ptr, result.str->count);
}

static Any slice(const Any &x, size_t i0, size_t i1) {
    Any args[] = { verify_callable_type_attrib(x.type, SYM_Slice), x, wrap(i0), wrap(i1) };
    return execute(args, 4);
}

/*
static size_t splice(const Any &value, Any *ret, size_t retsize) {
    return value.type->splice(value.type, value, ret, retsize);
}*/

static size_t countof(const Any &x) {
    Any args[] = { verify_callable_type_attrib(x.type, SYM_CountOf), x };
    Any result = execute(args, 2);
    verify_exact_type(result.type, TYPE_SizeT);
    return result.size;
}

//------------------------------------------------------------------------------

static bool is_splice_type(Type type) {
    return is_subtype(type, TYPE_Splice);
}

static bool is_quote_type(Type type) {
    return is_subtype(type, TYPE_Quote);
}

static bool is_macro_type(Type type) {
    return is_subtype(type, TYPE_Macro);
}

static bool is_typeref_type(Type type) {
    return type == TYPE_Type;
}

static bool is_none_type(Type type) {
    return is_subtype_or_type(type, TYPE_Void);
}

static bool is_bool_type(Type type) {
    return is_subtype_or_type(type, TYPE_Bool);
}

static bool is_integer_type(Type type) {
    return is_subtype(type, TYPE_Integer);
}

static bool is_real_type(Type type) {
    return is_subtype(type, TYPE_Real);
}

/*
static bool is_struct_type(Type *type) {
    return is_subtype(type, Types::TStruct);
}
*/

static bool is_tuple_type(Type type) {
    return is_subtype(type, TYPE_Tuple);
}

static bool is_symbol_type(Type type) {
    return is_subtype_or_type(type, TYPE_Symbol);
}

/*
static bool is_array_type(Type *type) {
    return is_subtype(type, Types::TArray);
}
*/

static bool is_cfunction_type(Type type) {
    return is_subtype(type, TYPE_CFunction);
}

static bool is_parameter_type(Type type) {
    return is_subtype_or_type(type, TYPE_Parameter);
}

static bool is_flow_type(Type type) {
    return is_subtype_or_type(type, TYPE_Flow);
}

static bool is_builtin_flow_type(Type type) {
    return is_subtype_or_type(type, TYPE_BuiltinFlow);
}

static bool is_frame_type(Type type) {
    return is_subtype_or_type(type, TYPE_Frame);
}

static bool is_closure_type(Type type) {
    return is_subtype_or_type(type, TYPE_Closure);
}

static bool is_list_type(Type type) {
    return is_subtype_or_type(type, TYPE_List);
}

static bool is_special_form_type(Type type) {
    return is_subtype_or_type(type, TYPE_SpecialForm);
}

static bool is_pointer_type(Type type) {
    return is_subtype(type, TYPE_Pointer);
}

static bool is_table_type(Type type) {
    return is_subtype_or_type(type, TYPE_Table);
}

static std::string try_extract_string(const Any &value, const std::string &defvalue) {
    if (value.type == TYPE_String) {
        return std::string(value.str->ptr, value.str->count);
    } else if (value.type == TYPE_Rawstring) {
        return value.c_str;
    } else {
        return defvalue;
    }
}

static std::string extract_string(const Any &value) {
    if (is_subtype_or_type(value.type, TYPE_Rawstring)) {
        return value.c_str;
    } else if (is_subtype_or_type(value.type, TYPE_String)) {
        return std::string(value.str->ptr, value.str->count);
    } else {
        error("string expected");
        return "";
    }
}

static const char *extract_cstr(const Any &value) {
    if (is_subtype_or_type(value.type, TYPE_Rawstring)) {
        return value.c_str;
    } else if (is_subtype_or_type(value.type, TYPE_String)) {
        return value.str->ptr;
    } else {
        error("string expected");
        return "";
    }
}

static Symbol extract_symbol(const Any &value) {
    if (is_symbol_type(value.type)) {
        return value.symbol;
    } else {
        error("symbol expected");
        return 0;
    }
}

static std::string extract_symbol_string(const Any &value) {
    return get_symbol_name(extract_symbol(value));
}

static bool extract_bool(const Any &value) {
    if (is_bool_type(value.type)) {
        return value.i1;
    }
    error("boolean expected");
    return false;
}

static Type extract_type(const Any &value) {
    if (is_typeref_type(value.type)) {
        return value.typeref;
    }
    error("type constant expected");
    return TYPE_Void;
}

template<typename T>
static Slice<T> *alloc_slice(const T *ptr, size_t count) {
    Slice<T> *s = (Slice<T> *)malloc(sizeof(Slice<T>));
    s->ptr = ptr;
    s->count = count;
    return s;
}

static String *alloc_string(const char *ptr, size_t count) {
    char *ptrcpy = (char *)malloc(sizeof(char) * count + 1);
    memcpy(ptrcpy, ptr, sizeof(char) * count);
    ptrcpy[count] = 0;
    return alloc_slice(ptrcpy, count);
}

static Any wrap(Type type, const void *ptr) {
    Any any = make_any(type);
    if (is_embedded(type)) {
        memcpy(any.embedded, ptr, get_size(type));
    } else {
        any.ptr = ptr;
    }
    return any;
}

static Any pstring(const std::string &s) {
    Any any = make_any(TYPE_String);
    any.ptr = alloc_string(s.c_str(), s.size());
    return any;
}

static Any wrap_symbol(const Symbol &s) {
    return wrap(TYPE_Symbol, &s);
}

static Any symbol(const std::string &s) {
    return wrap_symbol(get_symbol(s));
}

static Any integer(Type type, int64_t value) {
    Any any = make_any(type);
    switch(type.builtin_value()) {
    case TYPE_Bool: any.i1 = (bool)value; break;
    case TYPE_I8: any.i8 = (int8_t)value; break;
    case TYPE_I16: any.i16 = (int16_t)value; break;
    case TYPE_I32: any.i32 = (int32_t)value; break;
    case TYPE_I64: any.i64 = value; break;
    case TYPE_U8: any.u8 = (uint8_t)((uint64_t)value); break;
    case TYPE_U16: any.u16 = (uint16_t)((uint64_t)value); break;
    case TYPE_U32: any.u32 = (uint32_t)((uint64_t)value); break;
    case TYPE_U64: any.u64 = ((uint64_t)value); break;
    default:
        error("not an integer type"); break;
    }
    return any;
}

static int64_t extract_integer(const Any &value) {
    auto type = value.type;
    switch(type.builtin_value()) {
    case TYPE_Bool: return (int64_t)value.i1;
    case TYPE_I8: return (int64_t)value.i8;
    case TYPE_I16: return (int64_t)value.i16;
    case TYPE_I32: return (int64_t)value.i32;
    case TYPE_I64: return value.i64;
    case TYPE_U8: return (int64_t)value.u8;
    case TYPE_U16: return (int64_t)value.u16;
    case TYPE_U32: return (int64_t)value.u32;
    case TYPE_U64: return (int64_t)value.u64;
    default:
        error("integer expected, not %s", get_name(type).c_str()); break;
    }
    return 0;
}

static Any real(Type type, double value) {
    Any any = make_any(type);
    switch(type.builtin_value()) {
    case TYPE_R32: any.r32 = (float)value; break;
    case TYPE_R64: any.r64 = value; break;
    default:
        error("not a real type"); break;
    }
    return any;
}

static double extract_real(const Any &value) {
    auto type = value.type;
    switch(type.builtin_value()) {
    case TYPE_R32: return (double)value.r32;
    case TYPE_R64: return value.r64;
    default:
        error("real expected, not %s", get_name(type).c_str()); break;
    }
    return 0;
}

template<typename T>
static T *extract_ptr(const bangra::Any &value) {
    assert(is_embedded(value.type));
    return (T *)value.ptr;
}

static Any wrap(bool value) {
    return integer(TYPE_Bool, value);
}

static Any wrap(int32_t value) {
    return integer(TYPE_I32, value);
}

static Any wrap(int64_t value) {
    return integer(TYPE_I64, value);
}

static std::vector<Any> extract_tuple(const Any &value) {
    if (is_tuple_type(value.type)) {
        std::vector<Any> result;
        size_t count = get_field_count(value.type);
        for (size_t i = 0; i < count; ++i) {
            result.push_back(at(value, wrap(i)));
        }
        return result;
    }
    error("can not extract tuple");
    return {};
}

static bangra::Any pointer_element(const bangra::Any &value) {
    return wrap(get_element_type(value.type), value.ptr);
}

static const Table *extract_table(const Any &value) {
    if (is_table_type(value.type)) {
        return value.table;
    }
    error("table expected, not %s", get_name(value.type).c_str());
    return nullptr;
}

//------------------------------------------------------------------------------

struct Parameter {
    // changes need to be mirrored to Types::PParameter

    Flow *parent;
    size_t index;
    Type parameter_type;
    Symbol name;
    bool vararg;

    Parameter() :
        parent(nullptr),
        index(-1),
        parameter_type(TYPE_Any),
        name(SYM_Unnamed),
        vararg(false) {
    }

    Flow *getParent() const {
        return parent;
    }

    std::string getReprName() const {
        if (name == SYM_Unnamed) {
            return format("%s%zu",
                ansi(ANSI_STYLE_OPERATOR,"@").c_str(),
                index);
        } else {
            return get_symbol_name(name);
        }
    }

    /*
    std::string getRepr() const {
        return format("%s %s %s",
            getReprName().c_str(),
            ansi(ANSI_STYLE_OPERATOR,":").c_str(),
            bangra::getRepr(getType(this)).c_str());
    }
    */

    std::string getRefRepr () const;

    static Parameter *create(const Symbol &name = SYM_Unnamed) {
        auto value = new Parameter();
        value->index = (size_t)-1;
        value->name = name;
        value->parameter_type = TYPE_Any;
        if (endswith(get_symbol_name(name), "..."))
            value->vararg = true;
        return value;
    }
};

static const Parameter *extract_parameter(const Any &value) {
    if (is_parameter_type(value.type)) {
        return value.parameter;
    }
    error("parameter type expected, not %s.",
        get_name(value.type).c_str());
    return nullptr;
}

//------------------------------------------------------------------------------

static Any wrap(const List *slist) {
    return wrap_ptr(TYPE_List, slist);
}
static Any wrap(List *slist) {
    return wrap_ptr(TYPE_List, slist);
}

struct List {
    Any at;
    const List *next;
    size_t count;

    static List *create(const Any &at, const List *next) {
        auto result = new List();
        result->at = at;
        result->next = next;
        result->count = next?(next->count + 1):1;
        //set_anchor(result, get_anchor(at));
        return result;
    }

    static List *create(const Any &at, const List *next, const Anchor *anchor) {
        auto result = create(at, next);
        set_anchor(result, anchor);
        return result;
    }

    static const List *create_from_c_array(const Any *values, size_t count) {
        List *result = nullptr;
        while (count) {
            --count;
            result = create(values[count], result);
        }
        return result;
    }

    static const List *create_from_array(const std::vector<Any> &values) {
        return create_from_c_array(const_cast<Any *>(&values[0]), values.size());
    }
};

static const List *extract_list(const Any &value) {
    if (is_list_type(value.type)) {
        return value.list;
    } else {
        error("list expected, not %s", get_name(value.type).c_str());
    }
    return nullptr;
}

#if 0
// (a . (b . (c . (d . NIL)))) -> (d . (c . (b . (a . NIL))))
// this is the version for immutables; input lists are not modified
static const List *reverse_list(const List *l, const List *eol = nullptr) {
    const List *next = nullptr;
    while (l != eol) {
        next = List::create(l->at, next, get_anchor(l));
        l = l->next;
    }
    return next;
}
#endif

// (a . (b . (c . (d . NIL)))) -> (d . (c . (b . (a . NIL))))
// this is the mutating version; input lists are modified, direction is inverted
static const List *reverse_list_inplace(
    const List *l, const List *eol = nullptr, const List *cat_to = nullptr) {
    const List *next = cat_to;
    size_t count = cat_to?cat_to->count:0;
    while (l != eol) {
        ++count;
        const List *iternext = l->next;
        const_cast<List *>(l)->next = next;
        const_cast<List *>(l)->count = count;
        next = l;
        l = iternext;
    }
    return next;
}

//------------------------------------------------------------------------------

struct Flow {
private:
    static int64_t unique_id_counter;
protected:
    int64_t uid;

public:
    Flow() :
        uid(unique_id_counter++) {
    }

    Symbol name;
    std::vector<Parameter *> parameters;

    // default path
    std::vector<Any> arguments;

    size_t getParameterCount() {
        return parameters.size();
    }

    Parameter *getParameter(size_t i) {
        return parameters[i];
    }

    bool hasArguments() const {
        return !arguments.empty();
    }

    int64_t getUID() const {
        return uid;
    }

    std::string getRefRepr () const {
        return format("%s%s%" PRId64,
            ansi(ANSI_STYLE_KEYWORD, "Î»").c_str(),
            get_symbol_name(name).c_str(),
            uid);
    }

    Parameter *appendParameter(Parameter *param) {
        param->parent = this;
        param->index = parameters.size();
        parameters.push_back(param);
        return param;
    }

    Parameter *appendParameter(const Symbol &name = SYM_Unnamed) {
        return appendParameter(Parameter::create(name));
    }

    // an empty function
    // you have to add the continuation argument manually
    static Flow *create_empty_function(const Symbol &name) {
        auto value = new Flow();
        value->name = name;
        return value;
    }

    // a function that eventually returns
    static Flow *create_function(const Symbol &name) {
        auto value = new Flow();
        value->name = name;
        // continuation is always first argument
        // this argument will be called when the function is done
        value->appendParameter(get_symbol("return-" + get_symbol_name(name)));
        return value;
    }

    // a continuation that never returns
    static Flow *create_continuation(const Symbol &name) {
        auto value = new Flow();
        value->name = name;
        // first argument is present, but unused
        value->appendParameter(SYM_Unnamed);
        return value;
    }
};

int64_t Flow::unique_id_counter = 1;

static const Flow *extract_flow(const Any &value) {
    if (is_flow_type(value.type)) {
        return value.flow;
    }
    error("flow expected");
    return nullptr;
}

//------------------------------------------------------------------------------

// flow -> {frame-idx, {values}}
typedef std::unordered_map<const Flow *,
    std::pair< size_t, std::vector<Any> > >
    FlowValuesMap;

struct Frame {
protected:
    size_t idx;
    Frame *parent;
    Frame *owner;
    FlowValuesMap *_map;

    FlowValuesMap &map() {
        return *_map;
    }

    const FlowValuesMap &map() const {
        return *_map;
    }

public:

    Frame *bind(const Flow *flow, const std::vector<Any> &values) {
        Frame *frame = Frame::create(this);
        if (frame->map().count(flow)) {
            frame->_map = new FlowValuesMap();
            frame->owner = frame;
        }
        frame->map()[flow] = { frame->idx, values };
        return frame;
    }

    void rebind(const Flow *cont, size_t index, const Any &value) {
        assert(cont);
        // parameter is bound - attempt resolve
        Frame *ptr = this;
        while (ptr) {
            ptr = ptr->owner;
            auto it = ptr->map().find(cont);
            if (it != ptr->map().end()) {
                auto &entry = it->second;
                auto &values = entry.second;
                assert (index < values.size());
                values[index] = value;
                return;
            }
            ptr = ptr->parent;
        }
    }

    Any get(const Flow *cont, size_t index, const Any &defvalue) const {
        if (cont) {
            // parameter is bound - attempt resolve
            const Frame *ptr = this;
            while (ptr) {
                ptr = ptr->owner;
                auto it = ptr->map().find(cont);
                if (it != ptr->map().end()) {
                    auto &entry = it->second;
                    auto &values = entry.second;
                    assert (index < values.size());
                    if (this->idx >= entry.first) {
                        return values[index];
                    }
                }
                ptr = ptr->parent;
            }
        }
        return defvalue;
    }

    static void print_stack(const Frame *frame) {
        if (!frame) return;
        print_stack(frame->parent);
        auto anchor = get_anchor(frame);
        if (anchor) {
            Anchor::printMessage(anchor, "while evaluating");
        }
    }

    static Frame *create() {
        // create closure
        Frame *newframe = new Frame();
        newframe->parent = nullptr;
        newframe->owner = newframe;
        newframe->_map = new FlowValuesMap();
        newframe->idx = 0;
        return newframe;
    }

    static Frame *create(Frame *frame) {
        // create closure
        Frame *newframe = new Frame();
        newframe->parent = frame;
        newframe->owner = frame->owner;
        newframe->idx = frame->idx + 1;
        newframe->_map = frame->_map;
        return newframe;
    }
};

static const Frame *extract_frame(const Any &value) {
    if (is_frame_type(value.type)) {
        return value.frame;
    }
    error("frame expected");
    return nullptr;
}

static Any wrap(const Frame *frame) {
    return wrap_ptr(TYPE_Frame, frame);
}

static Any wrap(Frame *frame) {
    return wrap_ptr(TYPE_Frame, frame);
}

//------------------------------------------------------------------------------

struct Closure {
    // changes here must be mirrored to Types::PClosure
    const Flow *entry;
    Frame *frame;
};

static const Closure *create_closure(
    const Flow *entry,
    Frame *frame) {
    auto result = new Closure();
    result->entry = entry;
    result->frame = frame;
    return result;
}

static const Closure *extract_closure(const Any &value) {
    if (is_closure_type(value.type)) {
        return value.closure;
    }
    error("closure expected");
    return nullptr;
}

static Any wrap(const Closure *closure) {
    return wrap_ptr(TYPE_Closure, closure);
}

static Any wrap(Closure *closure) {
    return wrap_ptr(TYPE_Closure, closure);
}

//------------------------------------------------------------------------------

static Any macro(const Any &value) {
    Any result = value;
    result.type = Types::Macro(value.type);
    return result;
}

static Any unmacro(const Any &value) {
    assert(is_macro_type(value.type));
    Any result = value;
    result.type = get_element_type(value.type);
    return result;
}

static Any wrap(const Flow *flow) {
    return wrap_ptr(TYPE_Flow, flow);
}
static Any wrap(Flow *flow) {
    return wrap_ptr(TYPE_Flow, flow);
}

static Any wrap(const Parameter *param) {
    return wrap_ptr(TYPE_Parameter, param);
}
static Any wrap(Parameter *param) {
    return wrap_ptr(TYPE_Parameter, param);
}

static Any wrap(const Table *table) {
    return wrap_ptr(TYPE_Table, table);
}
static Any wrap(Table *table) {
    return wrap_ptr(TYPE_Table, table);
}

static Any wrap(const Any *args, size_t argcount) {
    std::vector<Type> types;
    types.resize(argcount);
    for (size_t i = 0; i < argcount; ++i) {
        types[i] = args[i].type;
    }
    Type type = Types::Tuple(types);
    char *data = (char *)malloc(get_size(type));
    for (size_t i = 0; i < argcount; ++i) {
        auto elemtype = get_type(type, i);
        auto offset = get_field_offset(type, i);
        auto size = get_size(elemtype);
        if (get_alignment(elemtype)) {
            const void *srcptr =
                is_embedded(elemtype)?args[i].embedded:args[i].ptr;
            memcpy(data + offset, srcptr, size);
        } else {
            error("attempting to pack opaque type %s in tuple",
                get_name(args[i].type).c_str());
        }
    }

    return wrap(type, data);
}

static Any wrap_array(Type elemtype, const Any *args, size_t argcount) {
    size_t alignment = get_alignment(elemtype);
    if (!alignment) {
        error("attempting to pack opaque type %s in array",
            get_name(elemtype).c_str());
    }
    Type type = Types::Array(elemtype, argcount);
    char *data = (char *)malloc(get_size(type));
    size_t size = get_size(elemtype);
    size_t stride = align(size, alignment);
    size_t offset = 0;
    for (size_t i = 0; i < argcount; ++i) {
        if (!is_subtype_or_type(args[i].type, elemtype)) {
            error("attempting to pack incompatible type %s in array of type %s",
                get_name(args[i].type).c_str(),
                get_name(type).c_str());
        }
        const void *srcptr =
            is_embedded(elemtype)?args[i].embedded:args[i].ptr;
        memcpy(data + offset, srcptr, size);
        offset += stride;
    }
    return wrap(type, data);
}

static Any wrap(
    const std::vector<Any> &args) {
    return wrap(&args[0], args.size());
}

static Any wrap(double value) {
    return real(TYPE_R64, value);
}

static Any wrap(const std::string &s) {
    return pstring(s);
}

static bool builtin_checkparams (size_t count,
    int mincount, int maxcount, int skip = 0) {
    if ((mincount <= 0) && (maxcount == -1))
        return true;

    int argcount = (int)count - skip;

    if ((maxcount >= 0) && (argcount > maxcount)) {
        error("excess argument. At most %i arguments expected", maxcount);
        return false;
    }
    if ((mincount >= 0) && (argcount < mincount)) {
        error("at least %i arguments expected", mincount);
        return false;
    }
    return true;
}

static bool builtin_checkparams (const std::vector<Any> &args,
    int mincount, int maxcount, int skip = 0) {
    return builtin_checkparams(args.size(), mincount, maxcount, skip);
}

static void error (const char *format, ...) {
    //const Anchor *anchor = nullptr;
    // TODO: find valid anchor
    //std::cout << "at\n  " << getRepr(value) << "\n";
    //anchor = find_valid_anchor(value);
    va_list args;

    va_start (args, format);
    std::string str = vformat(format, args);
    va_end (args);

    throw_any(wrap(str));
}


//------------------------------------------------------------------------------
// IL MODEL UTILITY FUNCTIONS
//------------------------------------------------------------------------------

static void unescape(String &s) {
    s.count = inplace_unescape(const_cast<char *>(s.ptr));
}

// matches (///...)
static bool is_comment(const Any &expr) {
    if (is_list_type(expr.type)) {
        if (expr.list) {
            const Any &sym = expr.list->at;
            if (is_symbol_type(sym.type)) {
                auto s = extract_symbol_string(sym);
                if (!memcmp(s.c_str(),"///",3))
                    return true;
            }
        }
    }
    return false;
}

static const Anchor *find_valid_anchor(const List *l) {
    const Anchor *a = nullptr;
    while (l) {
        a = get_anchor(l);
        if (!a) {
            a = find_valid_anchor(l->at);
        }
        if (!a) {
            l = l->next;
        } else {
            break;
        }
    }
    return a;
}

static const Anchor *find_valid_anchor(const Any &expr) {
    const Anchor *a = get_anchor(expr);
    if (!a) {
        if (is_list_type(expr.type)) {
            a = find_valid_anchor(expr.list);
        }
    }
    return a;
}

static Any strip(const Any &expr) {
    if (is_list_type(expr.type)) {
        const List *l = nullptr;
        auto it = extract_list(expr);
        while (it) {
            auto value = strip(it->at);
            if (!is_comment(value)) {
                l = List::create(value, l, get_anchor(it));
            }
            it = it->next;
        }
        return wrap(reverse_list_inplace(l));
    }
    return expr;
}

static size_t getSize(const List *expr) {
    size_t c = 0;
    while (expr) {
        c++;
        expr = expr->next;
    }
    return c;
}

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
        auto it = extract_list(e);
        while (it) {
            if (it->at.type == TYPE_List)
                return true;
            it = it->next;
        }
    }
    return false;
}

template<typename T>
static void streamAnchor(T &stream, const Any &e, size_t depth=0) {
    const Anchor *anchor = find_valid_anchor(e);
    if (anchor) {
        stream <<
            format("%s:%i:%i: ",
                anchor->path,
                anchor->lineno,
                anchor->column);
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
            auto it = extract_list(e);
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
            if (support_ansi) stream << ANSI_STYLE_KEYWORD;
            streamString(stream, extract_symbol_string(e), "[]{}()\"");
            if (support_ansi) stream << ANSI_RESET;
        } else if (e.type == TYPE_String) {
            if (support_ansi) stream << ANSI_STYLE_STRING;
            stream << '"';
            streamString(stream, extract_string(e), "\"");
            stream << '"';
            if (support_ansi) stream << ANSI_RESET;
        } else {
            auto s = get_string(e);
            if (e.type == TYPE_Bool) {
                stream << ansi(ANSI_STYLE_KEYWORD, s);
            } else if (is_integer_type(e.type)) {
                stream << ansi(ANSI_STYLE_NUMBER, s);
            } else {
                stream << s;
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

void valueError (const Any &expr, const char *format, ...) {
    const Anchor *anchor = find_valid_anchor(expr);
    if (!anchor) {
        printValue(expr);
        std::cout << "\n";
    }
    va_list args;
    va_start (args, format);
    Anchor::printErrorV(anchor, format, args);
    va_end (args);
    throw_any(const_none);
}

void valueErrorV (const Any &expr, const char *fmt, va_list args) {
    const Anchor *anchor = find_valid_anchor(expr);
    if (!anchor) {
        printValue(expr);
        std::cout << "\n";
    }
    Anchor::printErrorV(anchor, fmt, args);
    throw_any(const_none);
}

static void verifyValueKind(Type type, const Any &expr) {
    if (!is_subtype_or_type(expr.type, type)) {
        valueError(expr, "%s expected, not %s",
            get_name(type).c_str(),
            get_name(expr.type).c_str());
    }
}

//------------------------------------------------------------------------------
// S-EXPR LEXER / TOKENIZER
//------------------------------------------------------------------------------

typedef enum {
    token_none = -1,
    token_eof = 0,
    token_open = '(',
    token_close = ')',
    token_square_open = '[',
    token_square_close = ']',
    token_curly_open = '{',
    token_curly_close = '}',
    token_string = '"',
    token_symbol = 'S',
    token_escape = '\\',
    token_statement = ';',
    token_integer = 'I',
    token_real = 'R',
} Token;

const char symbol_terminators[]  = "()[]{}\"';#";
const char integer_terminators[] = "()[]{}\"';#";
const char real_terminators[]    = "()[]{}\"';#";

struct Lexer {
    const char *path;
    const char *input_stream;
    const char *eof;
    const char *cursor;
    const char *next_cursor;
    // beginning of line
    const char *line;
    // next beginning of line
    const char *next_line;

    int lineno;
    int next_lineno;

    int base_offset;

    int token;
    const char *string;
    int string_len;
    int64_t integer;
    bool is_unsigned;
    float real;

    std::string error_string;

    Lexer() {}

    void init (const char *input_stream, const char *eof, const char *path, int offset = 0) {
        if (eof == NULL) {
            eof = input_stream + strlen(input_stream);
        }

        this->base_offset = offset;
        this->path = path;
        this->input_stream = input_stream;
        this->eof = eof;
        this->next_cursor = input_stream;
        this->next_lineno = 1;
        this->next_line = input_stream;
        this->error_string.clear();
    }

    void dumpLine() {
        dumpFileLine(path, offset());
    }

    int offset () {
        return base_offset + (cursor - input_stream);
    }

    int column () {
        return cursor - line + 1;
    }

    void initAnchor(Anchor &anchor) {
        anchor.path = path;
        anchor.lineno = lineno;
        anchor.column = column();
        anchor.offset = offset();
    }

    Anchor getAnchor() {
        Anchor anchor;
        initAnchor(anchor);
        return anchor;
    }

    const Anchor *newAnchor() {
        Anchor *anchor = new Anchor();
        initAnchor(*anchor);
        return anchor;
    }

    void error( const char *fmt, ... ) {
        va_list args;
        va_start (args, fmt);
        error_string = vformat(fmt, args);
        va_end (args);
        token = token_eof;
    }

    char next() {
        return *next_cursor++;
    }

    bool verifyGoodTaste(char c) {
        if (c == '\t') {
            error("please use spaces instead of tabs.");
            return false;
        }
        return true;
    }

    void readSymbol () {
        bool escape = false;
        while (true) {
            if (next_cursor == eof) {
                break;
            }
            char c = next();
            if (escape) {
                if (c == '\n') {
                    ++next_lineno;
                    next_line = next_cursor;
                }
                // ignore character
                escape = false;
            } else if (c == '\\') {
                // escape
                escape = true;
            } else if (isspace(c)
                || strchr(symbol_terminators, c)) {
                -- next_cursor;
                break;
            }
        }
        string = cursor;
        string_len = next_cursor - cursor;
    }

    void readSingleSymbol () {
        string = cursor;
        string_len = next_cursor - cursor;
    }

    void readString (char terminator) {
        bool escape = false;
        while (true) {
            if (next_cursor == eof) {
                error("unterminated sequence");
                break;
            }
            char c = next();
            if (c == '\n') {
                ++next_lineno;
                next_line = next_cursor;
            }
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
        string = cursor;
        string_len = next_cursor - cursor;
    }

    bool readInteger() {
        char *end;
        errno = 0;
        integer = std::strtoll(cursor, &end, 0);
        if ((end == cursor)
            || (errno == ERANGE)
            || (end >= eof)
            || (!isspace(*end) && !strchr(integer_terminators, *end)))
            return false;
        is_unsigned = false;
        next_cursor = end;
        return true;
    }

    bool readUInteger() {
        char *end;
        errno = 0;
        integer = std::strtoull(cursor, &end, 0);
        if ((end == cursor)
            || (errno == ERANGE)
            || (end >= eof)
            || (!isspace(*end) && !strchr(integer_terminators, *end)))
            return false;
        is_unsigned = true;
        next_cursor = end;
        return true;
    }

    bool readReal() {
        char *end;
        errno = 0;
        real = std::strtof(cursor, &end);
        if ((end == cursor)
            || (errno == ERANGE)
            || (end >= eof)
            || (!isspace(*end) && !strchr(real_terminators, *end)))
            return false;
        next_cursor = end;
        return true;
    }

    int readToken () {
        lineno = next_lineno;
        line = next_line;
        cursor = next_cursor;
        while (true) {
            if (next_cursor == eof) {
                token = token_eof;
                break;
            }
            char c = next();
            if (!verifyGoodTaste(c)) break;
            if (c == '\n') {
                ++next_lineno;
                next_line = next_cursor;
            }
            if (isspace(c)) {
                lineno = next_lineno;
                line = next_line;
                cursor = next_cursor;
            } else if (c == '#') {
                readString('\n');
                // and continue
                lineno = next_lineno;
                line = next_line;
                cursor = next_cursor;
            } else if (c == '(') {
                token = token_open;
                break;
            } else if (c == ')') {
                token = token_close;
                break;
            } else if (c == '[') {
                token = token_square_open;
                break;
            } else if (c == ']') {
                token = token_square_close;
                break;
            } else if (c == '{') {
                token = token_curly_open;
                break;
            } else if (c == '}') {
                token = token_curly_close;
                break;
            } else if (c == '\\') {
                token = token_escape;
                break;
            } else if (c == '"') {
                token = token_string;
                readString(c);
                break;
            } else if (c == '\'') {
                token = token_string;
                readString(c);
                break;
            } else if (c == ';') {
                token = token_statement;
                break;
            } else if (readInteger() || readUInteger()) {
                token = token_integer;
                break;
            } else if (readReal()) {
                token = token_real;
                break;
            } else {
                token = token_symbol;
                readSymbol();
                break;
            }
        }
        return token;
    }

    Any getAsString() {
        // TODO: anchor
        auto result = make_any(TYPE_String);
        auto s = alloc_string(string + 1, string_len - 2);
        unescape(*s);
        result.str = s;
        return result;
    }

    Any getAsSymbol() {
        // TODO: anchor
        std::string s(string, string_len);
        inplace_unescape(const_cast<char *>(s.c_str()));
        return bangra::symbol(s);
    }

    Any getAsInteger() {
        // TODO: anchor
        size_t width;
        if (is_unsigned) {
            width = ((uint64_t)integer > (uint64_t)INT_MAX)?64:32;
        } else {
            width =
                ((integer < (int64_t)INT_MIN) || (integer > (int64_t)INT_MAX))?64:32;
        }
        auto type = Types::Integer(width, !is_unsigned);
        return bangra::integer(type, this->integer);
    }

    Any getAsReal() {
        return bangra::real(TYPE_R32, this->real);
    }

};

//------------------------------------------------------------------------------
// S-EXPR PARSER
//------------------------------------------------------------------------------

struct Parser {
    Lexer lexer;

    Anchor error_origin;
    Anchor parse_origin;
    std::string error_string;
    int errors;

    Parser() :
        errors(0)
        {}

    void init() {
        error_string.clear();
    }

    void error( const char *fmt, ... ) {
        ++errors;
        lexer.initAnchor(error_origin);
        parse_origin = error_origin;
        va_list args;
        va_start (args, fmt);
        error_string = vformat(fmt, args);
        va_end (args);
    }

    struct ListBuilder {
    protected:
        Lexer &lexer;
        const List *prev;
        const List *eol;
        Anchor anchor;
    public:
        ListBuilder(Lexer &lexer_) :
            lexer(lexer_),
            prev(nullptr),
            eol(nullptr) {
            anchor = lexer.getAnchor();
        }

        const Anchor &getAnchor() {
            return anchor;
        }

        /*
        const List *getPrev() {
            return prev;
        }

        void setPrev(const List *prev) {
            this->prev = prev;
        }
        */

        void append(const Any &value) {
            this->prev = List::create(value, this->prev, get_anchor(value));
        }

        void resetStart() {
            eol = prev;
        }

        bool split() {
            // if we haven't appended anything, that's an error
            if (!prev) {
                return false;
            }
            // reverse what we have, up to last split point and wrap result
            // in cell
            prev = List::create(
                wrap(reverse_list_inplace(prev, eol)), eol, lexer.newAnchor());
            resetStart();
            return true;
        }

        bool isSingleResult() {
            return prev && !prev->next;
        }

        Any getSingleResult() {
            return this->prev?this->prev->at:const_none;
        }

        const List *getResult() {
            return reverse_list_inplace(this->prev);
        }
    };

    // parses a list to its terminator and returns a handle to the first cell
    const List *parseList(int end_token) {
        ListBuilder builder(lexer);
        lexer.readToken();
        while (true) {
            if (lexer.token == end_token) {
                break;
            } else if (lexer.token == token_escape) {
                int column = lexer.column();
                lexer.readToken();
                auto elem = parseNaked(column, end_token);
                if (errors) return nullptr;
                builder.append(elem);
            } else if (lexer.token == token_eof) {
                error("missing closing bracket");
                // point to beginning of list
                error_origin = builder.getAnchor();
                return nullptr;
            } else if (lexer.token == token_statement) {
                if (!builder.split()) {
                    error("empty expression");
                    return nullptr;
                }
                lexer.readToken();
            } else {
                auto elem = parseAny();
                if (errors) return nullptr;
                builder.append(elem);
                lexer.readToken();
            }
        }
        return builder.getResult();
    }

    // parses the next sequence and returns it wrapped in a cell that points
    // to prev
    Any parseAny () {
        assert(lexer.token != token_eof);
        auto anchor = lexer.newAnchor();
        Any result = const_none;
        if (lexer.token == token_open) {
            result = wrap(parseList(token_close));
        } else if (lexer.token == token_square_open) {
            const List *list = parseList(token_square_close);
            if (errors) return const_none;
            Any sym = symbol("[");
            result = wrap(List::create(sym, list, anchor));
        } else if (lexer.token == token_curly_open) {
            const List *list = parseList(token_curly_close);
            if (errors) return const_none;
            Any sym = symbol("{");
            result = wrap(List::create(sym, list, anchor));
        } else if ((lexer.token == token_close)
            || (lexer.token == token_square_close)
            || (lexer.token == token_curly_close)) {
            error("stray closing bracket");
        } else if (lexer.token == token_string) {
            result = lexer.getAsString();
        } else if (lexer.token == token_symbol) {
            result = lexer.getAsSymbol();
        } else if (lexer.token == token_integer) {
            result = lexer.getAsInteger();
        } else if (lexer.token == token_real) {
            result = lexer.getAsReal();
        } else {
            error("unexpected token: %c (%i)", *lexer.cursor, (int)*lexer.cursor);
        }
        if (errors) return const_none;
        set_anchor(result, anchor);
        return result;
    }

    Any parseRoot () {
        ListBuilder builder(lexer);

        while (lexer.token != token_eof) {
            if (lexer.token == token_none)
                break;
            auto elem = parseNaked(1, token_none);
            if (errors) return const_none;
            builder.append(elem);
        }

        return wrap(builder.getResult());
    }

    Any parseNaked (int column = 0, int end_token = token_none) {
        int lineno = lexer.lineno;

        bool escape = false;
        int subcolumn = 0;

        ListBuilder builder(lexer);

        while (lexer.token != token_eof) {
            if (lexer.token == end_token) {
                break;
            } else if (lexer.token == token_escape) {
                escape = true;
                lexer.readToken();
                if (lexer.lineno <= lineno) {
                    error("escape character is not at end of line");
                    parse_origin = builder.getAnchor();
                    return const_none;
                }
                lineno = lexer.lineno;
            } else if (lexer.lineno > lineno) {
                if (subcolumn == 0) {
                    subcolumn = lexer.column();
                } else if (lexer.column() != subcolumn) {
                    error("indentation mismatch");
                    parse_origin = builder.getAnchor();
                    return const_none;
                }
                if (column != subcolumn) {
                    if ((column + 4) != subcolumn) {
                        error("indentations must nest by 4 spaces.");
                        return const_none;
                    }
                }

                escape = false;
                builder.resetStart();
                lineno = lexer.lineno;
                // keep adding elements while we're in the same line
                while ((lexer.token != token_eof)
                        && (lexer.token != end_token)
                        && (lexer.lineno == lineno)) {
                    auto elem = parseNaked(
                        subcolumn, end_token);
                    if (errors) return const_none;
                    builder.append(elem);
                }
            } else if (lexer.token == token_statement) {
                if (!builder.split()) {
                    error("empty expression");
                    return const_none;
                }
                lexer.readToken();
                // if we are in the same line, continue in parent
                if (lexer.lineno == lineno)
                    break;
            } else {
                auto elem = parseAny();
                if (errors) return const_none;
                builder.append(elem);
                lineno = lexer.next_lineno;
                lexer.readToken();
            }

            if ((!escape || (lexer.lineno > lineno))
                && (lexer.column() <= column)) {
                break;
            }
        }

        if (builder.isSingleResult()) {
            return builder.getSingleResult();
        } else {
            return wrap(builder.getResult());
        }
    }

    Any parseMemory (
        const char *input_stream, const char *eof, const char *path, int offset = 0) {
        init();
        lexer.init(input_stream, eof, path, offset);

        lexer.readToken();

        auto result = parseRoot();

        if (error_string.empty() && !lexer.error_string.empty()) {
            error_string = lexer.error_string;
            lexer.initAnchor(error_origin);
            parse_origin = error_origin;
        }

        if (!error_string.empty()) {
            stb_printf("%s:%i:%i: error: %s\n",
                error_origin.path,
                error_origin.lineno,
                error_origin.column,
                error_string.c_str());
            dumpFileLine(path, error_origin.offset);
            if (!(parse_origin == error_origin)) {
                stb_printf("%s:%i:%i: while parsing expression\n",
                    parse_origin.path,
                    parse_origin.lineno,
                    parse_origin.column);
                dumpFileLine(path, parse_origin.offset);
            }
            return const_none;
        }

        return strip(result);
    }

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


};

//------------------------------------------------------------------------------
// FOREIGN FUNCTION INTERFACE
//------------------------------------------------------------------------------

// TODO: libffi based calls

struct FFI {
    std::unordered_map<Type, ffi_type *> ffi_types;

    FFI() {}

    ~FFI() {}

    ffi_type *new_type() {
        ffi_type *result = (ffi_type *)malloc(sizeof(ffi_type));
        memset(result, 0, sizeof(ffi_type));
        return result;
    }

    ffi_type *create_ffi_type(Type type) {
        if (is_none_type(type)) {
            return &ffi_type_void;
        } else if (is_integer_type(type)) {
            auto width = get_width(type);
            if (is_signed(type)) {
                switch(width) {
                    case 8: return &ffi_type_sint8;
                    case 16: return &ffi_type_sint16;
                    case 32: return &ffi_type_sint32;
                    case 64: return &ffi_type_sint64;
                    default: break;
                }
            } else {
                switch(width) {
                    case 8: return &ffi_type_uint8;
                    case 16: return &ffi_type_uint16;
                    case 32: return &ffi_type_uint32;
                    case 64: return &ffi_type_uint64;
                    default: break;
                }
            }
        } else if (is_pointer_type(type)) {
            return &ffi_type_pointer;
        }
        error("can not translate %s to FFI type",
            get_name(type).c_str());
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

    bool are_types_compatible(Type have, Type want) {
        if (is_subtype(have, TYPE_Array)
            && is_subtype(want, TYPE_Pointer)) {
            return
                is_subtype_or_type(
                    get_element_type(have),
                    get_element_type(want));
        }
        return is_subtype_or_type(have, want);
    }

    Any runFunction(
        const Any &func, const Any *args, size_t argcount) {
        assert(is_pointer_type(func.type));
        auto ftype = get_element_type(func.type);
        assert(is_cfunction_type(ftype));
        auto paramtypes = get_parameter_types(ftype);
        auto count = get_field_count(paramtypes);
        if (is_vararg(ftype)) {
            if (count > argcount) {
                error("not enough arguments for vararg function");
            }
        } else {
            if (count != argcount) {
                error("argument count mismatch");
            }
        }
        /*
        if (is_vararg(ftype)) {
            error("vararg functions not supported yet");
        }*/

        auto rtype = get_return_type(ftype);

        ffi_cif cif;
        auto maxcount = std::max(count, argcount);
        ffi_type *argtypes[maxcount];
        void *avalues[maxcount];
        for (size_t i = 0; i < maxcount; ++i) {
            Type ptype;
            if (i < count) {
                ptype = get_type(paramtypes, i);
                if (!are_types_compatible(args[i].type, ptype)) {
                    error("%s type expected, %s provided",
                        get_name(ptype).c_str(),
                        get_name(args[i].type).c_str());
                }
            } else {
                ptype = args[i].type;
            }
            argtypes[i] = get_ffi_type(ptype);
            if (is_subtype(args[i].type, TYPE_Array)
                && is_subtype(ptype, TYPE_Pointer)) {
                avalues[i] = (void *)&args[i].ptr;
            } else {
                avalues[i] = is_embedded(args[i].type)?
                    const_cast<uint8_t *>(args[i].embedded)
                    :const_cast<void *>(args[i].ptr);
            }
        }
        auto prep_result = ffi_prep_cif(
            &cif, FFI_DEFAULT_ABI, maxcount, get_ffi_type(rtype), argtypes);
        assert(prep_result == FFI_OK);

        Any result = make_any(rtype);
        // TODO: align properly
        void *rvalue;
        if (is_embedded(rtype)) {
            rvalue = result.embedded;
        } else {
            rvalue = malloc(get_size(rtype));
            result.ptr = rvalue;
        }

        auto funcptr = extract_ptr<void>(func);
        if (!funcptr)
            error("attempting to call null pointer");
        ffi_call(&cif, FFI_FN(funcptr), rvalue, avalues);

        return result;
    }

};

static FFI *ffi;

//------------------------------------------------------------------------------
// INTERPRETER
//------------------------------------------------------------------------------

// set by execute()
static Frame *handler_frame = nullptr;

typedef std::vector<Any> ILValueArray;

static Any evaluate(size_t argindex, const Frame *frame, const Any &value) {
    if (is_parameter_type(value.type)) {
        auto param = value.parameter;
        return frame->get(param->parent, param->index, value);
    } else if (is_flow_type(value.type)) {
        if (argindex == ARG_Func)
            // no closure creation required
            return value;
        else
            // create closure
            return wrap_ptr(TYPE_Closure,
                create_closure(value.flow, const_cast<Frame *>(frame)));
    }
    return value;
}

static bool trace_arguments = false;

static void print_fallback_value(const Any &arg, const Any &exc) {
    stb_printf("<error printing value of type %s: %s>\n",
        get_name(arg.type).c_str(),
        try_extract_string(exc, format(
            "exception of type %s raised",
            get_name(exc.type).c_str())).c_str());
}

static void print_minimal_call_trace(const Any *rbuf, size_t rcount) {
    if (!rcount) {
        stb_printf("  <command buffer is empty>\n");
    } else {
        for (size_t i = 0; i < rcount; ++i) {
            if (i == ARG_Cont)
                stb_printf("  Return: ");
            else if (i == ARG_Func)
                stb_printf("  Callee: ");
            else if (i >= ARG_Arg0)
                stb_printf("  Argument #%zu: ", i - ARG_Arg0);
            if (rbuf[i].type == TYPE_BuiltinFlow) {
                stb_printf("<%s '%s'>\n",
                    get_name(rbuf[i].type).c_str(),
                    get_symbol_name(get_ptr_symbol(rbuf[i].ptr)).c_str());
            } else {
                stb_printf("<value of type %s>\n",
                    get_name(rbuf[i].type).c_str());
            }
        }
    }
}

static void print_call_trace(const Any *rbuf, size_t rcount) {
    if (!rcount) {
        stb_printf("  <command buffer is empty>\n");
    } else {
        StreamValueFormat fmt(0, true);
        fmt.maxdepth = 2;
        fmt.maxlength = 5;

        for (size_t i = 0; i < rcount; ++i) {
            if (i == ARG_Cont)
                stb_printf("  Return: ");
            else if (i == ARG_Func)
                stb_printf("  Callee: ");
            else if (i >= ARG_Arg0)
                stb_printf("  Argument #%zu: ", i - ARG_Arg0);
            try {
                printValue(rbuf[i], fmt);
            } catch (const Any &any) {
                print_fallback_value(rbuf[i], any);
            }
        }
    }
}

static int execute_depth = 0;
static size_t run_apply_loop(const Any *args, size_t argcount, Any *ret, const ExecuteOpts &opts) {
    size_t retcount = 0;
    execute_depth++;

    // double buffer arguments
    Any *argbuf[2];
    for (size_t i = 0; i < 2; ++i) {
        argbuf[i] = (Any *)malloc(sizeof(Any) * BANGRA_MAX_FUNCARGS);
    }
    Any *wbuf = argbuf[0];
    size_t wcount = argcount;
    assert(argcount >= 2);
    assert(argcount <= BANGRA_MAX_FUNCARGS);
    // track exit_cont so we can leave the loop when it is invoked
    auto exit_cont = args[ARG_Cont];
    assert(exit_cont.type == TYPE_BuiltinFlow);
    // init read buffer for arguments
    for (size_t i = 0; i < argcount; ++i) {
        wbuf[i] = args[i];
    }

    Any *rbuf = argbuf[1];
    size_t rcount = 0;

    State S;
    S.frame = Frame::create();
    S.exception_handler = const_none;

    std::vector<Any> tmpargs;

    if (opts.trace) {
        stb_printf("TRACE: execute enter (exit = %s)\n",
            get_string(exit_cont).c_str());
    }

continue_execution:
    try {
        while (true) {
            // flip buffers
            {
                Any *xbuf = wbuf;
                wbuf = rbuf;
                rbuf = xbuf;
                rcount = wcount;
                wcount = 0;

                S.rargs = rbuf;
                S.rcount = rcount;
                S.wargs = wbuf;
                S.wcount = wcount;
            }

            if (opts.trace) {
                stb_printf("TRACE: executing at level %i:\n", execute_depth);
                print_call_trace(rbuf, rcount);
            }

            assert(rcount >= 2);

            Any callee = rbuf[ARG_Func];
            if ((callee.type == TYPE_BuiltinFlow)
                && (callee.ptr == exit_cont.ptr)) {
                // exit continuation invoked, copy result and break loop
                for (size_t i = 0; i < rcount; ++i) {
                    ret[i] = rbuf[i];
                }
                retcount = rcount;
                if (opts.trace) {
                    stb_printf("TRACE: execute leave\n");
                }
                break;
            }

            if (is_closure_type(callee.type)) {
                auto closure = callee.closure;

                S.frame = closure->frame;
                callee = wrap(closure->entry);
            }

            if (is_flow_type(callee.type)) {
                auto flow = callee.flow;
                // TODO: assert(get_anchor(flow));
                assert(flow->parameters.size() >= 1);

                // tmpargs map directly to param indices; that means
                // the callee is not included.
                auto pcount = flow->parameters.size();
                tmpargs.resize(pcount);

                // copy over continuation argument
                tmpargs[PARAM_Cont] = rbuf[ARG_Cont];
                size_t tcount = pcount - PARAM_Arg0;
                size_t srci = ARG_Arg0;
                for (size_t i = 0; i < tcount; ++i) {
                    size_t dsti = PARAM_Arg0 + i;
                    auto param = flow->parameters[dsti];
                    if (param->vararg) {
                        // how many parameters after this one
                        int remparams = (int)tcount - (int)i - 1;
                        // how many varargs to capture
                        int vargsize = std::max(0, (int)rcount - (int)srci - remparams);
                        auto arg = wrap(&rbuf[srci], vargsize);
                        arg.type = Types::Splice(arg.type);
                        tmpargs[dsti] = arg;
                        srci += vargsize;
                    } else if (srci < rcount) {
                        tmpargs[dsti] = rbuf[srci++];
                    } else {
                        tmpargs[dsti] = const_none;
                    }
                }

                S.frame = S.frame->bind(flow, tmpargs);

                set_anchor(S.frame, get_anchor(flow));

                assert(!flow->arguments.empty());
                size_t idx = 0;
                for (size_t i = 0; i < flow->arguments.size(); ++i) {
                    Any arg = evaluate(i, S.frame, flow->arguments[i]);
                    if (is_splice_type(arg.type)) {
                        arg.type = get_element_type(arg.type);
                        arg = evaluate(i, S.frame, arg);
                        if (!is_tuple_type(arg.type)) {
                            error("only tuples can be spliced");
                        }
                        size_t fcount = countof(arg);
                        for (size_t k = 0; k < fcount; ++k) {
                            wbuf[idx] = at(arg, wrap(k));
                            idx++;
                        }
                    } else {
                        wbuf[idx++] = arg;
                    }
                }
                wcount = idx;

                if (opts.trace) {
                    auto anchor = get_anchor(flow);
                    Anchor::printMessage(anchor, "trace");
                }
            } else if (is_builtin_flow_type(callee.type)) {
                auto cb = callee.builtin_flow;
                auto _oldframe = handler_frame;
                handler_frame = S.frame;
                cb(&S);
                wcount = S.wcount;
                assert(wcount <= BANGRA_MAX_FUNCARGS);
                handler_frame = _oldframe;
            } else if (is_typeref_type(callee.type)) {
                // redirect to type's apply-type handler
                assert(S.rcount < BANGRA_MAX_FUNCARGS);
                auto cb = callee.typeref;
                wbuf[ARG_Cont] = rbuf[ARG_Cont];
                wbuf[ARG_Func] = get_type_attrib(cb, SYM_ApplyType, const_none);
                wbuf[ARG_Arg0] = callee;
                for (size_t k = ARG_Arg0; k < S.rcount; ++k) {
                    wbuf[k + 1] = rbuf[k];
                }
                wcount = S.rcount + 1;
            } else if (
                is_pointer_type(callee.type)
                    && is_cfunction_type(get_element_type(callee.type))) {
                wbuf[ARG_Cont] = const_none;
                wbuf[ARG_Func] = rbuf[ARG_Cont];
                wbuf[ARG_Arg0] = ffi->runFunction(callee, rbuf + ARG_Arg0, rcount - 2);
                wcount = 3;
            } else {
                error("can not apply value of type %s",
                    get_name(callee.type).c_str());
            }
        }
    } catch (const Any &any) {

        if (!isnone(S.exception_handler)) {
            wbuf[ARG_Cont] = const_none;
            wbuf[ARG_Func] = S.exception_handler;
            wbuf[ARG_Arg0 + 0] = any;
            wbuf[ARG_Arg0 + 1] = wrap(S.frame);
            wbuf[ARG_Arg0 + 2] = wrap(rbuf, rcount);
            wcount = 5;
            S.exception_handler = const_none;
            goto continue_execution;
        }

        if (opts.dump_error) {
            stb_printf("while evaluating arguments:\n");
            print_call_trace(rbuf, rcount);
            stb_printf("\n");

            Frame::print_stack(S.frame);

            std::string str;
            try {
                str = get_string(any);
            } catch (const Any &any) {
                str = format("<exception raised while converting value of type %s to string>",
                    get_name(any.type).c_str());
            }

            std::cout << ansi(ANSI_STYLE_ERROR, "error:") << " " << str << "\n";

            //goto continue_execution;
            fflush(stdout);
        }

        // throwing will leave the buffers dangling, so free them first
        for (size_t i = 0; i < 2; ++i) {
            free(argbuf[i]);
        }
        execute_depth--;
        throw_any(any);
    }

    for (size_t i = 0; i < 2; ++i) {
        free(argbuf[i]);
    }
    execute_depth--;

    return retcount;
}

static Any execute(const Any *args, size_t argcount, const ExecuteOpts &opts) {
    assert(argcount < BANGRA_MAX_FUNCARGS);
    assert(argcount >= 1);
    if (args[0].type == TYPE_BuiltinFlow) {
        // we can do a direct C to C call, no need to set up the whole thing

        Any tmp_inargs[BANGRA_MAX_FUNCARGS];
        Any tmp_outargs[BANGRA_MAX_FUNCARGS];

        tmp_inargs[ARG_Cont] = const_none;
        for (size_t i = 0; i < argcount; ++i) {
            tmp_inargs[i + ARG_Func] = args[i];
        }

        State S;
        S.frame = nullptr;
        S.exception_handler = const_none;
        S.rargs = tmp_inargs;
        S.wargs = tmp_outargs;
        S.wcount = 0;
        S.rcount = argcount + 1;

        if (opts.trace) {
            stb_printf("TRACE: C2C call:\n");
            print_minimal_call_trace(S.rargs, S.rcount);
        }

        args[0].builtin_flow(&S);
        assert(S.wcount <= BANGRA_MAX_FUNCARGS);
        assert(S.wcount >= 3);
        assert(isnone(S.wargs[ARG_Cont]));
        assert(isnone(S.wargs[ARG_Func]));
        return S.wargs[ARG_Arg0];
    } else {
        // use the interpreter
        Any ret[BANGRA_MAX_FUNCARGS];
        // special return value that is never invoked
        ret[ARG_Cont].u64 = 1;
        ret[ARG_Cont].type = TYPE_BuiltinFlow;
        for (size_t i = 0; i < argcount; ++i) {
            ret[i + ARG_Func] = args[i];
        }
        size_t retcount = run_apply_loop(ret, argcount + 1, ret, opts);
        assert(retcount >= 3);
        return ret[ARG_Arg0];
    }
}

static Any execute(const Any *args, size_t argcount) {
    auto opts = ExecuteOpts();
    opts.trace = trace_arguments;
    return execute(args, argcount, opts);
}

//------------------------------------------------------------------------------
// TABLES
//------------------------------------------------------------------------------

static Table *new_table() {
    auto result = new Table();
    result->meta = nullptr;
    return result;
}

static void set_key(Table &table, const Any &key, const Any &value) {
    table._[key] = value;
}

static void set_key(Table &table, const Symbol &key, const Any &value) {
    set_key(table, wrap_symbol(key), value);
}

static bool has_key(const Table &table, const Any &key) {
    return table._.find(key) != table._.end();
}

static bool has_key(const Table &table, const Symbol &key) {
    return has_key(table, wrap_symbol(key));
}

static const Any &get_key(const Table &table,
    const Any &key, const Any &defvalue) {
    auto it = table._.find(key);
    if (it == table._.end())
        return defvalue;
    else
        return it->second;
}

static const Any &get_key(const Table &table,
    const Symbol &key, const Any &defvalue) {
    return get_key(table, wrap_symbol(key), defvalue);
}

//------------------------------------------------------------------------------
// BUILTIN WRAPPING
//------------------------------------------------------------------------------

typedef
    Any (*UnaryOpFunction)(const Any &value);
typedef
    Any (*BinaryOpFunction)(const Any &a, const Any &b);
typedef
    Ordering (*CompareFunction)(const Any &a, const Any &b);
typedef
    Ordering (*CompareTypeFunction)(Type self, Type other);
typedef
    std::string (*ToStringFunction)(const Any &value);
typedef
    uint64_t (*HashFunction)(const Any &value);
typedef
    Any (*SliceFunction)(const Any &value, size_t i0, size_t i1);
typedef
    Any (*ApplyTypeFunction)(Type self, const Any *args, size_t argcount);
typedef
    size_t (*SpliceFunction)(const Any &value, Any *ret, size_t retsize);
typedef
    size_t (*CountOfFunction)(const Any &value);

template<ApplyTypeFunction F>
static B_FUNC(apply_type_call) {
    if (B_RCOUNT(S) < 3)
        error("insufficient number of arguments");
    B_CALL(S, const_none, B_GETCONT(S),
        F(extract_type(S->rargs[ARG_Arg0]),
            S->rargs + ARG_Arg0 + 1, B_RCOUNT(S) - 3));
}

template<BuiltinFunction F>
static B_FUNC(builtin_call) {
    assert(B_RCOUNT(S) >= 2);
    B_CALL(S, const_none, B_GETCONT(S),
        F(S->rargs + ARG_Arg0, B_RCOUNT(S) - 2));
}

template<UnaryOpFunction F>
Any unary_op_call(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return F(args[0]);
}

template<BinaryOpFunction F>
Any binary_op_call(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    return F(args[0], args[1]);
}

template<CompareFunction F>
Any compare_call(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    return wrap(F(args[0], args[1]));
}

template<CompareTypeFunction F>
Any compare_type_call(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    return wrap(F(extract_type(args[0]), extract_type(args[1])));
}

template<ToStringFunction F>
Any tostring_call(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(F(args[0]));
}

template<HashFunction F>
Any hash_call(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(F(args[0]));
}

template<SliceFunction F>
Any slice_call(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 3, 3);
    return F(args[0], extract_integer(args[1]), extract_integer(args[2]));
}

template<BuiltinFlowFunction func> static Any wrap() {
    return wrap_ptr(TYPE_BuiltinFlow, (void *)func); }
template<BuiltinFunction func> static Any wrap() {
    return wrap< builtin_call<func> >(); }
template<ApplyTypeFunction func> static Any wrap() {
    return wrap< apply_type_call<func> >(); }
template<UnaryOpFunction func> static Any wrap() {
    return wrap< builtin_call< unary_op_call<func> > >(); }
template<BinaryOpFunction func> static Any wrap() {
    return wrap< builtin_call< binary_op_call<func> > >(); }
template<CompareFunction func> static Any wrap() {
    return wrap< builtin_call< compare_call<func> > >(); }
template<CompareTypeFunction func> static Any wrap() {
    return wrap< builtin_call< compare_type_call<func> > >(); }
template<ToStringFunction func> static Any wrap() {
    return wrap< builtin_call< tostring_call<func> > >(); }
template<SliceFunction func> static Any wrap() {
    return wrap< builtin_call< slice_call<func> > >(); }
template<HashFunction func> static Any wrap() {
    return wrap< builtin_call< hash_call<func> > >(); }

//------------------------------------------------------------------------------
// TYPE SETUP
//------------------------------------------------------------------------------

// TOSTRING
//--------------------------------------------------------------------------

static std::string _string_tostring(const Any &value) {
    return extract_string(value);
}

static std::string _symbol_tostring(const Any &value) {
    return extract_symbol_string(value);
}

template<typename T>
static std::string _named_object_tostring(const Any &value) {
    auto sym = ((T *)value.ptr)->name;
    return "<" + get_name(value.type) + " " + get_symbol_name(sym) + format("=%p>", value.ptr);
}

static std::string _parameter_tostring(const Any &value) {
    auto param = (Parameter *)value.ptr;
    auto name = format("@%s", get_symbol_name(param->name).c_str());
    if (param->parent) {
        name = get_string(wrap(param->parent)) + name + format("%zu",param->index);
    }
    return "<" + get_name(value.type) + " " + name + ">";
}

static std::string _closure_tostring(const Any &value) {
    auto closure = (Closure *)value.ptr;
    return "<" + get_name(value.type)
        + " entry=" + get_string(wrap(closure->entry))
        + format(" frame=%p", closure->frame) + ">";
}

static std::string _named_ptr_tostring(const Any &value) {
    auto sym = get_ptr_symbol(value.ptr);
    return "<" + get_name(value.type) + " " +
        (sym?get_symbol_name(sym):format("%p", value.ptr)) + ">";
}

static std::string _integer_tostring(const Any &value) {
    auto ivalue = extract_integer(value);
    if (get_width(value.type) == 1) {
        return ivalue?"true":"false";
    }
    if (is_signed(value.type))
        return format("%" PRId64, ivalue);
    else
        return format("%" PRIu64, ivalue);
}

static std::string _real_tostring(const Any &value) {
    return format("%g", extract_real(value));
}

static std::string _list_tostring(const Any &value) {
    std::stringstream ss;
    streamValue(ss, value, StreamValueFormat(0, false));
    return ss.str();
}

static std::string _none_tostring(const Any &value) {
    return "none";
}

static std::string _pointer_tostring(const Any &value) {
    return ansi(ANSI_STYLE_COMMENT, "<")
        + get_name(value.type) + format("=%p", value.ptr)
        + ansi(ANSI_STYLE_COMMENT, ">");
}

static std::string _type_tostring(const Any &value) {
    return "type:" + get_name(value.typeref);
}

static std::string _tuple_tostring(const Any &value) {
    std::stringstream ss;
    ss << ansi(ANSI_STYLE_COMMENT, "<");
    ss << ansi(ANSI_STYLE_KEYWORD, "tupleof");
    size_t fcount = get_field_count(value.type);
    for (size_t i = 0; i < fcount; i++) {
        auto offset = get_field_offset(value.type, i);
        ss << " " << get_string(
            wrap(get_type(value.type, i), (char *)value.ptr + offset));
    }
    ss << ansi(ANSI_STYLE_COMMENT, ">");
    return ss.str();
}

// COUNTOF
//--------------------------------------------------------------------------

static size_t _pointer_countof(const Any &value) {
    return countof(pointer_element(value));
}

static size_t _string_countof(const Any &value) {
    return value.str->count;
}

static size_t _list_countof(const Any &value) {
    return value.list?value.list->count:0;
}

static size_t _tuple_countof(const Any &value) {
    return get_field_count(value.type);
}

// SLICE
//--------------------------------------------------------------------------

static Any _tuple_slice(const Any &value, size_t i0, size_t i1) {
    size_t count = i1 - i0;
    std::vector<Type> types;
    types.resize(count);
    for (size_t i = 0; i < count; ++i) {
        types[i] = get_type(value.type, i0 + i);
    }
    return wrap(Types::Tuple(types),
        (char *)value.ptr + get_field_offset(value.type, i0));
}

static Any _string_slice(const Any &value, size_t i0, size_t i1) {
    const String *str;
    if (i1 < value.str->count) {
        // allocate a new one, since we need the string to be always
        // zero-terminated.
        str = alloc_string(value.str->ptr + i0, i1 - i0);
    } else {
        str = alloc_slice(value.str->ptr + i0, i1 - i0);
    }
    return wrap(TYPE_String, str);
}

static Any _list_slice(const Any &value, size_t i0, size_t i1) {
    const List *list = value.list;
    size_t i = 0;
    while (i < i0) {
        assert(list);
        list = list->next;
        ++i;
    }
    size_t count = list?list->count:0;
    if (count != (i1 - i0)) {
        // need to chop off tail, which requires creating a new list
        assert(list);
        const List *outlist = nullptr;
        while (i < i1) {
            assert(list);
            outlist = List::create(list->at, outlist, get_anchor(list));
            list = list->next;
            ++i;
        }
        list = reverse_list_inplace(outlist);
    }
    return wrap(list);
}

// AT
//--------------------------------------------------------------------------

static Any type_pointer_at(const Any &value, const Any &index) {
    return at(pointer_element(value), index);
}

static Any type_array_at(const Any &value, const Any &vindex) {
    auto index = (size_t)extract_integer(vindex);
    auto element_type = get_element_type(value.type);
    auto padded_size = align(
        get_size(element_type), get_alignment(element_type));
    auto offset = padded_size * index;
    auto size = get_size(value.type);
    if (offset >= size) {
        error("index %zu out of array bounds (%zu)",
            index, size / padded_size);
        return const_none;
    }
    return wrap(element_type, (char *)value.ptr + offset);
}

static Any _tuple_get(const Any &value, size_t index) {
    auto fcount = get_field_count(value.type);
    if (index >= fcount) {
        error("index %zu out of tuple bounds (%zu)",
            index, fcount);
        return const_none;
    }
    auto offset = get_field_offset(value.type, index);
    return wrap(get_type(value.type, index), (char *)value.ptr + offset);
}

static Any type_tuple_at(const Any &value, const Any &vindex) {
    return _tuple_get(value, (size_t)extract_integer(vindex));
}

static Any type_struct_at(const Any &value, const Any &vindex) {
    if (is_symbol_type(vindex.type)) {
        auto key = extract_symbol(vindex);
        auto fcount = get_field_count(value.type);
        for (size_t i = 0; i < fcount; ++i) {
            auto name = get_field_name(value.type, i);
            if (name == key)
                return _tuple_get(value, i);
        }
        error("no such field in struct: %s",
            get_symbol_name(key).c_str());
        return const_none;
    } else {
        return type_tuple_at(value, vindex);
    }
}

static Any type_table_at(const Any &value, const Any &vindex) {
    return get_key(*value.table, vindex, const_none);
}

static Any type_list_at(const Any &value, const Any &vindex) {
    auto slist = value.list;
    auto count = (size_t)slist?slist->count:0;
    auto index = (size_t)extract_integer(vindex);
    if (index >= count) {
        error("index %zu out of list bounds (%zu)",
            index, count);
        return const_none;
    }
    const List *result = slist;
    while ((index != 0) && result) {
        --index;
        result = result->next;
    }
    assert(result);
    return result->at;
}

static Any _string_at(const Any &value, const Any &vindex) {
    auto index = (size_t)extract_integer(vindex);
    if (index >= value.str->count) {
        error("index %zu out of string bounds (%zu)",
            index, value.str->count);
    }
    return _string_slice(value, index, index + 1);
}

// SPLICE
//--------------------------------------------------------------------------

static size_t _list_splice(const Any &value, Any *ret, size_t retsize) {
    auto it = value.list;
    size_t count = 0;
    while (it && (count < retsize)) {
        ret[count] = it->at;
        it = it->next;
        ++count;
    }
    return count;
}

static size_t _tuple_splice(const Any &value, Any *ret, size_t retsize) {
    size_t count = std::min(retsize, get_field_count(value.type));
    for (size_t i = 0; i < count; ++i) {
        ret[i] = _tuple_get(value, i);
    }
    return count;
}

// VALUE COMPARISON
//--------------------------------------------------------------------------

static Ordering _list_cmp(const Any &a, const Any &b) {
    auto x = a.list;
    auto y = extract_list(b);
    while (true) {
        if (x == y) return Equal;
        else if (!x) return Less;
        else if (!y) return Greater;
        auto result = compare(x->at, y->at);
        if (result != Equal) return result;
        x = x->next;
        y = y->next;
    }
}

static Ordering value_pointer_cmp(const Any &a, const Any &b) {
    auto y = is_pointer_type(b.type)?pointer_element(b):b;
    return compare(pointer_element(a), y);
}

static Ordering _typeref_cmp(const Any &a, const Any &b) {
    if (a.type != b.type) {
        error("type comparison expected");
    }
    if (a.typeref == b.typeref) return Equal;
    try {
        Any args[] = { get_type_attrib(a.typeref, SYM_CmpType, const_none), a, b };
        auto result = extract_ordering(execute(args, 3));
        if (result != Unordered) return result;
    } catch (const Any &e1) {
    }
    try {
        Any args[] = { get_type_attrib(b.typeref, SYM_CmpType, const_none), b, a };
        auto result = extract_ordering(execute(args, 3));
        switch(result) {
            case Less: return Greater;
            case Greater: return Less;
            default: return result;
        }
    } catch (const Any &e2) {
    }
    return Unordered;
}

static Ordering _struct_cmp(const Any &a, const Any &b) {
    if (!eq(wrap(a.type), wrap(b.type))) return Unordered;
    return (a.ptr == b.ptr)?Equal:Unordered;
}

static Ordering value_none_cmp(const Any &a, const Any &b) {
    return (a.type == b.type)?Equal:Unordered;
}

static Ordering _real_cmp(const Any &a, const Any &b) {
    auto x = extract_real(a);
    auto y = extract_real(b);
    if (std::isnan(x + y)) return Unordered;
    return (Ordering)((y < x) - (x < y));
}

static Ordering _integer_cmp(const Any &a, const Any &b) {
    auto x = extract_integer(a);
    auto y = extract_integer(b);
    return (Ordering)((y < x) - (x < y));
}

static Ordering value_symbol_cmp(const Any &a, const Any &b) {
    if (a.type != b.type) error("can not compare to type");
    auto x = a.symbol;
    auto y = b.symbol;
    return (x == y)?Equal:Unordered;
}

static Ordering _tuple_cmp(const Any &a, const Any &b) {
    if (!is_tuple_type(b.type))
        error("tuple type expected, not %s", get_name(b.type).c_str());
    auto asize = get_field_count(a.type);
    auto bsize = get_field_count(b.type);
    if (asize == bsize) {
        for (size_t i = 0; i < asize; ++i) {
            auto x = _tuple_get(a, i);
            auto y = _tuple_get(b, i);
            auto result = compare(x, y);
            if (result != Equal) return result;
        }
        return Equal;
    } else if (asize < bsize) {
        return Less;
    } else {
        return Greater;
    }
}

static int zucmp(size_t a, size_t b) {
    return (b < a) - (a < b);
}

static Ordering value_string_cmp(const Any &a, const Any &b) {
    if (b.type != a.type)
        error("incomparable values");
    int c = zucmp(a.str->count, b.str->count);
    if (!c) {
        c = memcmp(a.str->ptr, b.str->ptr, a.str->count);
    }
    return (Ordering)c;
}

// ARITHMETIC
//--------------------------------------------------------------------------

static Any _integer_add(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) + extract_integer(b));
}

static Any _integer_sub(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) - extract_integer(b));
}

static Any _integer_mul(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) * extract_integer(b));
}

static Any _integer_div(const Any &a, const Any &b) {
    return wrap((double)extract_integer(a) / (double)extract_integer(b));
}

static Any _integer_floordiv(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) / extract_integer(b));
}

static Any _integer_mod(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) % extract_integer(b));
}

static Any _integer_and(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) & extract_integer(b));
}

static Any _integer_or(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) | extract_integer(b));
}

static Any _integer_xor(const Any &a, const Any &b) {
    return integer(a.type, extract_integer(a) ^ extract_integer(b));
}

// thx to fabian for this one
template<typename T>
T ipow(T base, T exponent) {
    T result = 1, cur = base;
    while (exponent) {
        if (exponent & 1) result *= cur;
        cur *= cur;
        exponent >>= 1;
    }
    return result;
}

static Any _integer_pow(const Any &a, const Any &b) {
    return wrap(ipow(extract_integer(a), extract_integer(b)));
}

static Any _integer_neg(const Any &x) {
    return wrap(-extract_integer(x));
}

static Any _integer_rcp(const Any &x) {
    return wrap(1.0 / (double)extract_integer(x));
}

static Any _bool_not(const Any &x) {
    return wrap(!extract_bool(x));
}

static Any _real_add(const Any &a, const Any &b) {
    return real(a.type, extract_real(a) + extract_real(b));
}

static Any _real_sub(const Any &a, const Any &b) {
    return real(a.type, extract_real(a) - extract_real(b));
}

static Any _real_mul(const Any &a, const Any &b) {
    return real(a.type, extract_real(a) * extract_real(b));
}

static Any _real_div(const Any &a, const Any &b) {
    return real(a.type, extract_real(a) / extract_real(b));
}

static Any _real_neg(const Any &x) {
    return real(x.type, -extract_real(x));
}

static Any _real_rcp(const Any &x) {
    return real(x.type, 1.0 / extract_real(x));
}

static Any _real_pow(const Any &a, const Any &b) {
    return real(a.type, std::pow(extract_real(a), extract_real(b)));
}

template<Symbol OP, Symbol ROP>
static Any _pointer_fwd_op2(const Any &a, const Any &b) {
    auto x = pointer_element(a);
    return op2<OP, ROP>(x, b);
}

// TYPE COMPARISON
//--------------------------------------------------------------------------

static Ordering type_array_eq(Type self, Type other) {
    return (other == TYPE_Array)?Less:Unordered;
}

static Ordering type_vector_eq(Type self, Type other) {
    return (other == TYPE_Vector)?Less:Unordered;
}

static Ordering type_pointer_eq(Type self, Type other) {
    return (other == TYPE_Pointer)?Less:Unordered;
}

static Ordering type_splice_eq(Type self, Type other) {
    return (other == TYPE_Splice)?Less:Unordered;
}

static Ordering type_quote_eq(Type self, Type other) {
    return (other == TYPE_Quote)?Less:Unordered;
}

static Ordering type_macro_eq(Type self, Type other) {
    return (other == TYPE_Macro)?Less:Unordered;
}

static Ordering type_qualifier_eq(Type self, Type other) {
    return ((other == TYPE_Qualifier)
        || (other == get_super(self)))?Less:Unordered;
}

static Ordering type_tuple_eq(Type self, Type other) {
    return (other == TYPE_Tuple)?Less:Unordered;
}

static Ordering type_cfunction_eq(Type self, Type other) {
    return (other == TYPE_CFunction)?Less:Unordered;
}

static Ordering type_integer_eq(Type self, Type other) {
    return (other == TYPE_Integer)?Less:Unordered;
}

static Ordering type_real_eq(Type self, Type other) {
    return (other == TYPE_Real)?Less:Unordered;
}

static Ordering type_struct_eq(Type self, Type other) {
    return (other == TYPE_Struct)?Less:Unordered;
}

static Ordering type_enum_eq(Type self, Type other) {
    return (other == TYPE_Enum)?Less:Unordered;
}

static Ordering type_tag_eq(Type self, Type other) {
    return (other == TYPE_Tag)?Less:Unordered;
}

// APPLY TYPE
//--------------------------------------------------------------------------

static Any _symbol_apply_type(Type self,
    const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return symbol(get_string(args[0]));
}

static Any _string_apply_type(Type self,
    const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    if (args[0].type == TYPE_Rawstring) {
        const char *str = args[0].c_str;
        return wrap(TYPE_String, alloc_slice<char>(str, strlen(str)));
    } else {
        return pstring(get_string(args[0]));
    }
}

static Any _rawstring_apply_type(Type self,
    const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    if (!is_subtype_or_type(args[0].type, TYPE_String))
        error("string expected");
    return wrap_ptr(TYPE_Rawstring, args[0].str->ptr);
}

static Any _list_apply_type(Type self,
    const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, -1);
    auto result = List::create_from_c_array(args, argcount);
    //set_anchor(result, get_anchor(handler_frame));
    return wrap(result);
}

static Any _parameter_apply_type(Type self,
    const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto name = extract_symbol(args[0]);
    return wrap(Parameter::create(name));
}

static Any _integer_value_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return integer(self, extract_integer(args[0]));
}

static Any _qualified_type_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    Any value = args[0];
    if (value.type != get_element_type(self))
        error("cannot qualify %s with %s.",
            get_name(value.type).c_str(),
            get_name(self).c_str());
    value.type = self;
    return value;
}

// TYPE FACTORIES
//--------------------------------------------------------------------------

static Any _qualifier_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto type = extract_type(args[0]);
    return wrap(Types::Qualifier(self, type));
}

static Any _cfunction_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 3, 3);
    auto rettype = extract_type(args[0]);
    auto paramtype = extract_type(args[1]);
    if (!is_tuple_type(paramtype)) {
        error("tuple type expected");
    }
    bool vararg = extract_bool(args[2]);

    return wrap(Types::CFunction(rettype, paramtype, vararg));
}

static Any _pointer_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto type = extract_type(args[0]);
    return wrap(Types::Pointer(type));
}

static Any _tuple_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, -1);
    std::vector<Type> types;
    types.resize(argcount);
    for (size_t i = 0; i < argcount; ++i) {
        types[i] = extract_type(args[i]);
    }
    return wrap(Types::Tuple(types));
}

static Any _array_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto type = extract_type(args[0]);
    auto count = extract_integer(args[1]);
    return wrap(Types::Array(type, count));
}

static Any _vector_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto type = extract_type(args[0]);
    auto count = extract_integer(args[1]);
    return wrap(Types::Vector(type, count));
}

static Any _integer_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto width = extract_integer(args[0]);
    auto is_signed = extract_bool(args[1]);
    return wrap(Types::Integer(width, is_signed));
}

static Any _real_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto width = extract_integer(args[0]);
    return wrap(Types::Real(width));
}

static Any _struct_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto name = extract_symbol(args[0]);
    auto result = Types::Struct(get_symbol_name(name), false);
    return wrap(result);
}

static Any _tag_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto name = extract_symbol(args[0]);
    auto tag = Types::Tag(name);
    return wrap(tag);
}

static Any _closure_apply_type(
    Type self, const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto entry = extract_flow(args[0]);
    auto frame = extract_frame(args[1]);
    return wrap(create_closure(entry, const_cast<Frame *>(frame)));
}

// JOIN
//--------------------------------------------------------------------------

static Any _string_join(const Any &a, const Any &b) {
    return wrap(extract_string(a) + extract_string(b));
}

static Any _table_join(const Any &a, const Any &b) {
    auto ta = a.table;
    auto tb = extract_table(b);
    auto t = new_table();
    for (auto it = ta->_.begin(); it != ta->_.end(); ++it) {
        set_key(*t, it->first, it->second);
    }
    for (auto it = tb->_.begin(); it != tb->_.end(); ++it) {
        set_key(*t, it->first, it->second);
    }
    return wrap(t);
}

static Any _list_join(const Any &a, const Any &b) {
    auto la = a.list;
    auto lb = extract_list(b);
    const List *l = lb;
    while (la) {
        l = List::create(la->at, l, get_anchor(la));
        la = la->next;
    }
    return wrap(reverse_list_inplace(l, lb, lb));
}

// HASHING
//--------------------------------------------------------------------------

static uint64_t _integer_hash(const Any &value) {
    auto val = extract_integer(value);
    return *(uint64_t *)&val;
}

static uint64_t _symbol_hash(const Any &value) {
    return value.symbol;
}

static uint64_t _real_hash(const Any &value) {
    auto val = extract_real(value);
    return *(uint64_t *)&val;
}

static uint64_t _string_hash(const Any &value) {
    return CityHash64(value.str->ptr, value.str->count);
}

static uint64_t _embedded_hash(const Any &value) {
    assert(get_alignment(value.type) && "implementation error: opaque type can't be hashed");
    assert(is_embedded(value.type));
    return CityHash64((const char *)value.embedded, get_size(value.type));
}

static uint64_t _buffer_hash(const Any &value) {
    assert(get_alignment(value.type) && "implementation error: opaque type can't be hashed");
    assert(!is_embedded(value.type));
    return CityHash64((const char *)value.ptr, get_size(value.type));
}

static uint64_t _pointer_hash(const Any &value) {
    auto etype = get_element_type(value.type);
    auto h = get_type_attrib(etype, SYM_Hash, const_none);
    if (h.type != TYPE_Void) {
        return hash(pointer_element(value));
    }
    return *(uint64_t *)&value.ptr;
}

static uint64_t _tuple_hash(const Any &value) {
    size_t tsize = get_field_count(value.type);
    size_t sum = CityHash64(nullptr, 0);
    for (size_t i = 0; i < tsize; ++i) {
        uint64_t h = hash(_tuple_get(value, i));
        sum = HashLen16(h, sum);
    }
    return sum;
}

// STRUCT/TUPLE SETUP TOOLS
//--------------------------------------------------------------------------

static void _set_field_names(Type type, const std::vector<std::string> &names) {
    Type mtype = Types::Array(TYPE_Symbol, names.size());
    Symbol *data = (Symbol *)malloc(get_size(mtype));
    for (size_t i = 0; i < names.size(); ++i) {
        auto name = get_symbol(names[i]);
        /* if (name != SYM_Unnamed) { type->name_index_map[name] = i; } */
        data[i] = name;
    }
    set_type_attrib(type, SYM_FieldNames, wrap(mtype, data));
}

static void _set_field_names(Type type, const std::vector<Symbol> &names) {
    Type mtype = Types::Array(TYPE_Symbol, names.size());
    Symbol *data = (Symbol *)malloc(get_size(mtype));
    for (size_t i = 0; i < names.size(); ++i) {
        auto name = names[i];
        /* if (name != SYM_Unnamed) { type->name_index_map[name] = i; } */
        data[i] = name;
    }
    set_type_attrib(type, SYM_FieldNames, wrap(mtype, data));
}

static void _set_enum_tag_values(Type type, const std::vector<int64_t> &values) {
    Type vatype = Types::Array(TYPE_I64, values.size());
    int64_t *pvalues = (int64_t *)malloc(get_size(vatype));
    for (size_t i = 0; i < values.size(); ++i) {
        pvalues[i] = values[i];
    }
    set_type_attrib(type, SYM_TagValues, wrap(vatype, pvalues));
}

static void _set_struct_field_types(Type type, const std::vector<Type> &types) {
    Type tatype = Types::Array(TYPE_Type, types.size());
    Type *ptypes = (Type *)malloc(get_size(tatype));
    Type oatype = Types::Array(TYPE_SizeT, types.size());
    size_t *poffsets = (size_t *)malloc(get_size(oatype));

    size_t offset = 0;
    size_t max_alignment = 1;
    size_t idx = 0;
    for (auto &element : types) {
        ptypes[idx] = element;
        size_t size = get_size(element);
        size_t alignment = get_alignment(element);
        max_alignment = std::max(max_alignment, alignment);
        offset = align(offset, alignment);
        poffsets[idx] = offset;
        offset += size;
        idx++;
    }

    set_type_attrib(type, SYM_FieldTypes, wrap(tatype, ptypes));
    set_type_attrib(type, SYM_FieldOffsets, wrap(oatype, poffsets));

    set_type_attrib(type, SYM_Size, wrap(align(offset, max_alignment)));
    set_type_attrib(type, SYM_Alignment, wrap(max_alignment));
}

static void _set_union_field_types(Type type, const std::vector<Type> &types) {
    Type tatype = Types::Array(TYPE_Type, types.size());
    Type *ptypes = (Type *)malloc(get_size(tatype));
    Type oatype = Types::Array(TYPE_SizeT, types.size());
    size_t *poffsets = (size_t *)malloc(get_size(oatype));

    size_t max_size = 0;
    size_t max_alignment = 1;
    size_t idx = 0;
    for (auto &element : types) {
        ptypes[idx] = element;
        size_t size = get_size(element);
        size_t alignment = get_alignment(element);
        max_size = std::max(max_size, size);
        max_alignment = std::max(max_alignment, alignment);
        poffsets[idx] = 0;
        idx++;
    }

    set_type_attrib(type, SYM_FieldTypes, wrap(tatype, ptypes));
    set_type_attrib(type, SYM_FieldOffsets, wrap(oatype, poffsets));

    set_type_attrib(type, SYM_Size, wrap(align(max_size, max_alignment)));
    set_type_attrib(type, SYM_Alignment, wrap(max_alignment));
}

// TYPE CONSTRUCTORS
//--------------------------------------------------------------------------

static void __new_array_type(Type dest, Type element, size_t size) {
    auto alignment = get_alignment(element);
    auto padded_size = align(get_size(element), alignment);

    set_type_attrib(dest, SYM_IsEmbedded, wrap(false));
    set_type_attrib(dest, SYM_ElementCount, wrap(size));
    set_type_attrib(dest, SYM_OP2_At, wrap<type_array_at>());
    set_type_attrib(dest, SYM_ElementType, wrap(element));
    set_type_attrib(dest, SYM_Size, wrap(padded_size * size));
    set_type_attrib(dest, SYM_Alignment, wrap(alignment));
}

static Type _new_array_type(Type element, size_t size) {
    auto dest = new_type();
    __new_array_type(dest, element, size);
    set_type_attrib(dest, SYM_Name, wrap(format("<%s %s %s>",
            get_name(element).c_str(),
            ansi(ANSI_STYLE_KEYWORD, "@").c_str(),
            ansi(ANSI_STYLE_NUMBER,
                format("%zu", size)).c_str())));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_Array));
    set_type_attrib(dest, SYM_CmpType, wrap<type_array_eq>());
    return dest;
}

static Type _new_vector_type(Type element, size_t size) {
    auto dest = new_type();
    __new_array_type(dest, element, size);
    set_type_attrib(dest, SYM_Name, wrap(format("<%s %s %s>",
            get_name(element).c_str(),
            ansi(ANSI_STYLE_KEYWORD, "x").c_str(),
            ansi(ANSI_STYLE_NUMBER,
                format("%zu", size)).c_str())));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_Vector));
    set_type_attrib(dest, SYM_CmpType, wrap<type_vector_eq>());
    return dest;
}

static Type _new_pointer_type(Type element) {
    auto dest = new_type();
    set_type_attrib(dest, SYM_IsEmbedded, wrap(true));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_Pointer));
    set_type_attrib(dest, SYM_Name, wrap(format("%s%s",
            ansi(ANSI_STYLE_KEYWORD, "&").c_str(),
            get_name(element).c_str())));
    set_type_attrib(dest, SYM_CmpType, wrap<type_pointer_eq>());
    set_type_attrib(dest, SYM_Hash, wrap<_pointer_hash>());
    set_type_attrib(dest, SYM_OP2_At, wrap<type_pointer_at>());
    set_type_attrib(dest, SYM_OP2_Join, wrap< _pointer_fwd_op2<SYM_OP2_Join, SYM_OP2_RJoin> >());
    set_type_attrib(dest, SYM_CountOf, wrap<_pointer_countof>());
    set_type_attrib(dest, SYM_Cmp, wrap<value_pointer_cmp>());
    set_type_attrib(dest, SYM_ToString, wrap<_pointer_tostring>());
    set_type_attrib(dest, SYM_ElementType, wrap(element));
    set_type_attrib(dest, SYM_Size, wrap((size_t)ffi_type_pointer.size));
    set_type_attrib(dest, SYM_Alignment, wrap((size_t)ffi_type_pointer.alignment));
    return dest;
}

static Type _new_tuple_type(std::vector<Type> types) {
    auto dest = new_type();
    std::stringstream ss;
    ss << "(";
    ss << ansi(ANSI_STYLE_KEYWORD, "tuple");
    for (auto &element : types) {
        ss << " " << get_name(element);
    }
    ss << ")";
    set_type_attrib(dest, SYM_IsEmbedded, wrap(false));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_Tuple));
    set_type_attrib(dest, SYM_Name, wrap(ss.str()));
    set_type_attrib(dest, SYM_CmpType, wrap<type_tuple_eq>());
    set_type_attrib(dest, SYM_Cmp, wrap<_tuple_cmp>());
    set_type_attrib(dest, SYM_Hash, wrap<_tuple_hash>());
    set_type_attrib(dest, SYM_OP2_At, wrap<type_tuple_at>());
    set_type_attrib(dest, SYM_CountOf, wrap<_tuple_countof>());
    set_type_attrib(dest, SYM_ToString, wrap<_tuple_tostring>());
    set_type_attrib(dest, SYM_Slice, wrap<_tuple_slice>());
    _set_struct_field_types(dest, types);
    return dest;
}

static Type _new_cfunction_type(
    Type result, Type parameters, bool vararg) {
    assert(is_tuple_type(parameters));
    auto dest = new_type();
    std::stringstream ss;
    ss << "(";
    ss << ansi(ANSI_STYLE_KEYWORD, "cfunction");
    ss << " " << get_name(result);
    ss << " " << get_name(parameters);
    ss << " " << ansi(ANSI_STYLE_KEYWORD, vararg?"true":"false") << ")";
    set_type_attrib(dest, SYM_IsEmbedded, wrap(true));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_CFunction));
    set_type_attrib(dest, SYM_Name, wrap(ss.str()));
    set_type_attrib(dest, SYM_CmpType, wrap<type_cfunction_eq>());
    set_type_attrib(dest, SYM_IsVarArg, wrap(vararg));
    set_type_attrib(dest, SYM_ReturnType, wrap(result));
    set_type_attrib(dest, SYM_ParameterTypes, wrap(parameters));
    set_type_attrib(dest, SYM_Size, wrap((size_t)ffi_type_pointer.size));
    set_type_attrib(dest, SYM_Alignment, wrap((size_t)ffi_type_pointer.alignment));
    return dest;
}

static Type _new_integer_type(size_t width, bool signed_) {
    ffi_type *itype = nullptr;
    Type dest;
    if (signed_) {
        switch (width) {
            case 8:
                dest = TYPE_I8;
                itype = &ffi_type_sint8; break;
            case 16:
                dest = TYPE_I16;
                itype = &ffi_type_sint16; break;
            case 32:
                dest = TYPE_I32;
                itype = &ffi_type_sint32; break;
            case 64:
                dest = TYPE_I64;
                itype = &ffi_type_sint64; break;
            default:
                //dest = new_type();
                error("invalid width for signed type"); break;
        }
    } else {
        switch (width) {
            case 1:
                dest = TYPE_Bool;
                itype = &ffi_type_uint8; break;
            case 8:
                dest = TYPE_U8;
                itype = &ffi_type_uint8; break;
            case 16:
                dest = TYPE_U16;
                itype = &ffi_type_uint16; break;
            case 32:
                dest = TYPE_U32;
                itype = &ffi_type_uint32; break;
            case 64:
                dest = TYPE_U64;
                itype = &ffi_type_uint64; break;
            default:
                //dest = new_type();
                error("invalid width for unsigned type"); break;
        }
    }

    set_type_attrib(dest, SYM_IsEmbedded, wrap(true));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_Integer));
    if (width == 1) {
        set_type_attrib(dest, SYM_Name, wrap(std::string("bool")));
        set_type_attrib(dest, SYM_OP1_BoolNot, wrap<_bool_not>());
    } else {
        set_type_attrib(dest, SYM_Name,
            wrap(format("%sint%zu", signed_?"":"u", width)));
    }
    set_type_attrib(dest, SYM_CmpType, wrap<type_integer_eq>());

    set_type_attrib(dest, SYM_OP1_Neg, wrap<_integer_neg>());
    set_type_attrib(dest, SYM_OP1_Rcp, wrap<_integer_rcp>());

    set_type_attrib(dest, SYM_OP2_Add, wrap<_integer_add>());
    set_type_attrib(dest, SYM_OP2_RAdd, wrap<_integer_add>());
    set_type_attrib(dest, SYM_OP2_Sub, wrap<_integer_sub>());
    set_type_attrib(dest, SYM_OP2_Mul, wrap<_integer_mul>());
    set_type_attrib(dest, SYM_OP2_RMul, wrap<_integer_mul>());
    set_type_attrib(dest, SYM_OP2_Div, wrap<_integer_div>());
    set_type_attrib(dest, SYM_OP2_FloorDiv, wrap<_integer_floordiv>());
    set_type_attrib(dest, SYM_OP2_Mod, wrap<_integer_mod>());
    set_type_attrib(dest, SYM_OP2_Pow, wrap<_integer_pow>());

    set_type_attrib(dest, SYM_OP2_And, wrap<_integer_and>());
    set_type_attrib(dest, SYM_OP2_Or, wrap<_integer_or>());
    set_type_attrib(dest, SYM_OP2_Xor, wrap<_integer_xor>());

    set_type_attrib(dest, SYM_ApplyType, wrap<_integer_value_apply_type>());
    set_type_attrib(dest, SYM_Hash, wrap<_integer_hash>());

    set_type_attrib(dest, SYM_Cmp, wrap<_integer_cmp>());

    set_type_attrib(dest, SYM_ToString, wrap<_integer_tostring>());
    set_type_attrib(dest, SYM_Width, wrap(width));
    set_type_attrib(dest, SYM_IsSigned, wrap(signed_));
    set_type_attrib(dest, SYM_Size, wrap((size_t)itype->size));
    set_type_attrib(dest, SYM_Alignment, wrap((size_t)itype->alignment));
    return dest;
}

static Type _new_real_type(size_t width) {
    ffi_type *itype = nullptr;
    Type dest;
    switch (width) {
        case 16:
            dest = TYPE_R16;
            itype = &ffi_type_uint16; break;
        case 32:
            dest = TYPE_R32;
            itype = &ffi_type_float; break;
        case 64:
            dest = TYPE_R64;
            itype = &ffi_type_double; break;
        case 80:
            dest = TYPE_R80;
            itype = &ffi_type_longdouble; break;
        default: assert(false && "invalid width"); break;
    }

    set_type_attrib(dest, SYM_Name, wrap(format("real%zu", width)));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_Real));
    set_type_attrib(dest, SYM_Cmp, wrap<_real_cmp>());
    set_type_attrib(dest, SYM_CmpType, wrap<type_real_eq>());
    set_type_attrib(dest, SYM_IsEmbedded, wrap(true));
    set_type_attrib(dest, SYM_ToString, wrap<_real_tostring>());

    set_type_attrib(dest, SYM_OP2_Add, wrap<_real_add>());
    set_type_attrib(dest, SYM_OP2_RAdd, wrap<_real_add>());
    set_type_attrib(dest, SYM_OP2_Sub, wrap<_real_sub>());
    set_type_attrib(dest, SYM_OP2_Mul, wrap<_real_mul>());
    set_type_attrib(dest, SYM_OP2_RMul, wrap<_real_mul>());
    set_type_attrib(dest, SYM_OP2_Div, wrap<_real_div>());
    set_type_attrib(dest, SYM_OP2_Pow, wrap<_real_pow>());

    set_type_attrib(dest, SYM_OP1_Neg, wrap<_real_neg>());
    set_type_attrib(dest, SYM_OP1_Rcp, wrap<_real_rcp>());

    set_type_attrib(dest, SYM_Hash, wrap<_real_hash>());

    set_type_attrib(dest, SYM_Width, wrap(width));
    set_type_attrib(dest, SYM_Size, wrap((size_t)itype->size));
    set_type_attrib(dest, SYM_Alignment, wrap((size_t)itype->alignment));
    return dest;
}

static Type _new_qualifier_type(Type tag, Type element) {
    auto dest = new_type();

    set_type_attrib(dest, SYM_Name, wrap(format("<qualifier %s %s>",
        get_name(tag).c_str(),
        get_name(element).c_str())));
    set_type_attrib(dest, SYM_SuperType, wrap(tag));
    set_type_attrib(dest, SYM_IsEmbedded, wrap(is_embedded(element)));
    set_type_attrib(dest, SYM_Size, wrap(get_size(element)));
    set_type_attrib(dest, SYM_Alignment, wrap(get_alignment(element)));
    set_type_attrib(dest, SYM_ElementType, wrap(element));
    set_type_attrib(dest, SYM_CmpType, wrap<type_qualifier_eq>());
    set_type_attrib(dest, SYM_ApplyType, wrap<_qualified_type_apply_type>());
    return dest;
}

static void _init_struct_type(
    Type dest, const std::string &name, bool builtin = false) {
    if (builtin) {
        set_type_attrib(dest, SYM_Name, wrap(name));
    } else {
        std::stringstream ss;
        ss << "(";
        ss << ansi(ANSI_STYLE_KEYWORD, "struct");
        ss << " " << quoteReprString(name);
        ss << " " << dest.value();
        ss << ")";
        set_type_attrib(dest, SYM_Name, wrap(ss.str()));
    }
    set_type_attrib(dest, SYM_IsEmbedded, wrap(false));
    set_type_attrib(dest, SYM_CmpType, wrap<type_struct_eq>());
    set_type_attrib(dest, SYM_OP2_At, wrap<type_struct_at>());
    set_type_attrib(dest, SYM_Cmp, wrap<_struct_cmp>());
    set_type_attrib(dest, SYM_CountOf, wrap<_tuple_countof>());
}

static void _init_supertype(Type dest, const std::string &name) {
    _init_struct_type(dest, name, true);
}

static void _init_tag_type(Type dest, const Symbol &name) {
    set_type_attrib(dest, SYM_Name, wrap(format("<%s %s>",
            ansi(ANSI_STYLE_KEYWORD, "tag").c_str(),
            get_symbol_name(name).c_str())));
    set_type_attrib(dest, SYM_SuperType, wrap(TYPE_Tag));
    set_type_attrib(dest, SYM_ApplyType, wrap<_qualifier_apply_type>());
    set_type_attrib(dest, SYM_CmpType, wrap<type_tag_eq>());
}

static Type _init_object_type(Type dest, const std::string &name) {
    set_type_attrib(dest, SYM_IsEmbedded, wrap(true));
    set_type_attrib(dest, SYM_Name, wrap(name));
    set_type_attrib(dest, SYM_CmpType, wrap<type_struct_eq>());
    set_type_attrib(dest, SYM_OP2_At, wrap<type_struct_at>());
    set_type_attrib(dest, SYM_Cmp, wrap<_struct_cmp>());
    set_type_attrib(dest, SYM_CountOf, wrap<_tuple_countof>());
    set_type_attrib(dest, SYM_Size, wrap((size_t)ffi_type_pointer.size));
    set_type_attrib(dest, SYM_Alignment, wrap((size_t)ffi_type_pointer.alignment));
    set_type_attrib(dest, SYM_Hash, wrap<_embedded_hash>());
    set_type_attrib(dest, SYM_ToString, wrap<_pointer_tostring>());
    return dest;
}

namespace Types {

static Type Struct(const std::string &name, bool builtin) {
    auto dest = new_type();
    _init_struct_type(dest, name, builtin);
    return dest;
}

static Type Enum(const std::string &name) {
    auto dest = new_type();
    std::stringstream ss;
    ss << "(";
    ss << ansi(ANSI_STYLE_KEYWORD, "enum");
    ss << " " << quoteReprString(name);
    ss << ")";
    set_type_attrib(dest, SYM_IsEmbedded, wrap(true));
    set_type_attrib(dest, SYM_Name, wrap(ss.str()));
    set_type_attrib(dest, SYM_CmpType, wrap<type_enum_eq>());
    return dest;
}

static Type Tag(const Symbol &name) {
    auto dest = new_type();
    _init_tag_type(dest, name);
    return dest;
}

static Type Quote(Type element) {
    return Types::Qualifier(TYPE_Quote, element);
}

static Type Splice(Type element) {
    return Types::Qualifier(TYPE_Splice, element);
}

static Type Macro(Type element) {
    return Types::Qualifier(TYPE_Macro, element);
}

} // namespace Types

// TYPE INIT
//--------------------------------------------------------------------------

template<typename T>
static void _init_pod_size_alignment(Type dest) {
    struct _alignment { char c; T s; };
    set_type_attrib(dest, SYM_Size, wrap((size_t)sizeof(T)));
    set_type_attrib(dest, SYM_Alignment, wrap((size_t)offsetof(_alignment, s)));
}

static void initTypes() {
    const_none = make_any(TYPE_Void);
    const_none.ptr = nullptr;

    Type dest;

    dest = TYPE_BuiltinFlow;
    _init_object_type(dest, "builtin");
    set_type_attrib(dest, SYM_ToString, wrap<_named_ptr_tostring>());

    dest = TYPE_Type;
    _init_object_type(dest, "type");
    set_type_attrib(dest, SYM_ToString, wrap<_type_tostring>());
    set_type_attrib(dest, SYM_Cmp, wrap<_typeref_cmp>());

    dest = TYPE_Array;
    _init_supertype(dest, "array");
    set_type_attrib(dest, SYM_ApplyType, wrap<_array_apply_type>());

    dest = TYPE_Vector;
    _init_supertype(dest, "vector");
    set_type_attrib(dest, SYM_ApplyType, wrap<_vector_apply_type>());

    dest = TYPE_Tuple;
    _init_supertype(dest, "tuple");
    set_type_attrib(dest, SYM_ApplyType, wrap<_tuple_apply_type>());

    dest = TYPE_Pointer;
    _init_supertype(dest, "pointer");
    set_type_attrib(dest, SYM_ApplyType, wrap<_pointer_apply_type>());

    dest = TYPE_Tag;
    _init_supertype(dest, "tag");
    set_type_attrib(dest, SYM_ApplyType, wrap<_tag_apply_type>());

    dest = TYPE_Qualifier;
    _init_supertype(dest, "qualifier");

    dest = TYPE_Splice;
    _init_tag_type(dest, get_symbol("splice"));

    dest = TYPE_Quote;
    _init_tag_type(dest, get_symbol("quote"));

    dest = TYPE_Macro;
    _init_tag_type(dest, get_symbol("macro"));

    dest = TYPE_CFunction;
    _init_supertype(dest, "cfunction");
    set_type_attrib(dest, SYM_ApplyType, wrap<_cfunction_apply_type>());

    dest = TYPE_Integer;
    _init_supertype(dest, "integer");
    set_type_attrib(dest, SYM_ApplyType, wrap<_integer_apply_type>());

    dest = TYPE_Real;
    _init_supertype(dest, "real");
    set_type_attrib(dest, SYM_ApplyType, wrap<_real_apply_type>());

    dest = TYPE_Struct;
    _init_supertype(dest, "struct");
    set_type_attrib(dest, SYM_ApplyType, wrap<_struct_apply_type>());

    dest = TYPE_Enum;
    _init_supertype(dest, "enum");
    //set_type_attrib(dest, SYM_ApplyType, wrap<_enum_apply_type>());

    dest = TYPE_Any;
    _init_struct_type(dest, "any", true);

    dest = TYPE_AnchorRef;
    _init_struct_type(dest, "anchor", true);

    dest = TYPE_Void;
    _init_struct_type(dest, "void", true);
    set_type_attrib(dest, SYM_Size, wrap((size_t)0));
    set_type_attrib(dest, SYM_Alignment, wrap((size_t)1));
    set_type_attrib(dest, SYM_Cmp, wrap<value_none_cmp>());
    set_type_attrib(dest, SYM_ToString, wrap<_none_tostring>());

    dest = Types::Integer(1, false); assert(dest == TYPE_Bool);

    dest = Types::Integer(8, true); assert(dest == TYPE_I8);
    dest = Types::Integer(16, true); assert(dest == TYPE_I16);
    dest = Types::Integer(32, true); assert(dest == TYPE_I32);
    dest = Types::Integer(64, true); assert(dest == TYPE_I64);

    dest = Types::Integer(8, false); assert(dest == TYPE_U8);
    dest = Types::Integer(16, false); assert(dest == TYPE_U16);
    dest = Types::Integer(32, false); assert(dest == TYPE_U32);
    dest = Types::Integer(64, false); assert(dest == TYPE_U64);

    assert(sizeof(size_t) == 8);

    dest = Types::Real(16); assert(dest == TYPE_R16);
    dest = Types::Real(32); assert(dest == TYPE_R32);
    dest = Types::Real(64); assert(dest == TYPE_R64);
    dest = Types::Real(80); assert(dest == TYPE_R80);

    dest = TYPE_String;
    _init_struct_type(dest, "string", true);
    set_type_attrib(dest, SYM_ToString, wrap<_string_tostring>());
    set_type_attrib(dest, SYM_CountOf, wrap<_string_countof>());
    set_type_attrib(dest, SYM_Slice, wrap<_string_slice>());
    set_type_attrib(dest, SYM_Hash, wrap<_string_hash>());
    set_type_attrib(dest, SYM_OP2_At, wrap<_string_at>());
    set_type_attrib(dest, SYM_OP2_Join, wrap<_string_join>());
    set_type_attrib(dest, SYM_Cmp, wrap<value_string_cmp>());
    set_type_attrib(dest, SYM_ApplyType, wrap<_string_apply_type>());
    _init_pod_size_alignment<String>(dest);

    dest = TYPE_Symbol;
    _init_struct_type(dest, "symbol", true);
    set_type_attrib(dest, SYM_IsEmbedded, wrap(true));
    set_type_attrib(dest, SYM_ToString, wrap<_symbol_tostring>());
    set_type_attrib(dest, SYM_Cmp, wrap<value_symbol_cmp>());
    set_type_attrib(dest, SYM_Hash, wrap<_symbol_hash>());
    set_type_attrib(dest, SYM_ApplyType, wrap<_symbol_apply_type>());
    _init_pod_size_alignment<Symbol>(dest);

    dest = TYPE_List;
    _init_object_type(dest, "list");
    set_type_attrib(dest, SYM_OP2_At, wrap<type_list_at>());
    set_type_attrib(dest, SYM_OP2_Join, wrap<_list_join>());
    set_type_attrib(dest, SYM_ApplyType, wrap<_list_apply_type>());
    set_type_attrib(dest, SYM_ToString, wrap<_list_tostring>());
    set_type_attrib(dest, SYM_CountOf, wrap<_list_countof>());
    set_type_attrib(dest, SYM_Slice, wrap<_list_slice>());
    set_type_attrib(dest, SYM_Cmp, wrap<_list_cmp>());

    dest = TYPE_Table;
    _init_object_type(dest, "table");
    set_type_attrib(dest, SYM_OP2_At, wrap<type_table_at>());
    set_type_attrib(dest, SYM_OP2_Join, wrap<_table_join>());

    dest = TYPE_Flow;
    _init_object_type(dest, "flow");
    set_type_attrib(dest, SYM_ToString, wrap< _named_object_tostring<Flow> >());

    dest = TYPE_SpecialForm;
    _init_object_type(dest, "builtin-form");
    set_type_attrib(dest, SYM_ToString, wrap<_named_ptr_tostring>());

    dest = TYPE_BuiltinMacro;
    _init_object_type(dest, "builtin-macro");
    set_type_attrib(dest, SYM_ToString, wrap<_named_ptr_tostring>());

    dest = TYPE_Frame;
    _init_object_type(dest, "frame");

    {
        dest = TYPE_Parameter;
        _init_object_type(dest, "parameter");
        set_type_attrib(dest, SYM_ToString, wrap<_parameter_tostring>());
        set_type_attrib(dest, SYM_ApplyType, wrap<_parameter_apply_type>());

        std::vector<Type> types = { TYPE_Flow, TYPE_SizeT, TYPE_Type, TYPE_Symbol, TYPE_Bool };
        std::vector<std::string> names = { "flow", "index", "type", "name", "vararg" };
        _set_struct_field_types(dest, types);
        _set_field_names(dest, names);

        _init_pod_size_alignment<void *>(dest);
    }

    {
        dest = TYPE_Closure;
        _init_object_type(dest, "closure");
        set_type_attrib(dest, SYM_ToString, wrap<_closure_tostring>());
        set_type_attrib(dest, SYM_ApplyType, wrap<_closure_apply_type>());

        std::vector<Type> types = { TYPE_Flow, TYPE_Frame };
        std::vector<std::string> names = { "entry", "frame" };
        _set_struct_field_types(dest, types);
        _set_field_names(dest, names);

        _init_pod_size_alignment<void *>(dest);
    }

    dest = TYPE_Rawstring = Types::Pointer(TYPE_I8);
    set_type_attrib(dest, SYM_ApplyType, wrap<_rawstring_apply_type>());

    dest = TYPE_PVoid = Types::Pointer(TYPE_Void);

    dest = TYPE_ListTableTuple = Types::Tuple({TYPE_List, TYPE_Table});
}

static void initConstants() {}

//------------------------------------------------------------------------------
// C module utility functions
//------------------------------------------------------------------------------

static void *global_c_namespace = nullptr;

//------------------------------------------------------------------------------
// C BRIDGE (CLANG)
//------------------------------------------------------------------------------

class CVisitor : public clang::RecursiveASTVisitor<CVisitor> {
public:
    Table *dest;
    clang::ASTContext *Context;
    std::unordered_map<clang::RecordDecl *, bool> record_defined;
    std::unordered_map<clang::EnumDecl *, bool> enum_defined;
    std::unordered_map<const char *, char *> path_cache;
    std::unordered_map<std::string, Type> named_structs;
    std::unordered_map<std::string, Type> named_enums;
    std::unordered_map<std::string, Type> typedefs;

    CVisitor() : Context(NULL) {
    }

    Anchor anchorFromLocation(clang::SourceLocation loc) {
        Anchor anchor;
        auto &SM = Context->getSourceManager();

        auto PLoc = SM.getPresumedLoc(loc);

        if (PLoc.isValid()) {
            auto fname = PLoc.getFilename();
            // get resident path by pointer
            char *rpath = path_cache[fname];
            if (!rpath) {
                rpath = strdup(fname);
                path_cache[fname] = rpath;
            }

            anchor.path = rpath;
            anchor.lineno = PLoc.getLine();
            anchor.column = PLoc.getColumn();
        }

        return anchor;
    }

    /*
    static ValueRef fixAnchor(Anchor &anchor, ValueRef value) {
        if (value && !value->anchor.isValid() && anchor.isValid()) {
            value->anchor = anchor;
            if (auto ptr = llvm::dyn_cast<Pointer>(value)) {
                auto elem = ptr->getAt();
                while (elem) {
                    fixAnchor(anchor, elem);
                    elem = elem->getNext();
                }
            }
            fixAnchor(anchor, value->getNext());
        }
        return value;
    }
    */

    void SetContext(clang::ASTContext * ctx, Table *dest_) {
        Context = ctx;
        dest = dest_;
    }

    void GetFields(Type struct_type, clang::RecordDecl * rd) {
        //auto &rl = Context->getASTRecordLayout(rd);

        std::vector<std::string> names;
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
                it->isAnonymousStructOrUnion()?"":
                                    declname.getAsString());
            types.push_back(fieldtype);
            //anchors->push_back(anchorFromLocation(it->getSourceRange().getBegin()));
        }

        _set_field_names(struct_type, names);
        if (rd->isUnion()) {
            _set_union_field_types(struct_type, types);
        } else {
            _set_struct_field_types(struct_type, types);
        }
    }

    Type TranslateRecord(clang::RecordDecl *rd) {
        if (!rd->isStruct() && !rd->isUnion())
            error("can not translate record: is neither struct nor union");

        assert(rd->getName().data());
        std::string name = rd->getName().data();

        Type struct_type;
        if (name.size() && named_structs.count(name)) {
            struct_type = named_structs[name];
        } else {
            struct_type = Types::Struct(name, false);
            if (name.size()) {
                named_structs[name] = struct_type;
            }
        }

        clang::RecordDecl * defn = rd->getDefinition();
        if (defn && !record_defined[rd]) {
            record_defined[rd] = true;
            /*
            Anchor anchor = anchorFromLocation(rd->getSourceRange().getBegin());
            set_key(*struct_type, "anchor",
                pointer(Types::AnchorRef, new Anchor(anchor)));*/

            GetFields(struct_type, defn);

            auto &rl = Context->getASTRecordLayout(rd);
            auto align = rl.getAlignment();
            auto size = rl.getSize();
            // should match
            assert ((size_t)align.getQuantity() == get_alignment(struct_type));
            assert ((size_t)size.getQuantity() == get_size(struct_type));

            // todo: make sure these fit
            // align.getQuantity()
            // size.getQuantity()

        }

        return struct_type;
    }

    Type TranslateEnum(clang::EnumDecl *ed) {
        std::string name = ed->getName();

        Type enum_type;
        if (name.size() && named_enums.count(name)) {
            enum_type = named_enums[name];
        } else {
            enum_type = Types::Enum(name);
            if (name.size()) {
                named_enums[name] = enum_type;
            }
        }

        clang::EnumDecl * defn = ed->getDefinition();
        if (defn && !enum_defined[ed]) {
            enum_defined[ed] = true;
            /*
            set_key(*enum_type, "anchor",
                pointer(Types::AnchorRef,
                    new Anchor(
                        anchorFromLocation(
                            ed->getIntegerTypeRange().getBegin()))));
            */

            auto tag_type = TranslateType(ed->getIntegerType());
            set_type_attrib(enum_type, SYM_TagType, wrap(tag_type));

            std::vector<std::string> names;
            std::vector<int64_t> tags;
            std::vector<Type> types;
            //auto anchors = new std::vector<Anchor>();

            for (auto it : ed->enumerators()) {
                //Anchor anchor = anchorFromLocation(it->getSourceRange().getBegin());
                auto &val = it->getInitVal();

                auto name = it->getName().data();
                auto value = val.getExtValue();

                set_key(*dest, get_symbol(name), integer(tag_type, value));

                names.push_back(name);
                tags.push_back(value);
                types.push_back(TYPE_Void);
                //anchors->push_back(anchor);
            }

            _set_enum_tag_values(enum_type, tags);
            _set_union_field_types(enum_type, types);
            _set_field_names(enum_type, names);
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
            auto it = typedefs.find(td->getName().data());
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
            case clang::BuiltinType::Void: {
                return TYPE_Void;
            } break;
            case clang::BuiltinType::Bool: {
                return TYPE_Bool;
            } break;
            case clang::BuiltinType::Char_S:
            case clang::BuiltinType::Char_U:
            case clang::BuiltinType::SChar:
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
                return Types::Integer(sz, !Ty->isUnsignedIntegerType());
            } break;
            case clang::BuiltinType::Half: {
                return TYPE_R16;
            } break;
            case clang::BuiltinType::Float: {
                return TYPE_R32;
            } break;
            case clang::BuiltinType::Double: {
                return TYPE_R64;
            } break;
            case clang::BuiltinType::LongDouble: {
                return TYPE_R80;
            } break;
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
            Type pointee = TranslateType(ETy);
            return Types::Pointer(pointee);
        } break;
        case clang::Type::VariableArray:
        case clang::Type::IncompleteArray:
            break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            Type at = TranslateType(ATy->getElementType());
            int sz = ATy->getSize().getZExtValue();
            return Types::Array(at, sz);
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
            const clang::VectorType *VT = cast<clang::VectorType>(T);
            Type at = TranslateType(VT->getElementType());
            int n = VT->getNumElements();
            return Types::Vector(at, n);
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
        error("cannot convert type: %s (%s)\n",
            T.getAsString().c_str(),
            Ty->getTypeClassName());
        return TYPE_Void;
    }

    Type TranslateFuncType(const clang::FunctionType * f) {

        clang::QualType RT = f->getReturnType();

        Type returntype = TranslateType(RT);

        bool vararg = false;

        const clang::FunctionProtoType * proto = f->getAs<clang::FunctionProtoType>();
        std::vector<Type> argtypes;
        if(proto) {
            vararg = proto->isVariadic();
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                clang::QualType PT = proto->getParamType(i);
                Type paramtype = TranslateType(PT);
                argtypes.push_back(paramtype);
            }
        }

        return Types::CFunction(returntype,
            Types::Tuple(argtypes), vararg);
    }

    void exportType(const std::string &name, Type type) {
        set_key(*dest, get_symbol(name), wrap(type));
    }

    void exportExternal(const std::string &name, Type type,
        const Anchor &anchor) {
        auto sym = get_symbol(name);
        Any args[2] = {wrap_symbol(sym), wrap(type)};
        set_key(*dest, sym, wrap(args, 2));
    }

    bool TraverseRecordDecl(clang::RecordDecl *rd) {
        if (rd->isFreeStanding()) {
            auto type = TranslateRecord(rd);
            auto name = get_name(type);
            if (name.size()) {
                exportType(name, type);
            }
        }
        return true;
    }

    bool TraverseEnumDecl(clang::EnumDecl *ed) {
        if (ed->isFreeStanding()) {
            auto type = TranslateEnum(ed);
            auto name = get_name(type);
            if (name.size()) {
                exportType(name, type);
            }
        }
        return true;
    }

    bool TraverseVarDecl(clang::VarDecl *vd) {
        if (vd->isExternC()) {
            Anchor anchor = anchorFromLocation(vd->getSourceRange().getBegin());

            Type type = TranslateType(vd->getType());

            exportExternal(vd->getName().data(), type, anchor);
        }

        return true;

    }

    bool TraverseTypedefDecl(clang::TypedefDecl *td) {

        //Anchor anchor = anchorFromLocation(td->getSourceRange().getBegin());

        Type type = TranslateType(td->getUnderlyingType());

        typedefs[td->getName().data()] = type;
        exportType(td->getName().data(), type);

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

        Anchor anchor = anchorFromLocation(f->getSourceRange().getBegin());

        exportExternal(FuncName.c_str(), functype, anchor);

        return true;
    }
};

class CodeGenProxy : public clang::ASTConsumer {
public:
    Table *dest;
    CVisitor visitor;

    CodeGenProxy(Table *dest_) : dest(dest_) {}
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
    Table *dest;

    BangEmitLLVMOnlyAction(Table *dest_) :
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

static void initCCompiler() {
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

static Table *importCModule (
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


    auto result = new_table();

    // Create and execute the frontend to generate an LLVM bitcode module.
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction(result));
    if (compiler.ExecuteAction(*Act)) {
        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        //llvm_modules.push_back(M);
        LLVMAddModule(ee, M);
        return result;
    } else {
        error("compilation failed");
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// TRANSLATION
//------------------------------------------------------------------------------

static Table *new_scope() {
    return new_table();
}

static Table *new_scope(const Table *scope) {
    assert(scope);
    auto subscope = new_table();
    set_key(*subscope, SYM_Parent, wrap_ptr(TYPE_Table, scope));
    return subscope;
}

static void setLocal(Table *scope, const Symbol &name, const Any &value) {
    assert(scope);
    set_key(*scope, name, value);
}

static void setLocalString(Table *scope, const std::string &name, const Any &value) {
    setLocal(scope, get_symbol(name), value);
}

template<BuiltinFlowFunction func>
static void setBuiltinMacro(Table *scope, const std::string &name) {
    assert(scope);
    auto sym = get_symbol(name);
    set_ptr_symbol((void *)func, sym);
    setLocal(scope, sym, macro(wrap_ptr(TYPE_BuiltinFlow, (void *)func)));
}

template<SpecialFormFunction func>
static void setBuiltin(Table *scope, const std::string &name) {
    assert(scope);
    auto sym = get_symbol(name);
    set_ptr_symbol((void *)func, sym);
    setLocal(scope, sym,
        wrap_ptr(TYPE_SpecialForm, (void *)func));
}

template<BuiltinFlowFunction func>
static void setBuiltin(Table *scope, const std::string &name) {
    assert(scope);
    auto sym = get_symbol(name);
    set_ptr_symbol((void *)func, sym);
    setLocal(scope, sym,
        wrap_ptr(TYPE_BuiltinFlow, (void *)func));
}

template<BuiltinFunction func>
static void setBuiltin(Table *scope, const std::string &name) {
    setBuiltin< builtin_call<func> >(scope, name);
}

/*
static bool isLocal(StructValue *scope, const std::string &name) {
    assert(scope);
    size_t idx = scope->struct_type->getFieldIndex(name);
    if (idx == (size_t)-1) return false;
    return true;
}
*/

static const Table *getParent(const Table *scope) {
    auto parent = get_key(*scope, SYM_Parent, const_none);
    if (parent.type == TYPE_Table)
        return parent.table;
    return nullptr;
}

static bool hasLocal(const Table *scope, const Symbol &name) {
    assert(scope);
    while (scope) {
        if (has_key(*scope, name)) return true;
        scope = getParent(scope);
    }
    return false;
}

static Any getLocal(const Table *scope, const Symbol &name) {
    assert(scope);
    while (scope) {
        auto result = get_key(*scope, name, const_none);
        if (result.type != TYPE_Void)
            return result;
        scope = getParent(scope);
    }
    return const_none;
}

static Any getLocalString(const Table *scope, const std::string &name) {
    return getLocal(scope, get_symbol(name));
}

//------------------------------------------------------------------------------

static bool isSymbol (const Any &expr, const Symbol &sym) {
    if (expr.type == TYPE_Symbol) {
        return extract_symbol(expr) == sym;
    }
    return false;
}

//------------------------------------------------------------------------------
// BUILTINS
//------------------------------------------------------------------------------

static const Table *globals = nullptr;

static Any builtin_exit(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, 1);
    int exit_code = 0;
    if (argcount)
        exit_code = extract_integer(args[0]);
    exit(exit_code);
    return const_none;
}

static Any builtin_escape(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    Any result = args[0];
    result.type = Types::Quote(result.type);
    return result;
}

static Any builtin_print(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, -1);
    for (size_t i = 0; i < argcount; ++i) {
        if (i != 0)
            std::cout << " ";
        auto &arg = args[i];
        std::cout << get_string(arg);
    }
    std::cout << "\n";
    return const_none;
}

static Any builtin_globals(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, 0);
    return wrap(globals);
}

static Any builtin_countof(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(countof(args[0]));
}

static Any builtin_sizeof(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(get_size(extract_type(args[0])));
}

static Any builtin_slice(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 3);
    int64_t i0;
    int64_t i1;
    int64_t l = (int64_t)countof(args[0]);
    if (!is_integer_type(args[1].type)) {
        error("integer expected");
    }
    i0 = extract_integer(args[1]);
    if (i0 < 0)
        i0 += l;
    i0 = std::min(std::max(i0, (int64_t)0), l);
    if (argcount > 2) {
        if (!is_integer_type(args[2].type)) {
            error("integer expected");
        }
        i1 = extract_integer(args[2]);
        if (i1 < 0)
            i1 += l;
        i1 = std::min(std::max(i1, i0), l);
    } else {
        i1 = l;
    }

    return slice(args[0], (size_t)i0, (size_t)i1);
}

static B_FUNC(builtin_branch) {
    builtin_checkparams(B_RCOUNT(S), 4, 4, 1);
    B_CALL(S,
        B_GETCONT(S),
        B_GETARG(S, extract_bool(B_GETARG(S, 0))?1:2));
}

static Any quote(const Any &value) {
    Any result = value;
    result.type = Types::Quote(result.type);
    return result;
}

static Any unquote(const Any &value) {
    assert(is_quote_type(value.type));
    Any result = value;
    result.type = get_element_type(value.type);
    return result;
}

static B_FUNC(builtin_bind) {
    builtin_checkparams(B_RCOUNT(S), 3, 3, 1);
    auto arg0 = B_GETARG(S, 0);
    if (is_quote_type(arg0.type))
        arg0 = unquote(arg0);
    auto param = extract_parameter(arg0);
    if (!param->parent)
        error("can't rebind unbound parameter");
    B_GETFRAME(S)->rebind(param->parent, param->index, B_GETARG(S, 1));
    B_CALL(S,
        const_none,
        B_GETCONT(S),
        const_none);
}

static Any builtin_repr(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(formatValue(args[0], 0, false));
}

static Any builtin_hash(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(hash(args[0]));
}

static Any builtin_prompt(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 2);
    auto s = extract_string(args[0]);
    if (argcount > 1) {
        auto pre = extract_string(args[1]);
        linenoisePreloadBuffer(pre.c_str());
    }
    char *r = linenoise(s.c_str());
    if (!r) return const_none;
    linenoiseHistoryAdd(r);
    return wrap(TYPE_String, alloc_slice<char>(r, strlen(r)));
}

static Any builtin_bitcast(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto type = extract_type(args[0]);
    Any value = args[1];
    if (is_embedded(type) != is_embedded(value.type))
        error("cannot bitcast %s to %s: incompatible storage method.",
            get_name(value.type).c_str(), get_name(type).c_str());
    else if (get_size(type) > get_size(value.type))
        error("cannot bitcast %s to %s: target type size too large.",
            get_name(value.type).c_str(), get_name(type).c_str());
    else if ((get_alignment(value.type) % get_alignment(type)) != 0)
        error("cannot bitcast %s to %s: incompatible alignment.",
            get_name(value.type).c_str(), get_name(type).c_str());
    value.type = type;
    return value;
}

static Any builtin_element_type(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(get_element_type(extract_type(args[0])));
}

static Any builtin_dump(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    printValue(args[0], 0, true);
    return args[0];
}

static Any builtin_find_scope_symbol(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 3);

    auto scope = extract_table(args[0]);
    auto key = args[1];

    while (scope) {
        auto result = get_key(*scope, key, const_none);
        if (result.type != TYPE_Void)
            return result;
        scope = getParent(scope);
    }

    if (argcount == 3)
        return args[2];
    else
        return const_none;
}

static Any builtin_tupleof(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, -1);
    return wrap(args, argcount);
}

static Any builtin_arrayof(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, -1);
    auto etype = extract_type(args[0]);
    return wrap_array(etype, &args[1], argcount - 1);
}

static Any builtin_cons(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto &at = args[0];
    auto next = args[1];
    verifyValueKind(TYPE_List, next);
    return wrap(List::create(at, next.list /*, get_anchor(handler_frame)*/));
}

static Any builtin_structof(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, -1);
    std::vector<Symbol> names;
    names.resize(argcount);
    std::vector<Any> values;
    values.resize(argcount);
    for (size_t i = 0; i < argcount; ++i) {
        auto pair = extract_tuple(args[i]);
        if (pair.size() != 2)
            error("tuple must have exactly two elements");
        names[i] = extract_symbol(pair[0]);
        values[i] = pair[1];
    }

    Any t = wrap(values);

    auto struct_type = Types::Struct("");
    set_type_attrib(struct_type, SYM_FieldTypes,
        get_type_attrib(t.type, SYM_FieldTypes, const_none));
    set_type_attrib(struct_type, SYM_FieldOffsets,
        get_type_attrib(t.type, SYM_FieldOffsets, const_none));
    set_type_attrib(struct_type, SYM_Size,
        get_type_attrib(t.type, SYM_Size, const_none));
    set_type_attrib(struct_type, SYM_Alignment,
        get_type_attrib(t.type, SYM_Alignment, const_none));
    _set_field_names(struct_type, names);

    return wrap(struct_type, t.ptr);
}

static Any builtin_tableof(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 0, -1);

    auto t = new_table();

    for (size_t i = 0; i < argcount; ++i) {
        auto pair = extract_tuple(args[i]);
        if (pair.size() != 2)
            error("tuple must have exactly two elements");
        set_key(*t, pair[0], pair[1]);
    }

    return wrap(t);
}

static Any builtin_set_key(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 3);

    auto t = const_cast<Table *>(extract_table(args[0]));
    if (argcount == 2) {
        auto pair = extract_tuple(args[1]);
        if (pair.size() != 2)
            error("tuple must have exactly two elements");
        set_key(*t, pair[0], pair[1]);
        return const_none;
    } else {
        set_key(*t, args[1], args[2]);
        return const_none;
    }
}

static Any builtin_next_key(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);

    auto t = extract_table(args[0]);
    Table::map_type::const_iterator it;
    if (is_none_type(args[1].type)) {
        it = t->_.begin();
    } else {
        it = t->_.find(args[1]);
        ++it;
    }
    if (it == t->_.end())
        return const_none;
    std::vector<Any> result = { it->first, it->second };
    return wrap(result);
}

static Any builtin_block_scope_macro(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    verifyValueKind(TYPE_Closure, args[0]);
    return macro(args[0]);
}

static Any builtin_typeof(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return wrap(args[0].type);
}

static Any builtin_error(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    std::string msg = extract_string(args[0]);
    error("%s", msg.c_str());
    return const_none;
}

static Any builtin_raise(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    throw args[0];
    return const_none;
}

// (parse-c const-path (tupleof const-string ...) [string])
static Any builtin_parse_c(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 3);
    std::string path = extract_string(args[0]);
    auto compile_args = extract_tuple(args[1]);
    std::vector<std::string> cargs;
    for (size_t i = 0; i < compile_args.size(); ++i) {
        cargs.push_back(extract_string(compile_args[i]));
    }
    const char *buffer = nullptr;
    if (argcount == 3) {
        buffer = resident_c_str(extract_string(args[2]));
    }
    return wrap_ptr(TYPE_Table, importCModule(path, cargs, buffer));
}

static Any builtin_external(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto sym = extract_symbol(args[0]);
    auto type = extract_type(args[1]);

    auto name = get_symbol_name(sym).c_str();

    void *ptr = nullptr;
    ptr = reinterpret_cast<void *>(LLVMGetGlobalValueAddress(ee, name));
    if (!ptr) {
        ptr = dlsym(global_c_namespace, name);
    }
    if (!ptr) return const_none;
    if (is_cfunction_type(type)) {
        return wrap_ptr(Types::Pointer(type), ptr);
    } else {
        return wrap(type, ptr);
    }
}

template<BuiltinFunction F>
static Any builtin_variadic_ltr(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, -1);

    Any result = F(args, 2);
    for (size_t i = 2; i < argcount; ++i) {
        Any subargs[] = { result, args[i] };
        result = F(subargs, 2);
    }
    return result;
}

template<BuiltinFunction F>
static Any builtin_variadic_rtl(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, -1);
    Any result = args[argcount - 1];
    for (size_t i = 2; i <= argcount; ++i) {
        Any subargs[] = {args[argcount - i], result};
        result = F(subargs, 2);
    }
    return result;
}

template<Any (*F)(const Any &)>
static Any builtin_unary_op(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    return F(args[0]);
}

template<Any (*F1)(const Any &), Any (*F2)(const Any &, const Any &)>
static Any builtin_unary_binary_op(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 2);
    if (argcount == 1) {
        return F1(args[0]);
    } else {
        return F2(args[0], args[1]);
    }
}

template<Any (*F)(const Any &, const Any &)>
static Any builtin_binary_op(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    return F(args[0], args[1]);
}

template<bool (*F)(const Any &, const Any &)>
static Any builtin_bool_binary_op(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    return wrap(F(args[0], args[1]));
}

static Any builtin_flow_parameter_count(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto flow = extract_flow(args[0]);
    return wrap(flow->parameters.size());
}

static Any builtin_flow_parameter(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto flow = extract_flow(args[0]);
    auto index = extract_integer(args[1]);
    return wrap(flow->parameters[index]);
}

static Any builtin_flow_argument_count(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto flow = extract_flow(args[0]);
    return wrap(flow->arguments.size());
}

static Any builtin_flow_argument(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto flow = extract_flow(args[0]);
    auto index = extract_integer(args[1]);
    return flow->arguments[index];
}

static Any builtin_flow_name(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto flow = extract_flow(args[0]);
    return wrap_symbol(flow->name);
}

static Any builtin_flow_id(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto flow = extract_flow(args[0]);
    return wrap(flow->getUID());
}

static Any builtin_frame_eval(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 3, 3);
    auto frame = extract_frame(args[0]);
    auto argindex = extract_integer(args[1]);
    return evaluate(argindex, frame, args[2]);
}

static Any builtin_va_count(const Any *args, size_t argcount) {
    return wrap(argcount);
}

static Any builtin_va_arg(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, -1);
    auto idx = extract_integer(args[0]) + 1;
    if ((size_t)idx >= argcount)
        return const_none;
    return args[idx];
}

//------------------------------------------------------------------------------
// MACRO EXPANDER
//------------------------------------------------------------------------------

static bool verifyListParameterCount (const List *expr,
    int mincount, int maxcount) {
    if ((mincount <= 0) && (maxcount == -1))
        return true;
    int argcount = (int)getSize(expr) - 1;

    if ((maxcount >= 0) && (argcount > maxcount)) {
        error("excess argument. At most %i arguments expected", maxcount);
        return false;
    }
    if ((mincount >= 0) && (argcount < mincount)) {
        error("at least %i arguments expected", mincount);
        return false;
    }
    return true;
}

static bool verifyAtParameterCount (const List *topit,
    int mincount, int maxcount) {
    assert(topit);
    auto val = topit->at;
    verifyValueKind(TYPE_List, val);
    return verifyListParameterCount(val.list, mincount, maxcount);
}

//------------------------------------------------------------------------------

// new descending approach for compiler to optimize tail calls:
// 1. each function is called with a flow node as target argument; it represents
//    a continuation that should be called with the resulting value.

static Cursor expand (const Table *env, const List *topit);
static Any compile (const Any &expr, const Any &dest);

static Any toparameter (Table *env, const Symbol &key) {
    auto bp = wrap(Parameter::create(key));
    setLocal(env, key, bp);
    return bp;
}

static Any toparameter (Table *env, const Any &value) {
    if (is_parameter_type(value.type))
        return value;
    verifyValueKind(TYPE_Symbol, value);
    auto key = extract_symbol(value);
    auto bp = wrap(Parameter::create(key));
    setLocal(env, key, bp);
    return bp;
}

static const List *expand_expr_list (const Table *env, const List *it) {
    const List *l = nullptr;
    while (it) {
        auto cur = expand(env, it);
        if (!cur.list) break;
        l = List::create(cur.list->at, l, get_anchor(it));
        it = cur.list->next;
        env = cur.scope;
    }
    return reverse_list_inplace(l);
}

template <Cursor (*ExpandFunc)(const Table *, const List *)>
B_FUNC(wrap_expand_call) {
    builtin_checkparams(B_RCOUNT(S), 2, 2, 2);
    auto topit = extract_list(B_GETARG(S, 0));
    auto env = extract_table(B_GETARG(S, 1));
    auto cur = ExpandFunc(env, topit);
    Any out[] = { wrap(cur.list), wrap(cur.scope) };
    B_CALL(S,
        const_none,
        B_GETCONT(S),
        wrap(out, 2));
}

static Cursor expand_do (const Table *env, const List *topit) {

    auto it = extract_list(topit->at);
    auto topanchor = get_anchor(it);

    auto subenv = new_scope(env);

    return {
        List::create(
            quote(wrap(
                List::create(
                    getLocal(globals, SYM_DoForm),
                    expand_expr_list(subenv, it),
                    get_anchor(topanchor)))),
            topit->next),
        env };
}

static Cursor expand_continuation (const Table *env, const List *topit) {
    verifyAtParameterCount(topit, 1, -1);

    auto it = extract_list(topit->at);
    auto topanchor = get_anchor(it);
    it = it->next;

    Any sym;

    assert(it);
    if (is_symbol_type(it->at.type)) {
        sym = it->at;
        it = it->next;
        assert(it);
    } else {
        sym = wrap_symbol(SYM_Unnamed);
    }

    auto expr_parameters = it->at;
    it = it->next;

    auto subenv = new_scope(env);

    const List *outargs = nullptr;
    verifyValueKind(TYPE_List, expr_parameters);
    auto params = expr_parameters.list;
    auto param(params);
    while (param) {
        outargs = List::create(toparameter(subenv, param->at),
            outargs, get_anchor(param));
        param = param->next;
    }

    return {
        List::create(
            quote(wrap(
                List::create(
                    getLocal(globals, SYM_ContinuationForm),
                    List::create(
                        sym,
                        List::create(
                            wrap(reverse_list_inplace(outargs)),
                            expand_expr_list(subenv, it),
                            get_anchor(params)),
                        get_anchor(sym)),
                    topanchor))),
            topit->next),
        env };
}


static Cursor expand_syntax_extend (const Table *env, const List *topit) {
    auto cur = expand_continuation (env, topit);

    auto fun = compile(unquote(cur.list->at), const_none);

    Any exec_args[] = {fun, wrap_ptr(TYPE_Table, env)};
    auto expr_env = execute(exec_args, 2);
    verifyValueKind(TYPE_Table, expr_env);

    auto rest = cur.list->next;
    /*if (!rest) {
        error("syntax-extend: missing subsequent expression");
    }*/

    return { rest, expr_env.table };
}

static const List *expand_wildcard(
    const Table *env, const Any &handler, const List *topit) {
    Any exec_args[] = {handler,  wrap(topit), wrap(env)};
    auto result = execute(exec_args, 3);
    if (isnone(result))
        return nullptr;
    verifyValueKind(TYPE_List, result);
    if (!get_anchor(result.list)) {
        auto head = topit->at;
        const Anchor *anchor = get_anchor(head);
        if (!anchor) {
            anchor = get_anchor(topit);
        }
        set_anchor(result.list, anchor);
    }
    return result.list;
}

static Cursor expand_macro(
    const Table *env, const Any &handler, const List *topit) {
    Any exec_args[] = {handler,  wrap(topit), wrap(env)};
    auto result = execute(exec_args, 3);
    if (isnone(result))
        return { nullptr, nullptr };
    verifyValueKind(TYPE_ListTableTuple, result);
    auto result_list = at(result, wrap(0));
    auto result_scope = at(result, wrap(1));
    if (!get_anchor(result_list.list)) {
        auto head = topit->at;
        const Anchor *anchor = get_anchor(head);
        if (!anchor) {
            anchor = get_anchor(topit);
        }
        set_anchor(result_list.list, anchor);
    }
    return { result_list.list, result_scope.table };
}

static Cursor expand (const Table *env, const List *topit) {
    Any result = const_none;
process:
    assert(topit);
    auto expr = topit->at;
    if (is_quote_type(expr.type)) {
        // remove qualifier and return as-is
        return { List::create(unquote(expr), topit->next), env };
    } else if (is_list_type(expr.type)) {
        if (!expr.list) {
            error("expression is empty");
        }

        auto head = expr.list->at;

        // resolve symbol
        if (is_symbol_type(head.type)) {
            head = getLocal(env, extract_symbol(head));
        }

        if (is_macro_type(head.type)) {
            auto result = expand_macro(env, unmacro(head), topit);
            if (result.list) {
                topit = result.list;
                env = result.scope;
                goto process;
            } else if (result.scope) {
                topit = result.list;
                env = result.scope;
                return { nullptr, env };
            }
        }

        auto default_handler = getLocal(env, SYM_ListWC);
        if (default_handler.type != TYPE_Void) {
            auto result = expand_wildcard(env, default_handler, topit);
            if (result) {
                topit = result;
                goto process;
            }
        }

        auto it = extract_list(topit->at);
        result = wrap(expand_expr_list(env, it));
        topit = topit->next;
    } else if (is_symbol_type(expr.type)) {
        auto value = extract_symbol(expr);
        if (!hasLocal(env, value)) {
            auto default_handler = getLocal(env, SYM_SymbolWC);
            if (default_handler.type != TYPE_Void) {
                auto result = expand_wildcard(env, default_handler, topit);
                if (result) {
                    topit = result;
                    goto process;
                }
            }
            error("no such symbol in scope: '%s'",
                get_symbol_name(value).c_str());
        }
        result = getLocal(env, value);
        topit = topit->next;
    } else {
        result = expr;
        topit = topit->next;
    }
    return { List::create(result, topit), env };
}

static Any builtin_expand(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto expr_eval = extract_list(args[0]);
    auto scope = args[1];
    verifyValueKind(TYPE_Table, scope);

    auto retval = expand(scope.table, expr_eval);
    Any out[] = {wrap(retval.list), wrap(retval.scope)};
    return wrap(out, 2);
}

static Any builtin_set_globals(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto scope = args[0];
    verifyValueKind(TYPE_Table, scope);

    globals = scope.table;
    return const_none;
}

static B_FUNC(builtin_set_exception_handler) {
    builtin_checkparams(B_ARGCOUNT(S), 1, 1);
    auto old_exception_handler = S->exception_handler;
    auto func = B_GETARG(S, 0);
    S->exception_handler = func;
    B_CALL(S, const_none, B_GETCONT(S), old_exception_handler);
}

static B_FUNC(builtin_get_exception_handler) {
    builtin_checkparams(B_ARGCOUNT(S), 0, 0);
    B_CALL(S, const_none, B_GETCONT(S), S->exception_handler);
}

//------------------------------------------------------------------------------
// IL BUILDER
//------------------------------------------------------------------------------

struct ILBuilder {
    struct State {
        Flow *flow;
    };

    State state;

    State save() {
        return state;
    }

    void restore(const State &state) {
        this->state = state;
        if (state.flow) {
            assert(!state.flow->hasArguments());
        }
    }

    void continueAt(Flow *flow) {
        restore({flow});
    }

    void insertAndAdvance(
        const std::vector<Any> &values,
        Flow *next,
        const Anchor *anchor) {
        if (!state.flow) {
            error("can not insert call: continuation already exited.");
        }
        assert(!state.flow->hasArguments());
        state.flow->arguments = values;
        set_anchor(state.flow, anchor);
        state.flow = next;
    }

    Flow *getActiveFlow() const {
        return state.flow;
    }

    // arguments must include continuation
    void br(const std::vector<Any> &arguments, const Anchor *anchor) {
        assert(arguments.size() >= 2);
        if (!state.flow) {
            error("can not break: continuation already exited.");
        }
        insertAndAdvance(arguments, nullptr, anchor);
    }

    /*
    Parameter *call(std::vector<Any> values, const Any &dest, const Anchor *anchor) {
        assert(anchor);
        auto next = Flow::create_continuation(SYM_ReturnFlow);
        next->appendParameter(SYM_Result); //->vararg = true;
        values.insert(values.begin(), wrap(next));
        insertAndAdvance(values, next, anchor);
        return next->parameters[PARAM_Arg0];
    }
    */

};

static ILBuilder *builder;

//------------------------------------------------------------------------------
// COMPILER
//------------------------------------------------------------------------------

static Any compile (const Any &expr, const Any &dest);

static Any compile_to_parameter (const Any &value, const Anchor *anchor) {
    //assert(anchor);
    if (is_list_type(value.type)) {
        // complex expression
        return compile(value, wrap_symbol(SYM_Unnamed));
    } else {
        // constant value - can be inserted directly
        return compile(value, const_none);
    }
}

// for return values that don't need to be stored
static void compile_to_none (const Any &value, const Anchor *anchor) {
    //assert(anchor);
    if (is_list_type(value.type)) {
        // complex expression
        auto next = Flow::create_continuation(SYM_Unnamed);
        compile(value, wrap(next));
        builder->continueAt(next);
    } else {
        // constant value - technically we could just ignore it
        compile(value, const_none);
    }
}

static Any write_dest(const Any &dest, const Any &value) {
    if (!isnone(dest)) {
        if (is_symbol_type(dest.type)) {
            // a known value is returned - no need to generate code
            return value;
        } else {
            builder->br({const_none, dest, value}, get_anchor(value));
        }
    }
    return value;
}

static Any compile_expr_list (const List *it, const Any &dest) {
    if (!it) {
        return write_dest(dest, const_none);
    } else {
        while (it) {
            auto next = it->next;
            if (!next) { // last element goes to dest
                return compile(it->at, dest);
            } else {
                // write to unused parameter
                compile_to_none(it->at, get_anchor(it));
            }
            it = next;
        }
    }
    assert(false && "unreachable branch");
    return const_none;
}

static Any compile_do (const List *it, const Any &dest) {
    it = it->next;
    return compile_expr_list(it, dest);
}

static Any compile_continuation (const List *it, const Any &dest) {
    auto anchor = find_valid_anchor(it);
    if (!anchor || !anchor->isValid()) {
        //printValue(wrap(it), 0, true);
        //error("continuation expression not anchored");
    }

    it = it->next;

    assert(it);
    auto func_name = extract_symbol(it->at);

    it = it->next;

    assert(it);
    auto expr_parameters = it->at;

    it = it->next;

    auto currentblock = builder->save();

    auto function = Flow::create_empty_function(func_name);
    set_anchor(function, anchor);

    builder->continueAt(function);

    auto param = extract_list(expr_parameters);
    while (param) {
        function->appendParameter(
            const_cast<Parameter*>(extract_parameter(param->at)));
        param = param->next;
    }
    if (function->parameters.size() == 0) {
        // auto-add continuation parameter
        function->appendParameter(SYM_Unnamed);
    }
    auto ret = function->parameters[PARAM_Cont];
    assert(ret);

    compile_expr_list(it, wrap(ret));

    builder->restore(currentblock);

    return write_dest(dest, wrap(function));
}

static Any compile_splice (const List *it, const Any &dest) {
    it = it->next;
    assert(it);

    // spliced values are always returned directly
    Any value = compile_to_parameter(it->at, get_anchor(it));
    value.type = Types::Splice(value.type);
    return value;
}

static Any compile_implicit_call (const List *it, const Any &dest,
    const Anchor *anchor = nullptr) {

    if (!anchor) {
        anchor = find_valid_anchor(it);
        if (!anchor || !anchor->isValid()) {
            //printValue(wrap(it), 0, true);
            //error("call expression not anchored");
        }
    }

    auto callable = compile_to_parameter(it->at, anchor);
    it = it->next;

    std::vector<Any> args = { dest, callable };
    while (it) {
        args.push_back(compile_to_parameter(it->at, anchor));
        it = it->next;
    }

    if (is_symbol_type(dest.type)) {
        auto next = Flow::create_continuation(dest.symbol);
        next->appendParameter(dest.symbol);
        // patch dest to an actual function
        args[0] = wrap(next);
        builder->br(args, anchor);
        builder->continueAt(next);
        return wrap(next->parameters[PARAM_Arg0]);
    } else {
        builder->br(args, anchor);
        return const_none;
    }
}

static Any compile_call (const List *it, const Any &dest) {
    auto anchor = find_valid_anchor(it);
    if (!anchor || !anchor->isValid()) {
        printValue(wrap(it), 0, true);
        error("call expression not anchored");
    }
    it = it->next;
    return compile_implicit_call(it, dest, anchor);
}

static Any compile_contcall (const List *it, const Any &dest) {
    assert(!isnone(dest));

    auto anchor = find_valid_anchor(it);
    if (!anchor || !anchor->isValid()) {
        printValue(wrap(it), 0, true);
        error("call expression not anchored");
    }

    it = it->next;
    if (!it)
        error("continuation expected");
    std::vector<Any> args;
    args.push_back(compile_to_parameter(it->at, anchor));
    it = it->next;
    if (!it)
        error("callable expected");
    args.push_back(compile_to_parameter(it->at, anchor));
    it = it->next;
    while (it) {
        args.push_back(compile_to_parameter(it->at, anchor));
        it = it->next;
    }

    builder->br(args, anchor);
    return const_none;
}

//------------------------------------------------------------------------------

static Any compile (const Any &expr, const Any &dest) {
    if (is_list_type(expr.type)) {
        if (!expr.list) {
            error("empty expression");
        }
        auto slist = expr.list;
        auto head = slist->at;
        if (is_special_form_type(head.type)) {
            return head.special_form(slist, dest);
        } else {
            return compile_implicit_call(slist, dest);
        }
    } else {
        Any result = expr;
        if (is_quote_type(expr.type)) {
            // remove qualifier and return as-is
            result.type = get_element_type(expr.type);
        }
        return write_dest(dest, result);
    }
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

static Any builtin_loadlist(const Any *args, size_t argcount);
static Any builtin_parselist(const Any *args, size_t argcount);

static Any builtin_eval(const Any *args, size_t argcount);

static void initGlobals () {
    auto env = new_scope();
    globals = env;

    //setLocalString(env, "globals", wrap(env));

    setBuiltin<compile_call>(env, "call");
    setBuiltin<compile_contcall>(env, "cc/call");
    setBuiltin<compile_continuation>(env, "form:fn/cc");
    setBuiltin<compile_splice>(env, "splice");
    setBuiltin<compile_do>(env, "form:do");

    setBuiltinMacro< wrap_expand_call<expand_do> >(env, "do");
    setBuiltinMacro< wrap_expand_call<expand_continuation> >(env, "fn/cc");
    setBuiltinMacro< wrap_expand_call<expand_syntax_extend> >(env, "syntax-extend");

    setBuiltin< builtin_escape >(env, "escape");

    Any version_tuple[3] = {
        wrap((int)BANGRA_VERSION_MAJOR),
        wrap((int)BANGRA_VERSION_MINOR),
        wrap((int)BANGRA_VERSION_PATCH) };
    setLocalString(env, "bangra-version", wrap(version_tuple, 3));

    setLocalString(env, "integer", wrap(TYPE_Integer));
    setLocalString(env, "real", wrap(TYPE_Real));
    setLocalString(env, "cfunction", wrap(TYPE_CFunction));
    setLocalString(env, "array", wrap(TYPE_Array));
    setLocalString(env, "tuple", wrap(TYPE_Tuple));
    setLocalString(env, "vector", wrap(TYPE_Vector));
    setLocalString(env, "pointer", wrap(TYPE_Pointer));
    setLocalString(env, "struct", wrap(TYPE_Struct));
    setLocalString(env, "enum", wrap(TYPE_Enum));
    setLocalString(env, "tag", wrap(TYPE_Tag));
    setLocalString(env, "qualifier", wrap(TYPE_Qualifier));
    setLocalString(env, "type", wrap(TYPE_Type));
    setLocalString(env, "string", wrap(TYPE_String));
    setLocalString(env, "table", wrap(TYPE_Table));

    setLocalString(env, "void", wrap(TYPE_Void));

    setLocalString(env, "symbol", wrap(TYPE_Symbol));
    setLocalString(env, "list", wrap(TYPE_List));

    setLocalString(env, "closure", wrap(TYPE_Closure));
    setLocalString(env, "parameter", wrap(TYPE_Parameter));
    setLocalString(env, "frame", wrap(TYPE_Frame));
    setLocalString(env, "flow", wrap(TYPE_Flow));

    setLocalString(env, "scope-parent-symbol", wrap_symbol(SYM_Parent));
    setLocalString(env, "scope-list-wildcard-symbol", wrap_symbol(SYM_ListWC));
    setLocalString(env, "scope-symbol-wildcard-symbol", wrap_symbol(SYM_SymbolWC));

    setLocalString(env, "usize_t",
        wrap(Types::Integer(sizeof(size_t)*8,false)));
    setLocalString(env, "ssize_t",
        wrap(Types::Integer(sizeof(ssize_t)*8,true)));

    setLocalString(env, "rawstring", wrap(TYPE_Rawstring));

    setLocalString(env, "true", wrap(true));
    setLocalString(env, "false", wrap(false));

    setLocalString(env, "none", const_none);

    setLocalString(env, "support-ANSI?", wrap(support_ansi));

    setBuiltin<builtin_exit>(env, "exit");
    setBuiltin<builtin_globals>(env, "globals");
    setBuiltin<builtin_print>(env, "print");
    setBuiltin<builtin_repr>(env, "repr");
    setBuiltin<builtin_tupleof>(env, "tupleof");
    setBuiltin<builtin_arrayof>(env, "arrayof");
    setBuiltin<builtin_cons>(env, "cons");
    setBuiltin<builtin_structof>(env, "structof");
    setBuiltin<builtin_tableof>(env, "tableof");
    setBuiltin<builtin_set_key>(env, "set-key!");
    setBuiltin<builtin_bind>(env, "bind!");
    setBuiltin<builtin_next_key>(env, "next-key");
    setBuiltin<builtin_typeof>(env, "typeof");
    setBuiltin<builtin_external>(env, "external");
    setBuiltin<builtin_parse_c>(env, "parse-c");
    setBuiltin<builtin_branch>(env, "branch");
    setBuiltin<builtin_dump>(env, "dump");
    setBuiltin<builtin_block_scope_macro>(env, "block-scope-macro");

    setBuiltin<builtin_find_scope_symbol>(env, "find-scope-symbol");

    setBuiltin<builtin_expand>(env, "expand");
    setBuiltin<builtin_set_globals>(env, "set-globals!");
    setBuiltin<builtin_error>(env, "error");
    setBuiltin<builtin_raise>(env, "raise");
    setBuiltin<builtin_countof>(env, "countof");
    setBuiltin<builtin_sizeof>(env, "sizeof");
    setBuiltin<builtin_loadlist>(env, "list-load");
    setBuiltin<builtin_parselist>(env, "list-parse");
    setBuiltin<builtin_eval>(env, "eval");
    setBuiltin<builtin_hash>(env, "hash");
    setBuiltin<builtin_prompt>(env, "prompt");

    //setBuiltin<builtin_dlopen>(env, "dlopen");
    //setBuiltin<builtin_dlclose>(env, "dlclose");
    //setBuiltin<builtin_dlerror>(env, "dlerror");
    //setBuiltin<builtin_dlsym>(env, "dlsym");

    setBuiltin<builtin_bitcast>(env, "bitcast");
    setBuiltin<builtin_element_type>(env, "element-type");

    setBuiltin<builtin_va_count>(env, "va-countof");
    setBuiltin<builtin_va_arg>(env, "va-arg");

    setBuiltin<builtin_set_exception_handler>(env, "set-exception-handler!");
    setBuiltin<builtin_get_exception_handler>(env, "get-exception-handler");

    setBuiltin<builtin_flow_parameter_count>(env, "flow-parameter-count");
    setBuiltin<builtin_flow_parameter>(env, "flow-parameter");
    setBuiltin<builtin_flow_argument_count>(env, "flow-argument-count");
    setBuiltin<builtin_flow_argument>(env, "flow-argument");
    setBuiltin<builtin_flow_name>(env, "flow-name");
    setBuiltin<builtin_flow_id>(env, "flow-id");

    setBuiltin<builtin_frame_eval>(env, "frame-eval");

    //setBuiltin< builtin_variadic_ltr<builtin_at> >(env, "@");
    setBuiltin< builtin_binary_op<at> >(env, "@");

    setBuiltin<builtin_slice>(env, "slice");

    setBuiltin< builtin_variadic_ltr< builtin_binary_op<add> > >(env, "+");
    setBuiltin< builtin_unary_binary_op<neg, sub> >(env, "-");
    setBuiltin< builtin_variadic_ltr< builtin_binary_op<mul> > >(env, "*");
    setBuiltin< builtin_unary_binary_op<rcp, div> >(env, "/");
    setBuiltin< builtin_binary_op<floordiv> >(env, "//");
    setBuiltin< builtin_binary_op<mod> >(env, "%");

    setBuiltin< builtin_variadic_rtl<builtin_binary_op<join> > >(env, "..");

    setBuiltin< builtin_binary_op<bit_and> >(env, "&");
    setBuiltin< builtin_variadic_ltr< builtin_binary_op<bit_or> > >(env, "|");
    setBuiltin< builtin_binary_op<bit_xor> >(env, "^");
    setBuiltin< builtin_unary_op<bit_not> >(env, "~");

    setBuiltin< builtin_binary_op<lshift> >(env, "<<");
    setBuiltin< builtin_binary_op<rshift> >(env, ">>");

    setBuiltin< builtin_binary_op<pow> >(env, "**");

    setBuiltin< builtin_unary_op<bool_not> >(env, "not");

    setBuiltin< builtin_bool_binary_op<eq> >(env, "==");
    setBuiltin< builtin_bool_binary_op<ne> >(env, "!=");
    setBuiltin< builtin_bool_binary_op<gt> >(env, ">");
    setBuiltin< builtin_bool_binary_op<ge> >(env, ">=");
    setBuiltin< builtin_bool_binary_op<lt> >(env, "<");
    setBuiltin< builtin_bool_binary_op<le> >(env, "<=");
}

static void init() {
    bangra::support_ansi = isatty(fileno(stdout));
    bangra::global_c_namespace = dlopen(NULL, RTLD_LAZY);

    initCCompiler();

    initSymbols();

    initTypes();
    initConstants();

    ffi = new FFI();
    builder = new ILBuilder();

    initGlobals();
}

//------------------------------------------------------------------------------

static void handleException(const Table *env, const Any &expr) {
    streamValue(std::cerr, expr, StreamValueFormat(0, true));
    error("an exception was raised");
}

// path must be resident
static Any compileFlow (const Table *env, const Any &expr,
    const char *path) {
    verifyValueKind(TYPE_List, expr);

    Anchor *anchor = new Anchor();
    anchor->path = path;
    anchor->lineno = 1;
    anchor->column = 1;
    anchor->offset = 0;

    auto rootit = extract_list(expr);
    auto expexpr = expand_expr_list(env, rootit);

    auto mainfunc = Flow::create_function(get_symbol(path));
    auto ret = mainfunc->parameters[PARAM_Cont];
    builder->continueAt(mainfunc);

    compile_expr_list(expexpr, wrap(ret));

    return wrap(mainfunc);
}

// path must be resident
static Any loadFile(const char *path) {
    auto file = MappedFile::open(path);
    if (file) {
        bangra::Parser parser;
        return parser.parseMemory(
            file->strptr(), file->strptr() + file->size(),
            path);
    }
    return const_none;
}

static Any builtin_loadlist(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto path = extract_string(args[0]);
    return loadFile(resident_c_str(path));
}

static Any builtin_parselist(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto text = extract_string(args[0]);
    bangra::Parser parser;
    return parser.parseMemory(
        text.c_str(), text.c_str() + text.size(),
        "<string>");
}

static Any builtin_eval(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 3);
    auto expr = args[0];
    auto scope = (argcount > 1)?extract_table(args[1]):globals;
    const char *path = (argcount > 2)?extract_cstr(args[2]):"<eval>";

    return compileFlow(scope, expr, path);
}

static bool compileMain (Any expr, const char *path) {
    Any exec_args[] = {compileFlow(globals, expr, path)};
    execute(exec_args, 1);
    return true;
}

static void runREPL () {
    Any args[] = { getLocal(globals, SYM_ReadEvalPrintLoop)};
    execute(args, 1);
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

static Any parseLoader(const char *executable_path) {
    // attempt to read bootstrap expression from end of binary
    auto file = MappedFile::open(executable_path);
    if (!file) {
        stb_fprintf(stderr, "could not open binary\n");
        return const_none;
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
        if (cursor < ptr) return const_none;
    }
    if (*cursor != ')') return const_none;
    cursor--;
    // seek backwards to find beginning of expression
    while ((cursor >= ptr) && (*cursor != '('))
        cursor--;

    bangra::Parser footerParser;
    auto expr = footerParser.parseMemory(
        cursor, ptr + size, executable_path, cursor - ptr);
    if (isnone(expr)) {
        stb_fprintf(stderr, "could not parse footer expression\n");
        return const_none;
    }
    if (!is_list_type(expr.type))  {
        stb_fprintf(stderr, "footer expression is not a symbolic list\n");
        return const_none;
    }
    auto symlist = expr.list;
    auto it = symlist;
    if (!it) {
        stb_fprintf(stderr, "footer expression is empty\n");
        return const_none;
    }
    auto head = it->at;
    it = it->next;
    if (!is_symbol_type(head.type))  {
        stb_fprintf(stderr, "footer expression does not begin with symbol\n");
        return const_none;
    }
    if (!isSymbol(head, SYM_ScriptSize))  {
        stb_fprintf(stderr, "footer expression does not begin with '%s'\n",
            get_symbol_name(SYM_ScriptSize).c_str());
        return const_none;
    }
    if (!it) {
        stb_fprintf(stderr, "footer expression needs two arguments\n");
        return const_none;
    }
    auto arg = it->at;
    it = it->next;
    if (!is_integer_type(arg.type))  {
        stb_fprintf(stderr, "script-size argument is not integer\n");
        return const_none;
    }
    auto offset = extract_integer(arg);
    if (offset <= 0) {
        stb_fprintf(stderr, "script-size must be larger than zero\n");
        return const_none;
    }
    bangra::Parser parser;
    auto script_start = cursor - offset;
    return parser.parseMemory(script_start, cursor, executable_path, script_start - ptr);
}

static bool compileStartupScript() {
    char *base = strdup(bangra_interpreter_path);
    char *ext = extension(base);
    if (ext) {
        *ext = 0;
    }
    std::string path = format("%s.b", base);
    free(base);

    const char *cpath = resident_c_str(path);
    Any expr = loadFile(cpath);

    if (!isnone(expr)) {
        return bangra::compileMain(expr, cpath);
    }

    return true;
}

} // namespace bangra

// C API
//------------------------------------------------------------------------------

char *bangra_interpreter_path = NULL;
char *bangra_interpreter_dir = NULL;
int bangra_argc = 0;
char **bangra_argv = NULL;

void print_version() {
    std::string versionstr = bangra::format("%i.%i",
        BANGRA_VERSION_MAJOR, BANGRA_VERSION_MINOR);
    if (BANGRA_VERSION_PATCH) {
        versionstr += bangra::format(".%i", BANGRA_VERSION_PATCH);
    }
    stb_printf(
    "Bangra version %s\n"
    "Executable path: %s\n"
    , versionstr.c_str()
    , bangra_interpreter_path
    );
    exit(0);
}

void print_help(const char *exename) {
    stb_printf(
    "usage: %s [option [...]] [filename]\n"
    "Options:\n"
    "   -h, --help                  print this text and exit.\n"
    "   -v, --version               print program version and exit.\n"
    "   --skip-startup              skip startup script.\n"
    "   -a, --enable-ansi           enable ANSI output.\n"
    "   -t, --trace                 trace interpreter commands.\n"
    "   --                          terminate option list.\n"
    , exename
    );
    exit(0);
}

int bangra_main(int argc, char ** argv) {
    bangra_argc = argc;
    bangra_argv = argv;

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

    try {
        bangra::init();

        bangra::Any expr = bangra::const_none;
        const char *cpath = "<?>";

        if (argv) {
            if (argv[0]) {
                std::string loader = bangra::GetExecutablePath(argv[0]);
                // string must be kept resident
                bangra_interpreter_path = bangra::resident_c_str(loader);

                expr = bangra::parseLoader(bangra_interpreter_path);
                cpath = "<loader>";
            } else {
                bangra_interpreter_path = strdup("");
            }

            bangra_interpreter_dir = dirname(strdup(bangra_interpreter_path));

            bangra::setLocalString(const_cast<bangra::Table *>(bangra::globals),
                "interpreter-path",
                bangra::wrap(std::string(bangra_interpreter_path)));
            bangra::setLocalString(const_cast<bangra::Table *>(bangra::globals),
                "interpreter-dir",
                bangra::wrap(std::string(bangra_interpreter_dir)));

            if (isnone(expr)) {
                // running in interpreter mode
                char *sourcepath = NULL;
                // skip startup script
                bool skip_startup = false;

                if (argv[1]) {
                    bool parse_options = true;

                    char ** arg = argv;
                    while (*(++arg)) {
                        if (parse_options && (**arg == '-')) {
                            if (!strcmp(*arg, "--help") || !strcmp(*arg, "-h")) {
                                print_help(argv[0]);
                            } else if (!strcmp(*arg, "--version") || !strcmp(*arg, "-v")) {
                                print_version();
                            } else if (!strcmp(*arg, "--skip-startup")) {
                                skip_startup = true;
                            } else if (!strcmp(*arg, "--enable-ansi") || !strcmp(*arg, "-a")) {
                                bangra::support_ansi = true;
                            } else if (!strcmp(*arg, "--trace") || !strcmp(*arg, "-t")) {
                                bangra::trace_arguments = true;
                            } else if (!strcmp(*arg, "--")) {
                                parse_options = false;
                            } else {
                                stb_printf("unrecognized option: %s. Try --help for help.\n", *arg);
                                exit(1);
                            }
                        } else if (!sourcepath) {
                            sourcepath = *arg;
                        } else {
                            stb_printf("unrecognized argument: %s. Try --help for help.\n", *arg);
                            exit(1);
                        }
                    }
                }

                if (!skip_startup && bangra_interpreter_path) {
                    if (!bangra::compileStartupScript()) {
                        return 1;
                    }
                }

                if (sourcepath) {
                    bangra::Parser parser;
                    expr = parser.parseFile(sourcepath);
                    cpath = sourcepath;
                }
            }
        }

        if (isnone(expr)) {
            bangra::runREPL();
            return 0;
        } else {
            bangra::compileMain(expr, cpath);
        }

    } catch (const bangra::Any &any) {
        std::cout << bangra::ansi(ANSI_STYLE_ERROR, "error:")
            << " " << bangra::get_string(any) << "\n";
        fflush(stdout);
        return 1;
    }

    return 0;
}

bangra::Any bangra_parse_file(const char *path) {
    bangra::Parser parser;
    return parser.parseFile(path);
}

//------------------------------------------------------------------------------

#endif // BANGRA_CPP_IMPL
#ifdef BANGRA_MAIN_CPP_IMPL

//------------------------------------------------------------------------------
// MAIN EXECUTABLE IMPLEMENTATION
//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
    return bangra_main(argc, argv);
}

int print_number(int value) {
    stb_printf("NUMBER: %i\n", value);
    return value + 1;
}

#endif // BANGRA_MAIN_CPP_IMPL