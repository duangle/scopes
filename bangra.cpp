#ifndef BANGRA_CPP
#define BANGRA_CPP

//------------------------------------------------------------------------------
// C HEADER
//------------------------------------------------------------------------------

#if defined __cplusplus
extern "C" {
#endif

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

// LLVM compatibility
//------------------------------------------------------------------------------

Environment *bangra_parent_env(Environment *env);
Environment *bangra_meta_env(Environment *env);
void *bangra_llvm_module(Environment *env);
void *bangra_llvm_value(Environment *env, const char *name);
void *bangra_llvm_type(Environment *env, const char *name);
void *bangra_llvm_engine(Environment *env);
void *bangra_import_c_module(ValueRef dest,
    const char *modulename, const char *path, const char **args, int argcount);
void *bangra_import_c_string(ValueRef dest,
    const char *modulename, const char *str, const char *path, const char **args, int argcount);

// methods that apply to all types
//------------------------------------------------------------------------------

int bangra_get_kind(ValueRef expr);
int bangra_eq(Value *a, Value *b);

ValueRef bangra_next(ValueRef expr);
ValueRef bangra_set_next(ValueRef lhs, ValueRef rhs);
ValueRef bangra_set_next_mutable(ValueRef lhs, ValueRef rhs);

ValueRef bangra_dump_value(ValueRef expr);

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

ValueRef bangra_string(const char *value);
ValueRef bangra_symbol(const char *value);
const char *bangra_string_value(ValueRef expr);

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

// handle
//------------------------------------------------------------------------------

ValueRef bangra_handle(void *ptr);
void *bangra_handle_value(ValueRef expr);

// metaprogramming
//------------------------------------------------------------------------------

typedef ValueRef (*bangra_preprocessor)(Environment *, ValueRef );

void bangra_error_message(
    Environment *env, ValueRef context, const char *format, ...);
void bangra_set_preprocessor(const char *name, bangra_preprocessor f);
bangra_preprocessor bangra_get_preprocessor(const char *name);
void bangra_set_macro(Environment *env, const char *name, bangra_preprocessor f);
bangra_preprocessor bangra_get_macro(Environment *env, const char *name);
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

//------------------------------------------------------------------------------
// SHARED LIBRARY IMPLEMENTATION
//------------------------------------------------------------------------------

#undef NDEBUG
#include <sys/types.h>
#ifdef _WIN32
#include "mman.h"
#include "stdlib_ex.h"
#else
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

//#include <execinfo.h>

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
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
        printf("^\n");
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

};

struct Value;
static void printValue(ValueRef e, size_t depth=0, bool naked=true);

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

