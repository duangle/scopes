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

#ifdef BANGRA_CPP_IMPL
namespace bangra {
struct Value;
struct Environment;
} // namespace bangra
typedef bangra::Value Value;
typedef bangra::Environment Environment;
#else
typedef struct _Environment Environment;
typedef struct _Value Value;
#endif

typedef Value *ValueRef;

extern int bang_argc;
extern char **bang_argv;
extern char *bang_executable_path;

// high level
//------------------------------------------------------------------------------

int bangra_main(int argc, char ** argv);
ValueRef bangra_parse_file(const char *path);

// LLVM compatibility
//------------------------------------------------------------------------------

Environment *bangra_parent_env(Environment *env);
void *bangra_import_c_module(ValueRef dest,
    const char *path, const char **args, int argcount);
void *bangra_import_c_string(ValueRef dest,
    const char *str, const char *path, const char **args, int argcount);


// methods that apply to all types
//------------------------------------------------------------------------------

int bangra_get_kind(ValueRef expr);
int bangra_eq(Value *a, Value *b);

ValueRef bangra_clone(ValueRef expr);
ValueRef bangra_deep_clone(ValueRef expr);

ValueRef bangra_next(ValueRef expr);
ValueRef bangra_set_next(ValueRef lhs, ValueRef rhs);
ValueRef bangra_set_next_mutable(ValueRef lhs, ValueRef rhs);

void bangra_print_value(ValueRef expr, int depth);
ValueRef bangra_format_value(ValueRef expr, int depth);

const char *bangra_anchor_path(ValueRef expr);
int bangra_anchor_lineno(ValueRef expr);
int bangra_anchor_column(ValueRef expr);
int bangra_anchor_offset(ValueRef expr);
ValueRef bangra_set_anchor(
    ValueRef expr, const char *path, int lineno, int column, int offset);
ValueRef bangra_set_anchor_mutable(
    ValueRef expr, const char *path, int lineno, int column, int offset);

// pointer
//------------------------------------------------------------------------------

ValueRef bangra_ref(ValueRef lhs);
ValueRef bangra_at(ValueRef expr);
ValueRef bangra_set_at_mutable(ValueRef lhs, ValueRef rhs);

// string and symbol
//------------------------------------------------------------------------------

ValueRef bangra_string(const char *value, signed long long int size);
ValueRef bangra_symbol(const char *value);
const char *bangra_string_value(ValueRef expr);
signed long long int bangra_string_size(ValueRef expr);
ValueRef bangra_string_concat(ValueRef a, ValueRef b);
ValueRef bangra_string_slice(ValueRef expr, int start, int end);

// real
//------------------------------------------------------------------------------

ValueRef bangra_real(double value);
double bangra_real_value(ValueRef value);

// integer
//------------------------------------------------------------------------------

ValueRef bangra_integer(signed long long int value);
signed long long int bangra_integer_value(ValueRef value);

// table
//------------------------------------------------------------------------------

ValueRef bangra_table();
void bangra_set_key(ValueRef expr, ValueRef key, ValueRef value);
ValueRef bangra_get_key(ValueRef expr, ValueRef key);
void bangra_set_meta(ValueRef expr, ValueRef meta);
ValueRef bangra_get_meta(ValueRef expr);

// handle
//------------------------------------------------------------------------------

ValueRef bangra_handle(void *ptr);
void *bangra_handle_value(ValueRef expr);

// exception handling
//------------------------------------------------------------------------------

void *bangra_xpcall (void *ctx,
    void *(*try_func)(void *),
    void *(*except_func)(void *, ValueRef));
void bangra_raise (ValueRef expr);

// metaprogramming
//------------------------------------------------------------------------------

typedef ValueRef (*bangra_preprocessor)(Environment *, ValueRef );

void bangra_error_message(
    Environment *env, ValueRef context, const char *format, ...);
void bangra_set_preprocessor(const char *name, bangra_preprocessor f);
bangra_preprocessor bangra_get_preprocessor(const char *name);
ValueRef bangra_unique_symbol(const char *name);

#if defined __cplusplus
}
#endif

#endif // BANGRA_CPP
#ifdef BANGRA_CPP_IMPL

/*
TODO:
    - validate getelementptr arguments where possible
    - validate that used values are in the same block
    - validate: Call parameter type does not match function signature!
    - validate: PHI node operands are not the same type as the result!
    - validate: Called function must be a pointer!
    - validate: Function return type does not match operand type of return inst!
    - validate: Function arguments must have first-class types!
        (passing function without pointer to constructor)
*/

#define BANGRA_HEADER "bangra"

//------------------------------------------------------------------------------
// SHARED LIBRARY IMPLEMENTATION
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------

template<typename T>
class shared_back_ptr;

template<typename T>
class enable_shared_back_from_this :
    public std::enable_shared_from_this<T> {
public:
private:
    friend class shared_back_ptr<T>;

    shared_back_ptr<T> *_first_shared_back_ptr;

    T *_shared_back_ptr_upcast() const {
        return static_cast<T*>(this);
    }

public:

    enable_shared_back_from_this() :
        _first_shared_back_ptr(nullptr)
    {
    }

    enable_shared_back_from_this(const enable_shared_back_from_this<T> &other) :
        _first_shared_back_ptr(nullptr)
    {
    }

    template<typename PointerT>
    struct iteratorT {
        PointerT *_p;
        PointerT *_next;

        iteratorT(PointerT *p) :
            _p(p),
            _next(p?p->_next:nullptr)
        {}

        bool operator !=(const iteratorT &other) const {
            return _p != other._p;
        }

        void operator ++() {
            if (_p) {
                _p = _next;
                _next = _p?_p->_next:nullptr;
            }
        }

        PointerT *operator *() const {
            return _p;
        }
    };

    struct back_ptr_iterable {
        std::shared_ptr<T> _ref;
        typedef iteratorT<shared_back_ptr<T> > iterator;
        typedef iteratorT<const shared_back_ptr<T> > const_iterator;

        back_ptr_iterable(const std::shared_ptr<T> &ref) :
            _ref(ref) {}

        iterator begin() { return _ref->_first_shared_back_ptr; }
        const_iterator begin() const { return _ref->_first_shared_back_ptr; }
        iterator end() { return nullptr; }
        const_iterator end() const { return nullptr; }
    };

    back_ptr_iterable back_ptrs() {
        return back_ptr_iterable(
            std::enable_shared_from_this<T>::shared_from_this());
    }

    size_t back_refcount() {
        size_t count = 0;
        for (auto link : back_ptrs()) {
            if (link) {
                ++count;
            }
        }
        return count;
    }

};

template<typename T>
class shared_back_ptr {
    friend struct std::hash< bangra::shared_back_ptr<T> >;
    friend struct bangra::enable_shared_back_from_this<T>::iteratorT<shared_back_ptr<T> >;
    friend struct bangra::enable_shared_back_from_this<T>::iteratorT<const shared_back_ptr<T> >;
private:
    std::shared_ptr<T> _ref;
    shared_back_ptr *_prev;
    shared_back_ptr *_next;

    void _link_shared_back_ptr() {
        if (_ref) {
            _prev = nullptr;
            _next = _ref->_first_shared_back_ptr;
            if (_next) {
                assert(_next->_prev == nullptr);
                _next->_prev = this;
            }
            _ref->_first_shared_back_ptr = this;
        }
    }

    void _unlink_shared_back_ptr() {
        if (_ref) {
            if (_next) {
                _next->_prev = _prev;
            }
            if (_prev) {
                _prev->_next = _next;
            } else {
                assert(_ref->_first_shared_back_ptr == this);
                _ref->_first_shared_back_ptr = _next;
            }
            _prev = _next = nullptr;
            _ref = nullptr;
        }
    }

public:
    shared_back_ptr() :
        _ref(nullptr),
        _prev(nullptr),
        _next(nullptr)
    {}

    shared_back_ptr(const std::nullptr_t &) :
        _ref(nullptr),
        _prev(nullptr),
        _next(nullptr)
    {}

    template<typename AnyT>
    shared_back_ptr(const std::shared_ptr<AnyT> &ref) :
        _ref(ref)
    {
        _link_shared_back_ptr();
    }

    shared_back_ptr(const shared_back_ptr &O) :
        _ref(O._ref) {
        _link_shared_back_ptr();
    }

    ~shared_back_ptr() {
        _unlink_shared_back_ptr();
    }

    T *get() const {
        return _ref.get();
    }

    T *operator ->() const {
        return _ref.get();
    }

    operator bool() const {
        return (bool)_ref;
    }

    operator const std::shared_ptr<T> &() const {
        return _ref;
    }

    void operator =(const std::nullptr_t &) {
        _unlink_shared_back_ptr();
    }

    template<typename AnyT>
    void operator =(const std::shared_ptr<AnyT> &ref) {
        if (_ref == ref) return;
        _unlink_shared_back_ptr();
        _ref = ref;
        _link_shared_back_ptr();
    }

};

} // namespace bangra

namespace std {
    template<typename T>
    struct hash< bangra::shared_back_ptr<T> >
    {
        typedef bangra::shared_back_ptr<T> argument_type;
        typedef std::size_t result_type;
        result_type operator()(argument_type const& s) const {
            return std::hash<std::shared_ptr<T> >{}(s._ref);
        }
    };
}

namespace bangra {

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
// DATA MODEL
//------------------------------------------------------------------------------

enum ValueKind {
    V_None = 0,
    V_Pointer = 1,
    V_String = 2,
    V_Symbol = 3,
    V_Integer = 4,
    V_Real = 5,
    V_Handle = 6,
    V_Table = 7
};

static const char *valueKindName(int kind) {
    switch(kind) {
    case V_None: return "null";
    case V_Pointer: return "list";
    case V_String: return "string";
    case V_Symbol: return "symbol";
    case V_Integer: return "integer";
    case V_Real: return "real";
    case V_Handle: return "handle";
    case V_Table: return "table";
    default: return "#corrupted#";
    }
}

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

