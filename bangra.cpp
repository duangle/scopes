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
    BANGRA_VERSION_MINOR = 3,
    BANGRA_VERSION_PATCH = 0,
};

extern int bang_argc;
extern char **bang_argv;
extern char *bang_executable_path;

int bangra_main(int argc, char ** argv);

#if defined __cplusplus
}
#endif

#endif // BANGRA_CPP
#ifdef BANGRA_CPP_IMPL

#define BANGRA_HEADER "bangra"
//#define BANGRA_DEBUG_IL

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
#include "mman.h"
#include "stdlib_ex.h"
#else
// for backtrace
#include <execinfo.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <dlfcn.h>
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
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/ErrorHandling.h>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
//#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Casting.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/MultiplexConsumer.h"

namespace bangra {

//------------------------------------------------------------------------------
// UTILITIES
//------------------------------------------------------------------------------

template<typename ... Args>
std::string format( const std::string& format, Args ... args ) {
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... );
    std::string str;
    str.resize(size);
    snprintf( &str[0], size + 1, format.c_str(), args ... );
    return str;
}

template <typename R, typename... Args>
std::function<R (Args...)> memo(R (*fn)(Args...)) {
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

/*
static std::string quoteString(const std::string &a, const char *quote_chars = nullptr) {
    std::stringstream ss;
    streamString(ss, a, quote_chars);
    return ss.str();
}
*/

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

/*
static std::string quoteReprString(const std::string &value) {
    return ansi(ANSI_STYLE_STRING,
        format("\"%s\"",
            quoteString(value, "\"").c_str()));
}

static std::string quoteReprSymbol(const std::string &value) {
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
        printf("%.*s\n", (int)(end - start), str + start);
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
        printf("%s:%i:%i: %s\n", path, lineno, column, msg.c_str());
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
        vprintf (fmt, args);
        putchar('\n');
        if (anchor) {
            dumpFileLine(anchor->path, anchor->offset);
        }
    }

    static void printErrorV (const Anchor *anchor, const char *fmt, va_list args) {
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
        vprintf (fmt, args);
        putchar('\n');
        if (anchor) {
            dumpFileLine(anchor->path, anchor->offset);
        }
        exit(1);
    }

};

//------------------------------------------------------------------------------
// MID-LEVEL IL
//------------------------------------------------------------------------------

struct Parameter;
struct String;
struct Flow;
struct Any;
struct Hash;
struct SList;
struct Table;
struct Closure;
struct Builtin;
struct BuiltinFlow;
struct BuiltinMacro;
struct Type;
struct Macro;
struct SpecialForm;

typedef Any (*BuiltinFunction)(const std::vector<Any> &args);
typedef std::vector<Any> (*BuiltinFlowFunction)(const std::vector<Any> &args);

struct Any {
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

        void *ptr;
        char *c_str;
        BuiltinFunction func;
        SList *slist;
        std::string *str;
        Type *typeref;
        Parameter *parameter;
        Flow *flow;
        Closure *closure;
        Builtin *builtin;
        BuiltinFlow *builtin_flow;
        BuiltinMacro *builtin_macro;
        SpecialForm *special_form;
        Anchor *anchorref;
        Table *table;
        Macro *macro;
    };
    Type *type;
    Anchor *anchor;
};

static Any make_any(Type *type, Anchor *anchor = nullptr) {
    Any any;
    any.type = type;
    any.anchor = anchor;
    return any;
}

static Any pointer(Type *type, const void *ptr, Anchor *anchor = nullptr) {
    Any any = make_any(type, anchor);
    any.ptr = const_cast<void *>(ptr);
    return any;
}

static void error (const Any &any, const char *format, ...) {
    const Anchor *anchor = any.anchor;
    if (!anchor) {
        // TODO: find valid anchor
        //std::cout << "at\n  " << getRepr(value) << "\n";
        //anchor = find_valid_anchor(value);
    }
    va_list args;
    va_start (args, format);
    Anchor::printErrorV(anchor, format, args);
    va_end (args);
}

//------------------------------------------------------------------------------

namespace Types {
    static Type *TType;
    static Type *TArray;
    static Type *TVector;
    static Type *TTuple;
    static Type *TPointer;
    static Type *TCFunction;
    static Type *TInteger;
    static Type *TReal;
    static Type *TStruct;
    static Type *TEnum;

    static Type *Bool;
    static Type *I8;
    static Type *I16;
    static Type *I32;
    static Type *I64;
    static Type *U8;
    static Type *U16;
    static Type *U32;
    static Type *U64;

    static Type *R16;
    static Type *R32;
    static Type *R64;

    static Type *Rawstring;
    static Type *None;
    static Type *String; // std::string

    static Type *Parameter;
    static Type *Table;
    static Type *SList;
    static Type *Symbol; // std::string

    static Type *Builtin;
    static Type *BuiltinFlow;
    static Type *Flow;

    static Type *SpecialForm;
    static Type *BuiltinMacro;
    static Type *Frame;
    static Type *Closure;
    static Type *Macro;

    static Type *TypeRef;
    static Type *Any;
    static Type *AnchorRef;

    static Type *_new_array_type(Type *element, size_t size);
    static Type *_new_vector_type(Type *element, size_t size);
    static Type *_new_tuple_type(std::vector<Type *> types);
    static Type *_new_cfunction_type(
        Type *result, Type *parameters, bool vararg);
    static Type *_new_pointer_type(Type *element);
    static Type *_new_integer_type(size_t width, bool signed_);
    static Type *_new_real_type(size_t width);

    static auto Array = memo(_new_array_type);
    static auto Vector = memo(_new_vector_type);
    static auto Tuple = memo(_new_tuple_type);
    static auto CFunction = memo(_new_cfunction_type);
    static auto Pointer = memo(_new_pointer_type);
    static auto Integer = memo(_new_integer_type);
    static auto Real = memo(_new_real_type);

} // namespace Types

//------------------------------------------------------------------------------

static Any const_none;
static Any const_true;
static Any const_false;
static Any const_eol;

//------------------------------------------------------------------------------

struct Table {
    std::unordered_map<std::string, Any> _;
    Table *meta;
};

//------------------------------------------------------------------------------

struct Type {
    // dynamic attributes
    Table table;

    // bootstrapped attributes

    std::string name;

    // return true if self supports the interface of other
    bool (*eq_type)(Type *self, Type *other);
    // short string representation
    std::string (*tostring)(Type *self, const Any &value);
    // return contained item
    Any (*at)(Type *self, const Any &value, size_t index);
    // return true if objects are equivalent
    bool (*eq)(Type *self, const Any &a, const Any &b);

    size_t size;
    size_t alignment;
    bool is_ref;

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
        Type *element_type;
        // enum: type of tag
        Type *tag_type;
        // function: type of result
        Type *result_type;
    };
    // tuple, struct, union: field types
    // function: parameter types
    // enum: payload types
    std::vector<Type *> types;
    // tuple, struct: field offsets
    std::vector<size_t> offsets;
    // enum: tag values
    std::vector<int64_t> tags;
    // struct, union: name to types index lookup table
    // enum: name to types & tag index lookup table
    std::unordered_map<std::string, size_t> name_index_map;
};

static std::string get_name(Type *self) {
    assert(self);
    return self->name;
}

static size_t get_size(Type *self) {
    return self->size;
}

static bool is_ref(Type *self) {
    return self->is_ref;
}

static size_t get_width(Type *self) {
    return self->width;
}

static bool is_signed(Type *self) {
    return self->is_signed;
}

static Type *get_type(Type *self, size_t i) {
    return self->types[i];
}

static size_t get_field_count(Type *self) {
    return self->types.size();
}

static bool is_vararg(Type *self) {
    return self->is_vararg;
}

static size_t get_parameter_count(Type *self) {
    return get_field_count(self->types[0]);
}

static Type *get_parameter_type(Type *self, size_t i) {
    return get_type(self->types[0], i);
}

/*
static size_t get_field_index(Type *self, const std::string &name) {
    auto it = self->name_index_map.find(name);
    if (it == self->name_index_map.end()) {
        return (size_t)-1;
    } else {
        return it->second;
    }
}
*/

static size_t get_offset(Type *self, size_t i) {
    return self->offsets[i];
}

static size_t get_alignment(Type *self) {
    return self->alignment;
}

static Type *get_element_type(Type *self) {
    return self->element_type;
}
/*
static Type *get_tag_type(Type *self) {
    return self->tag_type;
}
*/
static Type *get_result_type(Type *self) {
    return self->result_type;
}

static bool type_eq_type_default(Type *self, Type *other) {
    assert(self && other);
    return false;
}

static bool type_eq_default(Type *self, const Any &a, const Any &b) {
    return false;
}

static Any type_at_default(Type *self, const Any &value, size_t index) {
    assert(false && "type not indexable");
    return const_none;
}

static std::string type_tostring_default(Type *self, const Any &value) {
    assert(self);
    return format("(%s %p)", get_name(self).c_str(), value.ptr);
}

static Type *new_type(const std::string &name) {
    //assert(!name.empty());
    auto result = new Type();
    result->size = 0;
    result->alignment = 1;
    result->is_ref = false;
    result->at = type_at_default;
    result->name = name;
    result->eq_type = type_eq_type_default;
    result->eq = type_eq_default;
    result->tostring = type_tostring_default;
    result->is_signed = false;
    result->is_vararg = false;
    result->width = 0;
    result->count = 0;
    //result->ctype = nullptr;
    return result;
}

static const void *get_pointer(const Any &value) {
    if (value.type->is_ref) {
        return value.ptr;
    } else {
        return &value.u64;
    }
}

static bool eq(Type *self, Type *other) {
    if (self == other) return true;
    assert(self && other);
    return self->eq_type(self, other) || other->eq_type(other, self);
}

static bool eq(const Any &a, const Any &b) {
    return a.type->eq(a.type, a, b) || b.type->eq(b.type, b, a);
}

static std::string get_string(const Any &value) {
    return value.type->tostring(value.type, value);
}

static Any at(const Any &value, size_t index) {
    return value.type->at(value.type, value, index);
}

static Any from_pointer(Type *self, const void *ptr) {
    if (self->is_ref) {
        return pointer(self, ptr);
    } else {
        Any result = make_any(self);
        result.u64 = 0;
        memcpy(&result.u64, ptr, get_size(self));
        return result;
    }
}

//------------------------------------------------------------------------------

static Any pstring(const std::string &s) {
    return pointer(Types::String, new std::string(s));
}

static Any symbol(const std::string &s) {
    return pointer(Types::Symbol, new std::string(s));
}

//------------------------------------------------------------------------------

static Table *new_table() {
    auto result = new Table();
    result->meta = nullptr;
    return result;
}

/*
static void set_meta(Table &table, Table *meta) {
    table.meta = meta;
}

static Table *get_meta(Table &table) {
    return table.meta;
}
*/

static void set_key(Table &table, const std::string &key, const Any &value) {
    table._[key] = value;
}

/*
static bool has_key(Table &table, const std::string &key) {
    return table._.find(key) != table._.end();
}

static const Any &get_key(Table &table, const std::string &key) {
    auto it = table._.find(key);
    assert (it != table._.end());
    return it->second;
}
*/

static const Any &get_key(Table &table, const std::string &key, const Any &defvalue) {
    auto it = table._.find(key);
    if (it == table._.end())
        return defvalue;
    else
        return it->second;
}

static bool isnone(const Any &value) {
    return (value.type == Types::None);
}

/*
static Any call(const Any &what, const std::vector<Any> &args) {
    return const_none;
}
*/

static bool is_typeref_type(Type *type) {
    return eq(type, Types::TypeRef);
}

static bool is_none_type(Type *type) {
    return eq(type, Types::None);
}

static bool is_bool_type(Type *type) {
    return eq(type, Types::Bool);
}

static bool is_integer_type(Type *type) {
    return eq(type, Types::TInteger);
}

static bool is_real_type(Type *type) {
    return eq(type, Types::TReal);
}

/*
static bool is_struct_type(Type *type) {
    return eq(type, Types::TStruct);
}
*/

static bool is_tuple_type(Type *type) {
    return eq(type, Types::TTuple);
}

/*
static bool is_array_type(Type *type) {
    return eq(type, Types::TArray);
}
*/

static bool is_cfunction_type(Type *type) {
    return eq(type, Types::TCFunction);
}

static bool is_pointer_type(Type *type) {
    return eq(type, Types::TPointer);
}

static std::string extract_string(const Any &value) {
    if (eq(value.type, Types::Rawstring)) {
        return value.c_str;
    } else if (eq(value.type, Types::String)) {
        return *value.str;
    } else {
        error(value, "can not extract string");
        return "";
    }
}

static std::vector<Any> extract_tuple(const Any &value) {
    if (is_tuple_type(value.type)) {
        std::vector<Any> result;
        size_t count = get_field_count(value.type);
        for (size_t i = 0; i < count; ++i) {
            result.push_back(at(value, i));
        }
        return result;
    }
    error(value, "can not extract tuple");
    return {};
}

static std::string extract_any_string(const Any &value) {
    if (eq(value.type, Types::Rawstring)) {
        return value.c_str;
    } else if (eq(value.type, Types::String)) {
        return *value.str;
    } else if (eq(value.type, Types::Symbol)) {
        return *value.str;
    } else {
        error(value, "can not extract string");
        return "";
    }
}

static bool extract_bool(const Any &value) {
    if (eq(value.type, Types::Bool)) {
        return value.i1;
    }
    error(value, "boolean expected");
    return false;
}

static Type *extract_type(const Any &value) {
    if (is_typeref_type(value.type)) {
        return value.typeref;
    }
    error(value, "type constant expected");
    return nullptr;
}

static Any boolean(bool value) {
    return value?const_true:const_false;
}

static Any integer(Type *type, int64_t value) {
    Any any = make_any(type);
    if (type == Types::I8) {
        any.i8 = (int8_t)value;
    } else if (type == Types::I16) {
        any.i16 = (int16_t)value;
    } else if (type == Types::I32) {
        any.i32 = (int32_t)value;
    } else if (type == Types::I64) {
        any.i64 = value;
    } else if (type == Types::U8) {
        any.u8 = (uint8_t)value;
    } else if (type == Types::U16) {
        any.u16 = (uint16_t)value;
    } else if (type == Types::U32) {
        any.u32 = (uint32_t)value;
    } else if (type == Types::U64) {
        any.u64 = (uint64_t)value;
    } else {
        assert(false && "not an integer type");
    }
    return any;
}

static int64_t extract_integer(const Any &value) {
    auto type = value.type;
    if (type == Types::I8) {
        return (int64_t)value.i8;
    } else if (type == Types::I16) {
        return (int64_t)value.i16;
    } else if (type == Types::I32) {
        return (int64_t)value.i32;
    } else if (type == Types::I64) {
        return value.i64;
    } else if (type == Types::U8) {
        return (int64_t)value.u8;
    } else if (type == Types::U16) {
        return (int64_t)value.u16;
    } else if (type == Types::U32) {
        return (int64_t)value.u32;
    } else if (type == Types::U64) {
        return (int64_t)value.u64;
    } else {
        assert(false && "not an integer type");
        return 0;
    }
}

static Any real(Type *type, double value) {
    Any any = make_any(type);
    if (type == Types::R32) {
        any.r32 = (float)value;
    } else if (type == Types::R64) {
        any.r64 = value;
    } else {
        assert(false && "not a real type");
    }
    return any;
}

static double extract_real(const Any &value) {
    auto type = value.type;
    if (type == Types::R32) {
        return (double)value.r32;
    } else if (type == Types::R64) {
        return (double)value.r64;
    } else {
        assert(false && "not a real type");
        return 0;
    }
}


//------------------------------------------------------------------------------

struct Parameter {
    Flow *parent;
    size_t index;
    Type *parameter_type;
    std::string name;

    Parameter() :
        parent(nullptr),
        index(-1),
        parameter_type(nullptr) {
    }

    Flow *getParent() const {
        return parent;
    }

    std::string getReprName() const {
        if (name.empty()) {
            return format("%s%zu",
                ansi(ANSI_STYLE_OPERATOR,"@").c_str(),
                index);
        } else {
            return name;
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

    static Parameter *create(const std::string &name = "") {
        auto value = new Parameter();
        value->index = (size_t)-1;
        value->name = name;
        value->parameter_type = nullptr;
        return value;
    }
};

//------------------------------------------------------------------------------

struct SList {
public:
    Any at;
    SList *next;

    static SList *create(const Any &at, SList *next) {
        assert(next);
        auto result = new SList();
        result->at = at;
        result->next = next;
        return result;
    }

    static SList *create(Any *values, size_t count) {
        auto result = const_eol.slist;
        while (count) {
            --count;
            result = create(values[count], result);
        }
        return result;
    }

    static SList *create(const std::vector<Any> &values) {
        return create(const_cast<Any *>(&values[0]), values.size());
    }

    /*
    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }
    */

    /*
    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "slist");
        auto expr = this;
        get_eox();
        while (expr != EOX) {
            ss << " " << bangra::getRefRepr(expr->at);
            expr = expr->next;
        }
        ss << ")";
        return ss.str();
    }
    */
};

static SList *extract_slist(const Any &value) {
    assert(value.type == Types::SList);
    return value.slist;
}

//------------------------------------------------------------------------------

struct SListIter {
protected:
    const SList *expr;
public:
    const SList *getSList() const {
        return expr;
    }

    SListIter(const SList *value, size_t c=0) :
        expr(value)
    {
        auto eox = const_eol.slist;
        for (size_t i = 0; i < c; ++i) {
            assert(expr != eox);
            expr = expr->next;
        }
    }

    SListIter(const Any &value, size_t i=0) :
        SListIter(extract_slist(value), i)
    {}

    bool operator ==(const SListIter &other) const {
        return (expr == other.expr);
    }
    bool operator !=(const SListIter &other) const {
        return (expr != other.expr);
    }

    SListIter operator +(int offset) const {
        return SListIter(expr, (size_t)offset);
    }

    SListIter operator ++(int) {
        auto oldself = *this;
        assert (expr != const_eol.slist);
        expr = expr->next;
        return oldself;
    }

    operator bool() const {
        return expr != const_eol.slist;
    }

    const Any &operator *() const {
        assert(expr != const_eol.slist);
        return expr->at;
    }

};

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

    std::string name;
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

    std::string getRefRepr () const {
        return format("%s%s%" PRId64,
            ansi(ANSI_STYLE_KEYWORD, "λ").c_str(),
            name.c_str(),
            uid);
    }

    Parameter *appendParameter(Parameter *param) {
        param->parent = this;
        param->index = parameters.size();
        parameters.push_back(param);
        return param;
    }

    static Flow *create(
        size_t paramcount = 0,
        const std::string &name = "") {
        auto value = new Flow();
        value->name = name;
        for (size_t i = 0; i < paramcount; ++i) {
            value->appendParameter(Parameter::create());
        }
        return value;
    }
};

int64_t Flow::unique_id_counter = 1;

std::string Parameter::getRefRepr () const {
    auto parent = getParent();
    return format("%s%s%s",
        parent?
            (get_string(pointer(Types::Flow, parent)).c_str())
            :ansi(ANSI_STYLE_ERROR, "<unbound>").c_str(),
        ansi(ANSI_STYLE_OPERATOR,".").c_str(),
        getReprName().c_str());
}

//------------------------------------------------------------------------------

struct Builtin {

    BuiltinFunction handler;
    std::string name;

    static Builtin *create(BuiltinFunction func,
        const std::string &name) {
        auto result = new Builtin();
        result->handler = func;
        result->name = name;
        return result;
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "builtin");
        ss << " " << name;
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

struct BuiltinFlow {

    BuiltinFlowFunction handler;
    std::string name;

    static BuiltinFlow *create(BuiltinFlowFunction func,
        const std::string &name) {
        auto result = new BuiltinFlow();
        result->handler = func;
        result->name = name;
        return result;
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "builtin-cc");
        ss << " " << name;
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

typedef Any (*SpecialFormFunction)(SListIter);

struct SpecialForm {

    SpecialFormFunction handler;
    std::string name;

    static SpecialForm *create(SpecialFormFunction func,
        const std::string &name) {
        auto result = new SpecialForm();
        result->handler = func;
        result->name = name;
        return result;
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "form");
        ss << " " << name;
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

struct Cursor {
    Any value;
    SListIter next;
};

typedef Cursor (*MacroBuiltinFunction)(Table *, SListIter);

struct BuiltinMacro {

    MacroBuiltinFunction handler;
    std::string name;

    static BuiltinMacro *create(MacroBuiltinFunction func,
        const std::string &name) {
        auto result = new BuiltinMacro();
        result->handler = func;
        result->name = name;
        return result;
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "builtin-macro");
        ss << " " << name;
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

typedef std::unordered_map<Flow *, std::vector<Any> >
    FlowValuesMap;

struct Frame {
    size_t idx;
    Frame *parent;
    FlowValuesMap map;

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "#" << idx << ":" << this;
        return ss.str();
    }

    std::string getRepr() const {
        std::stringstream ss;
        ss << "#" << idx << ":" << this << ":\n";
        for (auto &entry : map) {
            ss << "  " << get_string(pointer(Types::Flow, entry.first));
            auto &value = entry.second;
            for (size_t i = 0; i < value.size(); ++i) {
                ss << " " << get_string(value[i]);
            }
            ss << "\n";
        }
        return ss.str();
    }

    static Frame *create() {
        // create closure
        Frame *newframe = new Frame();
        newframe->parent = nullptr;
        newframe->idx = 0;
        return newframe;
    }

    static Frame *create(Frame *frame) {
        // create closure
        Frame *newframe = new Frame();
        newframe->parent = frame;
        newframe->idx = frame->idx + 1;
        return newframe;
    }
};

//------------------------------------------------------------------------------

struct Closure {
    Flow *cont;
    Frame *frame;

    static Closure *create(
        Flow *cont,
        Frame *frame) {
        auto result = new Closure();
        result->cont = cont;
        result->frame = frame;
        return result;
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "closure");
        ss << " " << get_string(pointer(Types::Flow, cont));
        ss << " " << get_string(pointer(Types::Frame, frame));
        ss << ")";
        return ss.str();
    }

};

//------------------------------------------------------------------------------

struct Macro {

    Any value;

    static Macro *create(const Any &value) {
        auto result = new Macro();
        result->value = value;
        return result;
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "macro");
        ss << " " << get_string(value);
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

namespace Types {

    static std::string _string_tostring(Type *self, const bangra::Any &value) {
        return *value.str;
    }

    static bool type_array_eq(Type *self, Type *other) {
        return (other == TArray);
    }

    static bool type_vector_eq(Type *self, Type *other) {
        return (other == TVector);
    }

    static bool type_pointer_eq(Type *self, Type *other) {
        return (other == TPointer);
    }

    static bool type_tuple_eq(Type *self, Type *other) {
        return (other == TTuple);
    }

    static bool type_cfunction_eq(Type *self, Type *other) {
        return (other == TCFunction);
    }

    static bool type_integer_eq(Type *self, Type *other) {
        return (other == TInteger);
    }

    static bool type_real_eq(Type *self, Type *other) {
        return (other == TReal);
    }

    static bool type_struct_eq(Type *self, Type *other) {
        return (other == TStruct);
    }

    static bool type_enum_eq(Type *self, Type *other) {
        return (other == TEnum);
    }

    static bangra::Any type_array_at(Type *self,
        const bangra::Any &value, size_t index) {
        auto padded_size = align(
            get_size(self->element_type), get_alignment(self->element_type));
        auto offset = padded_size * index;
        assert(offset < self->size);
        return from_pointer(self->element_type, (char *)value.ptr + offset);
    }

    static Type *__new_array_type(Type *element, size_t size) {
        assert(element);
        Type *type = new_type("");
        type->count = size;
        type->is_ref = true;
        type->at = type_array_at;
        type->element_type = element;
        auto padded_size = align(get_size(element), get_alignment(element));
        type->size = padded_size * type->count;
        type->alignment = element->alignment;
        return type;
    }

    static Type *_new_array_type(Type *element, size_t size) {
        assert(element);
        Type *type = __new_array_type(element, size);
        type->name = format("(%s %s %s)",
            ansi(ANSI_STYLE_KEYWORD, "@").c_str(),
            get_name(element).c_str(),
            ansi(ANSI_STYLE_NUMBER,
                format("%zu", size)).c_str());
        type->eq_type = type_array_eq;
        return type;
    }

    static Type *_new_vector_type(Type *element, size_t size) {
        assert(element);
        Type *type = __new_array_type(element, size);
        type->name = format("(%s %s %s)",
            ansi(ANSI_STYLE_KEYWORD, "vector").c_str(),
            get_name(element).c_str(),
            ansi(ANSI_STYLE_NUMBER,
                format("%zu", size)).c_str());
        type->eq_type = type_vector_eq;
        return type;
    }

    static bangra::Any type_pointer_at(Type *self,
        const bangra::Any &value, size_t index) {
        assert(index == 0);
        return from_pointer(self->element_type, (char *)value.ptr);
    }

    static Type *_new_pointer_type(Type *element) {
        assert(element);
        Type *type = new_type(
            format("(%s %s)",
                ansi(ANSI_STYLE_KEYWORD, "&").c_str(),
                get_name(element).c_str()));
        type->eq_type = type_pointer_eq;
        type->is_ref = false;
        type->at = type_pointer_at;
        type->element_type = element;
        type->size = ffi_type_pointer.size;
        type->alignment = ffi_type_pointer.alignment;
        return type;
    }

    static bangra::Any type_tuple_at(Type *self,
        const bangra::Any &value, size_t index) {
        assert(index < self->types.size());
        auto offset = self->offsets[index];
        return from_pointer(self->types[index], (char *)value.ptr + offset);
    }

    static void _set_field_names(Type *type, const std::vector<std::string> &names) {
        for (size_t i = 0; i < names.size(); ++i) {
            auto &name = names[i];
            if (!name.empty()) {
                type->name_index_map[name] = i;
            }
        }
    }

    static void _set_struct_field_types(Type *type, const std::vector<Type *> &types) {
        type->types = types;
        size_t offset = 0;
        size_t max_alignment = 1;
        for (auto &element : types) {
            size_t size = get_size(element);
            size_t alignment = get_alignment(element);
            max_alignment = std::max(max_alignment, alignment);
            offset = align(offset, alignment);
            type->offsets.push_back(offset);
            offset += size;
        }
        type->size = align(offset, max_alignment);
        type->alignment = max_alignment;
    }

    static void _set_union_field_types(Type *type, const std::vector<Type *> &types) {
        type->types = types;
        size_t max_size = 0;
        size_t max_alignment = 1;
        for (auto &element : types) {
            size_t size = get_size(element);
            size_t alignment = get_alignment(element);
            max_size = std::max(max_size, size);
            max_alignment = std::max(max_alignment, alignment);
        }
        type->size = align(max_size, max_alignment);
        type->alignment = max_alignment;
    }

    static Type *_new_tuple_type(std::vector<Type *> types) {
        std::stringstream ss;
        ss << "(";
        ss << ansi(ANSI_STYLE_KEYWORD, "tuple");
        for (auto &element : types) {
            ss << " " << get_name(element);
        }
        ss << ")";
        Type *type = new_type(ss.str());
        type->eq_type = type_tuple_eq;
        type->at = type_tuple_at;
        type->is_ref = true;
        _set_struct_field_types(type, types);
        return type;
    }

    /*
    // size of type in bytes
    size_t size;
    // is represented as reference in Any?
    bool is_ref;

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
    // array, vector, pointer, tuple, struct, union, function: field types
    // array, vector, pointer: only have one type
    // index 0 for functions is the result type, 1+ is parameter type
    std::vector<Type *> types;
    // tuple, struct, union: field offsets
    std::vector<size_t> offsets;
    // struct, union, enum: name to types index lookup table
    std::unordered_map<std::string, size_t> name_index_map;

    */

    static Type *_new_cfunction_type(
        Type *result, Type *parameters, bool vararg) {
        assert(eq(parameters, TTuple));
        std::stringstream ss;
        ss << "(";
        ss << ansi(ANSI_STYLE_KEYWORD, "cfunction");
        ss << " " << get_name(result);
        ss << " " << get_name(parameters);
        ss << " " << ansi(ANSI_STYLE_KEYWORD, vararg?"true":"false") << ")";
        Type *type = new_type(ss.str());
        type->eq_type = type_cfunction_eq;
        type->is_vararg = vararg;
        type->result_type = result;
        type->is_ref = false;
        type->types = { parameters };
        type->size = ffi_type_pointer.size;
        type->alignment = ffi_type_pointer.alignment;
        return type;
    }

    static Type *_new_integer_type(size_t width, bool signed_) {
        ffi_type *itype = nullptr;
        if (signed_) {
            switch (width) {
                case 8: itype = &ffi_type_sint8; break;
                case 16: itype = &ffi_type_sint16; break;
                case 32: itype = &ffi_type_sint32; break;
                case 64: itype = &ffi_type_sint64; break;
                default: assert(false && "invalid width"); break;
            }
        } else {
            switch (width) {
                case 8: itype = &ffi_type_uint8; break;
                case 16: itype = &ffi_type_uint16; break;
                case 32: itype = &ffi_type_uint32; break;
                case 64: itype = &ffi_type_uint64; break;
                default: assert(false && "invalid width"); break;
            }
        }

        Type *type = new_type(format("%sint%zu", signed_?"":"u", width));
        type->eq_type = type_integer_eq;
        type->is_ref = false;
        type->width = width;
        type->is_signed = signed_;
        type->size = itype->size;
        type->alignment = itype->alignment;
        return type;
    }

    static Type *_new_real_type(size_t width) {
        ffi_type *itype = nullptr;
        switch (width) {
            case 16: itype = &ffi_type_uint16; break;
            case 32: itype = &ffi_type_float; break;
            case 64: itype = &ffi_type_double; break;
            default: assert(false && "invalid width"); break;
        }

        Type *type = new_type(format("real%zu", width));
        type->eq_type = type_real_eq;
        type->is_ref = false;
        type->width = width;
        type->size = itype->size;
        type->alignment = itype->alignment;
        return type;
    }

    static Type *Struct(const std::string &name, bool builtin = false) {
        Type *type = new_type("");
        if (builtin) {
            type->name = name;
        } else {
            std::stringstream ss;
            ss << "(";
            ss << ansi(ANSI_STYLE_KEYWORD, "struct");
            ss << " " << name;
            ss << " " << type;
            ss << ")";
            type->name = ss.str();
        }
        type->eq_type = type_struct_eq;
        type->is_ref = true;
        type->at = type_tuple_at;
        return type;
    }

    static Type *Enum(const std::string &name) {
        std::stringstream ss;
        ss << "(";
        ss << ansi(ANSI_STYLE_KEYWORD, "enum");
        ss << " " << name;
        ss << ")";
        Type *type = new_type(ss.str());
        type->eq_type = type_enum_eq;
        type->is_ref = true;
        return type;
    }

    static void initTypes() {
        TType = Struct("type", true);

        TArray = Struct("array", true);
        TVector = Struct("vector", true);
        TTuple = Struct("tuple", true);
        TPointer = Struct("pointer", true);
        TCFunction = Struct("cfunction", true);
        TInteger = Struct("integer", true);
        TReal = Struct("real", true);
        TStruct = Struct("struct", true);
        TEnum = Struct("enum", true);

        Any = Struct("Any", true);
        AnchorRef = Struct("Anchor", true);

        None = Struct("None", true);
        const_none = make_any(Types::None);
        const_none.ptr = nullptr;

        Bool = Struct("bool", true);
        const_true = make_any(Types::Bool);
        const_true.i1 = true;

        const_false = make_any(Types::Bool);
        const_false.i1 = false;

        I8 = Integer(8, true);
        I16 = Integer(16, true);
        I32 = Integer(32, true);
        I64 = Integer(64, true);

        U8 = Integer(8, false);
        U16 = Integer(16, false);
        U32 = Integer(32, false);
        U64 = Integer(64, false);

        R16 = Real(16);
        R32 = Real(32);
        R64 = Real(64);

        String = Struct("String", true);
        String->tostring = _string_tostring;

        Parameter = Struct("Parameter", true);
        Table = Struct("Table", true);
        Symbol = Struct("Symbol", true);
        SList = Struct("SList", true);
        Builtin = Struct("Builtin", true);
        BuiltinFlow = Struct("BuiltinFlow", true);
        Flow = Struct("Flow", true);

        SpecialForm = Struct("SpecialForm", true);
        BuiltinMacro = Struct("BuiltinMacro", true);
        Frame = Struct("Frame", true);
        Closure = Struct("Closure", true);
        Macro = Struct("Macro", true);

        TypeRef = Pointer(TType);
        Rawstring = Pointer(I8);
    }

} // namespace Types

static void initConstants() {
    const_eol = make_any(Types::SList);
    const_eol.slist = new SList();
    const_eol.slist->at = const_eol;
    const_eol.slist->next = const_eol.slist;
}

/*
static Any tuple(const std::vector<Any> &values) {
    std::vector<Type *> types;
    size_t size;
    for (auto &v : values) {
        types.push_back(v.type);

    }
    Type *tuple_type = Types::Tuple(types);


}
*/

static Any wrap(Flow *flow) {
    return pointer(Types::Flow, flow);
}

static Any wrap(Parameter *param) {
    return pointer(Types::Parameter, param);
}

static Any wrap(SList *slist) {
    return pointer(Types::SList, slist);
}

static Any wrap(Type *type) {
    return pointer(Types::TypeRef, type);
}

static Any wrap(bool value) {
    return boolean(value);
}

static Any wrap(int64_t value) {
    return integer(Types::I64, value);
}

static Any wrap(
    const std::vector<Any> &args) {

    std::vector<Type *> types;
    for (auto &arg : args) {
        types.push_back(arg.type);
    }
    Type *type = Types::Tuple(types);
    char *data = (char *)malloc(get_size(type));
    for (size_t i = 0; i < args.size(); ++i) {
        auto elemtype = get_type(type, i);
        auto offset = get_offset(type, i);
        auto srcptr = get_pointer(args[i]);
        memcpy(data + offset, srcptr, get_size(elemtype));
    }

    return pointer(type, data);
}

static Any wrap(double value) {
    return real(Types::R64, value);
}

static Any wrap(const std::string &s) {
    return pstring(s);
}

//------------------------------------------------------------------------------
// IL MODEL UTILITY FUNCTIONS
//------------------------------------------------------------------------------

static void unescape(std::string &s) {
    s.resize(inplace_unescape(&s[0]));
}

// matches ((///...))
static bool is_comment(const Any &expr) {
    if (eq(expr.type, Types::SList)) {
        if (expr.slist != const_eol.slist) {
            Any &sym = expr.slist->at;
            if (eq(sym.type, Types::Symbol)) {
                if (!memcmp(sym.str->c_str(),"///",3))
                    return true;
            }
        }
    }
    return false;
}

static Any strip(const Any &expr) {
    if (eq(expr.type, Types::SList)) {
        std::vector<Any> values;
        auto it = SListIter(expr);
        while (it) {
            auto value = strip(*it);
            if (!is_comment(value)) {
                values.push_back(value);
            }
            it++;
        }
        return pointer(Types::SList, SList::create(values));
    }
    return expr;
}

static const Anchor *find_valid_anchor(const Any &expr) {
    // TODO
    /*
    if (!expr) return nullptr;
    if (expr->anchor.isValid()) return &expr->anchor;
    if (auto slist = llvm::dyn_cast<SListValue>(expr)) {
        auto it = SListIter(slist);
        while (it) {
            const Anchor *result = find_valid_anchor(*it);
            if (result) return result;
            it++;
        }
    }
    */
    return nullptr;
}
static size_t getSize(SList *expr) {
    SListIter it(expr, 0);
    size_t c = 0;
    while (it) {
        c++;
        it++;
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
    if (e.type == Types::SList) {
        auto it = SListIter(e);
        while (it) {
            if ((*it).type == Types::SList)
                return true;
            it++;
        }
    }
    return false;
}

template<typename T>
static void streamAnchor(T &stream, const Any &e, size_t depth=0) {
    //if (e) {
        const Anchor *anchor = find_valid_anchor(e);
        if (anchor) {
            stream <<
                format("%s:%i:%i: ",
                    anchor->path,
                    anchor->lineno,
                    anchor->column);
        }
    //}
    for(size_t i = 0; i < depth; i++)
        stream << "    ";
}

template<typename T>
static void streamValue(T &stream, const Any &e, size_t depth=0, bool naked=true) {
    if (naked) {
        streamAnchor(stream, e, depth);
    }

    if (e.type == Types::SList) {
        //auto slist = llvm::cast<SListValue>(e);
        auto it = SListIter(e);
        if (!it) {
            stream << "()";
            if (naked)
                stream << '\n';
            return;
        }
        size_t offset = 0;
        if (naked) {
            bool single = (it + 1);
        print_terse:
            streamValue(stream, *it++, depth, false);
            offset++;
            while (it) {
                if (isNested(*it))
                    break;
                stream << ' ';
                streamValue(stream, *it, depth, false);
                offset++;
                it++;
            }
            stream << (single?";\n":"\n");
        //print_sparse:
            while (it) {
                auto value = *it;
                if ((value.type != Types::SList) // not a list
                    && (offset >= 1) // not first element in list
                    && (it + 1) // not last element in list
                    && !isNested(*(it + 1))) { // next element can be terse packed too
                    single = false;
                    streamAnchor(stream, *it, depth + 1);
                    stream << "\\ ";
                    goto print_terse;
                }
                streamValue(stream, value, depth + 1);
                offset++;
                it++;
            }

        } else {
            stream << '(';
            while (it) {
                if (offset > 0)
                    stream << ' ';
                streamValue(stream, *it, depth + 1, false);
                offset++;
                it++;
            }
            stream << ')';
            if (naked)
                stream << '\n';
        }
    } else {
        if (e.type == Types::Symbol) {
            streamString(stream, *e.str, "[]{}()\"");
        } else if (e.type == Types::String) {
            stream << '"';
            streamString(stream, *e.str, "\"");
            stream << '"';
        } else {
            stream << get_string(e);
        }
        if (naked)
            stream << '\n';
    }
}

/*
static std::string formatValue(Value *e, size_t depth=0, bool naked=false) {
    std::stringstream ss;
    streamValue(ss, e, depth, naked);
    return ss.str();
}
*/

static void printValue(const Any &e, size_t depth=0, bool naked=false) {
    streamValue(std::cout, e, depth, naked);
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
}

void valueErrorV (const Any &expr, const char *fmt, va_list args) {
    const Anchor *anchor = find_valid_anchor(expr);
    if (!anchor) {
        printValue(expr);
        std::cout << "\n";
    }
    Anchor::printErrorV(anchor, fmt, args);
}

static void verifyValueKind(Type *type, const Any &expr) {
    if (!eq(expr.type, type)) {
        valueError(expr, "%s expected, not %s",
            get_string(pointer(Types::TType, type)).c_str(),
            get_string(pointer(Types::TType, expr.type)).c_str());
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

    Anchor *newAnchor() {
        auto anchor = new Anchor();
        anchor->path = path;
        anchor->lineno = lineno;
        anchor->column = column();
        anchor->offset = offset();
        return anchor;
    }

    void error( const char *format, ... ) {
        va_list args;
        va_start (args, format);
        size_t size = vsnprintf(nullptr, 0, format, args);
        va_end (args);
        error_string.resize(size);
        va_start (args, format);
        vsnprintf( &error_string[0], size + 1, format, args );
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
        auto result = make_any(Types::String, newAnchor());
        result.str = new std::string(string + 1, string_len - 2);
        unescape(*result.str);
        return result;
    }

    Any getAsSymbol() {
        auto result = make_any(Types::Symbol, newAnchor());
        result.str = new std::string(string, string_len);
        unescape(*result.str);
        return result;
    }

    Any getAsInteger() {
        size_t width;
        if (is_unsigned) {
            width = ((uint64_t)integer > (uint64_t)INT_MAX)?64:32;
        } else {
            width =
                ((integer < (int64_t)INT_MIN) || (integer > (int64_t)INT_MAX))?64:32;
        }
        auto type = Types::Integer(width, !is_unsigned);
        auto result = make_any(type, newAnchor());

        if (width == 32)
            result.i32 = (int32_t)integer;
        else
            result.i64 = integer;
        return result;
    }

    Any getAsReal() {
        auto result = make_any(Types::R32, newAnchor());
        result.r32 = real;
        return result;
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

    void error( const char *format, ... ) {
        ++errors;
        lexer.initAnchor(error_origin);
        parse_origin = error_origin;
        va_list args;
        va_start (args, format);
        size_t size = vsnprintf(nullptr, 0, format, args);
        va_end (args);
        error_string.resize(size);
        va_start (args, format);
        vsnprintf( &error_string[0], size + 1, format, args );
        va_end (args);
    }

    struct ListBuilder {
    protected:
        std::vector<Any> values;
        size_t start;
        Anchor anchor;
    public:

        ListBuilder(Lexer &lexer) :
            start(0) {
            lexer.initAnchor(anchor);
        }

        const Anchor &getAnchor() const {
            return anchor;
        }

        void resetStart() {
            start = getResultSize();
        }

        bool split() {
            // if we haven't appended anything, that's an error
            if (start == getResultSize()) {
                return false;
            }
            // move tail to new list
            std::vector<Any> newvalues(
                values.begin() + start, values.end());
            // remove tail
            values.erase(
                values.begin() + start,
                values.end());
            // append new list
            append(pointer(Types::SList, SList::create(newvalues)));
            resetStart();
            return true;
        }

        void append(const Any &item) {
            values.push_back(item);
        }

        size_t getResultSize() {
            return values.size();
        }

        const Any &getSingleResult() {
            return values.front();
        }

        Any getResult() {
            auto result = make_any(Types::SList, new Anchor(anchor));
            result.slist = SList::create(values);
            return result;
        }
    };

    Any parseList(int end_token) {
        ListBuilder builder(lexer);
        lexer.readToken();
        while (true) {
            if (lexer.token == end_token) {
                break;
            } else if (lexer.token == token_escape) {
                int column = lexer.column();
                lexer.readToken();
                auto elem = parseNaked(column, 1, end_token);
                if (errors) return const_none;
                builder.append(elem);
            } else if (lexer.token == token_eof) {
                error("missing closing bracket");
                // point to beginning of list
                error_origin = builder.getAnchor();
                return const_none;
            } else if (lexer.token == token_statement) {
                if (!builder.split()) {
                    error("empty expression");
                    return const_none;
                }
                lexer.readToken();
            } else {
                auto elem = parseAny();
                if (errors) return const_none;
                builder.append(elem);
                lexer.readToken();
            }
        }
        return builder.getResult();
    }

    Any parseAny () {
        assert(lexer.token != token_eof);
        if (lexer.token == token_open) {
            return parseList(token_close);
        } else if (lexer.token == token_square_open) {
            auto list = parseList(token_square_close);
            if (errors) return const_none;
            Any sym = make_any(Types::Symbol, list.anchor);
            sym.str = new std::string("[");
            return pointer(Types::SList, SList::create(sym, list.slist));
        } else if (lexer.token == token_curly_open) {
            auto list = parseList(token_curly_close);
            if (errors) return const_none;
            Any sym = make_any(Types::Symbol, list.anchor);
            sym.str = new std::string("{");
            return pointer(Types::SList, SList::create(sym, list.slist));
        } else if ((lexer.token == token_close)
            || (lexer.token == token_square_close)
            || (lexer.token == token_curly_close)) {
            error("stray closing bracket");
        } else if (lexer.token == token_string) {
            return lexer.getAsString();
        } else if (lexer.token == token_symbol) {
            return lexer.getAsSymbol();
        } else if (lexer.token == token_integer) {
            return lexer.getAsInteger();
        } else if (lexer.token == token_real) {
            return lexer.getAsReal();
        } else {
            error("unexpected token: %c (%i)", *lexer.cursor, (int)*lexer.cursor);
        }

        return const_none;
    }

    Any parseNaked (int column = 0, int depth = 0, int end_token = token_none) {
        int lineno = lexer.lineno;

        bool escape = false;
        int subcolumn = 0;

        ListBuilder builder(lexer);

        while (lexer.token != token_eof) {
            if (lexer.token == end_token) {
                break;
            } if (lexer.token == token_escape) {
                escape = true;
                lexer.readToken();
                if (lexer.lineno <= lineno) {
                    error("escape character is not at end of line");
                    parse_origin = builder.getAnchor();
                    return const_none;
                }
                lineno = lexer.lineno;
            } else if (lexer.lineno > lineno) {
                if (depth > 0) {
                    if (subcolumn == 0) {
                        subcolumn = lexer.column();
                    } else if (lexer.column() != subcolumn) {
                        error("indentation mismatch");
                        parse_origin = builder.getAnchor();
                        return const_none;
                    }
                } else {
                    subcolumn = lexer.column();
                }
                if (column != subcolumn) {
                    if ((column + 4) != subcolumn) {
                        //printf("%i %i\n", column, subcolumn);
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
                    auto elem = parseNaked(subcolumn, depth + 1, end_token);
                    if (errors) return const_none;
                    builder.append(elem);
                }
            } else if (lexer.token == token_statement) {
                if (!builder.split()) {
                    error("empty expression");
                    return const_none;
                }
                lexer.readToken();
                if (depth > 0) {
                    // if we are in the same line and there was no preceding ":",
                    // continue in parent
                    if (lexer.lineno == lineno)
                        break;
                }
            } else {
                auto elem = parseAny();
                if (errors) return const_none;
                builder.append(elem);
                lineno = lexer.next_lineno;
                lexer.readToken();
            }

            if (depth > 0) {
                if ((!escape || (lexer.lineno > lineno))
                    && (lexer.column() <= column)) {
                    break;
                }
            }
        }

        if (builder.getResultSize() == 1) {
            return builder.getSingleResult();
        } else {
            return builder.getResult();
        }
    }

    Any parseMemory (
        const char *input_stream, const char *eof, const char *path, int offset = 0) {
        init();
        lexer.init(input_stream, eof, path, offset);

        lexer.readToken();

        auto result = parseNaked(lexer.column());

        if (error_string.empty() && !lexer.error_string.empty()) {
            error_string = lexer.error_string;
            lexer.initAnchor(error_origin);
            parse_origin = error_origin;
        }

        if (!error_string.empty()) {
            printf("%s:%i:%i: error: %s\n",
                error_origin.path,
                error_origin.lineno,
                error_origin.column,
                error_string.c_str());
            dumpFileLine(path, error_origin.offset);
            if (!(parse_origin == error_origin)) {
                printf("%s:%i:%i: while parsing expression\n",
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
            fprintf(stderr, "unable to open file: %s\n", path);
            return const_none;
        }
    }


};

//------------------------------------------------------------------------------
// FOREIGN FUNCTION INTERFACE
//------------------------------------------------------------------------------

// TODO: libffi based calls

struct FFI {
    std::unordered_map<Type *, ffi_type *> ffi_types;

    FFI() {}

    ~FFI() {}

    ffi_type *new_type() {
        ffi_type *result = (ffi_type *)malloc(sizeof(ffi_type));
        memset(result, 0, sizeof(ffi_type));
        return result;
    }

    ffi_type *create_ffi_type(Type *type) {
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
        }
        error(wrap(type), "can not translate %s to FFI type",
            get_name(type).c_str());
        return nullptr;
    }

    ffi_type *get_ffi_type(Type *type) {
        auto it = ffi_types.find(type);
        if (it == ffi_types.end()) {
            auto result = create_ffi_type(type);
            ffi_types[type] = result;
            return result;
        } else {
            return it->second;
        }
    }

    Any runFunction(
        const Any &func, const std::vector<Any> &args) {
        assert(is_pointer_type(func.type));
        auto ftype = get_element_type(func.type);
        assert(is_cfunction_type(ftype));
        auto count = get_parameter_count(ftype);
        if (count != args.size()) {
            error(func, "parameter count mismatch");
        }
        if (is_vararg(ftype)) {
            error(func, "vararg functions not supported yet");
        }

        auto rtype = get_result_type(ftype);

        ffi_cif cif;
        ffi_type *argtypes[count];
        for (size_t i = 0; i < count; ++i) {
            auto ptype = get_parameter_type(ftype, i);
            if (!eq(ptype, args[i].type)) {
                error(args[i], "%s type expected, %s provided",
                    get_name(ptype).c_str(),
                    get_name(args[i].type).c_str());
            }
            argtypes[i] = get_ffi_type(ptype);
        }
        auto prep_result = ffi_prep_cif(
            &cif, FFI_DEFAULT_ABI, count, get_ffi_type(rtype), argtypes);
        assert(prep_result == FFI_OK);

        Any result = make_any(rtype);
        if (is_ref(rtype)) {
            // todo: align when necessary
            result.ptr = malloc(get_size(rtype));
        } else {
            result.ptr = nullptr;
        }
        void *rvalue = const_cast<void *>(get_pointer(result));

        void *avalues[count];
        for (size_t i = 0; i < count; ++i) {
            avalues[i] = const_cast<void *>(get_pointer(args[i]));
        }

        ffi_call(&cif, FFI_FN(func.ptr), rvalue, avalues);

        return result;
    }

};

static FFI *ffi;

//------------------------------------------------------------------------------
// INTERPRETER
//------------------------------------------------------------------------------

typedef std::vector<Any> ILValueArray;

Any evaluate(size_t argindex, Frame *frame, const Any &value) {
    if (value.type == Types::Parameter) {
        auto param = value.parameter;
        Frame *ptr = frame;
        while (ptr) {
            auto cont = param->parent;
            assert(cont && "parameter has no parent");
            if (ptr->map.count(cont)) {
                auto &values = ptr->map[cont];
                assert(param->index < values.size());
                return values[param->index];
            }
            ptr = ptr->parent;
        }
        // return unbound value
        return value;
    } else if (value.type == Types::Flow) {
        if (argindex == 0)
            // no closure creation required
            return value;
        else
            // create closure
            return pointer(Types::Closure, Closure::create(
                value.flow,
                frame));
    }
    return value;
}

Any execute(std::vector<Any> arguments) {

    Frame *frame = Frame::create();
    frame->idx = 0;

    auto retcont = Flow::create(1);
    // add special flow as return function
    arguments.push_back(pointer(Types::Flow, retcont));

    while (true) {
        assert(arguments.size() >= 1);
#ifdef BANGRA_DEBUG_IL
        std::cout << frame->getRefRepr();
        for (size_t i = 0; i < arguments.size(); ++i) {
            std::cout << " ";
            std::cout << getRefRepr(arguments[i]);
        }
        std::cout << "\n";
        fflush(stdout);
#endif
        Any callee = arguments[0];
        if ((callee.type == Types::Flow)
            && (callee.flow == retcont)) {
            if (arguments.size() >= 2) {
                return arguments[1];
            } else {
                return const_none;
            }
        }

        if (callee.type == Types::Closure) {
            auto closure = callee.closure;

            frame = closure->frame;
            callee = pointer(Types::Flow, closure->cont);
        }

        if (callee.type == Types::Flow) {
            auto flow = callee.flow;

            arguments.erase(arguments.begin());
            if (arguments.size() > 0) {
                if (frame->map.count(flow)) {
                    frame = Frame::create(frame);
                }
                frame->map[flow] = arguments;
            }

            assert(!flow->arguments.empty());
            size_t argcount = flow->arguments.size();
            arguments.resize(argcount);
            for (size_t i = 0; i < argcount; ++i) {
                arguments[i] = evaluate(i, frame,
                    flow->arguments[i]);
            }
        } else if (callee.type == Types::Builtin) {
            auto cb = callee.builtin;
            Any closure = arguments.back();
            arguments.pop_back();
            arguments.erase(arguments.begin());
            Any result = cb->handler(arguments);
            // generate fitting resume
            arguments.resize(2);
            arguments[0] = closure;
            arguments[1] = result;
        } else if (callee.type == Types::BuiltinFlow) {
            auto cb = callee.builtin_flow;
            arguments = cb->handler(arguments);
        } else if (
            is_pointer_type(callee.type)
                && is_cfunction_type(get_element_type(callee.type))) {
            Any closure = arguments.back();
            arguments.pop_back();
            arguments.erase(arguments.begin());
            Any result = ffi->runFunction(callee, arguments);
            // generate fitting resume
            arguments.resize(2);
            arguments[0] = closure;
            arguments[1] = result;
        } else {
            error(callee, "can not apply %s",
                get_string(callee).c_str());
        }
    }

    return const_none;
}

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
    std::unordered_map<std::string, Type *> named_structs;
    std::unordered_map<std::string, Type *> named_enums;
    std::unordered_map<std::string, Type *> typedefs;

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

    void GetFields(Type *struct_type, clang::RecordDecl * rd) {
        //auto &rl = Context->getASTRecordLayout(rd);

        std::vector<std::string> names;
        std::vector<Type *> types;
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
            Type *fieldtype = TranslateType(FT);
            if(!fieldtype) {
                break;
            }
            // todo: work offset into structure
            names.push_back(
                it->isAnonymousStructOrUnion()?"":
                                    declname.getAsString());
            types.push_back(fieldtype);
            //anchors->push_back(anchorFromLocation(it->getSourceRange().getBegin()));
        }

        Types::_set_field_names(struct_type, names);
        if (rd->isUnion()) {
            Types::_set_union_field_types(struct_type, types);
        } else {
            Types::_set_struct_field_types(struct_type, types);
        }
    }

    Type *TranslateRecord(clang::RecordDecl *rd) {
        if (!rd->isStruct() && !rd->isUnion()) return NULL;

        std::string name = rd->getName();

        Type *struct_type = nullptr;
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
            /*
            Anchor anchor = anchorFromLocation(rd->getSourceRange().getBegin());
            set_key(*struct_type, "anchor",
                pointer(Types::AnchorRef, new Anchor(anchor)));*/

            GetFields(struct_type, defn);

            auto &rl = Context->getASTRecordLayout(rd);
            auto align = rl.getAlignment();
            auto size = rl.getSize();
            // should match
            assert ((size_t)align.getQuantity() == struct_type->alignment);
            assert ((size_t)size.getQuantity() == struct_type->size);

            // todo: make sure these fit
            // align.getQuantity()
            // size.getQuantity()

            record_defined[rd] = true;
        }

        return struct_type;
    }

    Type *TranslateEnum(clang::EnumDecl *ed) {
        std::string name = ed->getName();

        Type *enum_type = nullptr;
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
            /*
            set_key(*enum_type, "anchor",
                pointer(Types::AnchorRef,
                    new Anchor(
                        anchorFromLocation(
                            ed->getIntegerTypeRange().getBegin()))));
            */

            enum_type->tag_type = TranslateType(ed->getIntegerType());

            std::vector<std::string> names;
            std::vector<int64_t> tags;
            std::vector<Type *> types;
            //auto anchors = new std::vector<Anchor>();

            for (auto it : ed->enumerators()) {
                //Anchor anchor = anchorFromLocation(it->getSourceRange().getBegin());
                auto &val = it->getInitVal();

                names.push_back(it->getName().data());
                tags.push_back(val.getExtValue());
                types.push_back(Types::None);
                //anchors->push_back(anchor);
            }

            enum_type->tags = tags;
            Types::_set_union_field_types(enum_type, types);
            Types::_set_field_names(enum_type, names);

            enum_defined[ed] = true;
        }

        return enum_type;
    }

    Type *TranslateType(clang::QualType T) {
        using namespace clang;

        const clang::Type *Ty = T.getTypePtr();

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
            assert (it != typedefs.end());
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
                return Types::None;
            } break;
            case clang::BuiltinType::Bool: {
                return Types::Bool;
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
                return Types::R16;
            } break;
            case clang::BuiltinType::Float: {
                return Types::R32;
            } break;
            case clang::BuiltinType::Double: {
                return Types::R64;
            } break;
            case clang::BuiltinType::LongDouble:
            case clang::BuiltinType::NullPtr:
            case clang::BuiltinType::UInt128:
            default:
                break;
            }
        case clang::Type::Complex:
        case clang::Type::LValueReference:
        case clang::Type::RValueReference:
            break;
        case clang::Type::Pointer: {
            const clang::PointerType *PTy = cast<clang::PointerType>(Ty);
            QualType ETy = PTy->getPointeeType();
            Type *pointee = TranslateType(ETy);
            if (pointee != NULL) {
                return Types::Pointer(pointee);
            }
        } break;
        case clang::Type::VariableArray:
        case clang::Type::IncompleteArray:
            break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            Type *at = TranslateType(ATy->getElementType());
            if(at) {
                int sz = ATy->getSize().getZExtValue();
                Types::Array(at, sz);
            }
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
                const clang::VectorType *VT = cast<clang::VectorType>(T);
                Type *at = TranslateType(VT->getElementType());
                if(at) {
                    int n = VT->getNumElements();
                    return Types::Vector(at, n);
                }
        } break;
        case clang::Type::FunctionNoProto:
        case clang::Type::FunctionProto: {
            const clang::FunctionType *FT = cast<clang::FunctionType>(Ty);
            if (FT) {
                return TranslateFuncType(FT);
            }
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
        fprintf(stderr, "type not understood: %s (%i)\n",
            T.getAsString().c_str(),
            Ty->getTypeClass());

        return NULL;
    }

    Type *TranslateFuncType(const clang::FunctionType * f) {

        bool valid = true;
        clang::QualType RT = f->getReturnType();

        Type *returntype = TranslateType(RT);

        if (!returntype)
            valid = false;

        bool vararg = false;

        const clang::FunctionProtoType * proto = f->getAs<clang::FunctionProtoType>();
        std::vector<Type *> argtypes;
        if(proto) {
            vararg = proto->isVariadic();
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                clang::QualType PT = proto->getParamType(i);
                Type *paramtype = TranslateType(PT);
                if(!paramtype) {
                    valid = false;
                } else if(valid) {
                    argtypes.push_back(paramtype);
                }
            }
        }

        if(valid) {
            return Types::CFunction(returntype,
                Types::Tuple(argtypes), vararg);
        }

        return NULL;
    }

    void exportType(const std::string &name, Type *type) {
        set_key(*dest, name,
            pointer(Types::TypeRef, type));
    }

    void exportExternal(const std::string &name, Type *type, const Anchor &anchor) {
        set_key(*dest, name,
            pointer(type,
                dlsym(NULL, name.c_str()),
                new Anchor(anchor)));
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

            Type *type = TranslateType(vd->getType());
            if (!type) return true;

            exportExternal(vd->getName().data(), type, anchor);
        }

        return true;

    }

    bool TraverseTypedefDecl(clang::TypedefDecl *td) {

        //Anchor anchor = anchorFromLocation(td->getSourceRange().getBegin());

        Type *type = TranslateType(td->getUnderlyingType());
        if (!type) return true;

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

        Type *functype = TranslateFuncType(fntyp);
        if (!functype)
            return true;

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
        return result;
    } else {
        assert(false && "compilation failed");
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// BUILTINS
//------------------------------------------------------------------------------

static bool builtin_checkparams (const std::vector<Any> &args,
    int mincount, int maxcount, int skip = 0) {
    if ((mincount <= 0) && (maxcount == -1))
        return true;

    int argcount = (int)args.size() - skip;

    if ((maxcount >= 0) && (argcount > maxcount)) {
        error(const_none,
            "excess argument. At most %i arguments expected", maxcount);
        return false;
    }
    if ((mincount >= 0) && (argcount < mincount)) {
        error(const_none, "at least %i arguments expected", mincount);
        return false;
    }
    return true;
}

static Any builtin_string(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    return pstring(get_string(args[0]));
}

static Any builtin_symbol(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    return symbol(get_string(args[0]));
}

static Any builtin_print(const std::vector<Any> &args) {
    builtin_checkparams(args, 0, -1);
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0)
            std::cout << " ";
        auto &arg = args[i];
        std::cout << get_string(arg);
    }
    std::cout << "\n";
    return const_none;
}

static Any builtin_branch(const std::vector<Any> &args) {
    builtin_checkparams(args, 4, 4, 1);
    auto cond = extract_bool(args[1]);
    if (cond) {
        return wrap({ args[2], args[4] });
    } else {
        return wrap({ args[3], args[4] });
    }
}

static std::vector<Any> builtin_call_cc(const std::vector<Any> &args) {
    builtin_checkparams(args, 2, 2, 1);
    return { args[1], args[2], args[2] };
}

static Any builtin_repr(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    return wrap(get_string(args[0]));
}

static Any builtin_tupleof(const std::vector<Any> &args) {
    builtin_checkparams(args, 0, -1);
    return wrap(args);
}

static Any builtin_slist(const std::vector<Any> &args) {
    builtin_checkparams(args, 0, -1);
    return pointer(Types::SList, SList::create(args));
}

static Any builtin_parameter(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    auto name = extract_any_string(args[0]);
    return pointer(Types::Parameter, Parameter::create(name));
}

static Any builtin_cons(const std::vector<Any> &args) {
    builtin_checkparams(args, 2, 2);
    auto &at = args[0];
    auto next = args[1];
    verifyValueKind(Types::SList, next);
    return pointer(Types::SList, SList::create(at, next.slist));
}

static Any builtin_structof(const std::vector<Any> &args) {
    builtin_checkparams(args, 0, -1);
    std::vector<std::string> names;
    std::vector<Any> values;
    for (size_t i = 0; i < args.size(); ++i) {
        auto pair = extract_tuple(args[i]);
        if (pair.size() != 2)
            error(args[i], "tuple must have exactly two elements");
        auto name = extract_any_string(pair[0]);
        names.push_back(name);
        auto value = pair[1];
        values.push_back(value);
    }

    Any t = wrap(values);

    auto struct_type = Types::Struct("");
    Types::_set_struct_field_types(struct_type, t.type->types);
    Types::_set_field_names(struct_type, names);

    return pointer(struct_type, t.ptr);
}

static Any builtin_syntax_macro(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    verifyValueKind(Types::Closure, args[0]);
    return pointer(Types::Macro, Macro::create(args[0]));
}

static Any builtin_typeof(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    return wrap(args[0].type);
}

static Any builtin_cdecl(const std::vector<Any> &args) {
    builtin_checkparams(args, 3, 3);
    Type *rettype = extract_type(args[0]);
    auto params = extract_tuple(args[1]);
    bool vararg = extract_bool(args[2]);

    std::vector<Type *> paramtypes;
    size_t paramcount = params.size();
    for (size_t i = 0; i < paramcount; ++i) {
        paramtypes.push_back(extract_type(params[i]));
    }
    return wrap(Types::CFunction(rettype, Types::Tuple(paramtypes), vararg));
}

static Any builtin_error(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    std::string msg = extract_string(args[0]);
    error(args[0], "%s", msg.c_str());
    return const_none;
}

// (import-c const-path (tupleof const-string ...))
static Any builtin_import_c(const std::vector<Any> &args) {
    builtin_checkparams(args, 2, 2);
    std::string path = extract_string(args[0]);
    auto compile_args = extract_tuple(args[1]);
    std::vector<std::string> cargs;
    for (size_t i = 0; i < compile_args.size(); ++i) {
        cargs.push_back(extract_string(compile_args[i]));
    }
    return pointer(Types::Table, bangra::importCModule(path, cargs));
}

template<BuiltinFunction F>
static Any builtin_variadic_ltr(const std::vector<Any> &args) {
    builtin_checkparams(args, 2, -1);
    Any result = F({args[0], args[1]});
    for (size_t i = 2; i < args.size(); ++i) {
        result = F({result, args[i]});
    }
    return result;
}

template<typename A, typename B>
class cast_join_type {};
template<typename T> class cast_join_type<T, T> {
    public: typedef T return_type; };
template<> class cast_join_type<int64_t, double> {
    public: typedef double return_type; };
template<> class cast_join_type<double, int64_t> {
    public: typedef double return_type; };

class builtin_add_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a + b; }
};
class builtin_sub_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a - b; }
};
class builtin_mul_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a * b; }
};
class builtin_div_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a / b; }
    static double operate(const int64_t &a, const int64_t &b) {
            return (double)a / (double)b; }
};
class builtin_mod_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return fmod(a,b); }
};