static int countOf(ValueRef expr) {
    int count = 0;
    while (expr) {
        ++ count;
        expr = next(expr);
    }
    return count;
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

struct Table : Value {
protected:
    std::map< std::string, ValueRef > string_map;
    std::map< int64_t, ValueRef > integer_map;
    std::map< double, ValueRef > real_map;
    std::map< void *, ValueRef > handle_map;
    std::map< ValueRef, ValueRef > value_map;

public:
    Table(ValueRef next_ = NULL) :
        Value(V_Table, next_)
        {}

    void setKey(ValueRef key, ValueRef value) {
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
            default:
                value_map[ key ] = value;
                break;
        }
    }

    ValueRef getKey(ValueRef key) {
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
            default:
                return value_map[ key ];
        }
    }

    static bool classof(const Value *expr) {
        auto kind = expr->getKind();
        return (kind == V_Table);
    }

    static ValueKind kind() {
        return V_Table;
    }

    virtual ValueRef clone() const {
        return new Table(*this);
    }

    virtual void tag() {
        if (tagged()) return;
        Value::tag();

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

    void readSymbol () {
        bool escape = false;
        while (true) {
            if (next_cursor == eof) {
                break;
            }
            char c = *next_cursor++;
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
            char c = *next_cursor++;
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
            char c = *next_cursor++;
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
        while (true) {
            lexer.readToken();
            if (lexer.token == end_token) {
                break;
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
            } else {
                auto elem = parseAny();
                if (errors) return nullptr;
                builder.append(elem);
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

    ValueRef parseNaked (int column = 0, int depth = 0) {
        int lineno = lexer.lineno;

        bool escape = false;
        int subcolumn = 0;

        ListBuilder builder(lexer);

        while (lexer.token != token_eof) {
            if (lexer.token == token_escape) {
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
                escape = false;
                builder.resetStart();
                lineno = lexer.lineno;
                // keep adding elements while we're in the same line
                while ((lexer.token != token_eof)
                        && (lexer.lineno == lineno)) {
                    auto elem = parseNaked(subcolumn, depth + 1);
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

static void printAnchor(ValueRef e, size_t depth=0) {
    if (e) {
        Anchor *anchor = e->findValidAnchor();
        if (!anchor)
            anchor = &e->anchor;
        printf("%s:%i:%i: ",
            anchor->path,
            anchor->lineno,
            anchor->column);
    }
    for(size_t i = 0; i < depth; i++) printf("    ");
}

static void printValue(ValueRef e, size_t depth, bool naked) {
    if (naked) {
        printAnchor(e, depth);
    }

	if (!e) {
        printf("#null#");
        if (naked) putchar('\n');
        return;
    }

	switch(e->getKind()) {
	case V_Pointer: {
        e = at(e);
        if (!e) {
            printf("()");
            if (naked)
                putchar('\n');
            break;
        }
        if (naked) {
            int offset = 0;
            bool single = !next(e);
        print_terse:
            printValue(e, depth, false);
            e = next(e);
            offset++;
            while (e) {
                if (isNested(e))
                    break;
                putchar(' ');
                printValue(e, depth, false);
                e = next(e);
                offset++;
            }
            printf(single?";\n":"\n");
        //print_sparse:
            while (e) {
                if (isAtom(e) // not a list
                    && (offset >= 1) // not first element in list
                    && next(e) // not last element in list
                    && !isNested(next(e))) { // next element can be terse packed too
                    single = false;
                    printAnchor(e, depth + 1);
                    printf("\\ ");
                    goto print_terse;
                }
                printValue(e, depth + 1);
                e = next(e);
                offset++;
            }

        } else {
            putchar('(');
            int offset = 0;
            while (e) {
                if (offset > 0)
                    putchar(' ');
                printValue(e, depth + 1, false);
                e = next(e);
                offset++;
            }
            putchar(')');
            if (naked)
                putchar('\n');
        }
    } return;
    case V_Table: {
        printf("#table %p#", (void *)e);
        if (naked)
            putchar('\n');
    } return;
    case V_Integer: {
        const Integer *a = llvm::cast<Integer>(e);

        if (a->isUnsigned())
            printf("%" PRIu64, a->getValue());
        else
            printf("%" PRIi64, a->getValue());
        if (naked)
            putchar('\n');
    } return;
    case V_Real: {
        const Real *a = llvm::cast<Real>(e);
        printf("%g", a->getValue());
        if (naked)
            putchar('\n');
    } return;
    case V_Handle: {
        const Handle *h = llvm::cast<Handle>(e);
        printf("#handle %p#", h->getValue());
        if (naked)
            putchar('\n');
    } return;
	case V_Symbol:
	case V_String: {
        const String *a = llvm::cast<String>(e);
		if (a->getKind() == V_String) putchar('"');
		for (size_t i = 0; i < a->size(); i++) {
            char c = (*a)[i];
			switch(c) {
			case '"': case '\\':
				putchar('\\');
                putchar(c);
				break;
            case '\n':
                printf("\\n");
                break;
            case '\r':
                printf("\\r");
                break;
            case '\t':
                printf("\\t");
                break;
            case '[': case ']': case '{': case '}': case '(': case ')':
				if (a->getKind() == V_Symbol)
					putchar('\\');
                putchar(c);
				break;
            default:
                if ((c < 32) || (c >= 127)) {
                    unsigned char uc = c;
                    printf("\\x%02x", uc);
                } else {
                    putchar(c);
                }
                break;
			}
		}
		if (a->getKind() == V_String) putchar('"');
        if (naked)
            putchar('\n');
    } return;
    default:
        printf("invalid kind: %i\n", e->getKind());
        assert (false); break;
	}
}

//------------------------------------------------------------------------------
// TRANSLATION ENVIRONMENT
//------------------------------------------------------------------------------

typedef std::map<std::string, LLVMValueRef> NameLLVMValueMap;
typedef std::map<std::string, LLVMTypeRef> NameLLVMTypeMap;
typedef std::list< std::tuple<LLVMValueRef, void *> > GlobalPtrList;
typedef std::map<std::string, bangra_preprocessor> NameMacroMap;

//------------------------------------------------------------------------------

struct TranslationGlobals;

struct Environment {
    TranslationGlobals *globals;
    // currently active function
    LLVMValueRef function;
    // currently active block
    LLVMValueRef block;
    // currently evaluated value
    ValueRef expr;

    NameLLVMValueMap values;
    NameLLVMTypeMap types;
    NameMacroMap macros;

    // parent env
    Environment *parent;

    bangra_preprocessor preprocessor;

    struct WithValue {
        ValueRef prevexpr;
        Environment *env;

        WithValue(Environment *env_, ValueRef expr_) :
            prevexpr(env_->expr),
            env(env_) {
            if (expr_)
                env_->expr = expr_;
        }
        ~WithValue() {
            env->expr = prevexpr;
        }
    };

    Environment() :
        globals(NULL),
        function(NULL),
        block(NULL),
        expr(NULL),
        parent(NULL),
        preprocessor(NULL)
        {}

    Environment(Environment *parent_) :
        globals(parent_->globals),
        function(parent_->function),
        block(parent_->block),
        expr(parent_->expr),
        parent(parent_),
        preprocessor(parent_->preprocessor)
        {}

    WithValue with_expr(ValueRef expr) {
        return WithValue(this, expr);
    }

    Environment *getMeta() const;
    bool hasErrors() const;
    LLVMBuilderRef getBuilder() const;
    LLVMModuleRef getModule() const;
    LLVMExecutionEngineRef getEngine() const;
    void addQuote(LLVMValueRef value, ValueRef expr);

    LLVMValueRef resolveValue(const std::string &name) {
        Environment *penv = this;
        while (penv) {
            LLVMValueRef result = (*penv).values[name];
            if (result) {
                return result;
            }
            penv = penv->parent;
        }
        return NULL;
    }

    LLVMTypeRef resolveType(const std::string &name) {
        Environment *penv = this;
        while (penv) {
            LLVMTypeRef result = (*penv).types[name];
            if (result) {
                return result;
            }
            penv = (penv->parent)?penv->parent:penv->getMeta();
        }
        return NULL;
    }

    bangra_preprocessor resolveMacro(const std::string &name) {
        Environment *penv = this;
        while (penv) {
            bangra_preprocessor result = (*penv).macros[name];
            if (result) {
                return result;
            }
            penv = penv->parent;
        }
        return NULL;
    }
};

static std::unordered_map<std::string, bangra_preprocessor> preprocessors;

//------------------------------------------------------------------------------

struct TranslationGlobals {
    int compile_errors;
    // module for this translation
    LLVMModuleRef module;
    // builder for this translation
    LLVMBuilderRef builder;
    // execution engine if available
    LLVMExecutionEngineRef engine;
    // meta env; only valid for proto environments
    Environment *meta;
    // global pointers
    GlobalPtrList globalptrs;
    // root environment
    Environment rootenv;

    std::unordered_map<std::string, bool> includes;

    TranslationGlobals() :
        compile_errors(0),
        module(NULL),
        builder(NULL),
        engine(NULL),
        meta(NULL) {
        rootenv.globals = this;
    }

    TranslationGlobals(Environment *env) :
        compile_errors(0),
        module(NULL),
        builder(NULL),
        engine(NULL),
        meta(env) {
        rootenv.globals = this;
    }

    bool hasInclude(const std::string &path) {
        return includes[path];
    }

    void addInclude(const std::string &path) {
        includes[path] = true;
    }

};

//------------------------------------------------------------------------------

Environment *Environment::getMeta() const {
    return globals->meta;
}

bool Environment::hasErrors() const {
    return globals->compile_errors != 0;
}

LLVMBuilderRef Environment::getBuilder() const {
    return globals->builder;
}

LLVMModuleRef Environment::getModule() const {
    return globals->module;
}

LLVMExecutionEngineRef Environment::getEngine() const {
    return globals->engine;
}

void Environment::addQuote(LLVMValueRef value, ValueRef expr) {
    globals->globalptrs.push_back(std::make_tuple(value, (void *)expr));
    gc_root = cons(expr, gc_root);
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
            const PointerType *PTy = cast<PointerType>(Ty);
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
                const VectorType *VT = cast<VectorType>(T);
                ValueRef at = TranslateType(VT->getElementType());
                if(at) {
                    int n = VT->getNumElements();
                    at->setNext(new Integer(n));
                    return new Pointer(new Symbol("vector", at));
                }
        } break;
        case clang::Type::FunctionNoProto:
        case clang::Type::FunctionProto: {
            const FunctionType *FT = cast<FunctionType>(Ty);
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
    const char *modulename, const char *path, const char **args, int argcount,
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
        //LLVMAddModule(env->globals->engine, M);
        return M;
    }

    return NULL;
}

//------------------------------------------------------------------------------
// TRANSLATION
//------------------------------------------------------------------------------

static void dumpTraceback() {
/*
    void *array[10];
    size_t size;
    char **strings;
    size_t i;

    size = backtrace (array, 10);
    strings = backtrace_symbols (array, size);

    for (i = 0; i < size; i++)
        printf ("%s\n", strings[i]);

    free (strings);
*/
}

static void translateErrorV (Environment *env, const char *format, va_list args) {
    ++env->globals->compile_errors;
    if (env->expr && env->expr->anchor.isValid()) {
        Anchor anchor = env->expr->anchor;
        printf("%s:%i:%i: error: ", anchor.path, anchor.lineno, anchor.column);
    } else {
        printf("error: ");
    }
    vprintf (format, args);
    putchar('\n');
    if (env->expr) {
        Anchor anchor = env->expr->anchor;
        dumpFileLine(anchor.path, anchor.offset);
    }
    dumpTraceback();
}

static void translateError (Environment *env, const char *format, ...) {
    va_list args;
    va_start (args, format);
    translateErrorV(env, format, args);
    va_end (args);
}

static bool verifyCount (Environment *env, ValueRef expr, int mincount, int maxcount) {
    auto _ = env->with_expr(expr);

    if ((mincount <= 0) && (maxcount == -1))
        return true;

    int argcount = 0;
    while (expr) {
        ++ argcount;
        if (maxcount >= 0) {
            if (argcount > maxcount) {
                auto _ = env->with_expr(expr);
                translateError(env, "excess argument. At most %i arguments expected.", maxcount);
                return false;
            }
        } else if (mincount >= 0) {
            if (argcount >= mincount)
                break;
        }
        expr = next(expr);
    }
    if ((mincount >= 0) && (argcount < mincount)) {
        translateError(env, "at least %i arguments expected", mincount);
        return false;
    }
    return true;
}

static bool verifyParameterCount (Environment *env, ValueRef expr, int mincount, int maxcount) {
    return verifyCount(env, next(expr), mincount, maxcount);
}

static bool isSymbol (const Value *expr, const char *sym) {
    if (expr) {
        if (auto symexpr = llvm::dyn_cast<Symbol>(expr))
            return (symexpr->getValue() == sym);
    }
    return false;
}

static bool matchSpecialForm (Environment *env, ValueRef expr, const char *name, int mincount, int maxcount) {
    return isSymbol(expr, name) && verifyParameterCount(env, expr, mincount, maxcount);
}

//------------------------------------------------------------------------------

template <typename T>
static T *translateKind(Environment *env, ValueRef expr) {
    auto _ = env->with_expr(expr);
    T *obj = expr?llvm::dyn_cast<T>(expr):NULL;
    if (obj) {
        return obj;
    } else {
        translateError(env, "%s expected, not %s",
            valueKindName(T::kind()),
            valueKindName(kindOf(expr)));
    }
    return nullptr;
}

template <typename T>
static bool verifyKind(Environment *env, ValueRef expr) {
    auto _ = env->with_expr(expr);
    if (isKindOf<T>(expr)) return true;
    translateError(env, "%s expected, not %s",
        valueKindName(T::kind()),
        valueKindName(kindOf(expr)));
    return false;
}

static bool translateInt64 (Environment *env, ValueRef expr, int64_t &value) {
    if (expr) {
        if (auto i = translateKind<Integer>(env, expr)) {
            value = i->getValue();
            return true;
        }
    }
    return false;
}

static bool translateDouble (Environment *env, ValueRef expr, double &value) {
    if (expr) {
        if (auto i = translateKind<Real>(env, expr)) {
            value = i->getValue();
            return true;
        }
    }
    return false;
}

static const char *translateString (Environment *env, ValueRef expr) {
    if (expr) {
        if (auto str = translateKind<String>(env, expr))
            return str->c_str();
    }
    return nullptr;
}

static LLVMTypeRef translateType (Environment *env, ValueRef expr);
static LLVMValueRef translateValue (Environment *env, ValueRef expr);
static LLVMTypeRef translateTypeFromList (Environment *env, ValueRef expr);

static void setupRootEnvironment (Environment *env, const char *modulename);
static void teardownRootEnvironment (Environment *env);
static bool compileModule (Environment *env, ValueRef expr);

static LLVMValueRef translateValueList (Environment *env, ValueRef expr) {
    LLVMValueRef lastresult = NULL;
    while (expr) {
        lastresult = translateValue(env, expr);
        if (env->hasErrors())
            return NULL;
        expr = next(expr);
    }
    return lastresult;
}

static bool verifyInFunction(Environment *env) {
    if (!env->function) {
        translateError(env, "illegal use outside of function");
        return false;
    }
    return true;
}

static bool verifyInBlock(Environment *env) {
    if (!verifyInFunction(env)) return false;
    if (!env->block) {
        translateError(env, "illegal use outside of labeled block");
        return false;
    }
    LLVMBasicBlockRef block = LLVMValueAsBasicBlock(env->block);
    if (LLVMGetBasicBlockTerminator(block)) {
        translateError(env, "block is already terminated.");
        return false;
    }
    return true;
}

static void translateSetBlock(Environment *env, LLVMBasicBlockRef block) {
    if (block)
        LLVMPositionBuilderAtEnd(env->getBuilder(), block);
    LLVMValueRef blockvalue = block?LLVMBasicBlockAsValue(block):NULL;
    env->block = blockvalue;
    env->values["this-block"] = blockvalue;
}

/*
static LLVMValueRef verifyConstant(Environment *env, LLVMValueRef value) {
    if (value && !LLVMIsConstant(value)) {
        translateError(env, "constant value expected");
        return NULL;
    }
    return value;
}
*/

static LLVMBasicBlockRef verifyBasicBlock(Environment *env, LLVMValueRef value) {
    if (value && !LLVMValueIsBasicBlock(value)) {
        translateError(env, "block label expected");
        return NULL;
    }
    return LLVMValueAsBasicBlock(value);
}

static std::string getTypeString(LLVMTypeRef type) {
    char *s = LLVMPrintTypeToString(type);
    std::string result = s;
    LLVMDisposeMessage(s);
    return result;
}

LLVMTypeRef extractFunctionType(Environment *env, LLVMValueRef callee) {
    LLVMTypeRef functype = LLVMTypeOf(callee);
    LLVMTypeKind kind = LLVMGetTypeKind(functype);
    if (kind == LLVMPointerTypeKind) {
        functype = LLVMGetElementType(functype);
        kind = LLVMGetTypeKind(functype);
    }
    if (kind != LLVMFunctionTypeKind) {
        translateError(env, "function expected.");
        return NULL;
    }
    return functype;
}

#define UNPACK_ARG(expr, name) \
    expr = next(expr); ValueRef name = expr

bool translateCallParams(Environment *env, ValueRef expr,
    LLVMValueRef &callee, LLVMValueRef *&args, int &argcount) {

    ValueRef expr_func = expr;

    callee = translateValue(env, expr_func);
    if (!callee) return false;

    auto _ = env->with_expr(expr_func);
    LLVMTypeRef functype = extractFunctionType(env, callee);
    if (!functype) return false;

    expr = next(expr);

    int count = (int)LLVMCountParamTypes(functype);
    bool vararg = LLVMIsFunctionVarArg(functype);
    int mincount = count;
    int maxcount = vararg?-1:count;
    if (!verifyCount(env, expr, mincount, maxcount))
        return false;

    argcount = countOf(expr);
    LLVMTypeRef ptypes[count];
    LLVMGetParamTypes(functype, ptypes);
    args = new LLVMValueRef[argcount];
    int i = 0;
    while (expr) {
        args[i] = translateValue(env, expr);
        if (!args[i]) {
            delete args;
            return false;
        }
        if ((i < count) && (LLVMTypeOf(args[i]) != ptypes[i])) {
            auto _ = env->with_expr(expr);

            translateError(env, "call parameter type (%s) does not match function signature (%s)",
                getTypeString(LLVMTypeOf(args[i])).c_str(),
                getTypeString(ptypes[i]).c_str());
            delete args;
            return false;
        }
        expr = next(expr);
        ++i;
    }

    return true;
}

//------------------------------------------------------------------------------

enum {
    BlockInst = (1 << 0),
};

template<typename ResultT>
struct TranslateTable {
    typedef ResultT (*TranslatorFunc)(Environment *env, ValueRef expr);

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

    TranslatorFunc match(Environment *env, ValueRef expr) {
        Symbol *head = translateKind<Symbol>(env, expr);
        if (!head) return NULL;
        auto &t = translators[head->getValue()];
        if (!t.translate) return NULL;
        if (!verifyParameterCount(env, expr, t.mincount, t.maxcount))
            return NULL;
        if (t.flags & BlockInst) {
            if (!verifyInBlock(env)) return NULL;
#if 0 // TODO
            if (expr->anchor.path) {
                auto fileloc =
                    llvm::DIFile::get(
                        *llvm::unwrap(LLVMGetGlobalContext()),
                        llvm::StringRef(expr->anchor.path, strlen(expr->anchor.path)),
                        llvm::StringRef("", 0));
                auto loc = llvm::DebugLoc::get(
                    expr->anchor.lineno, expr->anchor.column, fileloc);
                llvm::unwrap(env->getBuilder())->SetCurrentDebugLocation(loc);
            }
#endif
        }
        return t.translate;
    }

};

//------------------------------------------------------------------------------
// TRANSLATE VALUES
//------------------------------------------------------------------------------

static TranslateTable<LLVMValueRef> valueTranslators;

typedef LLVMValueRef (*LLVMBinaryOpBuilderFunc)(
    LLVMBuilderRef, LLVMValueRef, LLVMValueRef, const char *);

template<LLVMBinaryOpBuilderFunc func>
void setBinaryOp(const std::string &name) {
    struct TranslateValueBinary {
        static LLVMValueRef translate (Environment *env, ValueRef expr) {
            if (!verifyInBlock(env)) return NULL;

            UNPACK_ARG(expr, expr_lhs);
            UNPACK_ARG(expr, expr_rhs);
            LLVMValueRef lhs = translateValue(env, expr_lhs);
            if (!lhs) return NULL;
            LLVMValueRef rhs = translateValue(env, expr_rhs);
            if (!rhs) return NULL;

            return func(env->getBuilder(), lhs, rhs, "");
        }
    };

    valueTranslators.set(TranslateValueBinary::translate, name, 2, 2, BlockInst);
}

static LLVMValueRef tr_value_int (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);
    UNPACK_ARG(expr, expr_value);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    int64_t value;
    if (!translateInt64(env, expr_value, value)) return NULL;

    return LLVMConstInt(type, value, 1);
}

static LLVMValueRef tr_value_real (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);
    UNPACK_ARG(expr, expr_value);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    double value;
    if (!translateDouble(env, expr_value, value)) return NULL;

    return LLVMConstReal(type, value);
}

static LLVMValueRef tr_value_dump_module (Environment *env, ValueRef expr) {
    LLVMDumpModule(env->getModule());

    return NULL;
}

static LLVMValueRef tr_value_dump (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_arg);

    LLVMValueRef value = translateValue(env, expr_arg);
    if (value) {
        LLVMDumpValue(value);
    }

    return value;
}

static LLVMValueRef tr_value_dumptype (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_arg);

    LLVMTypeRef type = translateType(env, expr_arg);
    if (type) {
        LLVMDumpType(type);
    }

    return NULL;
}

static LLVMValueRef tr_value_constant (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;
    LLVMValueRef init = LLVMGetInitializer(value);
    if (!init) {
        translateError(env, "global has no initializer.");
        return NULL;
    }
    if (!LLVMIsConstant(init)) {
        translateError(env, "global initializer isn't constant.");
        return NULL;
    }
    LLVMSetGlobalConstant(value, true);
    return value;
}

static LLVMValueRef tr_value_declare_global (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_type);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    LLVMValueRef result = LLVMAddGlobal(env->getModule(), type, name);

    if (isKindOf<Symbol>(expr_name))
        env->values[name] = result;

    return result;
}

static LLVMValueRef tr_value_global (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_value);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;

    auto _ = env->with_expr(expr_value);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    bool inlined = false;
    if (!strcmp(name, "")) {
        inlined = true;
        name = "global";
    }

    LLVMValueRef result = LLVMAddGlobal(env->getModule(), LLVMTypeOf(value), name);
    LLVMSetInitializer(result, value);

    if (inlined)
        LLVMSetLinkage(result, LLVMLinkOnceAnyLinkage);

    if (isKindOf<Symbol>(expr_name))
        env->values[name] = result;

    return result;
}

static LLVMValueRef tr_value_quote (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);
    UNPACK_ARG(expr, expr_value);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    LLVMValueRef result = LLVMAddGlobal(env->getModule(), type, "quote");
    env->addQuote(result, expr_value);
    LLVMSetGlobalConstant(result, true);

    return result;
}

static LLVMValueRef tr_value_bitcast (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_type);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    if (LLVMIsConstant(value)) {
        return LLVMConstBitCast(value, type);

    } else {
        if (!verifyInBlock(env)) return NULL;
        return LLVMBuildBitCast(env->getBuilder(), value, type, "");
    }
}