    static void printMessageV (Anchor *anchor, const char *fmt, va_list args) {
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

    static void printErrorV (Anchor *anchor, const char *fmt, va_list args) {
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

struct Value;
static void printValue(ValueRef e, size_t depth=0, bool naked=true);
static std::string formatValue(ValueRef e, size_t depth=0, bool naked=true);

static ValueRef next(ValueRef expr);

struct Value {
private:
    const ValueKind kind;
    int age;
    // NULL = end of list
    ValueRef next;
protected:
    Value(ValueKind kind_, ValueRef next_ = nullptr) :
        kind(kind_),
        age(0),
        next(next_) {
        collection.push_back(this);
    }

public:
    typedef std::list<ValueRef> CollectionType;

    static CollectionType collection;

    Anchor anchor;

    virtual ~Value() {}

    Anchor *findValidAnchor();
    ValueRef getNext() const {
        return next;
    }

    void setNext(ValueRef next) {
        this->next = next;
    }

    size_t count() {
        ValueRef self = this;
        size_t count = 0;
        while (self) {
            ++ count;
            self = bangra::next(self);
        }
        return count;
    }

    ValueKind getKind() const {
        return kind;
    }

    std::string getHeader() const;

    virtual void tag();

    static int collect(ValueRef gcroot);
    virtual ValueRef clone() const = 0;

    bool tagged() { return age == 0; }
};

Value::CollectionType Value::collection;

//------------------------------------------------------------------------------

static ValueRef at(ValueRef expr);
static bool isAtom(ValueRef expr);

// NULL = empty list
struct Pointer : Value {
protected:
    ValueRef _at;
public:
    Pointer(ValueRef at = NULL, ValueRef next_ = NULL) :
        Value(V_Pointer, next_),
        _at(at) {
    }

    ValueRef getAt() const {
        return _at;
    }

    void setAt(ValueRef at) {
        this->_at = at;
    }

    static bool classof(const Value *expr) {
        return expr->getKind() == V_Pointer;
    }

    static ValueKind kind() {
        return V_Pointer;
    }

    virtual void tag() {
        if (tagged()) return;
        Value::tag();

        if (_at)
            _at->tag();
    }

    virtual ValueRef clone() const {
        return new Pointer(*this);
    }
};

static ValueRef gc_root = NULL;

static ValueRef at(ValueRef expr) {
    assert(expr && (expr->getKind() == V_Pointer));
    if (auto pointer = llvm::dyn_cast<Pointer>(expr)) {
        return pointer->getAt();
    }
    return NULL;
}

template<typename T>
static T *kindAt(ValueRef expr) {
    ValueRef val = at(expr);
    if (val) {
        return llvm::dyn_cast<T>(val);
    }
    return NULL;
}

static int kindOf(ValueRef expr) {
    if (expr) {
        return expr->getKind();
    }
    return V_None;
}

static ValueRef cons(ValueRef lhs, ValueRef rhs) {
    assert(lhs);
    lhs = lhs->clone();
    lhs->setNext(rhs);
    return lhs;
}

template<typename T>
static bool isKindOf(ValueRef expr) {
    return kindOf(expr) == T::kind();
}

static ValueRef next(ValueRef expr) {
    if (expr) {
        return expr->getNext();
    }
    return NULL;
}

static bool isAtom(ValueRef expr) {
    if (expr) {
        if (kindOf(expr) == V_Pointer) {
            if (at(expr))
                return false;
        }
    }
    // empty list
    return true;
}

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

//------------------------------------------------------------------------------

struct Integer : Value {
protected:
    int64_t value;
    bool is_unsigned;

public:
    Integer(int64_t number, bool is_unsigned_, ValueRef next_ = NULL) :
        Value(V_Integer, next_),
        value(number),
        is_unsigned(is_unsigned_)
        {}

    Integer(int64_t number, ValueRef next_ = NULL) :
        Value(V_Integer, next_),
        value(number),
        is_unsigned(false)
        {}

    bool isUnsigned() const {
        return is_unsigned;
    }

    int64_t getValue() const {
        return value;
    }

    static bool classof(const Value *expr) {
        auto kind = expr->getKind();
        return (kind == V_Integer);
    }

    static ValueKind kind() {
        return V_Integer;
    }

    virtual ValueRef clone() const {
        return new Integer(*this);
    }
};

//------------------------------------------------------------------------------

struct Real : Value {
protected:
    double value;

public:
    Real(double number, ValueRef next_ = NULL) :
        Value(V_Real, next_),
        value(number)
        {}

    double getValue() const {
        return value;
    }

    static bool classof(const Value *expr) {
        auto kind = expr->getKind();
        return (kind == V_Real);
    }

    static ValueKind kind() {
        return V_Real;
    }

    virtual ValueRef clone() const {
        return new Real(*this);
    }
};

//------------------------------------------------------------------------------

struct String : Value {
protected:
    std::string value;

    String(ValueKind kind, const char *s, size_t len, ValueRef next_) :
        Value(kind, next_),
        value(s, len)
        {}

public:
    String(const char *s, size_t len, ValueRef next_ = NULL) :
        Value(V_String, next_),
        value(s, len)
        {}

    String(const char *s, ValueRef next_ = NULL) :
        Value(V_String, next_),
        value(s, strlen(s))
        {}

    const std::string &getValue() const {
        return value;
    }

    const char *c_str() const {
        return value.c_str();
    }

    size_t size() const {
        return value.size();
    };

    const char &operator [](size_t i) const {
        return value[i];
    }

    char &operator [](size_t i) {
        return value[i];
    }

    static bool classof(const Value *expr) {
        auto kind = expr->getKind();
        return (kind == V_String) || (kind == V_Symbol);
    }

    void unescape() {
        value.resize(inplace_unescape(&value[0]));
    }

    static ValueKind kind() {
        return V_String;
    }

    virtual ValueRef clone() const {
        return new String(*this);
    }
};

const char *stringAt(ValueRef expr) {
    ValueRef val = at(expr);
    if (val) {
        if (auto sym = llvm::dyn_cast<String>(val)) {
            return sym->c_str();
        }
    }
    return NULL;
}

//------------------------------------------------------------------------------

struct Symbol : String {
    Symbol(const char *s, size_t len, ValueRef next_ = NULL) :
        String(V_Symbol, s, len, next_) {}

    Symbol(const char *s, ValueRef next_ = NULL) :
        String(V_Symbol, s, strlen(s), next_) {}

    static bool classof(const Value *expr) {
        return expr->getKind() == V_Symbol;
    }

    static ValueKind kind() {
        return V_Symbol;
    }

    virtual ValueRef clone() const {
        return new Symbol(*this);
    }
};

//------------------------------------------------------------------------------

struct Handle : Value {
protected:
    void *value;

public:
    Handle(void *ptr, ValueRef next_ = NULL) :
        Value(V_Handle, next_),
        value(ptr)
        {}

    void *getValue() const {
        return value;
    }

    static bool classof(const Value *expr) {
        auto kind = expr->getKind();
        return (kind == V_Handle);
    }

    static ValueKind kind() {
        return V_Handle;
    }

    virtual ValueRef clone() const {
        return new Handle(*this);
    }
};

//------------------------------------------------------------------------------

struct TableBody {
    friend struct Table;
protected:
    std::map< std::string, ValueRef > string_map;
    std::map< int64_t, ValueRef > integer_map;
    std::map< double, ValueRef > real_map;
    std::map< void *, ValueRef > handle_map;
    std::map< std::shared_ptr<TableBody> , ValueRef > table_map;
    std::map< ValueRef, ValueRef > value_map;

    std::shared_ptr<TableBody> meta;

    void setKey(ValueRef key, ValueRef value);

    ValueRef getLocalKey(ValueRef key);
    ValueRef getLocalKey(const std::string &name);

    ValueRef getKey(ValueRef key);
    ValueRef getKey(const std::string &name);

    std::shared_ptr<TableBody> clone();

    void tag() {
        for (auto val : string_map) { assert(val.second); val.second->tag(); }
        for (auto val : integer_map) { assert(val.second); val.second->tag(); }
        for (auto val : real_map) { assert(val.second); val.second->tag(); }
        for (auto val : handle_map) { assert(val.second); val.second->tag(); }
        for (auto val : value_map) {
            if (val.first) val.first->tag();
            assert(val.second);
            val.second->tag();
        }
    }
};

//------------------------------------------------------------------------------

struct Table : Value {
protected:
    std::shared_ptr<TableBody> body;

public:
    Table(ValueRef next_ = NULL) :
        Value(V_Table, next_),
        body(new TableBody())
        {}

    Table(std::shared_ptr<TableBody> body_, ValueRef next_ = NULL) :
        Value(V_Table, next_),
        body(body_)
        {}

    void setMeta(ValueRef meta) {
        if (meta) {
            if (auto t = llvm::dyn_cast<Table>(meta)) {
                body->meta = t->getValue();
                return;
            }
        }
        body->meta = nullptr;
    }
    ValueRef getMeta() {
        if (body->meta)
            return new Table(body->meta);
        else
            return NULL;
    }

    void setKey(ValueRef key, ValueRef value) {
        body->setKey(key, value);
    }

    ValueRef getKey(ValueRef key) {
        return body->getKey(key);
    }

    ValueRef getKey(const std::string &name) {
        return body->getKey(name);
    }

    std::shared_ptr<TableBody> getValue() {
        return body;
    }

    static bool classof(const Value *expr) {
        auto kind = expr->getKind();
        return (kind == V_Table);
    }

    static ValueKind kind() {
        return V_Table;
    }

    ValueRef deepClone() const {
        return new Table(body->clone(), getNext());
    }

    virtual ValueRef clone() const {
        return new Table(*this);
    }

    virtual void tag() {
        if (tagged()) return;
        Value::tag();

        body->tag();
    }
};

void TableBody::setKey(ValueRef key, ValueRef value) {
    switch(kindOf(key)) {
        case V_String:
        case V_Symbol:
            string_map[ llvm::cast<String>(key)->getValue() ] = value;
            break;
        case V_Integer:
            integer_map[ llvm::cast<Integer>(key)->getValue() ] = value;
            break;
        case V_Real:
            real_map[ llvm::cast<Real>(key)->getValue() ] = value;
            break;
        case V_Handle:
            handle_map[ llvm::cast<Handle>(key)->getValue() ] = value;
            break;
        case V_Table:
            table_map[ llvm::cast<Table>(key)->getValue() ] = value;
            break;
        default:
            value_map[ key ] = value;
            break;
    }
}

ValueRef TableBody::getLocalKey(const std::string &name) {
    return string_map[name];
}

ValueRef TableBody::getLocalKey(ValueRef key) {
    switch(kindOf(key)) {
        case V_String:
        case V_Symbol:
            return string_map[ llvm::cast<String>(key)->getValue() ];
        case V_Integer:
            return integer_map[ llvm::cast<Integer>(key)->getValue() ];
        case V_Real:
            return real_map[ llvm::cast<Real>(key)->getValue() ];
        case V_Handle:
            return handle_map[ llvm::cast<Handle>(key)->getValue() ];
        case V_Table:
            return table_map[ llvm::cast<Table>(key)->getValue() ];
        default:
            return value_map[ key ];
    }
}

ValueRef TableBody::getKey(const std::string &name) {
    ValueRef result = getLocalKey(name);
    if (!result && meta)
        return meta->getKey(name);
    else
        return result;
}

ValueRef TableBody::getKey(ValueRef key) {
    ValueRef result = getLocalKey(key);
    if (!result && meta)
        return meta->getKey(key);
    else
        return result;
}

std::shared_ptr<TableBody> TableBody::clone() {
    return std::shared_ptr<TableBody>(new TableBody(*this));
}

//------------------------------------------------------------------------------

Anchor *Value::findValidAnchor() {
    if (anchor.isValid()) return &anchor;
    if (auto pointer = llvm::dyn_cast<Pointer>(this)) {
        if (pointer->getAt()) {
            Anchor *result = pointer->getAt()->findValidAnchor();
            if (result) return result;
        }
    }
    if (getNext()) {
        Anchor *result = getNext()->findValidAnchor();
        if (result) return result;
    }
    return NULL;
}

std::string Value::getHeader() const {
    if (auto pointer = llvm::dyn_cast<Pointer>(this)) {
        if (pointer->getAt()) {
            if (auto head = llvm::dyn_cast<Symbol>(pointer->getAt())) {
                return head->getValue();
            }
        }
    }
    return "";
}

void Value::tag() {
    if (tagged()) return;
    // reset age
    age = 0;
    if (next)
        next->tag();
}

int Value::collect(ValueRef gcroot) {
    assert(gcroot);

    CollectionType::iterator iter = collection.begin();
    while (iter != collection.end()) {
        ValueRef value = *iter++;
        value->age++;
    }

    // walk gcroot, reset age of values we find
    gcroot->tag();
    assert(gcroot->age == 0);

    int collected = 0;
    iter = collection.begin();
    while (iter != collection.end()) {
        ValueRef value = *iter;
        auto this_iter = iter++;
        if (value->age) {
            // wasn't reached in this cycle; can be deleted
            collection.erase(this_iter);
            delete value;
            collected++;
        }
    }
    return collected;
}

//------------------------------------------------------------------------------

// matches ((///...))
static bool isComment(ValueRef expr) {
    if (isAtom(expr)) return false;
    if (isKindOf<Symbol>(at(expr)) && !memcmp(stringAt(expr),"///",3)) {
        return true;
    }
    return false;
}

static ValueRef strip(ValueRef expr) {
    if (!expr) return nullptr;
    if (isComment(expr)) {
        // skip
        return strip(next(expr));
    } else if (!isAtom(expr)) {
        ValueRef atelem = at(expr);
        ValueRef nextelem = next(expr);
        ValueRef newatelem = strip(atelem);
        ValueRef newnextelem = strip(nextelem);
        if ((newatelem == atelem) && (newnextelem == nextelem))
            return expr;
        else
            return new Pointer(newatelem, newnextelem);
    } else {
        ValueRef nextelem = next(expr);
        ValueRef newnextelem = strip(nextelem);
        if (newnextelem == nextelem)
            return expr;
        else {
            expr = expr->clone();
            expr->setNext(newnextelem);
            return expr;
        }
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
    double real;

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
        real = std::strtod(cursor, &end);
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

    ValueRef getAsString() {
        auto result = new String(string + 1, string_len - 2);
        initAnchor(result->anchor);
        result->unescape();
        return result;
    }

    ValueRef getAsSymbol() {
        auto result = new Symbol(string, string_len);
        initAnchor(result->anchor);
        result->unescape();
        return result;
    }

    ValueRef getAsInteger() {
        auto result = new Integer(integer, is_unsigned);
        initAnchor(result->anchor);
        return result;
    }

    ValueRef getAsReal() {
        auto result = new Real(real);
        initAnchor(result->anchor);
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
        ValueRef result;
        ValueRef start;
        ValueRef lastend;
        ValueRef tail;
        Anchor anchor;

        ListBuilder(Lexer &lexer) :
            result(nullptr),
            start(nullptr),
            lastend(nullptr),
            tail(nullptr) {
            lexer.initAnchor(anchor);
        }

        void resetStart() {
            start = nullptr;
        }

        bool split() {
            // wrap newly added elements in new list
            if (!start) {
                return false;
            }
            ValueRef sublist = new Pointer(start);
            if (lastend) {
                // if a previous tail is known, reroute
                lastend->setNext(sublist);
            } else {
                // list starts with sublist
                result = sublist;
            }
            lastend = sublist;
            tail = sublist;
            start = nullptr;
            return true;
        }

        void append(ValueRef newtail) {
            if (tail) {
                tail->setNext(newtail);
            } else if (!result) {
                result = newtail;
                //result->anchor = anchor;
            }
            tail = newtail;
            if (!start)
                start = tail;
        }

        ValueRef getResult() {
            auto ptr = new Pointer(result);
            ptr->anchor = anchor;
            return ptr;
        }

    };

    ValueRef parseList(int end_token) {
        ListBuilder builder(lexer);
        lexer.readToken();
        while (true) {
            if (lexer.token == end_token) {
                break;
            } else if (lexer.token == token_escape) {
                int column = lexer.column();
                lexer.readToken();
                auto elem = parseNaked(column, 1, end_token);
                if (errors) return nullptr;
                builder.append(elem);
            } else if (lexer.token == token_eof) {
                error("missing closing bracket");
                // point to beginning of list
                error_origin = builder.anchor;
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

    ValueRef parseAny () {
        assert(lexer.token != token_eof);
        if (lexer.token == token_open) {
            return parseList(token_close);
        } else if (lexer.token == token_square_open) {
            auto list = parseList(token_square_close);
            if (errors) return nullptr;
            auto result = new Pointer(new Symbol("[", at(list)));
            result->anchor = list->anchor;
            return result;
        } else if (lexer.token == token_curly_open) {
            auto list = parseList(token_curly_close);
            if (errors) return nullptr;
            auto result = new Pointer(new Symbol("{", at(list)));
            result->anchor = list->anchor;
            return result;
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

        return nullptr;
    }

    ValueRef parseNaked (int column = 0, int depth = 0, int end_token = token_none) {
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
                    parse_origin = builder.anchor;
                    return nullptr;
                }
                lineno = lexer.lineno;
            } else if (lexer.lineno > lineno) {
                if (depth > 0) {
                    if (subcolumn == 0) {
                        subcolumn = lexer.column();
                    } else if (lexer.column() != subcolumn) {
                        error("indentation mismatch");
                        parse_origin = builder.anchor;
                        return nullptr;
                    }
                } else {
                    subcolumn = lexer.column();
                }
                if (column != subcolumn) {
                    if ((column + 4) != subcolumn) {
                        //printf("%i %i\n", column, subcolumn);
                        error("indentations must nest by 4 spaces.");
                        return nullptr;
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
                    if (errors) return nullptr;
                    builder.append(elem);
                }
            } else if (lexer.token == token_statement) {
                if (!builder.split()) {
                    error("empty expression");
                    return nullptr;
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
                if (errors) return nullptr;
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

        if (!builder.result) {
            assert(depth == 0);
            return builder.getResult();
        } else if (!builder.result->getNext()) {
            return builder.result;
        } else {
            return builder.getResult();
        }
    }

    ValueRef parseMemory (
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
            return nullptr;
        }

        assert(result);
        return strip(result);
    }

    ValueRef parseFile (const char *path) {
        auto file = MappedFile::open(path);
        if (file) {
            return parseMemory(
                file->strptr(), file->strptr() + file->size(),
                path);
        } else {
            fprintf(stderr, "unable to open file: %s\n", path);
            return NULL;
        }
    }


};

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

static bool isNested(ValueRef e) {
    if (isAtom(e)) return false;
    e = at(e);
    while (e) {
        if (!isAtom(e)) {
            return true;
        }
        e = next(e);
    }
    return false;
}

template<typename T>
static void streamAnchor(T &stream, ValueRef e, size_t depth=0) {
    if (e) {
        Anchor *anchor = e->findValidAnchor();
        if (!anchor)
            anchor = &e->anchor;
        stream <<
            format("%s:%i:%i: ",
                anchor->path,
                anchor->lineno,
                anchor->column);
    }
    for(size_t i = 0; i < depth; i++)
        stream << "    ";
}

template<typename T>
static void streamValue(T &stream, ValueRef e, size_t depth=0, bool naked=true) {
    if (naked) {
        streamAnchor(stream, e, depth);
    }

	if (!e) {
        stream << "#null#";
        if (naked)
            stream << '\n';
        return;
    }

	switch(e->getKind()) {
	case V_Pointer: {
        e = at(e);
        if (!e) {
            stream << "()";
            if (naked)
                stream << '\n';
            break;
        }
        if (naked) {
            int offset = 0;
            bool single = !next(e);
        print_terse:
            streamValue(stream, e, depth, false);
            e = next(e);
            offset++;
            while (e) {
                if (isNested(e))
                    break;
                stream << ' ';
                streamValue(stream, e, depth, false);
                e = next(e);
                offset++;
            }
            stream << (single?";\n":"\n");
        //print_sparse:
            while (e) {
                if (isAtom(e) // not a list
                    && (offset >= 1) // not first element in list
                    && next(e) // not last element in list
                    && !isNested(next(e))) { // next element can be terse packed too
                    single = false;
                    streamAnchor(stream, e, depth + 1);
                    stream << "\\ ";
                    goto print_terse;
                }
                streamValue(stream, e, depth + 1);
                e = next(e);
                offset++;
            }

        } else {
            stream << '(';
            int offset = 0;
            while (e) {
                if (offset > 0)
                    stream << ' ';
                streamValue(stream, e, depth + 1, false);
                e = next(e);
                offset++;
            }
            stream << ')';
            if (naked)
                stream << '\n';
        }
    } return;
    case V_Table: {
        Table *a = llvm::cast<Table>(e);
        void *ptr = (void *)a->getValue().get();
        stream << format("<table@%p", ptr);
        ValueRef repr = a->getKey("repr");
        if (repr) {
            stream << ':';
            streamValue(stream, repr, 0, false);
        }
        stream << '>';
        if (naked)
            stream << '\n';
    } return;
    case V_Integer: {
        const Integer *a = llvm::cast<Integer>(e);

        if (a->isUnsigned())
            stream << format("%" PRIu64, a->getValue());
        else
            stream << format("%" PRIi64, a->getValue());
        if (naked)
            stream << '\n';
    } return;
    case V_Real: {
        const Real *a = llvm::cast<Real>(e);
        stream << format("%g", a->getValue());
        if (naked)
            stream << '\n';
    } return;
    case V_Handle: {
        const Handle *h = llvm::cast<Handle>(e);
        stream << format("<handle@%p>", h->getValue());
        if (naked)
            stream << '\n';
    } return;
	case V_Symbol:
	case V_String: {
        const String *a = llvm::cast<String>(e);
		if (a->getKind() == V_String) stream << '"';
        streamString(stream, a->getValue(), (a->getKind() == V_Symbol)?"[]{}()\"":"\"");
		if (a->getKind() == V_String) stream << '"';
        if (naked)
            stream << '\n';
    } return;
    default:
        printf("invalid kind: %i\n", e->getKind());
        assert (false); break;
	}
}

static std::string formatValue(ValueRef e, size_t depth, bool naked) {
    std::stringstream ss;
    streamValue(ss, e, depth, naked);
    return ss.str();
}

static void printValue(ValueRef e, size_t depth, bool naked) {
    streamValue(std::cout, e, depth, naked);
}

//------------------------------------------------------------------------------
// TYPE SYSTEM
//------------------------------------------------------------------------------

enum TypeKind {
    T_Any,
    T_Void,
    T_Null,
    T_Integer,
    T_Real,
    T_Pointer,
    T_Array,
    T_Vector,
    T_Tuple,
    T_Struct,
    T_CFunction,
    T_Continuation,
};

//------------------------------------------------------------------------------

struct Type;
typedef std::vector< std::pair<std::string, Type *> > NamedTypeArray;
typedef std::vector<Type *> TypeArray;
typedef std::vector<std::string> NameArray;

struct Type {
private:
    const TypeKind kind;

    static Type *newIntegerType(int _width, bool _signed);
    static Type *newRealType(int _width);
    static Type *newPointerType(Type *_element);
    static Type *newArrayType(Type *_element, unsigned _size);
    static Type *newVectorType(Type *_element, unsigned _size);
    static Type *newTupleType(NamedTypeArray _elements);
    static Type *newCFunctionType(
        Type *_returntype, TypeArray _parameters, bool vararg);
    static Type *newContinuationType(TypeArray _parameters);

protected:
    Type(TypeKind kind_) :
        kind(kind_)
        {}

    virtual ~Type () {};

public:
    static Type *TypePointer;
    static Type *Void;
    static Type *Null;
    static Type *Bool;
    static Type *Empty;
    static Type *Any;

    static Type *Opaque;
    static Type *OpaquePointer;

    static Type *Int8;
    static Type *Int16;
    static Type *Int32;
    static Type *Int64;

    static Type *UInt8;
    static Type *UInt16;
    static Type *UInt32;
    static Type *UInt64;

    static Type *Half;
    static Type *Float;
    static Type *Double;

    static Type *Rawstring;

    TypeKind getKind() const {
        return kind;
    }

    static void initTypes();

    static std::function<Type * (int, bool)> Integer;
    static std::function<Type * (int)> Real;
    static std::function<Type * (Type *)> Pointer;
    static std::function<Type * (Type *, unsigned)> Array;
    static std::function<Type * (Type *, unsigned)> Vector;
    static std::function<Type * (NamedTypeArray)> Tuple;
    static Type *Struct(const std::string &name, bool builtin);
    static std::function<Type * (Type *, TypeArray, bool)> CFunction;
    static std::function<Type * (TypeArray)> Continuation;

    virtual std::string getRepr() = 0;
};

Type *Type::TypePointer;
Type *Type::Void;
Type *Type::Null;
Type *Type::Bool;
Type *Type::Int8;
Type *Type::Int16;
Type *Type::Int32;
Type *Type::Int64;
Type *Type::UInt8;
Type *Type::UInt16;
Type *Type::UInt32;
Type *Type::UInt64;
Type *Type::Half;
Type *Type::Float;
Type *Type::Double;
Type *Type::Rawstring;
Type *Type::Empty;
Type *Type::Any;
Type *Type::Opaque;
Type *Type::OpaquePointer;
std::function<Type * (int, bool)> Type::Integer = memo(Type::newIntegerType);
std::function<Type * (int)> Type::Real = memo(Type::newRealType);
std::function<Type * (Type *)> Type::Pointer = memo(Type::newPointerType);
std::function<Type * (Type *, unsigned)> Type::Array = memo(Type::newArrayType);
std::function<Type * (Type *, unsigned)> Type::Vector = memo(Type::newVectorType);
std::function<Type * (NamedTypeArray)> Type::Tuple = memo(Type::newTupleType);
std::function<Type * (Type *, TypeArray, bool)> Type::CFunction = memo(Type::newCFunctionType);
std::function<Type * (TypeArray)> Type::Continuation = memo(Type::newContinuationType);

//------------------------------------------------------------------------------

template<class T, TypeKind KindT>
struct TypeImpl : Type {
    TypeImpl() :
        Type(KindT)
        {}

    static bool classof(const Type *node) {
        return node->getKind() == KindT;
    }
};

//------------------------------------------------------------------------------

struct VoidType : TypeImpl<VoidType, T_Void> {
    virtual std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, "void");
    }
};

//------------------------------------------------------------------------------

struct NullType : TypeImpl<NullType, T_Null> {
    virtual std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, "null");
    }
};

//------------------------------------------------------------------------------

struct AnyType : TypeImpl<AnyType, T_Any> {
    virtual std::string getRepr() {
        return ansi(ANSI_STYLE_ERROR, "any");
    }
};

//------------------------------------------------------------------------------

struct IntegerType : TypeImpl<IntegerType, T_Integer> {
protected:
    int width;
    bool is_signed;

public:
    int getWidth() const { return width; }
    bool isSigned() const { return is_signed; }

    IntegerType(int _width, bool _signed) :
        width(_width),
        is_signed(_signed)
        {}

    virtual std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, format("%sint%i", is_signed?"":"u", width));
    }

};

Type *Type::newIntegerType(int _width, bool _signed) {
    return new IntegerType(_width, _signed);
}

//------------------------------------------------------------------------------

struct RealType : TypeImpl<RealType, T_Real> {
protected:
    int width;

public:
    int getWidth() const { return width; }

    RealType(int _width) :
        width(_width)
        {}

    virtual std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, format("real%i", width));
    }
};