class builtin_bitand_op { public:
    template<typename T>
    static T operate(const T &a, const T &b) { return a & b; }
};
class builtin_bitor_op { public:
    template<typename T>
    static T operate(const T &a, const T &b) { return a | b; }
};
class builtin_bitxor_op { public:
    template<typename T>
    static T operate(const T &a, const T &b) { return a ^ b; }
};
class builtin_bitnot_op { public:
    template<typename T>
    static T operate(const T &x) { return ~x; }
};
class builtin_not_op { public:
    template<typename T>
    static T operate(const T &x) { return !x; }
};

static bool operator ==(const Any &a, const Any &b) {
    return eq(a, b);
}
static bool operator !=(const Any &a, const Any &b) {
    return !eq(a, b);
}

class builtin_eq_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a == b; }
};
class builtin_ne_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a != b; }
};
class builtin_gt_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a > b; }
};
class builtin_ge_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a >= b; }
};
class builtin_lt_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a < b; }
};
class builtin_le_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a <= b; }
};

template <class NextT>
class builtin_filter_op { public:
    static Any operate(const double &a, const int64_t &b) {
        return wrap(NextT::operate(a, b));
    }
    static Any operate(const int64_t &a, const double &b) {
        return wrap(NextT::operate(a, b));
    }
    template<typename T>
    static Any operate(const T &a, const T &b) {
        return wrap(NextT::operate(a, b));
    }
    template<typename Ta, typename Tb>
    static Any operate(const Ta &a, const Tb &b) {
        error(const_none, "illegal operands");
        return const_none;
    }
};