static LLVMValueRef tr_value_ptrtoint (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_type);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    return LLVMBuildPtrToInt(env->getBuilder(), value, type, "");
}

static LLVMValueRef tr_value_inttoptr (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_type);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    return LLVMBuildIntToPtr(env->getBuilder(), value, type, "");
}

static LLVMValueRef tr_value_icmp (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_op);
    UNPACK_ARG(expr, expr_lhs);
    UNPACK_ARG(expr, expr_rhs);

    const char *opname = translateString(env, expr_op);
    if (!opname) return NULL;
    LLVMValueRef lhs = translateValue(env, expr_lhs);
    if (!lhs) return NULL;
    LLVMValueRef rhs = translateValue(env, expr_rhs);
    if (!rhs) return NULL;

    LLVMIntPredicate op;
    if (!strcmp(opname, "==")) {         op = LLVMIntEQ;
    } else if (!strcmp(opname, "!=")) {  op = LLVMIntNE;
    } else if (!strcmp(opname, "u>")) {  op = LLVMIntUGT;
    } else if (!strcmp(opname, "u>=")) { op = LLVMIntUGE;
    } else if (!strcmp(opname, "u<")) {  op = LLVMIntULT;
    } else if (!strcmp(opname, "u<=")) { op = LLVMIntULE;
    } else if (!strcmp(opname, "i>")) {  op = LLVMIntSGT;
    } else if (!strcmp(opname, "i>=")) { op = LLVMIntSGE;
    } else if (!strcmp(opname, "i<")) {  op = LLVMIntSLT;
    } else if (!strcmp(opname, "i<=")) { op = LLVMIntSLE;
    } else {
        auto _ = env->with_expr(expr_op);
        translateError(env,
            "illegal operand. Try one of == != u> u>= u< u<= i> i>= i< i<=.");
        return NULL;
    }

    return LLVMBuildICmp(env->getBuilder(), op, lhs, rhs, "");
}

