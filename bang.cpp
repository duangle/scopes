#ifndef BANG0_CPP
#define BANG0_CPP

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

type expressions:
(function return-type-expr ([param-type-expr [...]] [`...`])
(* type-expr)
(dump type-expr)
(struct name-expr [`packed`] [type-expr [...]])

value expressions:
(int type-expr number)
(real type-expr number)
(dump-module)
(dump value-expr)
(gep value-expr index-expr [index-expr [...]])
(global value-expr)
(bitcast value-expr type-expr)
(inttoptr value-expr type-expr)
(ptrtoint value-expr type-expr)
(defvalue symbol-name value-expr)
(deftype symbol-name type-expr)
(define external-name ([param-name [...]]) type-expr body-expr [...])
(label label-name body-expr [...])
(br label-expr)
(cond-br value-expr then-label-expr else-label-expr)
(ret [value-expr])
(declare external-name type-expr)
(call value-expr [param-expr [...]])
(phi type-expr [(value-expr block-expr) [...]])
(run function-expr)
(nop)
*/

//------------------------------------------------------------------------------
// C HEADER
//------------------------------------------------------------------------------

#if defined __cplusplus
extern "C" {
#endif

#ifdef BANG_CPP_IMPL
namespace bang {
struct Value;
struct Environment;
} // namespace bang
typedef bang::Value Value;
typedef bang::Environment Environment;
#else
typedef struct _Environment Environment;
typedef struct _Value Value;
#endif

typedef Value *ValueRef;

void bang_dump_value(ValueRef expr);
int bang_main(int argc, char ** argv);

typedef ValueRef (*bang_preprocessor)(Environment *, ValueRef );
void bang_set_preprocessor(bang_preprocessor f);
bang_preprocessor bang_get_preprocessor();

int bang_get_kind(ValueRef expr);
ValueRef bang_at(ValueRef expr);
ValueRef bang_next(ValueRef expr);
ValueRef bang_set_next(ValueRef lhs, ValueRef rhs);
ValueRef bang_ref(ValueRef lhs);

const char *bang_string_value(ValueRef expr);
void *bang_handle_value(ValueRef expr);
ValueRef bang_handle(void *ptr);
ValueRef bang_table();
void bang_set_key(ValueRef expr, ValueRef key, ValueRef value);
ValueRef bang_get_key(ValueRef expr, ValueRef key);
typedef ValueRef (*bang_mapper)(ValueRef, int, void *);
ValueRef bang_map(ValueRef expr, bang_mapper map, void *ctx);
ValueRef bang_set_anchor(ValueRef toexpr, ValueRef anchorexpr);

void bang_error_message(ValueRef context, const char *format, ...);

int bang_eq(Value *a, Value *b);

#if defined __cplusplus
}
#endif

#endif // BANG0_CPP
#ifdef BANG_CPP_IMPL

//------------------------------------------------------------------------------
// SHARED LIBRARY IMPLEMENTATION
//------------------------------------------------------------------------------

#undef NDEBUG
#include <sys/types.h>
#ifdef _WIN32
#include "mman.h"
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
#include "llvm/Support/Casting.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/MultiplexConsumer.h"

namespace bang {

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
    case V_Pointer: return "pointer";
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
        if (next && next->anchor.isValid()) {
            anchor = next->anchor;
        }
        collection.push_back(this);
    }

public:
    typedef std::list<ValueRef> CollectionType;

    static CollectionType collection;

    Anchor anchor;

    virtual ~Value() {}

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
            self = bang::next(self);
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
        if (at && at->anchor.isValid()) {
            anchor = at->anchor;
        }
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

const char symbol_terminators[]  = "()[]{}\"';#:,.";
const char integer_terminators[] = "()[]{}\"';#:,";
const char real_terminators[]    = "()[]{}\"';#:,.";

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

    int token;
    const char *string;
    int string_len;
    int64_t integer;
    bool is_unsigned;
    double real;

    std::string error_string;

    Lexer() {}

    void init (const char *input_stream, const char *eof, const char *path) {
        if (eof == NULL) {
            eof = input_stream + strlen(input_stream);
        }

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
        return cursor - input_stream;
    }

    int column () {
        return cursor - line + 1;
    }

    void initAnchor(Anchor &anchor) {
        anchor.path = path;
        anchor.lineno = lineno;
        anchor.column = column();
        anchor.offset = cursor - input_stream;
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

    void readDotSequence () {
        while (true) {
            if (next_cursor == eof) {
                break;
            }
            char c = *next_cursor++;
            if (strchr(".:", c)) {
                // consume
            } else {
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
            } else if (c == ',') {
                token = token_symbol;
                readSingleSymbol();
                break;
            } else if (c == ':') {
                token = token_symbol;
                readDotSequence();
                break;
            } else if (readInteger() || readUInteger()) {
                token = token_integer;
                break;
            } else if (readReal()) {
                token = token_real;
                break;
            } else if (c == '.') {
                token = token_symbol;
                readDotSequence();
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
                result->anchor = anchor;
            }
            tail = newtail;
            if (!start)
                start = tail;
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
        return new Pointer(builder.result);
    }

    ValueRef parseAny () {
        assert(lexer.token != token_eof);
        if (lexer.token == token_open) {
            return parseList(token_close);
        } else if (lexer.token == token_square_open) {
            auto list = parseList(token_square_close);
            if (errors) return nullptr;
            return new Pointer(new Symbol("["), list);
        } else if (lexer.token == token_curly_open) {
            auto list = parseList(token_curly_close);
            if (errors) return nullptr;
            return new Pointer(new Symbol("{"), list);
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
            return new Pointer(nullptr);
        } else if (!builder.result->getNext()) {
            return builder.result;
        } else {
            return new Pointer(builder.result);
        }
    }

    ValueRef parseRoot (
        const char *input_stream, const char *eof, const char *path) {
        lexer.init(input_stream, eof, path);

        lexer.readToken();

        auto result = parseNaked(lexer.column());

        if (error_string.empty() && !lexer.error_string.empty()) {
            error_string = lexer.error_string;
            return nullptr;
        }

        return result;
    }

    ValueRef parseFile (const char *path) {
        auto file = MappedFile::open(path);
        if (file) {
            init();
            auto expr = parseRoot(
                file->strptr(), file->strptr() + file->size(),
                path);
            if (!error_string.empty()) {
                printf("%s:%i:%i: error: %s",
                    error_origin.path,
                    error_origin.lineno,
                    error_origin.column,
                    error_string.c_str());
                dumpFileLine(path, error_origin.offset);
                assert(expr == NULL);
                if (!(parse_origin == error_origin)) {
                    printf("%s:%i:%i: while parsing expression",
                        parse_origin.path,
                        parse_origin.lineno,
                        parse_origin.column);
                    dumpFileLine(path, parse_origin.offset);
                }
            }
            if (expr)
                expr = strip(expr);
            return expr;
        } else {
            fprintf(stderr, "unable to open file: %s", path);
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
        printf("%s:%i:%i: ",
            e->anchor.path,
            e->anchor.lineno,
            e->anchor.column);
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
			switch((*a)[i]) {
			case '"': case '\\':
				putchar('\\');
                putchar((*a)[i]);
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
            case 0:
                printf("\\0");
                break;
            case '[': case ']': case '{': case '}': case '(': case ')':
				if (a->getKind() == V_Symbol)
					putchar('\\');
                putchar((*a)[i]);
				break;
            default:
                putchar((*a)[i]);
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

//------------------------------------------------------------------------------

struct Environment;

struct TranslationGlobals {
    int compile_errors;
    // module for this translation
    LLVMModuleRef module;
    // builder for this translation
    LLVMBuilderRef builder;
    // meta env; only valid for proto environments
    Environment *meta;
    // global pointers
    GlobalPtrList globalptrs;

    TranslationGlobals() :
        compile_errors(0),
        module(NULL),
        builder(NULL),
        meta(NULL)
        {}

    TranslationGlobals(Environment *env) :
        compile_errors(0),
        module(NULL),
        builder(NULL),
        meta(env)
        {}

};

//------------------------------------------------------------------------------

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

    // parent env
    Environment *parent;

    static bang_preprocessor preprocessor;

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
        parent(NULL)
        {}

    Environment(Environment *parent_) :
        globals(parent_->globals),
        function(parent_->function),
        block(parent_->block),
        expr(parent_->expr),
        parent(parent_)
        {}

    WithValue with_expr(ValueRef expr) {
        return WithValue(this, expr);
    }

    Environment *getMeta() const {
        return globals->meta;
    }

    bool hasErrors() const {
        return globals->compile_errors != 0;
    };

    LLVMBuilderRef getBuilder() const {
        return globals->builder;
    }

    LLVMModuleRef getModule() const {
        return globals->module;
    }

    void addQuote(LLVMValueRef value, ValueRef expr) {
        globals->globalptrs.push_back(std::make_tuple(value, (void *)expr));
        gc_root = cons(expr, gc_root);
    }

};

bang_preprocessor Environment::preprocessor = NULL;

//------------------------------------------------------------------------------
// CLANG SERVICES
//------------------------------------------------------------------------------

/*
static void translateError (Environment *env, const char *format, ...);

class CVisitor : public clang::RecursiveASTVisitor<CVisitor> {
public:
    Environment *env;
    clang::ASTContext *Context;

    NameTypeMap taggedStructs;
    NameTypeMap namedStructs;

    CVisitor() : Context(NULL) {
    }

    void SetContext(clang::ASTContext * ctx, Environment *env_) {
        Context = ctx;
        env = env_;
    }

    bool GetFields(clang::RecordDecl * rd) {
        // ASTContext::getASTRecordLayout(const RecordDecl *D)

        //check the fields of this struct, if any one of them is not understandable, then this struct becomes 'opaque'
        //that is, we insert the type, and link it to its llvm type, so it can be used in terra code
        //but none of its fields are exposed (since we don't understand the layout)
        bool opaque = false;
        int anonname = 0;
        for(clang::RecordDecl::field_iterator it = rd->field_begin(), end = rd->field_end(); it != end; ++it) {
            clang::DeclarationName declname = it->getDeclName();

            if(it->isBitField() || (!it->isAnonymousStructOrUnion() && !declname)) {
                opaque = true;
                continue;
            }
            std::string declstr;
            if(it->isAnonymousStructOrUnion()) {
                char buf[32];
                sprintf(buf,"_%d",anonname++);
                declstr = buf;
            } else {
                declstr = declname.getAsString();
            }
            clang::QualType FT = it->getType();
            Type fieldtype = TranslateType(FT);
            if(fieldtype == Type::Undefined) {
                opaque = true;
                continue;
            }
            //printf("%s\n", declstr.c_str());
        }
        return !opaque;

    }

    Type TranslateRecord(clang::RecordDecl * rd) {
        if(rd->isStruct() || rd->isUnion()) {
            std::string name = rd->getName();

            bool tagged = true;

            if(name == "") {
                clang::TypedefNameDecl * decl = rd->getTypedefNameForAnonDecl();
                if(decl) {
                    tagged = false;
                    name = decl->getName();
                }
            }

            Type structtype = (tagged)?taggedStructs[name]:namedStructs[name];

            if (structtype == Type::Undefined) {
                structtype = Type::createStruct(name);

                if (tagged)
                    taggedStructs[name] = structtype;
                else
                    namedStructs[name] = structtype;
            }

            // size_t argpos = RegisterRecordType(Context->getRecordType(rd));
            // thenamespace->setfield(name.c_str()); //register the type

            clang::RecordDecl * defn = rd->getDefinition();
            if (defn != NULL) {
                if (GetFields(defn)) {
                    if(!defn->isUnion()) {
                        //structtype.entries = {entry1, entry2, ... }
                    } else {
                        //add as a union:
                        //structtype.entries = { {entry1,entry2,...} }
                    }
                }
            }

            return structtype;
        } else {
            //return ImportError("non-struct record types are not supported");
            return Type::Undefined;
        }
    }

    Type TranslateType(clang::QualType T) {
        using namespace clang;

        T = Context->getCanonicalType(T);
        const clang::Type *Ty = T.getTypePtr();

        switch (Ty->getTypeClass()) {
        case clang::Type::Record: {
            const RecordType *RT = dyn_cast<RecordType>(Ty);
            RecordDecl * rd = RT->getDecl();
            //return GetRecordTypeFromDecl(rd, tt);
            return TranslateRecord(rd);
        }  break; //TODO
        case clang::Type::Builtin:
            switch (cast<BuiltinType>(Ty)->getKind()) {
            case clang::BuiltinType::Void: {
                return Type::Void;
            } break;
            case clang::BuiltinType::Bool: {
                return Type::Bool;
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
                if (Ty->isUnsignedIntegerType()) {
                    if (sz == 8)
                        return Type::UInt8;
                    else if (sz == 16)
                        return Type::UInt16;
                    else if (sz == 32)
                        return Type::UInt32;
                    else if (sz == 64)
                        return Type::UInt64;
                } else {
                    if (sz == 8)
                        return Type::Int8;
                    else if (sz == 16)
                        return Type::Int16;
                    else if (sz == 32)
                        return Type::Int32;
                    else if (sz == 64)
                        return Type::Int64;
                }
            } break;
            case clang::BuiltinType::Half: {
                return Type::Half;
            } break;
            case clang::BuiltinType::Float: {
                return Type::Float;
            } break;
            case clang::BuiltinType::Double: {
                return Type::Double;
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
            Type pointee = TranslateType(ETy);
            if (pointee != Type::Undefined) {
                if (pointee == Type::Void)
                    pointee = Type::Opaque;
                return Type::pointer(pointee);
            }
        } break;
        case clang::Type::VariableArray:
        case clang::Type::IncompleteArray:
            break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            Type at = TranslateType(ATy->getElementType());
            if(at != Type::Undefined) {
                int sz = ATy->getSize().getZExtValue();
                return Type::array(at, sz);
            }
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
                const VectorType *VT = cast<VectorType>(T);
                Type at = TranslateType(VT->getElementType());
                if(at != Type::Undefined) {
                    int n = VT->getNumElements();
                    return Type::vector(at, n);
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
        case clang::Type::Enum: {
            return Type::Int32;
        } break;
        case clang::Type::BlockPointer:
        case clang::Type::MemberPointer:
        case clang::Type::Atomic:
        default:
            break;
        }
        fprintf(stderr, "type not understood: %s (%i)\n", T.getAsString().c_str(), Ty->getTypeClass());

        //~ std::stringstream ss;
        //~ ss << "type not understood: " << T.getAsString().c_str() << " " << Ty->getTypeClass();
        //~ return ImportError(ss.str().c_str());
        // TODO: print error
        return Type::Undefined;
    }

    Type TranslateFuncType(const clang::FunctionType * f) {

        bool valid = true; // decisions about whether this function can be exported or not are delayed until we have seen all the potential problems
        clang::QualType RT = f->getReturnType();

        Type returntype = TranslateType(RT);

        if (returntype == Type::Undefined)
            valid = false;

        const clang::FunctionProtoType * proto = f->getAs<clang::FunctionProtoType>();
        std::vector<Type> argtypes;
        //proto is null if the function was declared without an argument list (e.g. void foo() and not void foo(void))
        //we don't support old-style C parameter lists, we just treat them as empty
        if(proto) {
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                clang::QualType PT = proto->getParamType(i);
                Type paramtype = TranslateType(PT);
                if(paramtype == Type::Undefined) {
                    valid = false; //keep going with attempting to parse type to make sure we see all the reasons why we cannot support this function
                } else if(valid) {
                    argtypes.push_back(paramtype);
                }
            }
        }

        if(valid) {
            return Type::function(returntype, argtypes, proto ? proto->isVariadic() : false);
        }

        return Type::Undefined;
    }

    bool TraverseFunctionDecl(clang::FunctionDecl *f) {
         // Function name
        clang::DeclarationName DeclName = f->getNameInfo().getName();
        std::string FuncName = DeclName.getAsString();
        const clang::FunctionType * fntyp = f->getType()->getAs<clang::FunctionType>();

        if(!fntyp)
            return true;

        if(f->getStorageClass() == clang::SC_Static) {
            //ImportError("cannot import static functions.");
            //SetErrorReport(FuncName.c_str());
            return true;
        }

        //~ //Obj typ;
        //~ if(!GetFuncType(fntyp,&typ)) {
            //~ SetErrorReport(FuncName.c_str());
            //~ return true;
        //~ }
        Type functype = TranslateFuncType(fntyp);
        if (functype == Type::Undefined)
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

        //printf("%s -> %s\n", FuncName.c_str(), InternalName.c_str());
        //CreateFunction(FuncName,InternalName,&typ);

        //LLVMDumpType(functype);

        ;

        // TODO
        //~ env->names[FuncName] = TypedValue(Type::pointer(functype),
            //~ LLVMAddFunction(env->globals->module, InternalName.c_str(), functype.getLLVMType()));

        //KeepLive(f);//make sure this function is live in codegen by creating a dummy reference to it (void) is to suppress unused warnings

        return true;
    }
};

class CodeGenProxy : public clang::ASTConsumer {
public:
    Environment *env;
    CVisitor visitor;

    CodeGenProxy(Environment *env_) : env(env_) {}
    virtual ~CodeGenProxy() {}

    virtual void Initialize(clang::ASTContext &Context) {
        visitor.SetContext(&Context, env);
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
    Environment *env;

    BangEmitLLVMOnlyAction(Environment *env_) :
        EmitLLVMOnlyAction((llvm::LLVMContext *)LLVMGetGlobalContext()),
        env(env_)
    {
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                 clang::StringRef InFile) override {

        std::vector< std::unique_ptr<clang::ASTConsumer> > consumers;
        consumers.push_back(clang::EmitLLVMOnlyAction::CreateASTConsumer(CI, InFile));
        consumers.push_back(llvm::make_unique<CodeGenProxy>(env));
        return llvm::make_unique<clang::MultiplexConsumer>(std::move(consumers));
    }
};

static LLVMModuleRef importCModule (Environment *env,
    const char *modulename, const char *path, const char **args, int argcount) {
    using namespace clang;

    //void *MainAddr = (void*) (intptr_t) GetExecutablePath;

    std::vector<const char *> aargs;
    aargs.push_back("clang");
    aargs.push_back(path);
    for (int i = 0; i < argcount; ++i) {
        aargs.push_back(args[i]);
    }

    // TODO: to input string instead of file remap filename using
    // addRemappedFile(llvm::StringRef From, const llvm::MemoryBuffer * To)

    CompilerInstance compiler;
    compiler.setInvocation(createInvocationFromCommandLine(aargs));

    // Create the compilers actual diagnostics engine.
    compiler.createDiagnostics();

    // Infer the builtin include path if unspecified.
    //~ if (compiler.getHeaderSearchOpts().UseBuiltinIncludes &&
        //~ compiler.getHeaderSearchOpts().ResourceDir.empty())
        //~ compiler.getHeaderSearchOpts().ResourceDir =
            //~ CompilerInvocation::GetResourcesPath(bang_argv[0], MainAddr);

    LLVMModuleRef M = NULL;

    // Create and execute the frontend to generate an LLVM bitcode module.
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction(env));
    if (compiler.ExecuteAction(*Act)) {
        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        //LLVMDumpModule(M);
        //LLVMAddModule(env->globals->engine, M);
    } else {
        translateError(env, "compiler failed");
    }

    return M;
}
*/

//------------------------------------------------------------------------------
// TRANSLATION
//------------------------------------------------------------------------------

//static void setupRootEnvironment (Environment *env, const char *modulename);
//static void teardownRootEnvironment (Environment *env);
//static void compileModule (Environment *env, ValueRef expr, int offset);

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
    if (env->expr) {
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

    int argcount = 0;
    while (expr) {
        ++ argcount;
        if ((maxcount >= 0) && (argcount > maxcount)) {
            auto _ = env->with_expr(expr);
            translateError(env, "excess argument. At most %i arguments expected.", maxcount);
            return false;
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

static bool translateValueList (Environment *env, ValueRef expr) {
    while (expr) {
        translateValue(env, expr);
        if (env->hasErrors())
            return false;
        expr = next(expr);
    }
    return true;
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
        translateError(env, "labeled block is already terminated.");
        return false;
    }
    return true;
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

static LLVMTypeRef resolveType(Environment *env, const std::string &name) {
    Environment *penv = (Environment *)env;
    while (penv) {
        LLVMTypeRef result = (*penv).types[name];
        if (result) {
            return result;
        }
        penv = (penv->parent)?penv->parent:penv->getMeta();
    }
    return NULL;
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
        translateError(env, "value is not a function.");
        return NULL;
    }
    return functype;
}

#define UNPACK_ARG(expr, name) \
    expr = next(expr); ValueRef name = expr

static LLVMValueRef translateValueFromList (Environment *env, ValueRef expr) {
    assert(expr);
    Symbol *head = translateKind<Symbol>(env, expr);
    if (!head) return NULL;
    if (matchSpecialForm(env, expr, "int", 2, 2)) {

        UNPACK_ARG(expr, expr_type);
        UNPACK_ARG(expr, expr_value);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        int64_t value;
        if (!translateInt64(env, expr_value, value)) return NULL;

        return LLVMConstInt(type, value, 1);

    } else if (matchSpecialForm(env, expr, "real", 2, 2)) {

        UNPACK_ARG(expr, expr_type);
        UNPACK_ARG(expr, expr_value);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        double value;
        if (!translateDouble(env, expr_value, value)) return NULL;

        return LLVMConstReal(type, value);

    } else if (matchSpecialForm(env, expr, "dump-module", 0, 0)) {

        LLVMDumpModule(env->getModule());

        return NULL;

    } else if (matchSpecialForm(env, expr, "dump", 1, 1)) {

        UNPACK_ARG(expr, expr_arg);

        LLVMValueRef value = translateValue(env, expr_arg);
        if (value) {
            LLVMDumpValue(value);
        }

        return value;
    } else if (matchSpecialForm(env, expr, "dumptype", 1, 1)) {

        UNPACK_ARG(expr, expr_arg);

        LLVMTypeRef type = translateType(env, expr_arg);
        if (type) {
            LLVMDumpType(type);
        }

        return NULL;

    } else if (matchSpecialForm(env, expr, "constant", 1, 1)) {

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

    } else if (matchSpecialForm(env, expr, "global", 2, 2)) {

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

    } else if (matchSpecialForm(env, expr, "quote", 2, 2)) {

        UNPACK_ARG(expr, expr_type);
        UNPACK_ARG(expr, expr_value);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        LLVMValueRef result = LLVMAddGlobal(env->getModule(), type, "quote");
        env->addQuote(result, expr_value);
        LLVMSetGlobalConstant(result, true);

        return result;

    } else if (matchSpecialForm(env, expr, "bitcast", 2, 2)) {

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

    } else if (matchSpecialForm(env, expr, "ptrtoint", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_value);
        UNPACK_ARG(expr, expr_type);

        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return NULL;

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        return LLVMBuildPtrToInt(env->getBuilder(), value, type, "");

    } else if (matchSpecialForm(env, expr, "inttoptr", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_value);
        UNPACK_ARG(expr, expr_type);

        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return NULL;

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        return LLVMBuildIntToPtr(env->getBuilder(), value, type, "");

    } else if (matchSpecialForm(env, expr, "and", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_lhs);
        UNPACK_ARG(expr, expr_rhs);
        LLVMValueRef lhs = translateValue(env, expr_lhs);
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr_rhs);
        if (!rhs) return NULL;

        return LLVMBuildAnd(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "or", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_lhs);
        UNPACK_ARG(expr, expr_rhs);
        LLVMValueRef lhs = translateValue(env, expr_lhs);
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr_rhs);
        if (!rhs) return NULL;

        return LLVMBuildOr(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "add", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_lhs);
        UNPACK_ARG(expr, expr_rhs);
        LLVMValueRef lhs = translateValue(env, expr_lhs);
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr_rhs);
        if (!rhs) return NULL;

        return LLVMBuildAdd(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "sub", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_lhs);
        UNPACK_ARG(expr, expr_rhs);
        LLVMValueRef lhs = translateValue(env, expr_lhs);
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr_rhs);
        if (!rhs) return NULL;

        return LLVMBuildSub(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "icmp", 3, 3)) {
        if (!verifyInBlock(env)) return NULL;

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

    } else if (matchSpecialForm(env, expr, "fcmp", 3, 3)) {
        if (!verifyInBlock(env)) return NULL;

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

    } else if (matchSpecialForm(env, expr, "getelementptr", 1, -1)) {

        UNPACK_ARG(expr, expr_array);

        LLVMValueRef ptr = translateValue(env, expr_array);
        if (!ptr) return NULL;

        auto _ = env->with_expr(expr_array);

        bool all_const = LLVMIsConstant(ptr);

        expr = next(expr);

        int valuecount = countOf(expr);
        LLVMValueRef indices[valuecount];
        int i = 0;
        while (expr) {
            indices[i] = translateValue(env, expr);
            if (indices[i] == NULL) {
                return NULL;
            }
            all_const = all_const && LLVMIsConstant(indices[i]);
            expr = next(expr);
            ++i;
        }

        if (all_const) {
            return LLVMConstInBoundsGEP(ptr, indices, valuecount);
        } else {
            if (!verifyInBlock(env)) return NULL;
            return LLVMBuildGEP(env->getBuilder(), ptr, indices, valuecount, "");
        }

    } else if (matchSpecialForm(env, expr, "extractelement", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

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

    } else if (matchSpecialForm(env, expr, "extractvalue", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

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

    } else if (matchSpecialForm(env, expr, "align", 2, 2)) {

        UNPACK_ARG(expr, expr_value);
        UNPACK_ARG(expr, expr_bytes);

        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return NULL;

        int64_t bytes;
        if (!translateInt64(env, expr_bytes, bytes)) return NULL;

        LLVMSetAlignment(value, bytes);
        return value;

    } else if (matchSpecialForm(env, expr, "load", 1, 1)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_value);

        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return NULL;

        return LLVMBuildLoad(env->getBuilder(), value, "");

    } else if (matchSpecialForm(env, expr, "store", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_value);
        UNPACK_ARG(expr, expr_ptr);

        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return NULL;
        LLVMValueRef ptr = translateValue(env, expr_ptr);
        if (!ptr) return NULL;

        return LLVMBuildStore(env->getBuilder(), value, ptr);

    } else if (matchSpecialForm(env, expr, "alloca", 1, 2)) {
        if (!verifyInBlock(env)) return NULL;

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

    } else if (matchSpecialForm(env, expr, "defvalue", 2, 2)) {

        UNPACK_ARG(expr, expr_name);
        UNPACK_ARG(expr, expr_value);

        const Symbol *sym_name = translateKind<Symbol>(env, expr_name);
        LLVMValueRef result = translateValue(env, expr_value);
        if (!result) return NULL;

        const char *name = sym_name->c_str();
        env->values[name] = result;

        return result;
    } else if (matchSpecialForm(env, expr, "deftype", 2, 2)) {

        UNPACK_ARG(expr, expr_name);
        UNPACK_ARG(expr, expr_value);

        const Symbol *sym_name = translateKind<Symbol>(env, expr_name);
        LLVMTypeRef result = translateType(env, expr_value);
        if (!result) return NULL;

        const char *name = sym_name->c_str();
        env->types[name] = result;

        return NULL;
    } else if (matchSpecialForm(env, expr, "struct", -1, -1)) {

        translateTypeFromList(env, expr);
        return NULL;

    } else if (matchSpecialForm(env, expr, "label", 1, -1)) {

        if (!verifyInFunction(env)) return NULL;

        UNPACK_ARG(expr, expr_name);

        const char *name = translateString(env, expr_name);
        if (!name) return NULL;

        LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(env->getBuilder());

        // continue existing label
        LLVMBasicBlockRef block = NULL;
        if (isKindOf<Symbol>(expr_name)) {
            LLVMValueRef maybe_block = env->values[name];
            if (maybe_block) {
                block = verifyBasicBlock(env, maybe_block);
                if (!block)
                    return NULL;
            }
        }

        if (!block) {
            block = LLVMAppendBasicBlock(env->function, name);
            if (isKindOf<Symbol>(expr_name))
                env->values[name] = LLVMBasicBlockAsValue(block);
        }

        LLVMValueRef blockvalue = LLVMBasicBlockAsValue(block);

        LLVMPositionBuilderAtEnd(env->getBuilder(), block);

        expr = next(expr);
        if (expr) {
            LLVMValueRef oldblockvalue = env->block;
            env->block = blockvalue;
            translateValueList(env, expr);
            if (env->hasErrors()) return NULL;
            env->block = oldblockvalue;
            LLVMValueRef t = LLVMGetBasicBlockTerminator(block);
            if (!t) {
                translateError(env, "labeled block must be terminated.");
                return NULL;
            }
        }

        if (env->hasErrors()) return NULL;

        if (oldblock)
            LLVMPositionBuilderAtEnd(env->getBuilder(), oldblock);

        return blockvalue;

    } else if (matchSpecialForm(env, expr, "null", 1, 1)) {

        UNPACK_ARG(expr, expr_type);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        return LLVMConstNull(type);

    } else if (matchSpecialForm(env, expr, "do", 0, -1)) {

        expr = next(expr);

        Environment subenv(env);
        translateValueList(&subenv, expr);

        return NULL;
    } else if (matchSpecialForm(env, expr, "do-splice", 0, -1)) {

        expr = next(expr);

        translateValueList(env, expr);

        return NULL;
    } else if (matchSpecialForm(env, expr, "define", 4, -1)) {

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

        {
            auto _ = env->with_expr(body_expr);

            translateValueList(&subenv, body_expr);
            if (env->hasErrors()) return NULL;
        }

        return func;

    } else if (matchSpecialForm(env, expr, "phi", 1, -1)) {

        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_type);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        expr = next(expr);
        int branchcount = countOf(expr);
        LLVMValueRef values[branchcount];
        LLVMBasicBlockRef blocks[branchcount];
        int i = 0;
        while (expr) {
            ValueRef expr_pair = expr;
            if (!verifyKind<Pointer>(env, expr_pair))
                return NULL;
            auto _ = env->with_expr(expr_pair);
            expr_pair = at(expr_pair);
            if (!verifyCount(env, expr_pair, 2, 2)) {
                translateError(env, "exactly 2 parameters expected");
                return NULL;
            }
            ValueRef expr_value = expr_pair;
            ValueRef expr_block = next(expr_pair);

            LLVMValueRef value = translateValue(env, expr_value);
            if (!value) return NULL;
            LLVMBasicBlockRef block = verifyBasicBlock(env, translateValue(env, expr_block));
            if (!block) return NULL;
            values[i] = value;
            blocks[i] = block;

            expr = next(expr);
            i++;
        }

        LLVMValueRef result =
            LLVMBuildPhi(env->getBuilder(), type, "");
        LLVMAddIncoming(result, values, blocks, branchcount);
        return result;

    } else if (matchSpecialForm(env, expr, "br", 1, 1)) {

        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_value);

        LLVMBasicBlockRef value = verifyBasicBlock(env, translateValue(env, expr_value));
        if (!value) return NULL;

        return LLVMBuildBr(env->getBuilder(), value);

    } else if (matchSpecialForm(env, expr, "cond-br", 3, 3)) {

        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_value);
        UNPACK_ARG(expr, expr_then);
        UNPACK_ARG(expr, expr_else);

        LLVMValueRef value = translateValue(env, expr_value);
        if (!value) return NULL;
        LLVMBasicBlockRef then_block = verifyBasicBlock(env, translateValue(env, expr_then));
        if (!then_block) return NULL;
        LLVMBasicBlockRef else_block = verifyBasicBlock(env, translateValue(env, expr_else));
        if (!else_block) return NULL;

        return LLVMBuildCondBr(env->getBuilder(), value, then_block, else_block);

    } else if (matchSpecialForm(env, expr, "ret", 0, 1)) {

        if (!verifyInBlock(env)) return NULL;

        auto _ = env->with_expr(expr);

        expr = next(expr);

        LLVMTypeRef functype = extractFunctionType(env, env->function);
        if (!functype) return NULL;

        LLVMTypeRef rettype = LLVMGetReturnType(functype);

        if (!expr) {
            if (rettype != LLVMVoidType()) {
                translateError(env, "return type does not match function signature.");
                return NULL;
            }

            LLVMBuildRetVoid(env->getBuilder());
        } else {
            ValueRef expr_value = expr;
            LLVMValueRef value = translateValue(env, expr_value);
            if (!value) return NULL;
            auto _ = env->with_expr(expr_value);
            if (rettype != LLVMTypeOf(value)) {
                translateError(env, "return type does not match function signature.");
                return NULL;
            }

            LLVMBuildRet(env->getBuilder(), value);
        }

        return NULL;
    } else if (matchSpecialForm(env, expr, "declare", 2, 2)) {

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

    } else if (matchSpecialForm(env, expr, "call", 1, -1)) {

        if (!verifyInBlock(env)) return NULL;

        UNPACK_ARG(expr, expr_func);

        LLVMValueRef callee = translateValue(env, expr_func);
        if (!callee) return NULL;

        auto _ = env->with_expr(expr_func);
        LLVMTypeRef functype = extractFunctionType(env, callee);
        if (!functype) return NULL;

        expr = next(expr);

        int count = (int)LLVMCountParamTypes(functype);
        bool vararg = LLVMIsFunctionVarArg(functype);
        int mincount = count;
        int maxcount = vararg?-1:count;
        if (!verifyCount(env, expr, mincount, maxcount))
            return NULL;

        int argcount = countOf(expr);
        LLVMTypeRef ptypes[count];
        LLVMGetParamTypes(functype, ptypes);
        LLVMValueRef args[argcount];
        int i = 0;
        while (expr) {
            args[i] = translateValue(env, expr);
            if (!args[i]) return NULL;
            if ((i < count) && (LLVMTypeOf(args[i]) != ptypes[i])) {
                auto _ = env->with_expr(expr);

                translateError(env, "call parameter type (%s) does not match function signature (%s)",
                    getTypeString(LLVMTypeOf(args[i])).c_str(),
                    getTypeString(ptypes[i]).c_str());
                return NULL;
            }
            expr = next(expr);
            ++i;
        }

        return LLVMBuildCall(env->getBuilder(), callee, args, argcount, "");

    } else if (matchSpecialForm(env, expr, "nop", 0, 0)) {

        // do nothing
        return NULL;

    } else if (matchSpecialForm(env, expr, "run", 1, 1)) {

        UNPACK_ARG(expr, expr_callee);

        LLVMValueRef callee = translateValue(env, expr_callee);
        if (!callee) return NULL;

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

        printf("running...\n");
        LLVMRunStaticConstructors(engine);
        LLVMRunFunction(engine, callee, 0, NULL);
        //LLVMRunStaticDestructors(engine);

        printf("done.\n");

        return NULL;

    /*
    } else if (matchSpecialForm(env, expr, "module", 1, -1)) {

        const char *name = translateString(env, expr->nth(1));
        if (!name) return NULL;

        TranslationGlobals globals(env);

        Environment module_env;
        module_env.globals = &globals;

        setupRootEnvironment(&module_env, name);

        compileModule(&module_env, expr, 2);

        teardownRootEnvironment(&module_env);
        return NULL;
    */

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
    } else if (env->hasErrors()) {
        return NULL;
    } else {
        auto _ = env->with_expr(head);
        translateError(env, "unhandled special form: %s. Did you forget a 'call'?", head->c_str());
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
        Environment *penv = (Environment *)env;
        while (penv) {
            LLVMValueRef result = (*penv).values[sym->getValue()];
            if (result) {
                return result;
            }
            penv = penv->parent;
        }

        translateError(env, "no such value: %s", sym->c_str());
        return NULL;
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

static LLVMTypeRef translateTypeFromList (Environment *env, ValueRef expr) {
    assert(expr);
    Symbol *head = translateKind<Symbol>(env, expr);
    if (!head) return NULL;
    if (matchSpecialForm(env, expr, "function", 1, -1)) {

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
    } else if (matchSpecialForm(env, expr, "dump", 1, 1)) {

        UNPACK_ARG(expr, expr_arg);

        LLVMTypeRef type = translateType(env, expr_arg);
        if (type) {
            LLVMDumpType(type);
        }

        return type;
    } else if (matchSpecialForm(env, expr, "*", 1, 1)) {

        UNPACK_ARG(expr, expr_arg);

        LLVMTypeRef type = translateType(env, expr_arg);
        if (!type) return NULL;

        return LLVMPointerType(type, 0);

    } else if (matchSpecialForm(env, expr, "typeof", 1, 1)) {

        UNPACK_ARG(expr, expr_arg);

        LLVMValueRef value = translateValue(env, expr_arg);
        if (!value) return NULL;

        return LLVMTypeOf(value);

    } else if (matchSpecialForm(env, expr, "array", 2, 2)) {

        UNPACK_ARG(expr, expr_type);
        UNPACK_ARG(expr, expr_count);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        int64_t count;
        if (!translateInt64(env, expr_count, count)) return NULL;

        return LLVMArrayType(type, count);
    } else if (matchSpecialForm(env, expr, "vector", 2, 2)) {

        UNPACK_ARG(expr, expr_type);
        UNPACK_ARG(expr, expr_count);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        int64_t count;
        if (!translateInt64(env, expr_count, count)) return NULL;

        return LLVMVectorType(type, count);
    } else if (matchSpecialForm(env, expr, "struct", 1, -1)) {

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
        LLVMTypeRef result = resolveType(env, sym->getValue());
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

static void init() {
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
}

static void teardownRootEnvironment (Environment *env) {
    LLVMDisposeBuilder(env->getBuilder());
}

static Environment *theEnv = NULL;

static bool translateRootValueList (Environment *env, ValueRef expr) {
    while (expr) {
        ValueRef stmt = expr;
        if (Environment::preprocessor) {
            theEnv = env;
            stmt = Environment::preprocessor(env, stmt);
            theEnv = NULL;
            if (env->hasErrors())
                return false;
        }
        if (stmt)
            translateValue(env, stmt);
        if (env->hasErrors())
            return false;
        expr = next(expr);
    }
    return true;
}

// parses stmt ...
static void compileModule (Environment *env, ValueRef expr) {
    assert(expr);
    assert(env->getBuilder());
    assert(env->getModule());

    auto _ = env->with_expr(expr);
    translateRootValueList (env, expr);
}

static void compileMain (ValueRef expr) {
    Environment env;
    TranslationGlobals globals;

    env.globals = &globals;

    {
        auto _ = env.with_expr(expr);
        Symbol *header = kindAt<Symbol>(expr);
        if (strcmp(header->c_str(), "IR")) {
            translateError(&env, "unrecognized header; try 'IR' instead.");
            return;
        }
    }

    setupRootEnvironment(&env, "main");

    compileModule(&env, next(at(expr)));

    teardownRootEnvironment(&env);
}

} // namespace bang

// C API
//------------------------------------------------------------------------------

void bang_dump_value(ValueRef expr) {
    return bang::printValue(expr);
}

int bang_main(int argc, char ** argv) {
    bang::init();

    int result = 0;

    if (argv && argv[1]) {
        bang::Parser parser;
        auto expr = parser.parseFile(argv[1]);
        if (expr) {
            assert(bang::isKindOf<bang::Pointer>(expr));
            //printValue(expr);
            bang::gc_root = cons(expr, bang::gc_root);
            bang::compileMain(expr);
        } else {
            result = 1;
        }
    }

    return result;
}

void bang_set_preprocessor(bang_preprocessor f) {
    Environment::preprocessor = f;
}

bang_preprocessor bang_get_preprocessor() {
    return Environment::preprocessor;
}

int bang_get_kind(ValueRef expr) {
    return kindOf(expr);
}

ValueRef bang_at(ValueRef expr) {
    if (expr) {
        if (bang::isKindOf<bang::Pointer>(expr)) {
            return at(expr);
        }
    }
    return NULL;
}

ValueRef bang_next(ValueRef expr) {
    return next(expr);
}

ValueRef bang_set_next(ValueRef lhs, ValueRef rhs) {
    if (lhs) {
        return cons(lhs, rhs);
    }
    return NULL;
}

const char *bang_string_value(ValueRef expr) {
    if (expr) {
        if (auto str = llvm::dyn_cast<bang::String>(expr)) {
            return str->getValue().c_str();
        }
    }
    return NULL;
}

void *bang_handle_value(ValueRef expr) {
    if (expr) {
        if (auto handle = llvm::dyn_cast<bang::Handle>(expr)) {
            return handle->getValue();
        }
    }
    return NULL;
}

ValueRef bang_handle(void *ptr) {
    auto handle = new bang::Handle(ptr);
    bang::gc_root = new bang::Pointer(handle, bang::gc_root);
    return handle;
}

void bang_error_message(ValueRef context, const char *format, ...) {
    assert(bang::theEnv);
    auto _ = bang::theEnv->with_expr(context);
    va_list args;
    va_start (args, format);
    bang::translateErrorV(bang::theEnv, format, args);
    va_end (args);
}

int bang_eq(Value *a, Value *b) {
    if (a == b) return true;
    if (a && b) {
        auto kind = a->getKind();
        if (kind != b->getKind())
            return false;
        if (kind == bang::V_Symbol) {
            bang::Symbol *sa = llvm::cast<bang::Symbol>(a);
            bang::Symbol *sb = llvm::cast<bang::Symbol>(b);
            if (sa->size() != sb->size()) return false;
            if (!memcmp(sa->c_str(), sb->c_str(), sa->size())) return true;
        }
    }
    return false;
}

ValueRef bang_table() {
    return new bang::Table();
}

void bang_set_key(ValueRef expr, ValueRef key, ValueRef value) {
    if (expr && key) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            table->setKey(key, value);
        }
    }
}

ValueRef bang_get_key(ValueRef expr, ValueRef key) {
    if (expr && key) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            return table->getKey(key);
        }
    }
    return NULL;
}