class dispatch_types_failed {
public:
    template<typename F>
    static Any dispatch(const Any &v, const F &next) {
        error(v, "illegal operand");
        return const_none;
    }
};

class dispatch_pointer_type {
public:
    template<typename F>
    static Any dispatch(const Any &v, const F &next) {
        return next(v);
    }
};

template<typename NextT>
class dispatch_string_type {
public:
    template<typename F>
    static Any dispatch(const Any &v, const F &next) {
        if (eq(v.type, Types::String)) {
            return next(*v.str);
        } else if (eq(v.type, Types::Symbol)) {
            return next(*v.str);
        } else {
            return NextT::template dispatch<F>(v, next);
        }
    }
};

template<typename NextT>
class dispatch_integer_type {
public:
    template<typename F>
    static Any dispatch(const Any &v, const F &next) {
        if (is_integer_type(v.type)) {
            return next(extract_integer(v));
        }
        return NextT::template dispatch<F>(v, next);
    }
};

template<typename NextT>
class dispatch_bool_type {
public:
    template<typename F>
    static Any dispatch(const Any &v, const F &next) {
        if (is_bool_type(v.type)) {
            return next(v.i1);
        }
        return NextT::template dispatch<F>(v, next);
    }
};

template<typename NextT>
class dispatch_real_type {
public:
    template<typename F>
    static Any dispatch(const Any &v, const F &next) {
        if (is_real_type(v.type)) {
            return next(extract_real(v));
        } else {
            return NextT::template dispatch<F>(v, next);
        }
    }
};