Type *Type::newRealType(int _width) {
    return new RealType(_width);
}

//------------------------------------------------------------------------------

struct PointerType : TypeImpl<PointerType, T_Pointer> {
protected:
    Type *element;

public:
    PointerType(Type *_element) :
        element(_element)
        {}

    Type *getElement() {
        return element;
    }

    virtual std::string getRepr() {
        return format("(%s %s)",
                ansi(ANSI_STYLE_KEYWORD, "pointer").c_str(),
                element->getRepr().c_str());
    }

};

Type *Type::newPointerType(Type *_element) {
    return new PointerType(_element);
}

//------------------------------------------------------------------------------

struct ArrayType : TypeImpl<ArrayType, T_Array> {
protected:
    Type *element;
    unsigned size;

public:
    Type *getElement() {
        return element;
    }

    unsigned getSize() {
        return size;
    }

    ArrayType(Type *_element, unsigned _size) :
        element(_element),
        size(_size)
        {}

    virtual std::string getRepr() {
        return format("(%s %s %i)",
                ansi(ANSI_STYLE_KEYWORD, "array").c_str(),
                element->getRepr().c_str(),
                size);
    }

};

Type *Type::newArrayType(Type *_element, unsigned _size) {
    return new ArrayType(_element, _size);
}

//------------------------------------------------------------------------------

struct VectorType : TypeImpl<VectorType, T_Vector> {
protected:
    Type *element;
    unsigned size;

public:
    Type *getElement() {
        return element;
    }

    unsigned getSize() {
        return size;
    }

    VectorType(Type *_element, unsigned _size) :
        element(_element),
        size(_size)
        {}

    virtual std::string getRepr() {
        return format("(%s %s %i)",
                ansi(ANSI_STYLE_KEYWORD, "vector").c_str(),
                element->getRepr().c_str(),
                size);
    }

};

Type *Type::newVectorType(Type *_element, unsigned _size) {
    return new VectorType(_element, _size);
}

//------------------------------------------------------------------------------

struct TupleType : TypeImpl<TupleType, T_Tuple> {
protected:
    NamedTypeArray elements;

public:
    static std::string getSpecRepr(const NamedTypeArray &elements) {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "tuple");
        for (size_t i = 0; i < elements.size(); ++i) {
            ss << " ";
            ss << elements[i].second->getRepr();
        }
        ss << ")";
        return ss.str();
    }

    TupleType(const NamedTypeArray &_elements) :
        elements(_elements)
        {}

    virtual std::string getRepr() {
        return getSpecRepr(elements);
    }

};

Type *Type::newTupleType(NamedTypeArray _elements) {
    return new TupleType(_elements);
}

//------------------------------------------------------------------------------

struct StructType : TypeImpl<StructType, T_Struct> {
protected:
    std::string name;
    NamedTypeArray elements;
    bool builtin;

public:
    StructType(const std::string &name_, bool builtin_) :
        name(name_),
        builtin(builtin_)
        {}

    virtual std::string getRepr() {
        if (builtin) {
            return ansi(ANSI_STYLE_TYPE, name).c_str();
        } else {
            return format("(%s<%p> %s)",
                    ansi(ANSI_STYLE_KEYWORD, "struct").c_str(),
                    this,
                    name.c_str());
        }
    }

};

Type *Type::Struct(const std::string &name, bool builtin) {
    return new StructType(name, builtin);
}

//------------------------------------------------------------------------------

struct ContinuationType : TypeImpl<ContinuationType, T_Continuation> {
protected:
    TypeArray parameters;

public:
    static std::string getSpecRepr(const TypeArray &parameters) {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "fn") << " ";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0)
                ss << " ";
            ss << parameters[i]->getRepr();
        }
        ss << ")";
        return ss.str();
    }

    size_t getParameterCount() const {
        return parameters.size();
    }

    Type *getParameter(int index) const {
        if (index >= (int)parameters.size())
            return NULL;
        else
            return parameters[index];
    }

    ContinuationType(const TypeArray &_parameters) :
        parameters(_parameters)
        {}

    virtual std::string getRepr() {
        return getSpecRepr(parameters);
    }

};

Type *Type::newContinuationType(TypeArray _parameters) {
    return new ContinuationType(_parameters);
}

//------------------------------------------------------------------------------

struct CFunctionType : TypeImpl<CFunctionType, T_CFunction> {
protected:
    Type *result;
    TypeArray parameters;
    bool isvararg;

public:
    static std::string getSpecRepr(Type *result,
        const TypeArray &parameters, bool isvararg) {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "cdecl") << " ";
        ss << result->getRepr();
        ss << " (";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0)
                ss << " ";
            ss << parameters[i]->getRepr();
        }
        if (isvararg) {
            if (parameters.size())
                ss << " ";
            ss << ansi(ANSI_STYLE_KEYWORD, "...");
        }
        ss << "))";
        return ss.str();
    }

    size_t getParameterCount() const {
        return parameters.size();
    }

    Type *getParameter(int index) const {
        if (index >= (int)parameters.size())
            return NULL;
        else
            return parameters[index];
    }

    Type *getResult() const {
        return result;
    }

    bool isVarArg() const { return isvararg; }

    CFunctionType(Type *result_, const TypeArray &_parameters, bool _isvararg) :
        result(result_),
        parameters(_parameters),
        isvararg(_isvararg)
        {}

    virtual std::string getRepr() {
        return getSpecRepr(result, parameters, isvararg);
    }

};

Type *Type::newCFunctionType(Type *_returntype, TypeArray _parameters, bool _isvararg) {
    return new CFunctionType(_returntype, _parameters, _isvararg);
}

//------------------------------------------------------------------------------

void Type::initTypes() {
    TypePointer = Pointer(Struct("Type", true));
    //BasicBlock = Struct("BasicBlock", true);

    Empty = Tuple({});

    Any = new AnyType();
    Void = new VoidType();
    Null = new NullType();

    Opaque = Struct("opaque", true);
    OpaquePointer = Pointer(Opaque);

    Bool = Integer(1, false);

    Int8 = Integer(8, true);
    Int16 = Integer(16, true);
    Int32 = Integer(32, true);
    Int64 = Integer(64, true);

    UInt8 = Integer(8, false);
    UInt16 = Integer(16, false);
    UInt32 = Integer(32, false);
    UInt64 = Integer(64, false);

    Half = Real(16);
    Float = Real(32);
    Double = Real(64);

    Rawstring = Pointer(Int8);

}

//------------------------------------------------------------------------------
// CLANG SERVICES
//------------------------------------------------------------------------------

class CVisitor : public clang::RecursiveASTVisitor<CVisitor> {
public:
    ValueRef dest;
    ValueRef lastref;
    clang::ASTContext *Context;
    std::unordered_map<clang::RecordDecl *, bool> record_defined;
    std::unordered_map<clang::EnumDecl *, bool> enum_defined;
    std::unordered_map<const char *, char *> path_cache;

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

    void appendValue(ValueRef decl) {
        if (!lastref) {
            llvm::cast<Pointer>(dest)->setAt(decl);
            lastref = decl;
        } else {
            lastref->setNext(decl);
            lastref = decl;
        }
    }

    void SetContext(clang::ASTContext * ctx, ValueRef dest_) {
        Context = ctx;
        dest = dest_;
        lastref = NULL;
    }

    ValueRef GetFields(clang::RecordDecl * rd) {
        auto &rl = Context->getASTRecordLayout(rd);

        ValueRef head = NULL;
        ValueRef tail = NULL;

        //check the fields of this struct, if any one of them is not understandable, then this struct becomes 'opaque'
        //that is, we insert the type, and link it to its llvm type, so it can be used in terra code
        //but none of its fields are exposed (since we don't understand the layout)
        bool opaque = false;
        for(clang::RecordDecl::field_iterator it = rd->field_begin(), end = rd->field_end(); it != end; ++it) {
            clang::DeclarationName declname = it->getDeclName();

            unsigned idx = it->getFieldIndex();

            auto offset = rl.getFieldOffset(idx);
            //unsigned width = it->getBitWidthValue(*Context);

            if(it->isBitField() || (!it->isAnonymousStructOrUnion() && !declname)) {
                opaque = true;
                break;
            }
            clang::QualType FT = it->getType();
            ValueRef fieldtype = TranslateType(FT);
            if(!fieldtype) {
                opaque = true;
                break;
            }
            Anchor anchor = anchorFromLocation(it->getSourceRange().getBegin());
            ValueRef fielddef =
                    fixAnchor(anchor,
                        new Pointer(
                            new Symbol(
                                it->isAnonymousStructOrUnion()?"$":
                                    declname.getAsString().c_str(),
                                cons(fieldtype,
                                    new Integer(offset)))));
            if (!tail) {
                head = fielddef;
            } else {
                tail->setNext(fielddef);
            }
            tail = fielddef;
        }
        return opaque?NULL:head;
    }