static LLVMValueRef tr_value_fcmp (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_op);
    UNPACK_ARG(expr, expr_lhs);
    UNPACK_ARG(expr, expr_rhs);

    const char *opname = translateString(env, expr_op);
    if (!opname) return NULL;
    LLVMValueRef lhs = translateValue(env, expr_lhs);
    if (!lhs) return NULL;
    LLVMValueRef rhs = translateValue(env, expr_rhs);
    if (!rhs) return NULL;

    LLVMRealPredicate op;
    if (!strcmp(opname, "false")) {       op = LLVMRealPredicateFalse;
    } else if (!strcmp(opname, "o==")) {  op = LLVMRealOEQ;
    } else if (!strcmp(opname, "o>")) {   op = LLVMRealOGT;
    } else if (!strcmp(opname, "o>=")) {  op = LLVMRealOGE;
    } else if (!strcmp(opname, "o<")) {   op = LLVMRealOLT;
    } else if (!strcmp(opname, "o<=")) {  op = LLVMRealOLE;
    } else if (!strcmp(opname, "o!=")) {  op = LLVMRealONE;
    } else if (!strcmp(opname, "ord")) {  op = LLVMRealORD;
    } else if (!strcmp(opname, "uno")) {  op = LLVMRealUNO;
    } else if (!strcmp(opname, "u==")) {  op = LLVMRealUEQ;
    } else if (!strcmp(opname, "u>")) {   op = LLVMRealUGT;
    } else if (!strcmp(opname, "u>=")) {  op = LLVMRealUGE;
    } else if (!strcmp(opname, "u<")) {   op = LLVMRealULT;
    } else if (!strcmp(opname, "u<=")) {  op = LLVMRealULE;
    } else if (!strcmp(opname, "u!=")) {  op = LLVMRealUNE;
    } else if (!strcmp(opname, "true")) { op = LLVMRealPredicateTrue;
    } else {
        auto _ = env->with_expr(expr_op);
        translateError(env,
            "illegal operand. Try one of false true ord uno o== o!= o> o>= o< o<= u== u!= u> u>= u< u<=.");
        return NULL;
    }

    return LLVMBuildFCmp(env->getBuilder(), op, lhs, rhs, "");
}

static bool isConstantInteger(LLVMValueRef value) {
    return LLVMIsConstant(value) && (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMIntegerTypeKind);
}

static LLVMValueRef tr_value_getelementptr (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_array);

    LLVMValueRef ptr = translateValue(env, expr_array);
    if (!ptr) return NULL;

    auto _ = env->with_expr(expr_array);

    bool all_const = LLVMIsConstant(ptr);

    expr = next(expr);

    LLVMTypeRef ptrtype = LLVMTypeOf(ptr);
    if (LLVMGetTypeKind(ptrtype) != LLVMPointerTypeKind) {
        translateError(env, "base value must be pointer.");
        return NULL;
    }

    int valuecount = countOf(expr);
    LLVMValueRef indices[valuecount];
    int i = 0;
    while (expr) {
        indices[i] = translateValue(env, expr);
        if (indices[i] == NULL) {
            return NULL;
        }

        // for non-constant indices, we can only verify that the typing makes sense
        // for constant indices, we can also verify the range
        bool is_const = LLVMIsConstant(indices[i]);

        LLVMTypeKind kind = LLVMGetTypeKind(ptrtype);
        switch(kind) {
            case LLVMPointerTypeKind: {
                auto _ = env->with_expr(expr);
                if (i != 0) {
                    translateError(env, "subsequent indices must not be pointers.");
                    return NULL;
                }
                ptrtype = LLVMGetElementType(ptrtype);
            } break;
            case LLVMStructTypeKind: {
                auto _ = env->with_expr(expr);
                if (LLVMTypeOf(indices[i]) != LLVMInt32Type()) {
                    translateError(env, "index into struct must be i32.");
                    return NULL;
                }
                if (!is_const) {
                    translateError(env, "index into struct must be constant.");
                    return NULL;
                }
                unsigned idx = (unsigned)LLVMConstIntGetSExtValue(indices[i]);
                unsigned count = LLVMCountStructElementTypes(ptrtype);
                if ((unsigned)idx >= count) {
                    translateError(env,
                        "struct field index is out of bounds.");
                    return NULL;
                }
                ptrtype = LLVMStructGetTypeAtIndex(ptrtype, idx);
            } break;
            case LLVMVectorTypeKind: {
                if (isConstantInteger(indices[i])) {
                    unsigned idx = (unsigned)LLVMConstIntGetSExtValue(indices[i]);
                    unsigned count = LLVMGetVectorSize(ptrtype);
                    if (idx >= count) {
                        auto _ = env->with_expr(expr);
                        translateError(env,
                            "vector index is out of bounds.");
                    }
                }
                ptrtype = LLVMGetElementType(ptrtype);
            } break;
            case LLVMArrayTypeKind: {
                if (isConstantInteger(indices[i])) {
                    unsigned idx = (unsigned)LLVMConstIntGetSExtValue(indices[i]);
                    unsigned count = LLVMGetArrayLength(ptrtype);
                    if (idx >= count) {
                        auto _ = env->with_expr(expr);
                        translateError(env,
                            "array index is out of bounds.");
                    }
                }
                ptrtype = LLVMGetElementType(ptrtype);
            } break;
            default: {
                auto _ = env->with_expr(expr);
                translateError(env,
                    "can not index value with this type. Struct, pointer, vector or array type expected.");
                return NULL;
            } break;
        }

        all_const = all_const && is_const;
        expr = next(expr);
        ++i;
    }

    if (all_const) {
        return LLVMConstInBoundsGEP(ptr, indices, valuecount);
    } else {
        if (!verifyInBlock(env)) return NULL;
        return LLVMBuildGEP(env->getBuilder(), ptr, indices, valuecount, "");
    }
}

static LLVMValueRef tr_value_extractelement (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_index);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    LLVMValueRef index = translateValue(env, expr_index);
    if (!index) return NULL;

    LLVMValueRef result = LLVMBuildExtractElement(env->getBuilder(), value, index, "");
    if (!result) {
        translateError(env, "can not use extract on this value");
    }
    return result;
}

static LLVMValueRef tr_value_extractvalue (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_index);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    int64_t index;
    if (!translateInt64(env, expr_index, index)) return NULL;

    LLVMTypeRef valuetype = LLVMTypeOf(value);
    LLVMTypeKind kind = LLVMGetTypeKind(valuetype);
    switch(kind) {
        case LLVMStructTypeKind: {
            auto _ = env->with_expr(expr_index);
            unsigned count = LLVMCountStructElementTypes(valuetype);
            if ((unsigned)index >= count) {
                translateError(env,
                    "struct field index is out of bounds.");
            }
        } break;
        case LLVMArrayTypeKind: {
            auto _ = env->with_expr(expr_index);
            unsigned count = LLVMGetArrayLength(valuetype);
            if ((unsigned)index >= count) {
                translateError(env,
                    "array offset is out of bounds.");
            }
        } break;
        default: {
            auto _ = env->with_expr(expr_value);
            translateError(env,
                "value passed to extractvalue has illegal type. Struct or array type expected.");
            return NULL;
        } break;
    }

    LLVMValueRef result = LLVMBuildExtractValue(env->getBuilder(), value, index, "");
    if (!result) {
        translateError(env, "can not use extract on this value");
    }
    return result;
}

static LLVMValueRef tr_value_align (Environment *env, ValueRef expr) {

    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_bytes);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    int64_t bytes;
    if (!translateInt64(env, expr_bytes, bytes)) return NULL;

    LLVMSetAlignment(value, bytes);
    return value;
}

static LLVMValueRef tr_value_load (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    return LLVMBuildLoad(env->getBuilder(), value, "");
}

static LLVMValueRef tr_value_store (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_ptr);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;
    LLVMValueRef ptr = translateValue(env, expr_ptr);
    if (!ptr) return NULL;

    return LLVMBuildStore(env->getBuilder(), value, ptr);
}