typedef dispatch_integer_type<
    dispatch_real_type <
        dispatch_types_failed> >
    dispatch_arithmetic_types;

typedef dispatch_integer_type<
            dispatch_real_type<
                dispatch_types_failed> >
    dispatch_arith_types;
typedef dispatch_integer_type<
            dispatch_real_type<
                dispatch_string_type<
                    dispatch_types_failed> > >
    dispatch_arith_string_types;
typedef dispatch_integer_type<
            dispatch_real_type<
                dispatch_string_type<
                    dispatch_pointer_type> > >
    dispatch_arith_string_ptr_types;
typedef dispatch_arithmetic_types dispatch_arith_cmp_types;
typedef dispatch_arith_string_types dispatch_cmp_types;
typedef dispatch_arith_string_ptr_types dispatch_eq_cmp_types;

typedef dispatch_integer_type<dispatch_types_failed>
    dispatch_bit_types;
typedef dispatch_bool_type<dispatch_types_failed>
    dispatch_boolean_types;

template<class D, class F>
class builtin_binary_op1 {
public:
    template<typename Q>
    Any operator ()(const Q &ca_value) const {
        return wrap(F::operate(ca_value));
    }
};

template<class D, class F, typename T>
class builtin_binary_op3 {
public:
    const T &ca_value;
    builtin_binary_op3(const T &ca_value_) : ca_value(ca_value_) {}