    ValueRef TranslateRecord(clang::RecordDecl *rd) {
        if (!rd->isStruct() && !rd->isUnion()) return NULL;

        std::string name = rd->getName();
        if (name == "") {
            name = "$";
        }

        const char *catname = rd->isUnion()?"union":"struct";

        Anchor anchor = anchorFromLocation(rd->getSourceRange().getBegin());

        clang::RecordDecl * defn = rd->getDefinition();
        ValueRef body = NULL;
        if (defn && !record_defined[rd]) {
            ValueRef fields = GetFields(defn);

            auto &rl = Context->getASTRecordLayout(rd);
            auto align = rl.getAlignment();
            auto size = rl.getSize();

            record_defined[rd] = true;
            body = new Integer(align.getQuantity(),
                new Integer(size.getQuantity(), fields));
        }

        return fixAnchor(anchor,
            new Pointer(
                new Symbol(catname,
                    new Symbol(name.c_str(), body))));
    }

    ValueRef TranslateEnum(clang::EnumDecl *ed) {
        std::string name = ed->getName();

        if(name == "") {
            name = "$";
        }

        Anchor anchor = anchorFromLocation(ed->getIntegerTypeRange().getBegin());

        clang::EnumDecl * defn = ed->getDefinition();
        ValueRef body = NULL;
        if (defn && !enum_defined[ed]) {
            ValueRef head = NULL;
            ValueRef tail = NULL;
            for (auto it : ed->enumerators()) {
                Anchor anchor = anchorFromLocation(it->getSourceRange().getBegin());
                auto &val = it->getInitVal();

                ValueRef constdef =
                    fixAnchor(anchor,
                        new Pointer(
                            new Symbol(it->getName().data(),
                                new Integer(val.getExtValue()))));

                if (!tail) {
                    head = constdef;
                } else {
                    tail->setNext(constdef);
                }
                tail = constdef;
            }

            enum_defined[ed] = true;
            ValueRef type = TranslateType(ed->getIntegerType());
            body = cons(type, head);
        }

        return fixAnchor(anchor,
            new Pointer(
                new Symbol("enum",
                    new Symbol(name.c_str(), body))));

    }

    ValueRef TranslateType(clang::QualType T) {
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
            return new Symbol(td->getName().data());
        } break;
        case clang::Type::Record: {
            const RecordType *RT = dyn_cast<RecordType>(Ty);
            RecordDecl * rd = RT->getDecl();
            return TranslateRecord(rd);
        }  break;
        case clang::Type::Enum: {
            const EnumType *ET = dyn_cast<EnumType>(Ty);
            EnumDecl * ed = ET->getDecl();
            return TranslateEnum(ed);
        } break;
        case clang::Type::Builtin:
            switch (cast<BuiltinType>(Ty)->getKind()) {
            case clang::BuiltinType::Void: {
                return new Symbol("void");
            } break;
            case clang::BuiltinType::Bool: {
                return new Symbol("i1");
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
                return new Symbol(format("%c%i",
                    Ty->isUnsignedIntegerType()?'u':'i',
                    sz).c_str());
            } break;
            case clang::BuiltinType::Half: {
                return new Symbol("half");
            } break;
            case clang::BuiltinType::Float: {
                return new Symbol("float");
            } break;
            case clang::BuiltinType::Double: {
                return new Symbol("double");
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
            ValueRef pointee = TranslateType(ETy);
            if (pointee != NULL) {
                return new Pointer(new Symbol("*", pointee));
            }
        } break;
        case clang::Type::VariableArray:
        case clang::Type::IncompleteArray:
            break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            ValueRef at = TranslateType(ATy->getElementType());
            if(at) {
                int sz = ATy->getSize().getZExtValue();
                at->setNext(new Integer(sz));
                return new Pointer(new Symbol("array", at));
            }
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
                const clang::VectorType *VT = cast<clang::VectorType>(T);
                ValueRef at = TranslateType(VT->getElementType());
                if(at) {
                    int n = VT->getNumElements();
                    at->setNext(new Integer(n));
                    return new Pointer(new Symbol("vector", at));
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

    ValueRef TranslateFuncType(const clang::FunctionType * f) {

        bool valid = true; // decisions about whether this function can be exported or not are delayed until we have seen all the potential problems
        clang::QualType RT = f->getReturnType();

        ValueRef returntype = TranslateType(RT);

        if (!returntype)
            valid = false;

        const clang::FunctionProtoType * proto = f->getAs<clang::FunctionProtoType>();
        ValueRef tail = returntype;
        //proto is null if the function was declared without an argument list (e.g. void foo() and not void foo(void))
        //we don't support old-style C parameter lists, we just treat them as empty
        if(proto) {
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                clang::QualType PT = proto->getParamType(i);
                ValueRef paramtype = TranslateType(PT);
                if(!paramtype) {
                    valid = false; //keep going with attempting to parse type to make sure we see all the reasons why we cannot support this function
                } else if(valid) {
                    tail->setNext(paramtype);
                    tail = paramtype;
                }
            }
        }

        if(valid) {
            return new Pointer(new Symbol("function", returntype));
        }

        return NULL;
    }

    bool TraverseRecordDecl(clang::RecordDecl *rd) {
        if (rd->isFreeStanding()) {
            appendValue(TranslateRecord(rd));
        }
        return true;
    }

    bool TraverseEnumDecl(clang::EnumDecl *ed) {
        if (ed->isFreeStanding()) {
            appendValue(TranslateEnum(ed));
        }
        return true;
    }

    bool TraverseVarDecl(clang::VarDecl *vd) {
        if (vd->isExternC()) {
            Anchor anchor = anchorFromLocation(vd->getSourceRange().getBegin());

            ValueRef type = TranslateType(vd->getType());
            if (!type) return true;

            appendValue(
                fixAnchor(anchor,
                    new Pointer(
                        new Symbol("global",
                            new Symbol(vd->getName().data(), type)))));

        }

        return true;

    }

    bool TraverseTypedefDecl(clang::TypedefDecl *td) {

        Anchor anchor = anchorFromLocation(td->getSourceRange().getBegin());

        ValueRef type = TranslateType(td->getUnderlyingType());
        if (!type) return true;

        appendValue(
            fixAnchor(anchor,
                new Pointer(
                    new Symbol("typedef",
                        new Symbol(td->getName().data(), type)))));

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

        ValueRef functype = TranslateFuncType(fntyp);
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

        appendValue(
            fixAnchor(anchor,
                new Pointer(
                    new Symbol("declare",
                        new Symbol(FuncName.c_str(),
                            functype)))));

        return true;
    }
};

class CodeGenProxy : public clang::ASTConsumer {
public:
    ValueRef dest;
    CVisitor visitor;

    CodeGenProxy(ValueRef dest_) : dest(dest_) {}
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
    ValueRef dest;

    BangEmitLLVMOnlyAction(ValueRef dest_) :
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

static LLVMModuleRef importCModule (ValueRef dest,
    const char *path, const char **args, int argcount,
    const char *buffer = NULL) {
    using namespace clang;

    std::vector<const char *> aargs;
    aargs.push_back("clang");
    aargs.push_back(path);
    for (int i = 0; i < argcount; ++i) {
        aargs.push_back(args[i]);
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

    // Create and execute the frontend to generate an LLVM bitcode module.
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction(dest));
    if (compiler.ExecuteAction(*Act)) {
        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        return M;
    }

    return NULL;
}

//------------------------------------------------------------------------------
// MID-LEVEL IL
//------------------------------------------------------------------------------

// CFF form implemented after
// Leissa et al., Graph-Based Higher-Order Intermediate Representation
// http://compilers.cs.uni-saarland.de/papers/lkh15_cgo.pdf

// some parts of the paper use hindley-milner notation
// https://en.wikipedia.org/wiki/Hindley%E2%80%93Milner_type_system

struct ILValue;
struct ILContinuation;
struct ILParameter;
struct ILPrimitive;
struct ILModule;
struct ILBuilder;
struct ILIntrinsic;

typedef std::shared_ptr<ILValue> ILValueRef;
typedef std::weak_ptr<ILValue> ILValueWeakRef;
typedef shared_back_ptr<ILValue> ILValueBackRef;

typedef std::shared_ptr<ILContinuation> ILContinuationRef;
typedef std::weak_ptr<ILContinuation> ILContinuationWeakRef;

typedef std::shared_ptr<ILModule> ILModuleRef;
typedef std::weak_ptr<ILModule> ILModuleWeakRef;

typedef std::shared_ptr<ILBuilder> ILBuilderRef;

typedef std::shared_ptr<ILParameter> ILParameterRef;
typedef std::shared_ptr<ILPrimitive> ILPrimitiveRef;

typedef std::shared_ptr<ILIntrinsic> ILIntrinsicRef;

//------------------------------------------------------------------------------

struct ILValue : enable_shared_back_from_this<ILValue> {
    enum Kind {
        Intrinsic,

        Primitive,
            Constant,
                ConstString,
                ConstInteger,
                ConstReal,
                ConstTypePointer,
                ConstTuple,
                ConstantEnd,

            Parameter,

            PrimitiveEnd,

        Op,
            OpAdd,
            OpSub,
            OpMul,
            OpDiv,
            OpMod,
            OpAnd,
            OpOr,
            OpXor,
            OpShiftLeft,
            OpShiftRight,
            OpEqual,
            OpNotEqual,
            OpGreaterThan,
            OpGreaterEqual,
            OpLessThan,
            OpLessEqual,
            OpEnd,

        Continuation,
    };

public:

    const Kind kind;
    Anchor anchor;

    ILValue(Kind kind_) :
        kind(kind_)
        {}

    virtual ~ILValue() {}

    virtual std::string getRepr () = 0;
    virtual std::string getRefRepr () = 0;
    virtual Type *inferType() = 0;

    Type *getType() {
        return inferType();
    }

    template<typename T = ILValue>
    std::shared_ptr<T> getSharedPtr() {
        ILValueRef value = shared_from_this();
        assert(llvm::isa<T>(value.get()));
        return std::static_pointer_cast<T>(value);
    }
};

static void ilMessage (const ILValueRef &value, const char *format, ...) {
    Anchor *anchor = NULL;
    if (value) {
        std::cout << "at\n" << value->getRepr();
        if (value->anchor.isValid()) {
            anchor = &value->anchor;
        }
    }
    va_list args;
    va_start (args, format);
    Anchor::printMessageV(anchor, format, args);
    va_end (args);
}

static void ilError (const ILValueRef &value, const char *format, ...) {
    Anchor *anchor = NULL;
    if (value) {
        std::cout << "at\n" << value->getRepr();
        if (value->anchor.isValid()) {
            anchor = &value->anchor;
        }
    }
    va_list args;
    va_start (args, format);
    Anchor::printErrorV(anchor, format, args);
    va_end (args);
}

//------------------------------------------------------------------------------

template<typename SelfT, ILValue::Kind KindT, typename BaseT>
struct ILValueImpl : BaseT {
    typedef ILValueImpl<SelfT, KindT, BaseT> ValueImplType;

    ILValueImpl() :
        BaseT(KindT)
    {}

    virtual ~ILValueImpl() {}

    static bool classof(const ILValue *value) {
        return value->kind == KindT;
    }

    std::shared_ptr<SelfT> sharedFromThis() {
        return BaseT::template getSharedPtr<SelfT>();
    }
};

//------------------------------------------------------------------------------

struct ILPrimitive : ILValue {
    ILPrimitive(Kind kind_) :
        ILValue(kind_)
        {}

    virtual std::string getRepr () {
        return getRefRepr();
    }

    static bool classof(const ILValue *value) {
        return (value->kind >= Primitive) && (value->kind < PrimitiveEnd);
    }
};

//------------------------------------------------------------------------------

struct ILConstant : ILPrimitive {
    ILConstant(Kind kind_) :
        ILPrimitive(kind_)
        {}

    static bool classof(const ILValue *value) {
        return (value->kind >= Constant) && (value->kind < ConstantEnd);
    }
};

//------------------------------------------------------------------------------

struct ILOp : ILValue {
    ILValueRef lvalue;
    ILValueRef rvalue;

    ILOp(Kind kind_) :
        ILValue(kind_)
        {}

    virtual Type *inferType() {
        Type *A = lvalue->getType();
        Type *B = rvalue->getType();
        if (A != B)
            return Type::Any;
        return A;
    }

    virtual std::string getRefRepr () {
        return getRepr();
    }