static ValueRef bang_map_1(ValueRef expr, int idx, bang_mapper map, void *ctx) {
    if (!expr) return NULL;
    ValueRef elem = expr;
    elem = map(elem, idx, ctx);
    ++idx;
    expr = next(expr);
    if (elem)
        return new bang::Pointer(elem, bang_map_1(expr, idx, map, ctx));
    else
        return bang_map_1(expr, idx, map, ctx);
}

ValueRef bang_map(ValueRef expr, bang_mapper map, void *ctx) {
    return bang_map_1(expr, 0, map, ctx);
}

ValueRef bang_set_anchor(ValueRef toexpr, ValueRef anchorexpr) {
    if (anchorexpr && toexpr) {
        if (anchorexpr->anchor.isValid()) {
            ValueRef clone = toexpr->clone();
            clone->anchor = anchorexpr->anchor;
            return clone;
        }
    }
    return toexpr;
}

ValueRef bang_ref(ValueRef lhs) {
    return new bang::Pointer(lhs);
}

//------------------------------------------------------------------------------

#endif // BANG_CPP_IMPL
#ifdef BANG_MAIN_CPP_IMPL

//------------------------------------------------------------------------------
// MAIN EXECUTABLE IMPLEMENTATION
//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
    return bang_main(argc, argv);
}
#endif