    template<typename Q>
    Any operator ()(const Q &cb_value) const {
        return builtin_filter_op<F>::operate(ca_value, cb_value);
    }
};

template<class D, class F>
class builtin_binary_op2 {
public:
    const Any &b;
    builtin_binary_op2(const Any &b_) : b(b_) {}

    template<typename T>
    Any operator ()(const T &ca_value) const {
        return D::dispatch(b, builtin_binary_op3<D, F, T>(ca_value));
    }
};

template<class D, class F>
static Any builtin_binary_op(
    const std::vector<Any> &args) {
    if (args.size() != 2) {
        error(const_none, "invalid number of arguments");
    }
    return D::dispatch(args[0], builtin_binary_op2<D, F>(args[1]));
}


template<class D, class F>
static Any builtin_unary_op(
    const std::vector<Any> &args) {
    if (args.size() != 1) {
        error(const_none, "invalid number of arguments");
    }
    return D::dispatch(args[0], builtin_binary_op1<D, F>());
}

//------------------------------------------------------------------------------
// TRANSLATION
//------------------------------------------------------------------------------

typedef Any (*bangra_preprocessor)(Table *, const Any &);

//------------------------------------------------------------------------------

static Table *new_scope() {
    return new_table();
}

static Table *new_scope(Table *scope) {
    assert(scope);
    auto subscope = new_table();
    set_key(*subscope, "#parent", pointer(Types::Table, scope));
    return subscope;
}