    static bool classof(const ILValue *value) {
        return (value->kind >= Op) && (value->kind < OpEnd);
    }
};

typedef ILOp ILBinaryOp;

template<typename SelfT, ILValue::Kind KindT>
struct ILBinaryOpImpl :
    ILValueImpl<SelfT, KindT, ILBinaryOp> {

    virtual ~ILBinaryOpImpl() {}

    virtual std::string getRepr () {
        std::stringstream ss;
        ss << ansi(ANSI_STYLE_OPERATOR, "(");
        ss << ILBinaryOp::lvalue->getRefRepr();
        ss << " " << ansi(
            (ILBinaryOp::getType() == Type::Any)?
                ANSI_STYLE_ERROR
                :ANSI_STYLE_OPERATOR,
            SelfT::OpName());
        ss << " " << ILBinaryOp::rvalue->getRefRepr();
        ss << ansi(ANSI_STYLE_OPERATOR, ")");
        return ss.str();
    }

    static std::shared_ptr<SelfT> create(
        const ILValueRef &lvalue,
        const ILValueRef &rvalue) {
        auto result = std::make_shared<SelfT>();
        result->lvalue = lvalue;
        result->rvalue = rvalue;
        return result;
    }

};

struct ILOpAdd : ILBinaryOpImpl<ILOpAdd, ILValue::OpAdd> {
    static std::string OpName() { return "+"; }
};
struct ILOpSub : ILBinaryOpImpl<ILOpSub, ILValue::OpSub> {
    static std::string OpName() { return "-"; }
};
struct ILOpMul : ILBinaryOpImpl<ILOpMul, ILValue::OpMul> {
    static std::string OpName() { return "*"; }
};
struct ILOpDiv : ILBinaryOpImpl<ILOpDiv, ILValue::OpDiv> {
    static std::string OpName() { return "/"; }
};
struct ILOpEqual : ILBinaryOpImpl<ILOpEqual, ILValue::OpEqual> {
    static std::string OpName() { return "=="; }
};
struct ILOpNotEqual : ILBinaryOpImpl<ILOpNotEqual, ILValue::OpNotEqual> {
    static std::string OpName() { return "!="; }
};
struct ILOpGreaterThan : ILBinaryOpImpl<ILOpGreaterThan, ILValue::OpGreaterThan> {
    static std::string OpName() { return ">"; }
};
struct ILOpGreaterEqual : ILBinaryOpImpl<ILOpGreaterEqual, ILValue::OpGreaterEqual> {
    static std::string OpName() { return ">="; }
};
struct ILOpLessThan : ILBinaryOpImpl<ILOpLessThan, ILValue::OpLessThan> {
    static std::string OpName() { return "<"; }
};
struct ILOpLessEqual : ILBinaryOpImpl<ILOpLessEqual, ILValue::OpLessEqual> {
    static std::string OpName() { return "<="; }
};

//------------------------------------------------------------------------------

struct ILParameter :
    ILValueImpl<ILParameter, ILValue::Parameter, ILPrimitive> {
    ILContinuationWeakRef parent;
    size_t index;
    Type *parameter_type;

    ILContinuationRef getParent() {
        return parent.lock();
    }

    virtual Type *inferType() {
        if (parameter_type)
            return parameter_type;
        else
            return Type::Any;
    }

    virtual std::string getRepr() {
        return format("%s%zu %s %s",
            ansi(ANSI_STYLE_OPERATOR,"@").c_str(),
            index,
            ansi(ANSI_STYLE_OPERATOR,":").c_str(),
            getType()->getRepr().c_str());
    }

    virtual std::string getRefRepr ();

    static ILParameterRef create() {
        auto value = std::make_shared<ILParameter>();
        value->index = (size_t)-1;
        value->parameter_type = nullptr;
        return value;
    }
};

//------------------------------------------------------------------------------

struct ILIntrinsic :
    ILValueImpl<ILIntrinsic, ILValue::Intrinsic, ILValue> {
    static ILIntrinsicRef Branch;

    std::string name;
    Type *intrinsic_type;

    virtual Type *inferType() {
        return intrinsic_type;
    }

    virtual std::string getRepr () {
        std::stringstream ss;
        ss << ansi(ANSI_STYLE_INSTRUCTION, name);
        ss << " " << ansi(ANSI_STYLE_OPERATOR, ":") << " ";
        ss << getType()->getRepr();
        return ss.str();
    }

    virtual std::string getRefRepr () {
        return ansi(ANSI_STYLE_KEYWORD, name);
    }

    static ILIntrinsicRef create(
        const std::string &name, Type *type) {
        assert(type);
        auto value = std::make_shared<ILIntrinsic>();
        value->name = name;
        value->intrinsic_type = type;
        return value;
    }

    static void initIntrinsics() {
        Branch = create("branch",
            Type::Continuation({ Type::Bool,
                Type::Continuation({}),
                Type::Continuation({}) }));
    }
};

ILIntrinsicRef ILIntrinsic::Branch;

//------------------------------------------------------------------------------

struct ILContinuation :
    ILValueImpl<ILContinuation, ILValue::Continuation, ILValue> {
private:
    static int64_t unique_id_counter;
protected:
    int64_t uid;
public:
    ILModuleWeakRef module;
    std::vector<ILParameterRef> parameters;
    std::vector<ILValueRef> values;

    ILContinuation() :
        uid(unique_id_counter++)
    {}

    void clear() {
        parameters.clear();
        values.clear();
    }

    virtual std::string getRepr () {
        std::stringstream ss;
        ss << getRefRepr();
        ss << " " << ansi(ANSI_STYLE_OPERATOR, "(");
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0) {
                ss << ansi(ANSI_STYLE_OPERATOR, ", ");
            }
            ss << parameters[i]->getRepr();
        }
        ss << ansi(ANSI_STYLE_OPERATOR, ")");
        if (values.size()) {
            for (size_t i = 0; i < values.size(); ++i) {
                ss << " ";
                ss << values[i]->getRefRepr();
            }
        } else {
            ss << ansi(ANSI_STYLE_ERROR, "<missing term>");
        }
        return ss.str();
    }

    virtual std::string getRefRepr () {
        return format("%s%" PRId64,
            ansi(ANSI_STYLE_KEYWORD, "λ").c_str(),
            uid);
    }

    Type *inferType() {
        std::vector<Type *> params;
        for (auto &param : parameters) {
            params.push_back(param->getType());
        }
        return Type::Continuation(params);
    }

    ILParameterRef appendParameter(const ILParameterRef &param) {
        param->parent = getSharedPtr<ILContinuation>();
        param->index = parameters.size();
        parameters.push_back(param);
        return param;
    }

    static ILContinuationRef create(
        const ILModuleRef &module, size_t paramcount = 0);
};

int64_t ILContinuation::unique_id_counter = 1;

std::string ILParameter::getRefRepr () {
    auto parent = getParent();
    if (parent) {
        return format("%s%s%zu",
            parent->getRefRepr().c_str(),
            ansi(ANSI_STYLE_OPERATOR, "@").c_str(),
            index);
    } else {
        return ansi(ANSI_STYLE_ERROR, "<unbound>");
    }
}

//------------------------------------------------------------------------------

struct ILModule : std::enable_shared_from_this<ILModule> {
    std::list<ILContinuationRef> continuations;

    ILContinuationRef getEntryContinuation() {
        return continuations.front();
    }

    std::string getRepr () {
        std::stringstream ss;
        ss << ansi(ANSI_STYLE_COMMENT, "# module\n");
        for (auto &cont : continuations) {
            ss << cont->getRepr() << "\n";
        }
        return ss.str();
    }

    void append(const ILContinuationRef &cont) {
        assert(cont->module.expired());
        cont->module = shared_from_this();
        continuations.push_back(cont);
    }
};

ILContinuationRef ILContinuation::create(
    const ILModuleRef &module, size_t paramcount) {
    auto value = std::make_shared<ILContinuation>();
    for (size_t i = 0; i < paramcount; ++i) {
        value->appendParameter(ILParameter::create());
    }
    module->append(value);
    return value;
}

//------------------------------------------------------------------------------

/*
template<typename T>
static std::string format_valuelist(ILValue *self,
    const std::vector<T> &values) {
    std::stringstream ss;
    ss << "(";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0)
            ss << " ";
        ss << values[i]->getRefRepr(self->isSameBlock(values[i]));
    }
    ss << ")";
    return ss.str();
}
*/

//------------------------------------------------------------------------------

struct ILConstString :
    ILValueImpl<ILConstString, ILValue::ConstString, ILConstant> {
    std::string value;

    static std::shared_ptr<ILConstString> create(String *c) {
        assert(c);
        auto result = std::make_shared<ILConstString>();
        result->value = c->getValue();
        return result;
    }

    virtual Type *inferType() {
        return Type::Array(Type::Int8, value.size() + 1);
    }

    virtual std::string getRefRepr() {
        return ansi(ANSI_STYLE_STRING,
            format("\"%s\"",
                quoteString(value, "\"").c_str()));
    }
};

//------------------------------------------------------------------------------

struct ILConstInteger :
    ILValueImpl<ILConstInteger, ILValue::ConstInteger, ILConstant> {
    int64_t value;
    Type *value_type;

    static std::shared_ptr<ILConstInteger> create(Integer *c, IntegerType *cdest) {
        assert(c);
        assert(cdest);
        auto result = std::make_shared<ILConstInteger>();
        result->value_type = cdest;
        result->value = c->getValue();
        return result;
    }

    virtual Type *inferType() {
        return value_type;
    }

    virtual std::string getRefRepr() {
        return ansi(ANSI_STYLE_NUMBER,
            format("%" PRIi64, value));
    }
};

//------------------------------------------------------------------------------

struct ILConstReal :
    ILValueImpl<ILConstReal, ILValue::ConstReal, ILConstant> {
    double value;
    Type *value_type;

    static std::shared_ptr<ILConstReal> create(Real *c, RealType *cdest) {
        assert(c);
        assert(cdest);
        auto result = std::make_shared<ILConstReal>();
        result->value_type = cdest;
        result->value = c->getValue();
        return result;
    }

    virtual Type *inferType() {
        return value_type;
    }

    virtual std::string getRefRepr() {
        return ansi(ANSI_STYLE_NUMBER,
            format("%g", value));
    }
};

//------------------------------------------------------------------------------