static LLVMValueRef tr_value_alloca (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;
    if (next(expr)) {
        UNPACK_ARG(expr, expr_value);
        LLVMValueRef value = translateValue(env, expr_value);
        return LLVMBuildArrayAlloca(env->getBuilder(), type, value, "");
    } else {
        return LLVMBuildAlloca(env->getBuilder(), type, "");
    }
}

static LLVMValueRef tr_value_defvalue (Environment *env, ValueRef expr) {

    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_value);

    const Symbol *sym_name = translateKind<Symbol>(env, expr_name);
    LLVMValueRef result = translateValue(env, expr_value);
    if (!result) return NULL;

    const char *name = sym_name->c_str();

    if (!strcmp(name, "this-block")) {
        translateError(env, "name is reserved.");
        return NULL;
    }

    env->values[name] = result;

    return result;
}

static LLVMValueRef tr_value_select (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_if);
    UNPACK_ARG(expr, expr_then);
    UNPACK_ARG(expr, expr_else);

    LLVMValueRef value_if = translateValue(env, expr_if);
    if (!value_if) return NULL;
    LLVMValueRef value_then = translateValue(env, expr_then);
    if (!value_then) return NULL;
    LLVMValueRef value_else = translateValue(env, expr_else);
    if (!value_else) return NULL;

    return LLVMBuildSelect(env->getBuilder(), value_if, value_then, value_else, "");
}

static LLVMValueRef tr_value_deftype (Environment *env, ValueRef expr) {

    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_value);

    const Symbol *sym_name = translateKind<Symbol>(env, expr_name);
    LLVMTypeRef result = translateType(env, expr_value);
    if (!result) return NULL;

    const char *name = sym_name->c_str();
    env->types[name] = result;

    return NULL;
}

static LLVMValueRef tr_value_struct (Environment *env, ValueRef expr) {
    translateTypeFromList(env, expr);
    return NULL;
}

static LLVMValueRef tr_value_block (Environment *env, ValueRef expr) {
    if (!verifyInFunction(env)) return NULL;

    UNPACK_ARG(expr, expr_name);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;

    LLVMBasicBlockRef block = LLVMAppendBasicBlock(env->function, name);
    if (isKindOf<Symbol>(expr_name))
        env->values[name] = LLVMBasicBlockAsValue(block);

    LLVMValueRef blockvalue = LLVMBasicBlockAsValue(block);

    return blockvalue;
}

static LLVMValueRef tr_value_set_block (Environment *env, ValueRef expr) {
    if (!verifyInFunction(env)) return NULL;

    UNPACK_ARG(expr, expr_block);

    LLVMValueRef blockvalue = translateValue(env, expr_block);
    if (!blockvalue) return NULL;
    LLVMBasicBlockRef block = verifyBasicBlock(env, blockvalue);
    if (!block) return NULL;

    LLVMValueRef t = LLVMGetBasicBlockTerminator(block);
    if (t) {
        translateError(env, "block is already terminated.");
        return NULL;
    }

    translateSetBlock(env, block);

    return blockvalue;
}

static LLVMValueRef tr_value_null (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    return LLVMConstNull(type);
}

static LLVMValueRef tr_value_splice (Environment *env, ValueRef expr) {
    expr = next(expr);

    return translateValueList(env, expr);
}

static LLVMValueRef tr_value_define (Environment *env, ValueRef expr) {

    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_params);
    UNPACK_ARG(expr, expr_type);
    UNPACK_ARG(expr, body_expr);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;
    if (!verifyKind<Pointer>(env, expr_params))
        return NULL;

    LLVMTypeRef functype = translateType(env, expr_type);

    if (!functype)
        return NULL;

    auto _ = env->with_expr(expr_type);

    bool inlined = false;
    if (!strcmp(name, "")) {
        inlined = true;
        name = "inlined";
    }

    // todo: external linkage?
    LLVMValueRef func = LLVMAddFunction(
        env->getModule(), name, functype);

    if (isKindOf<Symbol>(expr_name))
        env->values[name] = func;
    if (inlined)
        LLVMSetLinkage(func, LLVMLinkOnceAnyLinkage);

    Environment subenv(env);
    subenv.function = func;

    {
        auto _ = env->with_expr(expr_params);

        expr_params = at(expr_params);
        int argcount = countOf(expr_params);
        int paramcount = LLVMCountParams(func);
        if (argcount == paramcount) {
            LLVMValueRef params[paramcount];
            LLVMGetParams(func, params);
            int i = 0;
            while (expr_params) {
                const Symbol *expr_param = translateKind<Symbol>(env, expr_params);
                if (expr_param) {
                    const char *name = expr_param->c_str();
                    LLVMSetValueName(params[i], name);
                    subenv.values[name] = params[i];
                }
                expr_params = next(expr_params);
                i++;
            }
        } else {
            translateError(env, "parameter name count mismatch (%i != %i); must name all parameter types",
                argcount, paramcount);
            return NULL;
        }
    }

    if (env->hasErrors()) return NULL;

    translateSetBlock(&subenv, LLVMAppendBasicBlock(func, ""));

    {
        auto _ = env->with_expr(body_expr);

        translateValueList(&subenv, body_expr);
        if (env->hasErrors()) return NULL;
    }

    if (env->block)
        LLVMPositionBuilderAtEnd(env->getBuilder(), LLVMValueAsBasicBlock(env->block));

    return func;
}

static bool translateIncoming(Environment *env, ValueRef expr, LLVMValueRef phi) {
    int branchcount = countOf(expr);
    LLVMValueRef values[branchcount];
    LLVMBasicBlockRef blocks[branchcount];
    LLVMTypeRef phitype = LLVMTypeOf(phi);
    int i = 0;
    while (expr) {
        ValueRef expr_pair = expr;
        if (!verifyKind<Pointer>(env, expr_pair))
            return false;
        auto _ = env->with_expr(expr_pair);
        expr_pair = at(expr_pair);
        if (!verifyCount(env, expr_pair, 2, 2)) {
            translateError(env, "exactly 2 parameters expected");
            return false;
        }
        ValueRef expr_value = expr_pair;
        ValueRef expr_block = next(expr_pair);

        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return false;
        LLVMBasicBlockRef block = verifyBasicBlock(env, translateValue(env, expr_block));
        if (!block) return false;
        if (phitype != LLVMTypeOf(value)) {
            translateError(env, "phi node operand is not the same type as the result.");
            return false;
        }

        values[i] = value;
        blocks[i] = block;

        expr = next(expr);
        i++;
    }

    LLVMAddIncoming(phi, values, blocks, branchcount);
    return true;
}

static LLVMValueRef tr_value_phi (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    LLVMValueRef result = LLVMBuildPhi(env->getBuilder(), type, "");

    expr = next(expr);
    if (!translateIncoming(env, expr, result))
        return NULL;

    return result;
}

static LLVMValueRef tr_value_incoming (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_phi);

    LLVMValueRef result = translateValue(env, expr_phi);
    if (!result) return NULL;

    // continue existing phi

    if (LLVMGetInstructionOpcode(result) != LLVMPHI) {
        translateError(env, "value is not a phi instruction.");
        return NULL;
    }

    expr = next(expr);
    if (!translateIncoming(env, expr, result))
        return NULL;

    return result;
}

static LLVMValueRef tr_value_br (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);

    LLVMBasicBlockRef value = verifyBasicBlock(env, translateValue(env, expr_value));
    if (!value) return NULL;

    LLVMValueRef result = LLVMBuildBr(env->getBuilder(), value);
    translateSetBlock(env, NULL);
    return result;
}

static LLVMValueRef tr_value_cond_br (Environment *env, ValueRef expr) {

    UNPACK_ARG(expr, expr_value);
    UNPACK_ARG(expr, expr_then);
    UNPACK_ARG(expr, expr_else);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;
    LLVMBasicBlockRef then_block = verifyBasicBlock(env, translateValue(env, expr_then));
    if (!then_block) return NULL;
    LLVMBasicBlockRef else_block = verifyBasicBlock(env, translateValue(env, expr_else));
    if (!else_block) return NULL;

    LLVMValueRef result = LLVMBuildCondBr(env->getBuilder(), value, then_block, else_block);
    translateSetBlock(env, NULL);
    return result;
}

static LLVMValueRef tr_value_ret (Environment *env, ValueRef expr) {
    auto _ = env->with_expr(expr);

    expr = next(expr);

    LLVMTypeRef functype = extractFunctionType(env, env->function);
    if (!functype) return NULL;

    LLVMTypeRef rettype = LLVMGetReturnType(functype);

    LLVMValueRef result;
    if (!expr) {
        if (rettype != LLVMVoidType()) {
            translateError(env, "return type does not match function signature.");
            return NULL;
        }

        result = LLVMBuildRetVoid(env->getBuilder());
    } else {
        ValueRef expr_value = expr;
        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return NULL;
        auto _ = env->with_expr(expr_value);
        if (rettype != LLVMTypeOf(value)) {
            translateError(env, "return type does not match function signature.");
            return NULL;
        }

        result = LLVMBuildRet(env->getBuilder(), value);
    }
    translateSetBlock(env, NULL);
    return result;
}