static void setLocal(Table *scope, const std::string &name, const Any &value) {
    assert(scope);
    set_key(*scope, name, value);
}

static void setBuiltin(
    Table *scope, const std::string &name, MacroBuiltinFunction func) {
    assert(scope);
    setLocal(scope, name,
        pointer(Types::BuiltinMacro, BuiltinMacro::create(func, name)));
}

static void setBuiltin(
    Table *scope, const std::string &name, SpecialFormFunction func) {
    assert(scope);
    setLocal(scope, name,
        pointer(Types::SpecialForm, SpecialForm::create(func, name)));
}

static void setBuiltin(
    Table *scope, const std::string &name, BuiltinFunction func) {
    assert(scope);
    setLocal(scope, name,
        pointer(Types::Builtin, Builtin::create(func, name)));
}

static void setBuiltin(
    Table *scope, const std::string &name, BuiltinFlowFunction func) {
    assert(scope);
    setLocal(scope, name,
        pointer(Types::BuiltinFlow, BuiltinFlow::create(func, name)));
}

/*
static bool isLocal(StructValue *scope, const std::string &name) {
    assert(scope);
    size_t idx = scope->struct_type->getFieldIndex(name);
    if (idx == (size_t)-1) return false;
    return true;
}
*/

static Table *getParent(Table *scope) {
    auto parent = get_key(*scope, "#parent", const_none);
    if (parent.type == Types::Table)
        return parent.table;
    return nullptr;
}