struct ILConstTuple :
    ILValueImpl<ILConstTuple, ILValue::ConstTuple, ILConstant> {
    std::vector<ILPrimitiveRef> values;

    static std::shared_ptr<ILConstTuple> create(
        const std::vector<ILPrimitiveRef> &values_) {
        auto result = std::make_shared<ILConstTuple>();
        result->values = values_;
        return result;
    }

    virtual Type *inferType() {
        NamedTypeArray types;
        for (auto &value : values) {
            types.push_back(
                std::pair<std::string,Type*>("",value->getType()));
        }
        return Type::Tuple(types);
    }

    virtual std::string getRefRepr() {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "tupleof");
        for (auto &value : values) {
            ss << " " << value->getRefRepr();
        }
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

struct ILConstTypePointer :
    ILValueImpl<ILConstTypePointer, ILValue::ConstTypePointer, ILConstant> {
    Type *value;

    static std::shared_ptr<ILConstTypePointer> create(Type *t) {
        assert(t);
        auto result = std::make_shared<ILConstTypePointer>();
        result->value = t;
        return result;
    }

    virtual Type *inferType() {
        return Type::TypePointer;
    }

    virtual std::string getRefRepr() {
        return value->getRepr();
    }
};

static Type *extract_consttype(const ILValueRef &value) {
    if (auto resulttype = llvm::dyn_cast<ILConstTypePointer>(value.get())) {
        return resulttype->value;
    }
    return Type::Any;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

struct ILBuilder : std::enable_shared_from_this<ILBuilder> {
    ILModuleRef module;
    ILContinuationRef continuation;

    ILBuilder(const ILModuleRef &module_) :
        module(module_) {
        assert(module);
    }

    void continueAt(const ILContinuationRef &cont) {
        this->continuation = cont;
        assert(!cont->values.size());
    }

    void insertAndAdvance(
        const std::vector<ILValueRef> &values,
        const ILContinuationRef &next) {
        assert(continuation);
        assert(!continuation->values.size());
        continuation->values = values;
        continuation = next;
    }

    void br(const std::vector<ILValueRef> &arguments) {
        insertAndAdvance(arguments, nullptr);
    }

    ILParameterRef toparam(const ILValueRef &value) {
        if (llvm::isa<ILParameter>(value.get())) {
            return value->getSharedPtr<ILParameter>();
        } else {
            auto next = ILContinuation::create(module, 1);
            insertAndAdvance( { next, value } , next);
            return next->parameters[0];
        }
    }

    ILParameterRef call(std::vector<ILValueRef> values) {
        auto next = ILContinuation::create(module, 1);
        values.push_back(next);
        insertAndAdvance(values, next);
        return next->parameters[0];
    }

};

//------------------------------------------------------------------------------

std::string extract_string(const ILValueRef &value) {
    auto resulttype = llvm::dyn_cast<ILConstString>(value.get());
    if (!resulttype) {
        ilError(value, "string constant expected");
    }
    return resulttype->value;
}

Type *extract_type(const ILValueRef &value) {
    auto resulttype = llvm::dyn_cast<ILConstTypePointer>(value.get());
    if (!resulttype) {
        ilError(value, "type constant expected");
    }
    return resulttype->value;
}

//------------------------------------------------------------------------------
// IL SOLVER
//------------------------------------------------------------------------------

struct ILVisitor {
    std::unordered_set<ILContinuationRef> visited;
    std::list<ILContinuationRef> queue;

    void start(const ILContinuationRef &cont) {
        visited.clear();
        queue.clear();
        queue.push_front(cont);
    }

    bool hasVisited(const ILContinuationRef &cont) const {
        return visited.count(cont);
    }

    ILContinuationRef next() {
        while (!queue.empty()) {
            ILContinuationRef cont = queue.back();
            if (!hasVisited(cont)) {
                visited.insert(cont);
                return cont;
            } else {
                queue.pop_back();
                followTerm(cont);
            }
        }
        return nullptr;
    }

    void followTerm(const ILContinuationRef &cont) {
    }
};

static Type *merge_type(Type *a, Type *b) {
    if (!a) return b;
    if (a == b) return a;
    switch(a->getKind()) {
        case T_Array: {
            auto arra = llvm::dyn_cast<ArrayType>(a);
            switch(b->getKind()) {
                case T_Array: {
                    auto arrb = llvm::dyn_cast<ArrayType>(b);
                    if (arra->getElement() == arrb->getElement()) {
                        return Type::Pointer(arra->getElement());
                    }
                } break;
                case T_Pointer: {
                    auto ptrb = llvm::dyn_cast<PointerType>(b);
                    if (arra->getElement() == ptrb->getElement()) {
                        return ptrb;
                    }
                } break;
                default: break;
            }
        } break;
        case T_Pointer: {
            switch(b->getKind()) {
                case T_Array: {
                    return merge_type(b, a);
                } break;
                default: break;
            }
        } break;
        default: break;
    }
    return nullptr;
}

struct ILSolver {
    // after generation, the IL is not directly translatable because many types
    // have not been evaluated, types that depend on constant conditions.

    // the solver "runs" instructions in the IL
    // * resolves instructions with constant inputs to constants
    // * ensures that all types and templates have been specialized
    // * generates implicit casts
    // ? lowers block arguments that only have one predecessor
    // ? removes instructions and values that don't contribute to anything
    // ? removes unreachable blocks due to constant conditions,
    //   and removes single basic block arguments from blocks that are always
    //   reached.

    ILModuleRef module;
    ILBuilderRef builder;

    ILVisitor visitor;

    ILSolver(const ILModuleRef &module_) :
        module(module_) {
        builder = std::make_shared<ILBuilder>(module);
    }

    void eval_term(const ILContinuationRef &cont) {
    }

    void eval_parameters(const ILContinuationRef &cont) {
        /*
        if (cont->predecessors.size() == 1) {
            auto &pred = *cont->predecessors.begin();
            // all parameters can be inlined
            for (size_t i = 0; i < cont->parameters.size(); ++i) {
                auto &param = cont->parameters[i];
                auto &arg = pred->getArgument(i);
                replace_instruction_links(param, arg);
            }
            pred->clearArguments();
            cont->clearParameters();
        } else {

            for (size_t i = 0; i < cont->parameters.size(); ++i) {
                auto &param = cont->parameters[i];
                Type *common_type = nullptr;
                bool failed = false;
                for (auto &pred : cont->predecessors) {
                    auto &arg = pred->getArgument(i);
                    Type *type = arg->getType();
                    if (type == Type::Any) {
                        ilError(arg, "missing specialization");
                    }
                    common_type = merge_type(common_type, type);
                    if (!common_type) {
                        failed = true;
                        break;
                    }
                }
                if (failed) {
                    for (auto &pred : cont->predecessors) {
                        auto &arg = pred->getArgument(i);
                        ilMessage(arg, "conflicting type here");
                    }
                    ilError(param, "unable to merge conflicting types");
                }
                if (!common_type) {
                    ilError(param, "unable to type parameter");
                }
                param->parameter_type = common_type;
                // cast predecessors where necessary
                for (auto &pred : cont->predecessors) {
                    auto &arg = pred->getArgument(i);
                    generate_cast(pred->getBlock(), pred, arg, common_type);
                }
            }
        }
        */
    }

    void run() {
        visitor.start(module->getEntryContinuation());

        while (ILContinuationRef cont = visitor.next()) {
            eval_parameters(cont);
            eval_term(cont);
        }

        // delete blocks that were never visited
        std::list< std::list<ILContinuationRef>::iterator > todelete;
        for (auto iter = module->continuations.begin();
            iter != module->continuations.end(); ++iter) {
            if (!visitor.hasVisited(*iter)) {
                (*iter)->clear();
                todelete.push_back(iter);
            }
        }

        for (auto iter : todelete) {
            module->continuations.erase(iter);
        }
    }
};

//------------------------------------------------------------------------------
// IL -> IR CODE GENERATOR
//------------------------------------------------------------------------------

/*
struct CodeGenerator {
    LLVMBuilderRef builder;
    LLVMModuleRef llvm_module;
    ILModuleRef il_module;

    std::unordered_map<ILContinuationRef, LLVMBasicBlockRef> blockmap;
    std::unordered_map<ILValueRef, LLVMValueRef> valuemap;
    std::unordered_map<Type *, LLVMTypeRef> typemap;
    std::unordered_map<LLVMValueRef, LLVMValueRef> globals;

    CodeGenerator(ILModuleRef il_module_, LLVMModuleRef llvm_module_) :
        builder(nullptr),
        llvm_module(llvm_module_),
        il_module(il_module_)
    {
    }

    LLVMTypeRef resolveType(const ILValueRef &reference, Type *il_type = nullptr) {
        if (!il_type) {
            il_type = reference->getType();
        }
        LLVMTypeRef result = typemap[il_type];
        if (!result) {
            switch(il_type->getKind()) {
                case T_Void: {
                    result = LLVMVoidType();
                } break;
                case T_Null: {
                    result = LLVMVoidType();
                } break;
                case T_Integer: {
                    auto integer = llvm::cast<IntegerType>(il_type);
                    result = LLVMIntType(integer->getWidth());
                } break;
                case T_Real: {
                    auto real = llvm::cast<RealType>(il_type);
                    switch(real->getWidth()) {
                        case 16: {
                            result = LLVMHalfType();
                        } break;
                        case 32: {
                            result = LLVMFloatType();
                        } break;
                        case 64: {
                            result = LLVMDoubleType();
                        } break;
                        default: {
                            ilError(reference, "illegal real type");
                        } break;
                    }
                } break;
                case T_Array: {
                    auto array = llvm::cast<ArrayType>(il_type);
                    result = LLVMArrayType(
                        resolveType(
                            reference,
                            array->getElement()),
                        array->getSize());
                } break;
                case T_Pointer: {
                    result = LLVMPointerType(
                        resolveType(
                            reference,
                            llvm::cast<PointerType>(il_type)->getElement()),
                        0);
                } break;
                case T_CFunction: {
                    auto ftype = llvm::cast<CFunctionType>(il_type);

                    LLVMTypeRef parameters[ftype->getParameterCount()];
                    for (size_t i = 0; i < ftype->getParameterCount(); ++i) {
                        parameters[i] = resolveType(reference, ftype->getParameter(i));
                    }
                    result = LLVMFunctionType(
                        resolveType(reference, ftype->getResult()),
                        parameters, ftype->getParameterCount(),
                        ftype->isVarArg());
                } break;
                default: {
                    ilError(reference, "can not translate type");
                } break;
            }
            assert(result);
            typemap[il_type] = result;
        }
        return result;
    }

    LLVMValueRef resolveValue(const ILValueRef &il_value) {
        LLVMValueRef result = valuemap[il_value];
        if (!result) {
            switch(il_value->kind) {
                default: {
                    ilError(il_value, "does not dominate all uses");
                } break;
            }
            assert(result);
            valuemap[il_value] = result;
        }
        return result;
    }

    LLVMValueRef const_int(int value) {
        return LLVMConstInt(LLVMInt32Type(), value, true);
    }

    ILContinuationRef resolveBlock(const ILContinuationRef &block) {
        LLVMBasicBlockRef result = blockmap[block];
        assert(result);
        return result;
    }

    LLVMBasicBlockRef resolveLabel(const ILLabelRef &label) {
        return resolveBlock(label->getLabelBlock());
    }

    void generateTerminator(const ILValueRef &il_value) {
        switch(il_value->kind) {
            case ILValue::CondBr: {
                auto condbr = llvm::cast<ILCondBr>(il_value.get());
                auto llvm_value = resolveValue(condbr->condition);
                auto llvm_thenlabel = resolveLabel(
                    condbr->getThenLabel());
                auto llvm_elselabel = resolveLabel(
                    condbr->getElseLabel());

                LLVMBuildCondBr(builder, llvm_value,
                    llvm_thenlabel, llvm_elselabel);
            } break;
            case ILValue::Br: {
                auto br = llvm::cast<ILBr>(il_value.get());
                auto llvm_label = resolveLabel(
                    br->getLabel());
                LLVMBuildBr(builder, llvm_label);
            } break;
            case ILValue::Ret: {
                auto br = llvm::cast<ILRet>(il_value.get());
                if (br->value) {
                    LLVMBuildRet(builder, resolveValue(br->value));
                } else {
                    LLVMBuildRetVoid(builder);
                }
            } break;
            default:
                {
                    ilError(il_value, "illegal terminator");
                } break;
        }
    }

    void generateValue(const ILValueRef &il_value) {
        LLVMValueRef result = valuemap[il_value];
        assert(!result);
        switch(il_value->kind) {
            case ILValue::Nop: {
                return;
            } break;
            case ILValue::Label: {
                return;
            } break;
            case ILValue::ConstTypePointer: {
                return;
            } break;
            case ILValue::ConstInteger: {
                auto ci = llvm::cast<ILConstInteger>(il_value.get());

                auto il_type = llvm::cast<IntegerType>(ci->value_type);
                auto llvm_inttype = resolveType(il_value, ci->value_type);

                result = LLVMConstInt(llvm_inttype, ci->value, il_type->isSigned());
            } break;
            case ILValue::ConstReal: {
                auto cr = llvm::cast<ILConstReal>(il_value.get());

                auto llvm_realtype = resolveType(il_value, cr->value_type);

                result = LLVMConstReal(llvm_realtype, cr->value);
            } break;
            case ILValue::ConstString: {
                auto cs = llvm::cast<ILConstString>(il_value.get());

                result = LLVMConstString(cs->value.c_str(), cs->value.size(), false);
            } break;
            case ILValue::BasicBlockParameter: {
                auto bbp = llvm::cast<ILBasicBlockParameter>(il_value.get());

                auto llvm_type = resolveType(il_value, bbp->parameter_type);

                result = LLVMBuildPhi(builder, llvm_type, "");

            } break;
            case ILValue::External: {
                auto external = llvm::cast<ILExternal>(il_value.get());

                auto name = extract_string(external->name);
                auto ftype = resolveType(il_value,
                    extract_type(external->external_type));

                result = LLVMAddFunction(llvm_module, name.c_str(), ftype);

            } break;
            case ILValue::CastTo: {
                auto castto = llvm::cast<ILCastTo>(il_value.get());
                auto llvm_value = resolveValue(castto->value);
                auto casttype = resolveType(il_value,
                    extract_type(castto->cast_type));

                if (LLVMIsConstant(llvm_value)) {
                    result = LLVMConstBitCast(llvm_value, casttype);
                } else {
                    result = LLVMBuildBitCast(builder, llvm_value, casttype, "");
                }

            } break;
            case ILValue::AddressOf: {
                auto addressof = llvm::cast<ILAddressOf>(il_value.get());
                auto llvm_value = resolveValue(addressof->value);

                if (LLVMIsConstant(llvm_value)) {
                    auto globalvar = globals[llvm_value];
                    if (!globalvar) {
                        globalvar = LLVMAddGlobal(llvm_module,
                            LLVMTypeOf(llvm_value), "");
                        LLVMSetInitializer(globalvar, llvm_value);
                        LLVMSetGlobalConstant(globalvar, true);
                        LLVMSetUnnamedAddr(globalvar, true);
                        LLVMSetLinkage(globalvar, LLVMPrivateLinkage);
                        globals[llvm_value] = globalvar;
                    }

                    result = globalvar;
                } else {
                    ilError(il_value, "TODO: copy value to stack");
                }
            } break;
            case ILValue::Call: {
                auto instr = llvm::cast<ILCall>(il_value.get());
                LLVMValueRef llvm_args[instr->arguments.size()];
                for (size_t i = 0; i < instr->arguments.size(); ++i) {
                    llvm_args[i] = resolveValue(instr->arguments[i]);
                }
                result = LLVMBuildCall(builder,
                    resolveValue(instr->callee),
                    llvm_args,
                    instr->arguments.size(),
                    "");
            } break;
            default: {
                ilError(il_value, "illegal instruction");
            } break;
        }
        assert(result);
        valuemap[il_value] = result;
    }

    void createBlock(LLVMValueRef llvm_function,
        const ILBasicBlockRef &il_block) {
        LLVMBasicBlockRef llvm_block = LLVMAppendBasicBlock(
            llvm_function, il_block->name.c_str());
        blockmap[il_block] = llvm_block;
    }

    void linkBlock(const ILBasicBlockRef &il_block) {
        for (size_t i = 0; i < il_block->parameters.size(); ++i) {
            auto &param = il_block->parameters[i];
            LLVMValueRef llvm_phi = resolveValue(param);
            size_t predcount = il_block->predecessors.size();
            LLVMValueRef incoming_values[predcount];
            LLVMBasicBlockRef incoming_blocks[predcount];
            size_t k = 0;
            for (auto &pred : il_block->predecessors) {
                auto &arg = pred->getArgument(i);
                incoming_values[k] = resolveValue(arg);
                incoming_blocks[k] = resolveBlock(pred->getBlock());
                ++k;
            }
            LLVMAddIncoming(llvm_phi,
                incoming_values, incoming_blocks, predcount);
        }
    }

    void generateBlock(const ILBasicBlockRef &il_block) {
        LLVMBasicBlockRef llvm_block = blockmap[il_block];
        assert(llvm_block);
        LLVMPositionBuilderAtEnd(builder, llvm_block);

        for (auto &param : il_block->parameters) {
            generateValue(param);
        }

        ILInstructionRef value = il_block->firstvalue;
        while (value) {
            generateValue(value);
            value = value->nextvalue;
        }
        if (il_block->terminator) {
            generateTerminator(il_block->terminator);
        }

    }

    void generate() {
        builder = LLVMCreateBuilder();

        for (auto il_function : il_module->functions) {
            auto llvm_functype = resolveType(il_function, il_function->getType());
            auto llvm_function = LLVMAddFunction(llvm_module,
                il_function->name.c_str(), llvm_functype);

            for (auto block : il_function->blocks) {
                createBlock(llvm_function, block);
            }
            for (auto block : il_function->blocks) {
                generateBlock(block);
            }
            for (auto block : il_function->blocks) {
                linkBlock(block);
            }
        }

        LLVMDisposeBuilder(builder);
        builder = nullptr;
    }
};
*/

//------------------------------------------------------------------------------
// TRANSLATION
//------------------------------------------------------------------------------

typedef std::map<std::string, bangra_preprocessor> NameMacroMap;
typedef std::unordered_map<std::string, Type *> NameTypeMap;
typedef std::unordered_map<std::string, ILValueRef> NameValueMap;

//------------------------------------------------------------------------------

struct GlobalEnvironment {
    ILModuleRef module;
    ILBuilderRef builder;
};

struct Environment;
typedef std::shared_ptr<Environment> EnvironmentRef;

struct Environment : std::enable_shared_from_this<Environment> {
protected:
    NameValueMap values;
    NameValueMap mergevalues;
public:
    GlobalEnvironment &global;
    EnvironmentRef parent;

    Environment(GlobalEnvironment &global_) :
        std::enable_shared_from_this<Environment>(),
        global(global_)
    {
        assert(!values.size());
        assert(!mergevalues.size());
    }

    Environment(const EnvironmentRef &parent_) :
        std::enable_shared_from_this<Environment>(),
        global(parent_->global),
        parent(parent_)
    {
        assert(!values.size());
        assert(!mergevalues.size());
    }

    void mergeup() {
        if (!parent) return;

        //dump();

        for (auto entry : mergevalues) {
            if (parent->values.count(entry.first)) {
                parent->values[entry.first] = entry.second;
            } else {
                parent->mergevalues[entry.first] = entry.second;
            }
        }
    }

    const NameValueMap &getLocals() {
        return values;
    }

    const NameValueMap &getMergeLocals() {
        return mergevalues;
    }

    void dump() {
        printf("%i values, %i mergevalues\n",
            (int)values.size(), (int)mergevalues.size());
        for (auto entry : values) {
            ilMessage(entry.second, "%p.%s\n",
                (void *)this, entry.first.c_str());
            assert(entry.second);
        }
        for (auto entry : mergevalues) {
            ilMessage(entry.second, "%p.%s\n",
                (void *)this, entry.first.c_str());
            assert(entry.second);
        }
    }

    bool isLocal(const std::string &name) {
        return values.count(name);
    }

    bool isMergeLocal(const std::string &name) {
        return mergevalues.count(name);
    }

    ILValueRef getLocal(const std::string &name) {
        if (values.count(name)) {
            return values.at(name);
        }
        return nullptr;
    }

    ILValueRef getMergeLocal(const std::string &name) {
        if (mergevalues.count(name)) {
            return mergevalues.at(name);
        }
        return nullptr;
    }

    bool set(const std::string &name, const ILValueRef &value) {
        if (isLocal(name)) {
            replaceLocal(name, value);
        } else {
            if (resolve(name)) {
                replaceScoped(name, value);
            } else {
                return false;
            }
        }
        return true;
    }

    void setLocal(const std::string &name, const ILValueRef &value) {
        assert(!isLocal(name));
        assert(value);
        values[name] = value;
    }

    void replaceLocal(const std::string &name, const ILValueRef &value) {
        assert(isLocal(name));
        assert(value);
        values[name] = value;
    }

    void replaceScoped(const std::string &name, const ILValueRef &value) {
        assert(!isLocal(name));
        assert(value);
        mergevalues[name] = value;
    }

    ILValueRef resolve(const std::string &name) {
        Environment *penv = this;
        while (penv) {
            ILValueRef result = (*penv).getMergeLocal(name);
            if (result) {
                return result;
            }
            result = (*penv).getLocal(name);
            if (result) {
                return result;
            }
            penv = penv->parent.get();
        }
        return nullptr;
    }

};

static std::unordered_map<std::string, bangra_preprocessor> preprocessors;

//------------------------------------------------------------------------------

static bool isSymbol (const Value *expr, const char *sym) {
    if (expr) {
        if (auto symexpr = llvm::dyn_cast<Symbol>(expr))
            return (symexpr->getValue() == sym);
    }
    return false;
}

//------------------------------------------------------------------------------

#define UNPACK_ARG(expr, name) \
    expr = next(expr); ValueRef name = expr

//------------------------------------------------------------------------------

ILValueRef translate(const EnvironmentRef &env, ValueRef expr);

void valueError (ValueRef expr, const char *format, ...) {
    Anchor *anchor = NULL;
    if (expr)
        anchor = expr->findValidAnchor();
    if (!anchor) {
        if (expr)
            printValue(expr);
    }
    va_list args;
    va_start (args, format);
    Anchor::printErrorV(anchor, format, args);
    va_end (args);
}

void valueErrorV (ValueRef expr, const char *fmt, va_list args) {
    Anchor *anchor = NULL;
    if (expr)
        anchor = expr->findValidAnchor();
    if (!anchor) {
        if (expr)
            printValue(expr);
    }
    Anchor::printErrorV(anchor, fmt, args);
}

template <typename T>
static T *astVerifyKind(ValueRef expr) {
    T *obj = expr?llvm::dyn_cast<T>(expr):NULL;
    if (obj) {
        return obj;
    } else {
        valueError(expr, "%s expected, not %s",
            valueKindName(T::kind()),
            valueKindName(kindOf(expr)));
    }
    return nullptr;
}

/*
static ILValueRef parse_cdecl (const EnvironmentRef &env, ValueRef expr) {
    UNPACK_ARG(expr, expr_returntype);
    UNPACK_ARG(expr, expr_parameters);

    ILValueRef returntype = translate(env, expr_returntype);

    Pointer *params = astVerifyKind<Pointer>(expr_parameters);

    std::vector<ILValueRef> paramtypes;

    ValueRef param = at(params);
    bool vararg = false;
    while (param) {
        ValueRef nextparam = next(param);
        if (!nextparam && isSymbol(param, "...")) {
            vararg = true;
        } else {
            paramtypes.push_back(translate(env, param));
        }

        param = nextparam;
    }

    return env->global.builder->cdecl(returntype, paramtypes, vararg);
}

static ILValueRef parse_external (const EnvironmentRef &env, ValueRef expr) {
    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_type);

    ILValueRef name = translate(env, expr_name);
    ILValueRef type = translate(env, expr_type);

    return env->global.builder->external(name, type);
}
*/

static ILValueRef parse_do (const EnvironmentRef &env, ValueRef expr) {
    expr = next(expr);

    auto subenv = std::make_shared<Environment>(env);

    ILValueRef value;
    while (expr) {
        value = translate(subenv, expr);
        expr = next(expr);
    }

    subenv->mergeup();

    if (value)
        return value;
    else
        return ILConstTuple::create({});

}

static ILValueRef parse_function (const EnvironmentRef &env, ValueRef expr) {
    UNPACK_ARG(expr, expr_parameters);

    auto currentblock = env->global.builder->continuation;

    auto function = ILContinuation::create(env->global.module);

    env->global.builder->continueAt(function);
    auto subenv = std::make_shared<Environment>(env);

    subenv->setLocal("this-function", function);

    Pointer *params = astVerifyKind<Pointer>(expr_parameters);
    ValueRef param = at(params);
    while (param) {
        Symbol *symname = astVerifyKind<Symbol>(param);
        auto bp = ILParameter::create();
        function->appendParameter(bp);
        subenv->setLocal(symname->getValue(), bp);
        param = next(param);
    }
    auto ret = function->appendParameter(ILParameter::create());

    auto result = parse_do(subenv, expr_parameters);

    env->global.builder->br({ret, result});

    env->global.builder->continueAt(currentblock);

    return function;
}

static ILValueRef parse_implicit_apply (const EnvironmentRef &env, ValueRef expr) {
    ValueRef expr_callable = expr;
    UNPACK_ARG(expr, arg);

    ILValueRef callable = translate(env, expr_callable);

    std::vector<ILValueRef> args;
    args.push_back(callable);

    while (arg) {
        args.push_back(translate(env, arg));

        arg = next(arg);
    }

    return env->global.builder->call(args);
}

static ILValueRef parse_apply (const EnvironmentRef &env, ValueRef expr) {
    expr = next(expr);
    return parse_implicit_apply(env, expr);
}

bool hasTypeValue(Type *type) {
    assert(type);
    if ((type == Type::Void) || (type == Type::Empty))
        return false;
    return true;
}

static ILValueRef parse_select (const EnvironmentRef &env, ValueRef expr) {
    UNPACK_ARG(expr, expr_condition);
    UNPACK_ARG(expr, expr_true);
    UNPACK_ARG(expr, expr_false);

    ILValueRef condition = translate(env, expr_condition);
    auto bbstart = env->global.builder->continuation;

    auto bbtrue = ILContinuation::create(env->global.module);
    env->global.builder->continueAt(bbtrue);

    EnvironmentRef subenv_true = std::make_shared<Environment>(env);
    ILValueRef trueexpr = translate(subenv_true, expr_true);
    auto bbtrue_end = env->global.builder->continuation;

    bool returnValue = hasTypeValue(trueexpr->getType());

    ILContinuationRef bbfalse;
    ILContinuationRef bbfalse_end;
    ILValueRef falseexpr;
    EnvironmentRef subenv_false;
    if (expr_false) {
        subenv_false = std::make_shared<Environment>(env);
        bbfalse = ILContinuation::create(env->global.module);
        env->global.builder->continueAt(bbfalse);
        falseexpr = translate(subenv_false, expr_false);
        bbfalse_end = env->global.builder->continuation;
        returnValue = returnValue && hasTypeValue(falseexpr->getType());
    } else {
        returnValue = false;
    }

    auto bbdone = ILContinuation::create(env->global.module);
    ILValueRef result;
    std::vector<ILValueRef> trueexprs;
    trueexprs.push_back(bbdone);
    if (returnValue) {
        auto result_param = ILParameter::create();
        bbdone->appendParameter(result_param);
        trueexprs.push_back(trueexpr);
        result = result_param;
    } else {
        result = ILConstTuple::create({});
    }

    env->global.builder->continueAt(bbtrue_end);
    env->global.builder->br(trueexprs);

    if (bbfalse) {
        env->global.builder->continueAt(bbfalse_end);
        std::vector<ILValueRef> falsexprs;
        falsexprs.push_back(bbdone);
        if (returnValue)
            falsexprs.push_back(falseexpr);
        env->global.builder->br(falsexprs);
    } else {
        bbfalse = bbdone;
    }

    env->global.builder->continueAt(bbstart);
    env->global.builder->br(
        { ILIntrinsic::Branch, condition, bbtrue, bbfalse });

    env->global.builder->continueAt(bbdone);

    return result;
}

static ILValueRef parse_let(const EnvironmentRef &env, ValueRef expr) {
    UNPACK_ARG(expr, expr_sym);
    UNPACK_ARG(expr, expr_value);

    Symbol *symname = astVerifyKind<Symbol>(expr_sym);
    ILValueRef value;

    if (expr_value)
        value = translate(env, expr_value);
    else
        value = ILConstTuple::create({});

    if (env->isLocal(symname->getValue())) {
        valueError(symname, "already defined");
    }
    env->setLocal(symname->getValue(), value);

    return value;
}

static ILValueRef parse_set(const EnvironmentRef &env, ValueRef expr) {
    UNPACK_ARG(expr, expr_sym);
    UNPACK_ARG(expr, expr_value);

    auto value = translate(env, expr_value);

    Symbol *symname = astVerifyKind<Symbol>(expr_sym);
    std::string name = symname->getValue();

    if (!env->set(name, value)) {
        valueError(symname, "unknown symbol");
    }

    return value;
}

template<typename OpT>
static ILValueRef parse_binary_op (const EnvironmentRef &env, ValueRef expr) {
    UNPACK_ARG(expr, expr_lvalue);
    UNPACK_ARG(expr, expr_rvalue);

    ILValueRef lvalue = translate(env, expr_lvalue);
    ILValueRef rvalue = translate(env, expr_rvalue);

    return OpT::create(lvalue, rvalue);
}

struct TranslateTable {
    typedef ILValueRef (*TranslatorFunc)(const EnvironmentRef &env, ValueRef expr);

    struct Translator {
        int mincount;
        int maxcount;
        unsigned flags;

        TranslatorFunc translate;

        Translator() :
            mincount(-1),
            maxcount(-1),
            flags(0),
            translate(NULL)
            {}

    };

    std::unordered_map<std::string, Translator> translators;

    void set(TranslatorFunc translate, const std::string &name,
        int mincount, int maxcount, unsigned flags=0) {
        Translator translator;
        translator.mincount = mincount;
        translator.maxcount = maxcount;
        translator.translate = translate;
        translator.flags = flags;
        translators[name] = translator;
    }

    static bool verifyParameterCount (ValueRef expr, int mincount, int maxcount) {
        if ((mincount <= 0) && (maxcount == -1))
            return true;

        ValueRef head = expr;
        expr = next(expr);

        int argcount = 0;
        while (expr) {
            ++ argcount;
            if (maxcount >= 0) {
                if (argcount > maxcount) {
                    valueError(expr, "excess argument. At most %i arguments expected", maxcount);
                    return false;
                }
            } else if (mincount >= 0) {
                if (argcount >= mincount)
                    break;
            }
            expr = next(expr);
        }
        if ((mincount >= 0) && (argcount < mincount)) {
            valueError(head, "at least %i arguments expected", mincount);
            return false;
        }
        return true;
    }

    TranslatorFunc match(ValueRef expr) {
        Symbol *head = astVerifyKind<Symbol>(expr);
        auto &t = translators[head->getValue()];
        if (!t.translate) return nullptr;
        verifyParameterCount(expr, t.mincount, t.maxcount);
        return t.translate;
    }

};

static TranslateTable translators;

//------------------------------------------------------------------------------

template<typename OpT>
void registerBinaryOpParser() {
    translators.set(parse_binary_op<OpT>, OpT::OpName(), 2, 2);
}

static void registerTranslators() {
    auto &t = translators;
    // t.set(parse_external, "external", 2, 2);
    t.set(parse_let, "let", 1, 2);
    t.set(parse_set, "set", 2, 2);
    t.set(parse_apply, "apply", 1, -1);
    t.set(parse_do, "do", 0, -1);
    t.set(parse_select, "select", 2, 3);

    // t.set(parse_cdecl, "cdecl", 2, 2);
    t.set(parse_function, "function", 1, -1);

    registerBinaryOpParser<ILOpAdd>();
    registerBinaryOpParser<ILOpSub>();
    registerBinaryOpParser<ILOpMul>();
    registerBinaryOpParser<ILOpDiv>();

    registerBinaryOpParser<ILOpEqual>();
    registerBinaryOpParser<ILOpNotEqual>();
    registerBinaryOpParser<ILOpGreaterThan>();
    registerBinaryOpParser<ILOpGreaterEqual>();
    registerBinaryOpParser<ILOpLessThan>();
    registerBinaryOpParser<ILOpLessEqual>();
}

static ILValueRef translateFromList (const EnvironmentRef &env, ValueRef expr) {
    assert(expr);
    astVerifyKind<Symbol>(expr);
    auto func = translators.match(expr);
    if (func) {
        return func(env, expr);
    } else {
        return parse_implicit_apply(env, expr);
    }
}

ILValueRef translate (const EnvironmentRef &env, ValueRef expr) {
    assert(expr);
    ILValueRef result;
    if (!isAtom(expr)) {
        result = translateFromList(env, at(expr));
    } else if (auto sym = llvm::dyn_cast<Symbol>(expr)) {
        std::string value = sym->getValue();
        result = env->resolve(value);
        if (!result) {
            valueError(expr, "unknown symbol '%s'",
                value.c_str());
        }
    } else if (auto str = llvm::dyn_cast<String>(expr)) {
        result = ILConstString::create(str);
    } else if (auto integer = llvm::dyn_cast<Integer>(expr)) {
        result = ILConstInteger::create(integer,
            llvm::cast<IntegerType>(Type::Int32));
    } else if (auto real = llvm::dyn_cast<Real>(expr)) {
        result = ILConstReal::create(real,
            llvm::cast<RealType>(Type::Double));
    } else {
        valueError(expr, "expected expression, not %s",
            valueKindName(kindOf(expr)));
    }
    if (result && !result->anchor.isValid()) {
        Anchor *anchor = expr->findValidAnchor();
        if (anchor) {
            result->anchor = *anchor;
        }
    }
    assert(result);
    return result;
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

static void init() {
    bangra::support_ansi = isatty(fileno(stdout));

    Type::initTypes();
    ILIntrinsic::initIntrinsics();
    registerTranslators();

    if (!gc_root)
        gc_root = new Pointer();

    LLVMEnablePrettyStackTrace();
    LLVMLinkInMCJIT();
    //LLVMLinkInInterpreter();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeDisassembler();

}

//------------------------------------------------------------------------------

static void setupRootEnvironment (const EnvironmentRef &env) {
    auto builder = env->global.builder;
    env->setLocal("void", ILConstTypePointer::create(Type::Void));
    env->setLocal("null", ILConstTypePointer::create(Type::Null));
    env->setLocal("half", ILConstTypePointer::create(Type::Half));
    env->setLocal("float", ILConstTypePointer::create(Type::Float));
    env->setLocal("double", ILConstTypePointer::create(Type::Double));
    env->setLocal("bool", ILConstTypePointer::create(Type::Bool));

    env->setLocal("int8", ILConstTypePointer::create(Type::Int8));
    env->setLocal("int16", ILConstTypePointer::create(Type::Int16));
    env->setLocal("int32", ILConstTypePointer::create(Type::Int32));
    env->setLocal("int64", ILConstTypePointer::create(Type::Int64));

    env->setLocal("uint8", ILConstTypePointer::create(Type::UInt8));
    env->setLocal("uint16", ILConstTypePointer::create(Type::UInt16));
    env->setLocal("uint32", ILConstTypePointer::create(Type::UInt32));
    env->setLocal("uint64", ILConstTypePointer::create(Type::UInt64));

    env->setLocal("usize_t",
        ILConstTypePointer::create(Type::Integer(sizeof(size_t)*8,false)));

    env->setLocal("rawstring", ILConstTypePointer::create(Type::Rawstring));

    env->setLocal("int", env->resolve("int32"));

    auto booltype = llvm::cast<IntegerType>(Type::Bool);
    env->setLocal("true", ILConstInteger::create(new Integer(1), booltype));
    env->setLocal("false", ILConstInteger::create(new Integer(0), booltype));
}

static void teardownRootEnvironment (const EnvironmentRef &env) {
}

static void handleException(const EnvironmentRef &env, ValueRef expr) {
    ValueRef tb = expr->getNext();
    if (tb && tb->getKind() == V_String) {
        std::cerr << llvm::cast<String>(tb)->getValue();
    }
    streamValue(std::cerr, expr, 0, true);
    valueError(expr, "an exception was raised");
}

static bool translateRootValueList (const EnvironmentRef &env, ValueRef expr) {

    auto mainfunc = ILContinuation::create(env->global.module);
    auto ret = mainfunc->appendParameter(ILParameter::create());
    env->global.builder->continueAt(mainfunc);

    parse_do(env, expr);
    env->global.builder->br({ ret });

    std::cout << env->global.module->getRepr();
    /*
    {
        ILSolver solver(env->global.module);
        solver.run();
        std::cout << env->global.module->getRepr();
    }
    */


    fflush(stdout);

    /*
    LLVMModuleRef module = LLVMModuleCreateWithName("main");
    LLVMValueRef func = nullptr;
    {
        CodeGenerator cg(env->global.module, module);
        cg.generate();
        //func = cg.resolveValue(mainfunc);
    }
    LLVMDumpModule(module);
    assert(func);

    char *error = NULL;
    LLVMVerifyModule(module, LLVMAbortProcessAction, &error);
    LLVMDisposeMessage(error);

    error = NULL;
    LLVMExecutionEngineRef engine = NULL;

    LLVMMCJITCompilerOptions options;
    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    //options.OptLevel = 0;
    //options.EnableFastISel = true;
    //options.CodeModel = LLVMCodeModelLarge;

    LLVMCreateMCJITCompilerForModule(&engine, module,
        &options, sizeof(options), &error);

    if (error) {
        valueError(expr, "%s", error);
        LLVMDisposeMessage(error);
        return false;
    }

    //~ for (auto it : env->globals->globalptrs) {
        //~ LLVMAddGlobalMapping(engine, std::get<0>(it), std::get<1>(it));
    //~ }

    //~ for (auto m : env->globals->globalmodules) {
        //~ LLVMAddModule(engine, m);
    //~ }

    LLVMRunStaticConstructors(engine);

    void *f = LLVMGetPointerToGlobal(engine, func);

    try {
        typedef void (*signature)();
        ((signature)f)();
    } catch (ValueRef expr) {
        handleException(env, expr);
        return false;
    }

    */

    return true;
}

template <typename T>
static T *translateKind(const EnvironmentRef &env, ValueRef expr) {
    T *obj = expr?llvm::dyn_cast<T>(expr):NULL;
    if (obj) {
        return obj;
    } else {
        valueError(expr, "%s expected, not %s",
            valueKindName(T::kind()),
            valueKindName(kindOf(expr)));
    }
    return nullptr;
}

static bool compileModule (const EnvironmentRef &env, ValueRef expr) {
    assert(expr);

    std::string lastlang = "";
    while (true) {
        Symbol *head = translateKind<Symbol>(env, expr);
        if (!head) return false;
        if (head->getValue() == BANGRA_HEADER)
            break;
        auto preprocessor = preprocessors[head->getValue()];
        if (!preprocessor) {
            valueError(expr, "unrecognized header: '%s'; try '%s' instead.",
                head->getValue().c_str(),
                BANGRA_HEADER);
            return false;
        }
        if (lastlang == head->getValue()) {
            valueError(expr,
                "header has not changed after preprocessing; is still '%s'.",
                head->getValue().c_str());
        }
        lastlang = head->getValue();
        ValueRef orig_expr = expr;
        try {
            expr = preprocessor(env.get(), new Pointer(expr));
        } catch (ValueRef expr) {
            handleException(env, expr);
            return false;
        }
        if (!expr) {
            valueError(orig_expr,
                "preprocessor returned null.",
                head->getValue().c_str());
            return false;
        }
        expr = at(expr);
    }

    return translateRootValueList (env, expr);
}

static bool compileMain (ValueRef expr) {
    GlobalEnvironment global;
    global.module = std::make_shared<ILModule>();
    global.builder = std::make_shared<ILBuilder>(global.module);
    auto env = std::make_shared<Environment>(global);

    setupRootEnvironment(env);

    bool result = compileModule(env, at(expr));

    teardownRootEnvironment(env);

    return result;
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

static ValueRef parseLoader(const char *executable_path) {
    // attempt to read bootstrap expression from end of binary
    auto file = MappedFile::open(executable_path);
    if (!file) {
        fprintf(stderr, "could not open binary\n");
        return NULL;
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
        if (cursor < ptr) return NULL;
    }
    if (*cursor != ')') return NULL;
    cursor--;
    // seek backwards to find beginning of expression
    while ((cursor >= ptr) && (*cursor != '('))
        cursor--;

    bangra::Parser footerParser;
    ValueRef expr = footerParser.parseMemory(
        cursor, ptr + size, executable_path, cursor - ptr);
    if (!expr) {
        fprintf(stderr, "could not parse footer expression\n");
        return NULL;
    }
    if (expr->getKind() != V_Pointer)  {
        fprintf(stderr, "footer expression is not a list\n");
        return NULL;
    }
    expr = at(expr);
    if (expr->getKind() != V_Symbol)  {
        fprintf(stderr, "footer expression does not begin with symbol\n");
        return NULL;
    }
    if (!isSymbol(expr, "script-size"))  {
        fprintf(stderr, "footer expression does not begin with 'script-size'\n");
        return NULL;
    }
    expr = next(expr);
    if (expr->getKind() != V_Integer)  {
        fprintf(stderr, "script-size argument is not integer\n");
        return NULL;
    }
    auto offset = llvm::cast<Integer>(expr)->getValue();
    if (offset <= 0) {
        fprintf(stderr, "script-size must be larger than zero\n");
        return NULL;
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

    ValueRef expr = NULL;
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

    if (expr && bangra::isKindOf<bangra::Pointer>(expr)) {
        bangra::gc_root = cons(expr, bangra::gc_root);
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

    ValueRef expr = NULL;

    if (argv) {
        if (argv[0]) {
            std::string loader = bangra::GetExecutablePath(argv[0]);
            // string must be kept resident
            bang_executable_path = strdup(loader.c_str());

            expr = bangra::parseLoader(bang_executable_path);
        }

        if (!expr) {
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

    if (expr && bangra::isKindOf<bangra::Pointer>(expr)) {
        bangra::gc_root = cons(expr, bangra::gc_root);
        bangra::compileMain(expr);
    } else {
        return 1;
    }

    return 0;
}

ValueRef bangra_parse_file(const char *path) {
    bangra::Parser parser;
    ValueRef expr = parser.parseFile(path);
    if (expr) {
        bangra::gc_root = cons(expr, bangra::gc_root);
    }
    return expr;
}

void bangra_print_value(ValueRef expr, int depth) {
    if (depth < 0) {
        bangra::printValue(expr, 0, false);
    } else {
        bangra::printValue(expr, (size_t)depth, true);
    }
}

ValueRef bangra_format_value(ValueRef expr, int depth) {
    std::string str;
    if (depth < 0) {
        str = bangra::formatValue(expr, 0, false);
    } else {
        str = bangra::formatValue(expr, (size_t)depth, true);
    }
    return new bangra::String(str.c_str(), str.size());

}

void bangra_set_preprocessor(const char *name, bangra_preprocessor f) {
    bangra::preprocessors[name] = f;
}

bangra_preprocessor bangra_get_preprocessor(const char *name) {
    return bangra::preprocessors[name];
}

int bangra_get_kind(ValueRef expr) {
    return kindOf(expr);
}

ValueRef bangra_at(ValueRef expr) {
    if (expr) {
        if (bangra::isKindOf<bangra::Pointer>(expr)) {
            return at(expr);
        }
    }
    return NULL;
}

ValueRef bangra_clone(ValueRef expr) {
    if (expr) {
        return expr->clone();
    }
    return NULL;
}

ValueRef bangra_deep_clone(ValueRef expr) {
    if (expr) {
        if (auto tab = llvm::dyn_cast<bangra::Table>(expr)) {
            return tab->deepClone();
        }
        return expr->clone();
    }
    return NULL;
}

ValueRef bangra_next(ValueRef expr) {
    return next(expr);
}

ValueRef bangra_set_next(ValueRef lhs, ValueRef rhs) {
    if (lhs) {
        if (lhs->getNext() != rhs) {
            return cons(lhs, rhs);
        } else {
            return lhs;
        }
    }
    return NULL;
}

ValueRef bangra_set_at_mutable(ValueRef lhs, ValueRef rhs) {
    if (lhs) {
        if (auto ptr = llvm::dyn_cast<bangra::Pointer>(lhs)) {
            ptr->setAt(rhs);
            return lhs;
        }
    }
    return NULL;
}

ValueRef bangra_set_next_mutable(ValueRef lhs, ValueRef rhs) {
    if (lhs) {
        lhs->setNext(rhs);
        return lhs;
    }
    return NULL;
}

const char *bangra_string_value(ValueRef expr) {
    if (expr) {
        if (auto str = llvm::dyn_cast<bangra::String>(expr)) {
            return str->getValue().c_str();
        }
    }
    return NULL;
}

signed long long int bangra_string_size(ValueRef expr) {
    if (expr) {
        if (auto str = llvm::dyn_cast<bangra::String>(expr)) {
            return (signed long long int)str->getValue().size() + 1;
        }
    }
    return 0;
}

ValueRef bangra_string_concat(ValueRef a, ValueRef b) {
    if (a && b) {
        auto str_a = llvm::dyn_cast<bangra::String>(a);
        auto str_b = llvm::dyn_cast<bangra::String>(b);
        if (str_a && str_b) {
            auto str_result = str_a->getValue() + str_b->getValue();
            if (str_a->getKind() == bangra::V_String) {
                return new bangra::String(str_result.c_str(), str_result.size());
            } else {
                return new bangra::Symbol(str_result.c_str(), str_result.size());
            }
        }
    }
    return NULL;
}

ValueRef bangra_string_slice(ValueRef expr, int start, int end) {
    if (expr) {
        if (auto str = llvm::dyn_cast<bangra::String>(expr)) {
            auto value = str->getValue();
            int size = (int)value.size();
            if (start < 0)
                start = size + start;
            if (start < 0)
                start = 0;
            else if (start > size)
                start = size;
            if (end < 0)
                end = size + end;
            if (end < start)
                end = start;
            else if (end > size)
                end = size;
            int len = end - start;
            value = value.substr((size_t)start, (size_t)len);
            if (str->getKind() == bangra::V_String) {
                return new bangra::String(value.c_str(), value.size());
            } else {
                return new bangra::Symbol(value.c_str(), value.size());
            }
        }
    }
    return NULL;
}

void *bangra_handle_value(ValueRef expr) {
    if (expr) {
        if (auto handle = llvm::dyn_cast<bangra::Handle>(expr)) {
            return handle->getValue();
        }
    }
    return NULL;
}

ValueRef bangra_handle(void *ptr) {
    auto handle = new bangra::Handle(ptr);
    bangra::gc_root = new bangra::Pointer(handle, bangra::gc_root);
    return handle;
}

void bangra_error_message(ValueRef context, const char *format, ...) {
    va_list args;
    va_start (args, format);
    bangra::valueErrorV(context, format, args);
    va_end (args);
}

int bangra_eq(Value *a, Value *b) {
    if (a == b) return true;
    if (a && b) {
        auto kind = a->getKind();
        if (kind != b->getKind())
            return false;
        switch (kind) {
            case bangra::V_String:
            case bangra::V_Symbol: {
                bangra::String *sa = llvm::cast<bangra::String>(a);
                bangra::String *sb = llvm::cast<bangra::String>(b);
                return sa->getValue() == sb->getValue();
            } break;
            case bangra::V_Real: {
                bangra::Real *sa = llvm::cast<bangra::Real>(a);
                bangra::Real *sb = llvm::cast<bangra::Real>(b);
                return sa->getValue() == sb->getValue();
            } break;
            case bangra::V_Integer: {
                bangra::Integer *sa = llvm::cast<bangra::Integer>(a);
                bangra::Integer *sb = llvm::cast<bangra::Integer>(b);
                return sa->getValue() == sb->getValue();
            } break;
            case bangra::V_Pointer: {
                bangra::Pointer *sa = llvm::cast<bangra::Pointer>(a);
                bangra::Pointer *sb = llvm::cast<bangra::Pointer>(b);
                return sa->getAt() == sb->getAt();
            } break;
            case bangra::V_Handle: {
                bangra::Handle *sa = llvm::cast<bangra::Handle>(a);
                bangra::Handle *sb = llvm::cast<bangra::Handle>(b);
                return sa->getValue() == sb->getValue();
            } break;
            case bangra::V_Table: {
                bangra::Table *sa = llvm::cast<bangra::Table>(a);
                bangra::Table *sb = llvm::cast<bangra::Table>(b);
                return sa->getValue() == sb->getValue();
            } break;
            default: break;
        };
    }
    return false;
}

ValueRef bangra_table() {
    return new bangra::Table();
}

void bangra_set_key(ValueRef expr, ValueRef key, ValueRef value) {
    if (expr && key) {
        if (auto table = llvm::dyn_cast<bangra::Table>(expr)) {
            table->setKey(key, value);
        }
    }
}

ValueRef bangra_get_key(ValueRef expr, ValueRef key) {
    if (expr && key) {
        if (auto table = llvm::dyn_cast<bangra::Table>(expr)) {
            return table->getKey(key);
        }
    }
    return NULL;
}

void bangra_set_meta(ValueRef expr, ValueRef meta) {
    if (expr) {
        if (auto table = llvm::dyn_cast<bangra::Table>(expr)) {
            table->setMeta(meta);
        }
    }
}

ValueRef bangra_get_meta(ValueRef expr) {
    if (expr) {
        if (auto table = llvm::dyn_cast<bangra::Table>(expr)) {
            return table->getMeta();
        }
    }
    return NULL;
}

ValueRef bangra_set_anchor(
    ValueRef expr, const char *path, int lineno, int column, int offset) {
    if (expr) {
        ValueRef clone = expr->clone();
        clone->anchor.path = path;
        clone->anchor.lineno = lineno;
        clone->anchor.column = column;
        clone->anchor.offset = offset;
        return clone;
    }
    return NULL;
}

ValueRef bangra_set_anchor_mutable(
    ValueRef expr, const char *path, int lineno, int column, int offset) {
    if (expr) {
        expr->anchor.path = path;
        expr->anchor.lineno = lineno;
        expr->anchor.column = column;
        expr->anchor.offset = offset;
        return expr;
    }
    return NULL;
}

ValueRef bangra_ref(ValueRef lhs) {
    return new bangra::Pointer(lhs);
}

ValueRef bangra_string(const char *value, signed long long int size) {
    if (size < 0)
        size = strlen(value);
    return new bangra::String(value, (size_t)size);
}
ValueRef bangra_symbol(const char *value) {
    return new bangra::Symbol(value);
}

ValueRef bangra_real(double value) {
    return new bangra::Real(value);
}
double bangra_real_value(ValueRef value) {
    if (value) {
        if (auto real = llvm::dyn_cast<bangra::Real>(value)) {
            return real->getValue();
        }
    }
    return 0.0;
}

ValueRef bangra_integer(signed long long int value) {
    return new bangra::Integer(value);
}
signed long long int bangra_integer_value(ValueRef value) {
    if (value) {
        if (auto integer = llvm::dyn_cast<bangra::Integer>(value)) {
            return integer->getValue();
        }
    }
    return 0;
}

const char *bangra_anchor_path(ValueRef expr) {
    if (expr) { return expr->anchor.path; }
    return NULL;
}

int bangra_anchor_lineno(ValueRef expr) {
    if (expr) { return expr->anchor.lineno; }
    return 0;
}

int bangra_anchor_column(ValueRef expr) {
    if (expr) { return expr->anchor.column; }
    return 0;
}

int bangra_anchor_offset(ValueRef expr) {
    if (expr) { return expr->anchor.offset; }
    return 0;
}

Environment *bangra_parent_env(Environment *env) {
    return env->parent.get();
}

static int unique_symbol_counter = 0;
ValueRef bangra_unique_symbol(const char *name) {
    if (!name)
        name = "";
    auto symname = bangra::format("#%s%i", name, unique_symbol_counter++);
    return new bangra::Symbol(symname.c_str());
}

void *bangra_import_c_module(ValueRef dest,
    const char *path, const char **args, int argcount) {
    return bangra::importCModule(dest, path, args, argcount);
}

void *bangra_import_c_string(ValueRef dest,
    const char *str, const char *path, const char **args, int argcount) {
    return bangra::importCModule(dest, path, args, argcount, str);
}

void *bangra_xpcall (void *ctx,
    void *(*try_func)(void *),
    void *(*except_func)(void *, ValueRef)) {
    try {
        return try_func(ctx);
    } catch (ValueRef expr) {
        return except_func(ctx, expr);
    }
}

void bangra_raise (ValueRef expr) {
    /*
    std::string tb = bangra::formatTraceback();
    ValueRef annot_expr =
        bangra::cons(expr,
            new bangra::String(tb.c_str(), tb.size()));
    throw annot_expr;
    */
    throw expr;
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
#endif