static LLVMValueRef tr_value_declare (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_type);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;

    LLVMTypeRef functype = translateType(env, expr_type);

    auto _ = env->with_expr(expr_type);

    if (!functype)
        return NULL;

    LLVMValueRef result = LLVMAddFunction(
        env->getModule(), name, functype);

    if (isKindOf<Symbol>(expr_name))
        env->values[name] = result;

    return result;
}

static LLVMValueRef tr_value_call (Environment *env, ValueRef expr) {
    expr = next(expr);

    LLVMValueRef callee;
    LLVMValueRef *args;
    int argcount;
    if (!translateCallParams(env, expr, callee, args, argcount))
        return NULL;

    LLVMValueRef result = LLVMBuildCall(env->getBuilder(), callee, args, argcount, "");
    delete args;
    return result;
}

static LLVMValueRef tr_value_invoke (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_call);
    UNPACK_ARG(expr, expr_then);
    UNPACK_ARG(expr, expr_catch);

    if (isAtom(expr_call)
        || !matchSpecialForm(env, at(expr_call), "call", 1, -1)) {
        auto _ = env->with_expr(expr_call);
        translateError(env, "call expression expected.");
        return NULL;
    }

    expr_call = next(at(expr_call));

    LLVMValueRef callee;
    LLVMValueRef *args;
    int argcount;
    if (!translateCallParams(env, expr_call, callee, args, argcount))
        return NULL;

    LLVMValueRef result = NULL;

    LLVMBasicBlockRef then_block = verifyBasicBlock(env, translateValue(env, expr_then));
    if (then_block) {
        LLVMBasicBlockRef catch_block = verifyBasicBlock(env, translateValue(env, expr_catch));
        if (catch_block) {
            result = LLVMBuildInvoke(env->getBuilder(),
                callee, args, argcount, then_block, catch_block, "");
        }
    }

    delete args;
    return result;
}

static LLVMValueRef tr_value_landingpad (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);
    UNPACK_ARG(expr, expr_persfn);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;
    LLVMValueRef persfn = translateValue(env, expr_persfn);
    if (!persfn) return NULL;

    expr = next(expr);
    bool cleanup = false;
    if (isSymbol(expr, "cleanup")) {
        cleanup = true;
        expr = next(expr);
    }

    int clausecount = countOf(expr);
    LLVMValueRef result = LLVMBuildLandingPad(
        env->getBuilder(), type, persfn, clausecount, "");

    if (cleanup)
        LLVMSetCleanup(result, true);

    int i = 0;
    while (expr) {
        LLVMValueRef clauseval = translateValue(env, expr);
        LLVMAddClause(result, clauseval);
        expr = next(expr);
        ++i;
    }

    return result;
}

static LLVMValueRef tr_value_resume (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_value);

    LLVMValueRef value = translateValue(env, expr_value);
    if (!value) return NULL;

    return LLVMBuildResume(env->getBuilder(), value);
}

static LLVMValueRef tr_value_unreachable (Environment *env, ValueRef expr) {
    return LLVMBuildUnreachable(env->getBuilder());
}

static LLVMValueRef tr_value_include (Environment *env, ValueRef expr) {
    const char *relative_path = expr->anchor.path;
    if (!relative_path) {
        translateError(env, "can not infer relative path from anchor.");
        return NULL;
    }

    UNPACK_ARG(expr, expr_name);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;

    char *relative_pathc = strdup(relative_path);
    auto filepath = format("%s/%s", dirname(relative_pathc), name);
    free(relative_pathc);

    char filepathbuf[PATH_MAX];
    char *realfilepath = realpath(filepath.c_str(), filepathbuf);

    if (!realfilepath) {
        translateError(env, "could not resolve path.");
        return NULL;
    }

    if (env->globals->hasInclude(realfilepath))
        return NULL;

    env->globals->addInclude(realfilepath);

    // keep resident copy of string
    char *str_filepath = strdup(realfilepath);

    bangra::Parser parser;
    expr = parser.parseFile(str_filepath);
    if (!expr) {
        translateError(env, "unable to parse file at '%s'.",
            filepath.c_str());
        return NULL;
    }

    assert(bangra::isKindOf<bangra::Pointer>(expr));
    bangra::gc_root = cons(expr, bangra::gc_root);

    auto _ = env->with_expr(expr);
    expr = at(expr);

    compileModule(env, expr);

    return NULL;
}

static LLVMValueRef tr_value_nop (Environment *env, ValueRef expr) {
    // do nothing
    return NULL;
}

static LLVMValueRef tr_value_execute (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_callee);

    LLVMValueRef callee = translateValue(env, expr_callee);
    if (!callee) return NULL;

    LLVMTypeRef functype = extractFunctionType(env, callee);
    if (!functype) return NULL;

    if (LLVMGetReturnType(functype) != LLVMVoidType()) {
        translateError(env, "function needs to have return type void.\n");
        return NULL;
    }

    if (LLVMIsFunctionVarArg(functype)) {
        translateError(env, "function must not have variable number of parameters.\n");
        return NULL;
    }

    unsigned paramcount = LLVMCountParamTypes(functype);
    if (paramcount > 1) {
        translateError(env, "function must not have more than one parameter.\n");
        return NULL;
    }

    LLVMTypeRef paramtypes[paramcount];
    LLVMGetParamTypes(functype, paramtypes);

    if (paramcount >= 1) {
        LLVMTypeKind kind = LLVMGetTypeKind(paramtypes[0]);
        if (kind != LLVMPointerTypeKind) {
            translateError(env, "first function parameter must be pointer to environment.\n");
            return NULL;
        }
    }

    char *error = NULL;
    LLVMVerifyModule(env->getModule(), LLVMAbortProcessAction, &error);
    LLVMDisposeMessage(error);

    int opt_level = 0;

    error = NULL;
    LLVMExecutionEngineRef engine;
    int result = LLVMCreateJITCompilerForModule(
        &engine, env->getModule(), opt_level, &error);

    if (error) {
        fprintf(stderr, "error: %s\n", error);
        LLVMDisposeMessage(error);
        exit(EXIT_FAILURE);
    }

    if (result != 0) {
        fprintf(stderr, "failed to create execution engine\n");
        abort();
    }

    for (auto it : env->globals->globalptrs) {
        LLVMAddGlobalMapping(engine, std::get<0>(it), std::get<1>(it));
    }

    //printf("running...\n");
    LLVMRunStaticConstructors(engine);

    void *f = LLVMGetPointerToGlobal(engine, callee);

    env->globals->engine = engine;

    if (paramcount == 0) {
        typedef void (*signature)();
        ((signature)f)();
    } else {
        typedef void (*signature)(Environment *env);
        ((signature)f)(env);
    }

    env->globals->engine = NULL;

    //LLVMRunFunction(engine, callee, 0, NULL);
    //LLVMRunStaticDestructors(engine);

    //printf("done.\n");

    return NULL;
}

static LLVMValueRef tr_value_module (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_name);
    UNPACK_ARG(expr, expr_body);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;

    TranslationGlobals globals(env);

    setupRootEnvironment(&globals.rootenv, name);

    bool result = compileModule(&globals.rootenv, expr_body);

    teardownRootEnvironment(&globals.rootenv);
    if (!result) {
        auto _ = env->with_expr(expr_name);
        translateError(env, "while constructing module");
    }
    return NULL;
}

/*
} else if (matchSpecialForm(env, expr, "import-c", 3, 3)) {
    const char *modulename = translateString(env, expr->nth(1));
    const char *name = translateString(env, expr->nth(2));
    Pointer *args_expr = translateKind<Pointer>(env, expr->nth(3));

    if (modulename && name && args_expr) {
        int argcount = (int)args_expr->size();
        const char *args[argcount];
        bool success = true;
        for (int i = 0; i < argcount; ++i) {
            const char *arg = translateString(env, args_expr->nth(i));
            if (arg) {
                args[i] = arg;
            } else {
                success = false;
                break;
            }
        }
        if (success) {
            importCModule(env, modulename, name, args, argcount);
        }
    }
*/