static Any getLocal(Table *scope, const std::string &name) {
    assert(scope);
    while (scope) {
        auto parent = get_key(*scope, name, const_none);
        if (parent.type != Types::None)
            return parent;
        scope = getParent(scope);
    }
    return const_none;
}

static std::unordered_map<std::string, bangra_preprocessor> preprocessors;

//------------------------------------------------------------------------------

static bool isSymbol (const Any &expr, const char *sym) {
    if (expr.type == Types::Symbol) {
        return (*expr.str) == sym;
    }
    return false;
}

//------------------------------------------------------------------------------
// MACRO EXPANDER
//------------------------------------------------------------------------------

static Table *globals = nullptr;

static bool verifyParameterCount (SList *expr,
    int mincount, int maxcount) {
    if ((mincount <= 0) && (maxcount == -1))
        return true;
    int argcount = (int)getSize(expr) - 1;

    if ((maxcount >= 0) && (argcount > maxcount)) {
        error(const_none,
            "excess argument. At most %i arguments expected", maxcount);
        return false;
    }
    if ((mincount >= 0) && (argcount < mincount)) {
        error(const_none, "at least %i arguments expected", mincount);
        return false;
    }
    return true;
}

static bool verifyParameterCount (SListIter topit,
    int mincount, int maxcount) {
    auto val = *topit;
    verifyValueKind(Types::SList, val);
    return verifyParameterCount(val.slist, mincount, maxcount);
}

//------------------------------------------------------------------------------

static Cursor expand (Table *env, SListIter topit);
static Any compile(const Any &expr);

static Any toparameter (Table *env, const Any &value) {
    if (eq(value.type, Types::Parameter))
        return value;
    verifyValueKind(Types::Symbol, value);
    auto bp = pointer(Types::Parameter, Parameter::create(*value.str));
    setLocal(env, *value.str, bp);
    return bp;
}

static SList *expand_expr_list (Table *env, SListIter it) {
    std::vector<Any> result;
    while (it) {
        auto cur = expand(env, it);
        result.push_back(cur.value);
        it = cur.next;
    }

    return SList::create(result);
}
static Cursor expand_function (Table *env, SListIter topit) {
    verifyParameterCount(topit, 1, -1);

    SListIter it(*topit++, 1);
    auto expr_parameters = *it++;

    auto subenv = new_scope(env);

    std::vector<Any> outargs;
    verifyValueKind(Types::SList, expr_parameters);
    auto params = expr_parameters.slist;
    SListIter param(params);
    while (param) {
        outargs.push_back(toparameter(subenv, *param));
        param++;
    }

    return {
        wrap(
            SList::create(
                getLocal(globals, "form:function"),
                SList::create(
                    wrap(SList::create(outargs)),
                        expand_expr_list(subenv, it)))),
        topit };
}

static Cursor expand_quote (Table *env, SListIter topit) {
    verifyParameterCount(topit, 1, -1);

    SListIter it(*topit++, 1);
    SList *rest = const_cast<SList*>(it.getSList());
    auto result = SList::create(getLocal(globals, "form:quote"), rest);
    return { wrap(result), topit };
}

static Cursor expand_escape (Table *env, SListIter topit) {
    SListIter it(*topit++, 1);
    return { *it, topit };
}

static Cursor expand_let_syntax (Table *env, SListIter topit) {

    auto cur = expand_function (env, topit++);

    auto fun = compile(cur.value);

    auto expr_env = execute({fun, pointer(Types::Table, env)});
    verifyValueKind(Types::Table, expr_env);

    auto rest = expand_expr_list(expr_env.table, topit);
    if (rest == const_eol.slist) {
        error(*topit, "missing subsequent expression");
    }

    return { rest->at, rest->next };
}

static SList *expand_macro(
    Table *env, const Any &handler, SListIter topit) {
    auto topexpr = const_cast<SList*>(topit.getSList());
    auto result = execute({handler,
        wrap(env),
        wrap(topexpr)});
    if (isnone(result))
        return nullptr;
    verifyValueKind(Types::SList, result);
    return result.slist;
}

static Cursor expand (Table *env, SListIter topit) {
    Any result = const_none;
process:
    Any expr = *topit;
    if (eq(expr.type, Types::SList)) {
        if (expr.slist == const_eol.slist) {
            error(expr, "expression is empty");
        }

        auto head = expr.slist->at;
        if (eq(head.type, Types::Symbol)) {
            head = getLocal(env, *head.str);
        }
        if (head.type != Types::None) {
            if (eq(head.type, Types::BuiltinMacro)) {
                return head.builtin_macro->handler(env, topit);
            } else if (eq(head.type, Types::Macro)) {
                auto result = expand_macro(env, head.macro->value, topit);
                if (result) {
                    topit = SListIter(result);
                    goto process;
                }
            }
        }

        auto default_handler = getLocal(env, "#slist");
        if (default_handler.type != Types::None) {
            auto result = expand_macro(env, default_handler, topit);
            if (result) {
                topit = SListIter(result);
                goto process;
            }
        }

        SListIter it(*topit);
        result = wrap(expand_expr_list(env, it));
        topit++;
    } else if (eq(expr.type, Types::Symbol)) {
        std::string value = *expr.str;
        result = getLocal(env, *expr.str);
        if (result.type != Types::None) {
            auto default_handler = getLocal(env, "#symbol");
            if (default_handler.type != Types::None) {
                auto result = expand_macro(env, default_handler, topit);
                if (result) {
                    topit = SListIter(result);
                    goto process;
                }
            }
        } else {
            error(expr,
                "unknown symbol '%s'", expr.str->c_str());
        }
        topit++;
    } else {
        result = expr;
        topit++;
    }
    assert(result.type != Types::None);
    return { result, topit };
}

static Any builtin_expand(const std::vector<Any> &args) {
    builtin_checkparams(args, 2, 2);
    auto scope = args[0];
    verifyValueKind(Types::Table, scope);
    auto expr_eval = args[1];

    auto retval = expand(scope.table, expr_eval);

    auto topexpr = const_cast<SList*>(retval.next.getSList());
    return wrap(SList::create(retval.value, topexpr));
}

static Any builtin_set_globals(const std::vector<Any> &args) {
    builtin_checkparams(args, 1, 1);
    auto scope = args[0];
    verifyValueKind(Types::Table, scope);

    globals = scope.table;
    return const_none;
}

//------------------------------------------------------------------------------
// COMPILER
//------------------------------------------------------------------------------

struct ILBuilder {
    struct State {
        Flow *flow;
        Flow *prevflow;
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
        restore({flow,nullptr});
    }

    void insertAndAdvance(
        const std::vector<Any> &values,
        Flow *next) {
        assert(state.flow);
        assert(!state.flow->hasArguments());
        state.flow->arguments = values;
        state.prevflow = state.flow;
        state.flow = next;
    }

    void br(const std::vector<Any> &arguments) {
        // patch previous flow destination to jump right to
        // continuation when possible
        assert(state.flow);
        if (state.prevflow
            && (arguments.size() == 2)
            && (eq(Types::Parameter, arguments[1].type))
            && (arguments[1].parameter == state.flow->parameters[0])
            && (eq(Types::Flow, state.prevflow->arguments.back().type))
            && (state.prevflow->arguments.back().flow == state.flow)) {
            state.prevflow->arguments.back() = arguments[0];
        } else {
            insertAndAdvance(arguments, nullptr);
        }
    }

    Parameter *call(std::vector<Any> values) {
        auto next = Flow::create(1, "cret");
        values.push_back(wrap(next));
        insertAndAdvance(values, next);
        return next->parameters[0];
    }

};

static ILBuilder *builder;

//------------------------------------------------------------------------------

static Any compile_expr_list (SListIter it) {
    Any value = const_none;
    while (it) {
        value = compile(*it);
        it++;
    }
    return value;
}

static Any compile_do (SListIter it) {
    it++;
    return compile_expr_list(it);
}

static Any compile_function (SListIter it) {
    it++;

    auto expr_parameters = *it++;

    auto currentblock = builder->save();

    auto function = Flow::create(0, "func");

    builder->continueAt(function);

    verifyValueKind(Types::SList, expr_parameters);
    SListIter param(expr_parameters.slist);
    while (param) {
        verifyValueKind(Types::Parameter, *param);
        function->appendParameter((*param).parameter);
        param++;
    }
    auto ret = function->appendParameter(Parameter::create());

    auto result = compile_expr_list(it);

    builder->br({wrap(ret), result});

    builder->restore(currentblock);

    return wrap(function);
}

static Any compile_implicit_call (SListIter it) {
    Any callable = compile(*it++);

    std::vector<Any> args;
    args.push_back(callable);

    while (it) {
        args.push_back(compile(*it));
        it++;
    }

    return wrap(builder->call(args));
}

static Any compile_call (SListIter it) {
    it++;
    return compile_implicit_call(it);
}

static Any compile_quote (SListIter it) {
    it++;
    SList *rest = const_cast<SList*>(it.getSList());
    if (rest->next == const_eol.slist)
        return rest->at;
    else
        return wrap(rest);
}

//------------------------------------------------------------------------------