static void registerValueTranslators() {
    auto &t = valueTranslators;

    t.set(tr_value_int, "int", 2, 2);
    t.set(tr_value_real, "real", 2, 2);
    t.set(tr_value_dump_module, "dump-module", 0, 0);
    t.set(tr_value_dump, "dump", 1, 1);
    t.set(tr_value_dumptype, "dumptype", 1, 1);
    t.set(tr_value_constant, "constant", 1, 1);
    t.set(tr_value_declare_global, "declare-global", 2, 2);
    t.set(tr_value_global, "global", 2, 2);
    t.set(tr_value_quote, "quote", 2, 2);
    t.set(tr_value_bitcast, "bitcast", 2, 2);
    t.set(tr_value_ptrtoint, "ptrtoint", 2, 2, BlockInst);
    t.set(tr_value_inttoptr, "inttoptr", 2, 2, BlockInst);
    setBinaryOp<LLVMBuildAdd>("add");
    setBinaryOp<LLVMBuildNSWAdd>("add-nsw");
    setBinaryOp<LLVMBuildNUWAdd>("add-nuw");
    setBinaryOp<LLVMBuildFAdd>("fadd");
    setBinaryOp<LLVMBuildSub>("sub");
    setBinaryOp<LLVMBuildNSWSub>("sub-nsw");
    setBinaryOp<LLVMBuildNUWSub>("sub-nuw");
    setBinaryOp<LLVMBuildFSub>("fsub");
    setBinaryOp<LLVMBuildMul>("mul");
    setBinaryOp<LLVMBuildNSWMul>("mul-nsw");
    setBinaryOp<LLVMBuildNUWMul>("mul-nuw");
    setBinaryOp<LLVMBuildFMul>("fmul");
    setBinaryOp<LLVMBuildUDiv>("udiv");
    setBinaryOp<LLVMBuildSDiv>("sdiv");
    setBinaryOp<LLVMBuildExactSDiv>("exact-sdiv");
    setBinaryOp<LLVMBuildURem>("urem");
    setBinaryOp<LLVMBuildSRem>("srem");
    setBinaryOp<LLVMBuildFRem>("frem");
    setBinaryOp<LLVMBuildShl>("shl");
    setBinaryOp<LLVMBuildLShr>("lshr");
    setBinaryOp<LLVMBuildAShr>("ashr");
    setBinaryOp<LLVMBuildAnd>("and");
    setBinaryOp<LLVMBuildOr>("or");
    setBinaryOp<LLVMBuildXor>("xor");
    t.set(tr_value_icmp, "icmp", 3, 3, BlockInst);
    t.set(tr_value_fcmp, "fcmp", 3, 3, BlockInst);
    t.set(tr_value_getelementptr, "getelementptr", 1, -1);
    t.set(tr_value_extractelement, "extractelement", 2, 2, BlockInst);
    t.set(tr_value_extractvalue, "extractvalue", 2, 2, BlockInst);
    t.set(tr_value_align, "align", 2, 2);
    t.set(tr_value_load, "load", 1, 1, BlockInst);
    t.set(tr_value_store, "store", 2, 2, BlockInst);
    t.set(tr_value_alloca, "alloca", 1, 2, BlockInst);
    t.set(tr_value_defvalue, "defvalue", 2, 2);
    t.set(tr_value_deftype, "deftype", 2, 2);
    t.set(tr_value_struct, "struct", -1, -1);
    t.set(tr_value_block, "block", 1, 1);
    t.set(tr_value_set_block, "set-block", 1, 1);
    t.set(tr_value_null, "null", 1, 1);
    t.set(tr_value_splice, "splice", 0, -1);
    t.set(tr_value_define, "define", 4, -1);
    t.set(tr_value_phi, "phi", 1, -1, BlockInst);
    t.set(tr_value_incoming, "incoming", 1, -1, BlockInst);
    t.set(tr_value_br, "br", 1, 1, BlockInst);
    t.set(tr_value_cond_br, "cond-br", 3, 3, BlockInst);
    t.set(tr_value_ret, "ret", 0, 1, BlockInst);
    t.set(tr_value_select, "select", 3, 3, BlockInst);
    t.set(tr_value_declare, "declare", 2, 2);
    t.set(tr_value_call, "call", 1, -1, BlockInst);
    t.set(tr_value_invoke, "invoke", 3, 3, BlockInst);
    t.set(tr_value_landingpad, "landingpad", 2, -1, BlockInst);
    t.set(tr_value_resume, "resume", 1, 1, BlockInst);
    t.set(tr_value_unreachable, "unreachable", 0, 0, BlockInst);
    t.set(tr_value_include, "include", 1, 1);
    t.set(tr_value_nop, "nop", 0, 0);
    t.set(tr_value_execute, "execute", 1, 1);
    t.set(tr_value_module, "module", 1, -1);

}

static LLVMValueRef translateValueFromList (Environment *env, ValueRef expr) {
    assert(expr);
    Symbol *head = translateKind<Symbol>(env, expr);
    if (!head) return NULL;
    if (head->getValue() == "escape") {
        expr = at(next(expr));
        head = translateKind<Symbol>(env, expr);
        if (!head) return NULL;
    } else if (auto macro = env->resolveMacro(head->getValue())) {
        expr = macro(env, expr);
        if (env->hasErrors())
            return NULL;
        if (!expr) {
            translateError(env, "macro returned null");
            return NULL;
        }
        LLVMValueRef result = translateValue(env, expr);
        if (env->hasErrors()) {
            translateError(env, "while expanding macro");
            return NULL;
        }
        return result;
    }
    if (auto func = valueTranslators.match(env, expr)) {
        return func(env, expr);
    } else if (env->hasErrors()) {
        return NULL;
    } else {
        auto _ = env->with_expr(head);
        translateError(env, "unhandled special form or macro: %s. Did you forget a 'call'?", head->c_str());
        return NULL;
    }
}

static LLVMValueRef translateValue (Environment *env, ValueRef expr) {
    if (env->hasErrors()) return NULL;
    assert(expr);
    auto _ = env->with_expr(expr);

    if (!isAtom(expr)) {
        return translateValueFromList(env, at(expr));
    } else if (auto sym = llvm::dyn_cast<Symbol>(expr)) {
        LLVMValueRef result = env->resolveValue(sym->getValue());
        if (!result) {
            translateError(env, "no such value: %s", sym->c_str());
            return NULL;
        }

        return result;
    } else if (auto str = llvm::dyn_cast<String>(expr)) {
        return LLVMConstString(str->c_str(), str->size(), false);
    } else if (auto integer = llvm::dyn_cast<Integer>(expr)) {
        return LLVMConstInt(LLVMInt32Type(), integer->getValue(), 1);
    } else if (auto real = llvm::dyn_cast<Real>(expr)) {
        return LLVMConstReal(LLVMFloatType(), (float)real->getValue());
    } else {
        translateError(env, "expected value, not %s",
            valueKindName(kindOf(expr)));
        return NULL;
    }
}

//------------------------------------------------------------------------------
// TRANSLATE TYPES
//------------------------------------------------------------------------------

static TranslateTable<LLVMTypeRef> typeTranslators;

static LLVMTypeRef tr_type_function (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_rettype);

    LLVMTypeRef rettype = translateType(env, expr_rettype);
    if (!rettype) return NULL;

    bool vararg = false;
    expr = next(expr);
    int argcount = countOf(expr);
    LLVMTypeRef paramtypes[argcount];
    int i = 0;
    while (expr) {
        if (isSymbol(expr, "...")) {
            vararg = true;
            --argcount;
            if (i != argcount) {
                auto _ = env->with_expr(expr);
                translateError(env, "... must be last parameter.");
                return NULL;
            }
            break;
        }
        paramtypes[i] = translateType(env, expr);
        if (!paramtypes[i]) {
            return NULL;
        }
        expr = next(expr);
        ++i;
    }

    return LLVMFunctionType(rettype, paramtypes, argcount, vararg);
}

static LLVMTypeRef tr_type_dumptype (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_arg);

    LLVMTypeRef type = translateType(env, expr_arg);
    if (type) {
        LLVMDumpType(type);
    }

    return type;
}

static LLVMTypeRef tr_type_pointer (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_arg);

    LLVMTypeRef type = translateType(env, expr_arg);
    if (!type) return NULL;

    return LLVMPointerType(type, 0);
}

static LLVMTypeRef tr_type_typeof (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_arg);

    LLVMValueRef value = translateValue(env, expr_arg);
    if (!value) return NULL;

    return LLVMTypeOf(value);
}

static LLVMTypeRef tr_type_elementtype (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_arg);

    LLVMTypeRef type = translateType(env, expr_arg);
    if (!type) return NULL;

    LLVMTypeKind kind = LLVMGetTypeKind(type);
    switch(kind) {
        case LLVMPointerTypeKind:
        case LLVMVectorTypeKind:
        case LLVMArrayTypeKind: {
        } break;
        default: {
            auto _ = env->with_expr(expr_arg);
            translateError(env,
                "Pointer, vector or array type expected.");
            return NULL;
        } break;
    }

    return LLVMGetElementType(type);
}

static LLVMTypeRef tr_type_array (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);
    UNPACK_ARG(expr, expr_count);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    int64_t count;
    if (!translateInt64(env, expr_count, count)) return NULL;

    return LLVMArrayType(type, count);
}

static LLVMTypeRef tr_type_vector (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_type);
    UNPACK_ARG(expr, expr_count);

    LLVMTypeRef type = translateType(env, expr_type);
    if (!type) return NULL;

    int64_t count;
    if (!translateInt64(env, expr_count, count)) return NULL;

    return LLVMVectorType(type, count);
}

static LLVMTypeRef tr_type_struct (Environment *env, ValueRef expr) {
    UNPACK_ARG(expr, expr_name);

    const char *name = translateString(env, expr_name);
    if (!name) return NULL;

    LLVMTypeRef result = LLVMStructCreateNamed(LLVMGetGlobalContext(), name);
    if (isKindOf<Symbol>(expr_name))
        env->types[name] = result;

    expr = next(expr);

    bool packed = false;
    if (isSymbol(expr, "packed")) {
        packed = true;
        expr = next(expr);
    }

    int elemcount = countOf(expr);
    LLVMTypeRef elemtypes[elemcount];
    int i = 0;
    while (expr) {
        elemtypes[i] = translateType(env, expr);
        if (!elemtypes[i]) return NULL;
        expr = next(expr);
        ++i;
    }

    if (elemcount)
        LLVMStructSetBody(result, elemtypes, elemcount, packed);

    return result;
}

static void registerTypeTranslators() {
    auto &t = typeTranslators;
    t.set(tr_type_function, "function", 1, -1);
    t.set(tr_type_dumptype, "dumptype", 1, 1);
    t.set(tr_type_pointer, "&", 1, 1);
    t.set(tr_type_typeof, "typeof", 1, 1);
    t.set(tr_type_elementtype, "@", 1, 1);
    t.set(tr_type_array, "array", 2, 2);
    t.set(tr_type_vector, "vector", 2, 2);
    t.set(tr_type_struct, "struct", 1, -1);

}

static LLVMTypeRef translateTypeFromList (Environment *env, ValueRef expr) {
    assert(expr);
    Symbol *head = translateKind<Symbol>(env, expr);
    if (!head) return NULL;
    if (auto func = typeTranslators.match(env, expr)) {
        return func(env, expr);
    } else if (env->hasErrors()) {
        return NULL;
    } else {
        auto _ = env->with_expr(head);
        translateError(env, "unhandled special form: %s", head->c_str());
        return NULL;
    }
}

static LLVMTypeRef translateType (Environment *env, ValueRef expr) {
    if (env->hasErrors()) return NULL;
    assert(expr);
    auto _ = env->with_expr(expr);

    if (!isAtom(expr)) {
        return translateTypeFromList(env, at(expr));
    } else if (auto sym = llvm::dyn_cast<Symbol>(expr)) {
        LLVMTypeRef result = env->resolveType(sym->getValue());
        if (!result) {
            translateError(env, "no such type: %s", sym->c_str());
            return NULL;
        }

        return result;
    } else {
        translateError(env, "expected type, not %s",
            valueKindName(kindOf(expr)));
        return NULL;
    }
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

static LLVMTypeRef _opaque = NULL;

static void init() {
    registerValueTranslators();
    registerTypeTranslators();

    if (!gc_root)
        gc_root = new Pointer();

    if (!_opaque)
        _opaque = LLVMStructCreateNamed(LLVMGetGlobalContext(), "opaque");

    LLVMEnablePrettyStackTrace();
    LLVMLinkInMCJIT();
    //LLVMLinkInInterpreter();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeDisassembler();

}

//------------------------------------------------------------------------------

static void setupRootEnvironment (Environment *env, const char *modulename) {
    LLVMModuleRef module = LLVMModuleCreateWithName(modulename);

    LLVMBuilderRef builder = LLVMCreateBuilder();

    env->globals->module = module;
    env->globals->builder = builder;

    env->types["void"] = LLVMVoidType();
    env->types["half"] = LLVMHalfType();
    env->types["float"] = LLVMFloatType();
    env->types["double"] = LLVMDoubleType();
    env->types["i1"] = LLVMInt1Type();
    env->types["i8"] = LLVMInt8Type();
    env->types["i16"] = LLVMInt16Type();
    env->types["i32"] = LLVMInt32Type();
    env->types["i64"] = LLVMInt64Type();

    env->types["rawstring"] = LLVMPointerType(LLVMInt8Type(), 0);
    env->types["opaque"] = _opaque;

    env->values["true"] = LLVMConstInt(LLVMInt1Type(), 1, 1);
    env->values["false"] = LLVMConstInt(LLVMInt1Type(), 0, 1);
}

static void teardownRootEnvironment (Environment *env) {
    LLVMDisposeBuilder(env->getBuilder());
}

static bool translateRootValueList (Environment *env, ValueRef expr) {
    while (expr) {
        translateValue(env, expr);
        if (env->hasErrors())
            return false;
        expr = next(expr);
    }
    return true;
}

static bool compileModule (Environment *env, ValueRef expr) {
    assert(expr);
    assert(env->getBuilder());
    assert(env->getModule());

    auto _ = env->with_expr(expr);

    std::string lastlang = "";
    while (true) {
        Symbol *head = translateKind<Symbol>(env, expr);
        if (!head) return false;
        if (head->getValue() == "IR")
            break;
        auto preprocessor = preprocessors[head->getValue()];
        if (!preprocessor) {
            translateError(env, "unrecognized header: '%s'; try 'IR' instead.",
                head->getValue().c_str());
            return false;
        }
        if (lastlang == head->getValue()) {
            translateError(env,
                "header has not changed after preprocessing; is still '%s'.",
                head->getValue().c_str());
        }
        lastlang = head->getValue();
        expr = preprocessor(env, new Pointer(expr));
        if (env->hasErrors())
            return false;
        if (!expr) {
            translateError(env,
                "preprocessor returned null.",
                head->getValue().c_str());
            return false;
        }
        expr = at(expr);
    }

    expr = next(expr);

    return translateRootValueList (env, expr);
}

static bool compileMain (ValueRef expr) {
    TranslationGlobals globals;

    setupRootEnvironment(&globals.rootenv, "main");

    bool result = compileModule(&globals.rootenv, at(expr));

    teardownRootEnvironment(&globals.rootenv);

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
            if (bang_executable_path) {
                if (!bangra::compileStartupScript()) {
                    return 1;
                }
            }

            if (argv[1]) {
                bangra::Parser parser;
                expr = parser.parseFile(argv[1]);
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

ValueRef bangra_dump_value(ValueRef expr) {
    bangra::printValue(expr);
    return expr;
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

ValueRef bangra_next(ValueRef expr) {
    return next(expr);
}

ValueRef bangra_set_next(ValueRef lhs, ValueRef rhs) {
    if (lhs) {
        return cons(lhs, rhs);
    }
    return NULL;
}

ValueRef bangra_set_at_mutable(ValueRef lhs, ValueRef rhs) {
    if (lhs) {
        if (auto ptr = llvm::dyn_cast<bangra::Pointer>(lhs)) {
            ptr->setAt(rhs);
        }
    }
    return NULL;
}

ValueRef bangra_set_next_mutable(ValueRef lhs, ValueRef rhs) {
    if (lhs) {
        if (auto ptr = llvm::dyn_cast<bangra::Pointer>(lhs)) {
            ptr->setNext(rhs);
        }
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

void bangra_error_message(Environment *env, ValueRef context, const char *format, ...) {
    auto _ = env->with_expr(context);
    va_list args;
    va_start (args, format);
    bangra::translateErrorV(env, format, args);
    va_end (args);
}

int bangra_eq(Value *a, Value *b) {
    if (a == b) return true;
    if (a && b) {
        auto kind = a->getKind();
        if (kind != b->getKind())
            return false;
        switch (kind) {
            case bangra::V_Symbol: {
                bangra::Symbol *sa = llvm::cast<bangra::Symbol>(a);
                bangra::Symbol *sb = llvm::cast<bangra::Symbol>(b);
                if (sa->size() != sb->size()) return false;
                if (!memcmp(sa->c_str(), sb->c_str(), sa->size())) return true;
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
                Value *na = sa->getAt();
                Value *nb = sb->getAt();
                while (na && nb) {
                    if (!bangra_eq(na, nb))
                        return false;
                    na = na->getNext();
                    nb = nb->getNext();
                }
                return na == nb;
            } break;
            case bangra::V_Handle: {
                bangra::Handle *sa = llvm::cast<bangra::Handle>(a);
                bangra::Handle *sb = llvm::cast<bangra::Handle>(b);
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

ValueRef bangra_string(const char *value) {
    return new bangra::String(value);
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
    return env->parent;
}

Environment *bangra_meta_env(Environment *env) {
    return env->getMeta();
}

void bangra_set_macro(Environment *env, const char *name, bangra_preprocessor f) {
    env->macros[name] = f;
}

bangra_preprocessor bangra_get_macro(Environment *env, const char *name) {
    return env->macros[name];
}

void *bangra_llvm_module(Environment *env) {
    return env->getModule();
}

void *bangra_llvm_engine(Environment *env) {
    return env->getEngine();
}

void *bangra_llvm_value(Environment *env, const char *name) {
    return env->resolveValue(name);
}

void *bangra_llvm_type(Environment *env, const char *name) {
    return env->resolveType(name);
}

static int unique_symbol_counter = 0;
ValueRef bangra_unique_symbol(const char *name) {
    if (!name)
        name = "";
    auto symname = bangra::format("#%s%i", name, unique_symbol_counter++);
    return new bangra::Symbol(symname.c_str());
}

void *bangra_import_c_module(ValueRef dest,
    const char *modulename, const char *path, const char **args, int argcount) {
    return bangra::importCModule(dest, modulename, path, args, argcount);
}

void *bangra_import_c_string(ValueRef dest,
    const char *modulename, const char *str, const char *path,
    const char **args, int argcount) {
    return bangra::importCModule(dest, modulename, path, args, argcount, str);
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