static Any compile (const Any &expr) {
    Any result = const_none;
    if (eq(expr.type, Types::SList)) {
        if (expr.slist == const_eol.slist) {
            error(expr, "empty expression");
        }
        auto slist = expr.slist;
        auto head = slist->at;
        if (eq(head.type, Types::SpecialForm)) {
            result = head.special_form->handler(SListIter(slist));
        } else {
            result = compile_implicit_call(SListIter(slist));
        }
        assert(result.type != Types::None);
    } else {
        result = expr;
    }
    return result;
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

static int print_number(int value) {
    printf("NUMBER: %i\n", value);
    return value + 1;
}

static void initGlobals () {
    globals = new_scope();
    auto env = globals;

    setLocal(env, "globals", wrap(env));

    setBuiltin(env, "form:call", compile_call);
    setBuiltin(env, "form:function", compile_function);
    setBuiltin(env, "form:quote", compile_quote);
    setBuiltin(env, "do", compile_do);

    setBuiltin(env, "function", expand_function);
    setBuiltin(env, "quote", expand_quote);
    setBuiltin(env, "let-syntax", expand_let_syntax);
    setBuiltin(env, "escape", expand_escape);

    // test
    setLocal(env, "print-number",
        pointer(
            Types::Pointer(
                Types::CFunction(Types::I32, Types::Tuple({Types::I32}), false)),
            (void *)print_number));

    setLocal(env, "void", wrap(Types::None));
    setLocal(env, "None", wrap(Types::None));
    setLocal(env, "half", wrap(Types::R16));
    setLocal(env, "float", wrap(Types::R32));
    setLocal(env, "double", wrap(Types::R64));
    setLocal(env, "bool", wrap(Types::Bool));

    setLocal(env, "int8", wrap(Types::I8));
    setLocal(env, "int16", wrap(Types::I16));
    setLocal(env, "int32", wrap(Types::I32));
    setLocal(env, "int64", wrap(Types::I64));

    setLocal(env, "uint8", wrap(Types::U8));
    setLocal(env, "uint16", wrap(Types::U16));
    setLocal(env, "uint32", wrap(Types::U32));
    setLocal(env, "uint64", wrap(Types::U64));

    setLocal(env, "usize_t",
        wrap(Types::Integer(sizeof(size_t)*8,false)));

    setLocal(env, "rawstring", wrap(Types::Rawstring));

    setLocal(env, "int", getLocal(env, "int32"));

    setLocal(env, "true", wrap(true));
    setLocal(env, "false", wrap(false));

    setLocal(env, "none", const_none);

    setBuiltin(env, "print", builtin_print);
    setBuiltin(env, "repr", builtin_repr);
    setBuiltin(env, "cdecl", builtin_cdecl);
    setBuiltin(env, "tupleof", builtin_tupleof);
    setBuiltin(env, "slist", builtin_slist);
    setBuiltin(env, "cons", builtin_cons);
    setBuiltin(env, "structof", builtin_structof);
    setBuiltin(env, "typeof", builtin_typeof);
    //setBuiltin(env, "external", builtin_external);
    setBuiltin(env, "import-c", builtin_import_c);
    setBuiltin(env, "branch", builtin_branch);
    setBuiltin(env, "call/cc", builtin_call_cc);
    //setBuiltin(env, "dump", builtin_dump);
    setBuiltin(env, "syntax-macro", builtin_syntax_macro);
    setBuiltin(env, "string", builtin_string);
    setBuiltin(env, "symbol", builtin_symbol);
    setBuiltin(env, "parameter", builtin_parameter);
    //setBuiltin(env, "empty?", builtin_is_empty);
    setBuiltin(env, "expand", builtin_expand);
    setBuiltin(env, "set-globals!", builtin_set_globals);
    //setBuiltin(env, "slist?", builtin_is_slist);
    //setBuiltin(env, "symbol?", builtin_is_symbol);
    //setBuiltin(env, "integer?", builtin_is_integer);
    //setBuiltin(env, "null?", builtin_is_null);
    //setBuiltin(env, "key?", builtin_is_key);
    setBuiltin(env, "error", builtin_error);
    //setBuiltin(env, "length", builtin_length);

    //setBuiltin(env, "@", builtin_variadic_ltr<builtin_at_op>);

    setBuiltin(env, "+",
        builtin_variadic_ltr<
            builtin_binary_op<dispatch_arith_string_types, builtin_add_op>
            >);
    setBuiltin(env, "-",
        builtin_binary_op<dispatch_arith_types, builtin_sub_op>);
    setBuiltin(env, "*",
        builtin_variadic_ltr<
            builtin_binary_op<dispatch_arith_types, builtin_mul_op>
            >);
    setBuiltin(env, "/",
        builtin_binary_op<dispatch_arith_types, builtin_div_op>);
    setBuiltin(env, "%",
        builtin_binary_op<dispatch_arith_types, builtin_mod_op>);

    setBuiltin(env, "&",
        builtin_binary_op<dispatch_bit_types, builtin_bitand_op>);
    setBuiltin(env, "|",
        builtin_variadic_ltr<
            builtin_binary_op<dispatch_bit_types, builtin_bitor_op>
            >);
    setBuiltin(env, "^",
        builtin_binary_op<dispatch_bit_types, builtin_bitxor_op>);
    setBuiltin(env, "~",
        builtin_unary_op<dispatch_bit_types, builtin_bitnot_op>);

    setBuiltin(env, "not",
        builtin_unary_op<dispatch_boolean_types, builtin_not_op>);

    setBuiltin(env, "==",
        builtin_binary_op<dispatch_eq_cmp_types, builtin_eq_op>);
    setBuiltin(env, "!=",
        builtin_binary_op<dispatch_eq_cmp_types, builtin_ne_op>);
    setBuiltin(env, ">",
        builtin_binary_op<dispatch_cmp_types, builtin_gt_op>);
    setBuiltin(env, ">=",
        builtin_binary_op<dispatch_cmp_types, builtin_ge_op>);
    setBuiltin(env, "<",
        builtin_binary_op<dispatch_cmp_types, builtin_lt_op>);
    setBuiltin(env, "<=",
        builtin_binary_op<dispatch_cmp_types, builtin_le_op>);

}

static void init() {
    bangra::support_ansi = isatty(fileno(stdout));

    Types::initTypes();
    initConstants();

    LLVMEnablePrettyStackTrace();
    LLVMLinkInMCJIT();
    //LLVMLinkInInterpreter();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeDisassembler();

    ffi = new FFI();
    builder = new ILBuilder();

    initGlobals();
}

//------------------------------------------------------------------------------

static void handleException(Table *env, const Any &expr) {
    streamValue(std::cerr, expr, 0, true);
    error(expr, "an exception was raised");
}

static bool compileRootValueList (Table *env, const Any &expr) {

    auto expexpr = expand_expr_list(env, SListIter(expr, 1));

    auto mainfunc = Flow::create();
    auto ret = mainfunc->appendParameter(Parameter::create());
    builder->continueAt(mainfunc);

    compile_expr_list(SListIter(expexpr));
    builder->br({ wrap(ret) });

/*
#ifdef BANGRA_DEBUG_IL
    std::cout << env.global.module->getRepr();
    fflush(stdout);
#endif
*/

    execute({wrap(mainfunc)});

    return true;
}

static bool compileMain (Any expr) {
    verifyValueKind(Types::SList, expr);
    auto it = SListIter(expr.slist);

    auto env = globals;

    std::string lastlang = "";
    while (true) {
        verifyValueKind(Types::Symbol, *it);
        auto &head = *(*it).str;
        if (head == BANGRA_HEADER)
            break;
        auto preprocessor = preprocessors[head];
        if (!preprocessor) {
            error(expr, "unrecognized header: '%s'; try '%s' instead.",
                head.c_str(),
                BANGRA_HEADER);
            return false;
        }
        if (lastlang == head) {
            error(expr,
                "header has not changed after preprocessing; is still '%s'.",
                head.c_str());
        }
        lastlang = head;
        auto orig_expr = expr;
        try {
            expr = preprocessor(env, expr);
        } catch (Any expr) {
            handleException(env, expr);
            return false;
        }
        if (isnone(expr)) {
            error(orig_expr,
                "preprocessor returned none.");
            return false;
        }
    }

    return compileRootValueList (env, expr);
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
        fprintf(stderr, "could not open binary\n");
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
        fprintf(stderr, "could not parse footer expression\n");
        return const_none;
    }
    if (!eq(expr.type, Types::SList))  {
        fprintf(stderr, "footer expression is not a symbolic list\n");
        return const_none;
    }
    auto symlist = expr.slist;
    SListIter it(symlist);
    if (!it) {
        fprintf(stderr, "footer expression is empty\n");
        return const_none;
    }
    auto head = *it++;
    if (!eq(head.type, Types::Symbol))  {
        fprintf(stderr, "footer expression does not begin with symbol\n");
        return const_none;
    }
    if (!isSymbol(head, "script-size"))  {
        fprintf(stderr, "footer expression does not begin with 'script-size'\n");
        return const_none;
    }
    if (!it) {
        fprintf(stderr, "footer expression needs two arguments\n");
        return const_none;
    }
    auto arg = *it++;
    if (!is_integer_type(arg.type))  {
        fprintf(stderr, "script-size argument is not integer\n");
        return const_none;
    }
    auto offset = extract_integer(arg);
    if (offset <= 0) {
        fprintf(stderr, "script-size must be larger than zero\n");
        return const_none;
    }
    bangra::Parser parser;
    auto script_start = cursor - offset;
    return parser.parseMemory(script_start, cursor, executable_path, script_start - ptr);
}

static bool compileStartupScript() {
    char *base = strdup(bang_executable_path);
    char *ext = extension(base);
    if (ext) {
        *ext = 0;
    }
    std::string path = format("%s.b", base);
    free(base);

    Any expr = const_none;
    {
        auto file = MappedFile::open(path.c_str());
        if (file) {
            // keep a resident copy
            char *pathcpy = strdup(path.c_str());
            bangra::Parser parser;
            expr = parser.parseMemory(
                file->strptr(), file->strptr() + file->size(),
                pathcpy);
        }
    }

    if (!isnone(expr)) {
        return bangra::compileMain(expr);
    }

    return true;
}

} // namespace bangra

// C API
//------------------------------------------------------------------------------

char *bang_executable_path = NULL;
int bang_argc = 0;
char **bang_argv = NULL;

void print_version() {
    std::string versionstr = bangra::format("%i.%i",
        BANGRA_VERSION_MAJOR, BANGRA_VERSION_MINOR);
    if (BANGRA_VERSION_PATCH) {
        versionstr += bangra::format(".%i", BANGRA_VERSION_PATCH);
    }
    printf(
    "Bangra version %s\n"
    "Executable path: %s\n"
    , versionstr.c_str()
    , bang_executable_path
    );
    exit(0);
}

void print_help(const char *exename) {
    printf(
    "usage: %s [option [...]] [filename]\n"
    "Options:\n"
    "   -h, --help                  print this text and exit.\n"
    "   -v, --version               print program version and exit.\n"
    "   --skip-startup              skip startup script.\n"
    "   -a, --enable-ansi           enable ANSI output.\n"
    "   --                          terminate option list.\n"
    , exename
    );
    exit(0);
}

int bangra_main(int argc, char ** argv) {
    bang_argc = argc;
    bang_argv = argv;

    bangra::init();

    bangra::Any expr = bangra::const_none;

    if (argv) {
        if (argv[0]) {
            std::string loader = bangra::GetExecutablePath(argv[0]);
            // string must be kept resident
            bang_executable_path = strdup(loader.c_str());

            expr = bangra::parseLoader(bang_executable_path);
        }

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
                        } else if (!strcmp(*arg, "--")) {
                            parse_options = false;
                        } else {
                            printf("unrecognized option: %s. Try --help for help.\n", *arg);
                            exit(1);
                        }
                    } else if (!sourcepath) {
                        sourcepath = *arg;
                    } else {
                        printf("unrecognized argument: %s. Try --help for help.\n", *arg);
                        exit(1);
                    }
                }
            }

            if (!skip_startup && bang_executable_path) {
                if (!bangra::compileStartupScript()) {
                    return 1;
                }
            }

            if (sourcepath) {
                bangra::Parser parser;
                expr = parser.parseFile(sourcepath);
            }
        }
    }

    if (isnone(expr)) {
        return 1;
    } else {
        bangra::compileMain(expr);
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

#endif // BANGRA_MAIN_CPP_IMPL
