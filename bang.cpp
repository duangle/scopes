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
int bang_size(ValueRef expr);
ValueRef bang_at(ValueRef expr, int index);
const char *bang_string_value(ValueRef expr);
void *bang_handle_value(ValueRef expr);
ValueRef bang_handle(void *ptr);
ValueRef bang_set_at(ValueRef expr, int index, ValueRef value);
void bang_set_key(ValueRef expr, const char *key, ValueRef value);
ValueRef bang_get_key(ValueRef expr, const char *key);
ValueRef bang_slice(ValueRef expr, int start, int end);
ValueRef bang_merge(ValueRef left, ValueRef right);
typedef ValueRef (*bang_mapper)(ValueRef, int, void *);
ValueRef bang_map(ValueRef expr, bang_mapper map, void *ctx);
ValueRef bang_set_anchor(ValueRef toexpr, ValueRef anchorexpr);
ValueRef bang_wrap(ValueRef expr);
ValueRef bang_prepend(ValueRef expr, ValueRef left);

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
    V_Table = 1,
    V_String = 2,
    V_Symbol = 3,
    V_Integer = 4,
    V_Real = 5,
    V_Handle = 6
};

static const char *valueKindName(int kind) {
    switch(kind) {
    case V_None: return "?";
    case V_Table: return "table";
    case V_String: return "string";
    case V_Symbol: return "symbol";
    case V_Integer: return "integer";
    case V_Real: return "real";
    default: return "<corrupted>";
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

struct Value {
private:
    const ValueKind kind;
    int age;
protected:
    Value(ValueKind kind_) :
        kind(kind_),
        age(0) {
        collection.push_back(this);
    }

public:
    typedef std::list<ValueRef> CollectionType;

    static CollectionType collection;

    Anchor anchor;

    virtual ~Value() {}

    ValueKind getKind() const {
        return kind;
    }

    bool isListComment() const;
    bool isComment() const;

    std::string getHeader() const;

    virtual void tag();
    static int collect(ValueRef gcroot);
    virtual ValueRef clone() const = 0;

    bool tagged() { return age == 0; }
};

Value::CollectionType Value::collection;

//------------------------------------------------------------------------------

struct Table : Value {
protected:
    std::vector< ValueRef > values;
    std::map< std::string, ValueRef > string_map;
public:

    // 0 = naked, any
    // '(' = parens
    // '[' = square
    // '{' = curly
    char style;

    Table() :
        Value(V_Table),
        style(0)
        {}

    ValueRef append(ValueRef expr) {
        assert(expr);
        expr->tag();
        values.push_back(expr);
        return expr;
    }

    ValueRef remove(size_t i) {
        ValueRef tmp = values[i];
        values.erase(values.begin() + i);
        return tmp;
    }

    size_t size() const {
        return values.size();
    };

    ValueRef &getElement(size_t i) {
        return values[i];
    }

    const ValueRef &getElement(size_t i) const {
        return values[i];
    }

    ValueRef nth(int i) const {
        if (i < 0)
            i = (int)values.size() + i;
        if ((i < 0) || ((size_t)i >= values.size()))
            return NULL;
        else
            return values[i];
    }

    ValueRef nth(int i) {
        if (i < 0)
            i = (int)values.size() + i;
        if ((i < 0) || ((size_t)i >= values.size()))
            return NULL;
        else
            return values[i];
    }

    void setKey(const std::string &key, ValueRef value) {
        if (value) {
            value->tag();
        }
        string_map[key] = value;
    }

    ValueRef getKey(const std::string &key) {
        return string_map[key];
    }

    static bool classof(const Value *expr) {
        return expr->getKind() == V_Table;
    }

    static ValueKind kind() {
        return V_Table;
    }

    virtual void tag();

    virtual ValueRef clone() const {
        return new Table(*this);
    }
};

static Table *gc_root = NULL;

//------------------------------------------------------------------------------

struct Integer : Value {
protected:
    int64_t value;
    bool is_unsigned;

public:
    Integer(int64_t number, bool is_unsigned_ = false) :
        Value(V_Integer),
        value(number),
        is_unsigned(is_unsigned_)
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
    Real(double number) :
        Value(V_Real),
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

    String(ValueKind kind, const char *s, size_t len) :
        Value(kind),
        value(s, len)
        {}

public:
    String(const char *s, size_t len) :
        Value(V_String),
        value(s, len)
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

//------------------------------------------------------------------------------

struct Symbol : String {
    Symbol(const char *s, size_t len) :
        String(V_Symbol, s, len) {}

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
    Handle(void *ptr) :
        Value(V_Handle),
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

bool Value::isListComment() const {
    if (auto table = llvm::dyn_cast<Table>(this)) {
        if (table->size() > 0) {
            if (auto head = llvm::dyn_cast<Symbol>(table->nth(0))) {
                if (head->getValue().substr(0, 3) == "///")
                    return true;
            }
        }
    }
    return false;
}

bool Value::isComment() const {
    return isListComment();
}

std::string Value::getHeader() const {
    if (auto table = llvm::dyn_cast<Table>(this)) {
        if (table->size() >= 1) {
            if (auto head = llvm::dyn_cast<Symbol>(table->nth(0))) {
                return head->getValue();
            }
        }
    }
    return "";
}

void Value::tag() {
    // reset age
    age = 0;
}

void Table::tag() {
    if (tagged()) return;
    Value::tag();

    for (size_t i = 0; i < values.size(); ++i) {
        values[i]->tag();
    }

    for (auto k : string_map) {
        k.second->tag();
    }
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

ValueRef strip(ValueRef expr) {
    assert(expr);
    if (expr->isComment()) {
        return nullptr;
    } else if (expr->getKind() == V_Table) {
        auto table = llvm::cast<Table>(expr);
        auto result = new Table();
        bool changed = false;
        for (size_t i = 0; i < table->size(); ++i) {
            auto oldelem = table->getElement(i);
            auto newelem = strip(oldelem);
            if (oldelem != newelem)
                changed = true;
            if (newelem)
                result->append(newelem);
        }
        if (changed) {
            result->anchor = expr->anchor;
            return result;
        }
    }
    return expr;
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

    Parser() {}

    void init() {
        error_string.clear();
    }

    void error( const char *format, ... ) {
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

    bool splitList(
        Table *result, int list_split_start, char style = 0) {
        int count = (int)result->size();
        if (list_split_start == count) {
            error("empty expression");
            return false;
        } else {
            auto wrapped_result = new Table();
            wrapped_result->style = style;
            wrapped_result->anchor =
                result->getElement(list_split_start)->anchor;
            for (int i = list_split_start; i < count; ++i) {
                wrapped_result->append(result->getElement(i));
            }
            for (int i = count - 1; i >= list_split_start; --i) {
                result->remove(i);
            }
            result->append(wrapped_result);
            return true;
        }
    }

    ValueRef parseList(int end_token) {
        auto result = new Table();
        result->style = lexer.token;
        lexer.initAnchor(result->anchor);
        int statement_start = 0;
        while (true) {
            lexer.readToken();
            if (lexer.token == end_token) {
                break;
            } else if (lexer.token == token_eof) {
                error("missing closing bracket");
                // point to beginning of table
                error_origin = result->anchor;
                return nullptr;
            } else if (lexer.token == token_statement) {
                if (!splitList(result, statement_start))
                    return nullptr;
                statement_start++;
            } else {
                if (auto elem = parseAny())
                    result->append(elem);
                else
                    return nullptr;
            }
        }
        return result;
    }

    ValueRef parseAny () {
        assert(lexer.token != token_eof);
        if (lexer.token == token_open) {
            return parseList(token_close);
        } else if (lexer.token == token_square_open) {
            return parseList(token_square_close);
        } else if (lexer.token == token_curly_open) {
            return parseList(token_curly_close);
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

        auto result = new Table();
        lexer.initAnchor(result->anchor);

        int statement_start = 0;
        while (lexer.token != token_eof) {
            if (lexer.token == token_escape) {
                escape = true;
                lexer.readToken();
                if (lexer.lineno <= lineno) {
                    error("escape character is not at end of line");
                    parse_origin = result->anchor;
                    return nullptr;
                }
                lineno = lexer.lineno;
            } else if (lexer.lineno > lineno) {
                if (depth > 0) {
                    if (subcolumn == 0) {
                        subcolumn = lexer.column();
                    } else if (lexer.column() != subcolumn) {
                        error("indentation mismatch");
                        parse_origin = result->anchor;
                        return nullptr;
                    }
                } else {
                    subcolumn = lexer.column();
                }
                escape = false;
                statement_start = (int)result->size();
                lineno = lexer.lineno;
                // keep adding elements while we're in the same line
                while ((lexer.token != token_eof)
                        && (lexer.lineno == lineno)) {
                    if (auto elem = parseNaked(subcolumn, depth + 1)) {
                        result->append(elem);
                    } else {
                        return nullptr;
                    }
                }
            } else if (lexer.token == token_statement) {
                if (!splitList(result, statement_start))
                    return nullptr;
                statement_start++;
                lexer.readToken();
                if (depth > 0) {
                    // if we are in the same line and there was no preceding ":",
                    // continue in parent
                    if (lexer.lineno == lineno)
                        break;
                }
            } else {
                if (auto elem = parseAny())
                    result->append(elem);
                else
                    return nullptr;
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

        if (result->size() == 0) {
            assert(depth == 0);
            return nullptr;
        } else if (result->size() == 1) {
            return result->getElement(0);
        } else {
            return result;
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

static bool isNested(ValueRef e) {
    if (e->getKind() == V_Table) {
        Table *l = llvm::cast<Table>(e);
        for (size_t i = 0; i < l->size(); ++i) {
            if (l->nth(i)->getKind() == V_Table)
                return true;
        }
    }
    return false;
}

static void printAnchor(ValueRef e, size_t depth=0) {
    printf("%s:%i:%i: ",
        e->anchor.path,
        e->anchor.lineno,
        e->anchor.column);
    for(size_t i = 0; i < depth; i++) printf("    ");
}

static void printValue(ValueRef e, size_t depth, bool naked)
{
	if (!e) {
        printf("#null#");
        if (naked) putchar('\n');
        return;
    }

    if (naked) {
        printAnchor(e, depth);
    }

	switch(e->getKind()) {
	case V_Table: {
        Table *l = llvm::cast<Table>(e);
        if (naked && !l->style) {
            if (l->size() == 0) {
                printf("()\n");
            } else {
                size_t offset = 0;
                ValueRef e0 = l->nth(0);
                if (e0->getKind() == V_Table) {
                    printf(";\n");
                    goto print_sparse;
                }
            print_terse:
                printValue(e0, depth, false);
                offset++;
                while (offset < l->size()) {
                    e0 = l->nth(offset);
                    if (isNested(e0))
                        break;
                    putchar(' ');
                    printValue(e0, depth, false);
                    offset++;
                }
                printf((l->size() == 1)?" ;\n":"\n");
            print_sparse:
                while (offset < l->size()) {
                    e0 = l->nth(offset);
                    if ((e0->getKind() != V_Table) // not a table
                        && (offset >= 1) // not first element in table
                        && (offset < (l->size() - 1)) // not last element in table
                        && !isNested(l->nth(offset+1))) { // next element can be terse packed too
                        printAnchor(e0, depth + 1);
                        printf("\\ ");
                        goto print_terse;
                    }
                    printValue(e0, depth + 1);
                    offset++;
                }
            }
        } else {
            putchar((l->style == 0)?'(':l->style);
            for (size_t i = 0; i < l->size(); i++) {
                if (i > 0)
                    putchar(' ');
                printValue(l->nth(i), depth + 1, false);
            }
            switch(l->style) {
                case '[': putchar(']'); break;
                case '{': putchar('}'); break;
                case 0:
                default:
                    putchar(')'); break;
            }
            if (naked)
                putchar('\n');
        }
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
        printf("#%p#", h->getValue());
        if (naked)
            putchar('\n');
    } return;
	case V_Symbol:
	case V_String: {
        const String *a = llvm::cast<String>(e);
		if (a->getKind() == V_String) putchar('"');
		for (size_t i = 0; i < a->size(); i++) {
			switch((*a)[i]) {
			case '"':
			case '\\':
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
            case '(':
			case ')':
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
typedef std::map<LLVMValueRef, void *> GlobalPtrMap;

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
    GlobalPtrMap globalptrs;

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
        globals->globalptrs[value] = (void *)expr;
        gc_root->append(expr);
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
        //proto is null if the function was declared without an argument table (e.g. void foo() and not void foo(void))
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
}

static void translateError (Environment *env, const char *format, ...) {
    va_list args;
    va_start (args, format);
    translateErrorV(env, format, args);
    va_end (args);
}

static bool verifyParameterCount (Environment *env, Table *expr, int mincount, int maxcount, int start = 1) {
    if (expr) {
        auto _ = env->with_expr(expr);
        int argcount = (int)expr->size() - start;
        if ((mincount >= 0) && (argcount < mincount)) {
            translateError(env, "at least %i arguments expected", mincount);
            return false;
        }
        if ((maxcount >= 0) && (argcount > maxcount)) {
            auto _ = env->with_expr(expr->nth(maxcount + 1));
            translateError(env, "excess argument. At most %i arguments expected.", maxcount);
            return false;
        }
        return true;
    }
    return false;
}

static bool isSymbol (const Value *expr, const char *sym) {
    if (expr) {
        if (auto symexpr = llvm::dyn_cast<Symbol>(expr))
            return (symexpr->getValue() == sym);
    }
    return false;
}

static bool matchSpecialForm (Environment *env, Table *expr, const char *name, int mincount, int maxcount) {
    return isSymbol(expr->nth(0), name) && verifyParameterCount(env, expr, mincount, maxcount);
}

//------------------------------------------------------------------------------

template<typename T>
static T *translateKind(Environment *env, ValueRef expr) {
    if (expr) {
        auto _ = env->with_expr(expr);
        T *co = llvm::dyn_cast<T>(expr);
        if (co) {
            return co;
        } else {
            translateError(env, "%s expected, not %s",
                valueKindName(T::kind()),
                valueKindName(expr->getKind()));
        }
    }
    return nullptr;
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

static bool translateValueList (Environment *env, Table *expr, int offset) {
    int argcount = (int)expr->size() - offset;
    for (int i = 0; i < argcount; ++i) {
        translateValue(env, expr->nth(i + offset));
        if (env->hasErrors())
            return false;
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

static LLVMValueRef translateValueFromList (Environment *env, Table *expr) {
    if (expr->size() == 0) {
        translateError(env, "value expected");
        return NULL;
    }
    auto head = llvm::dyn_cast<Symbol>(expr->nth(0));
    if (!head) {
        auto _ = env->with_expr(expr->nth(0));
        translateError(env, "first element of table must be symbol, not %s",
            valueKindName(expr->nth(0)->getKind()));
        return NULL;
    }
    if (matchSpecialForm(env, expr, "int", 2, 2)) {

        ValueRef expr_type = expr->nth(1);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        int64_t value;
        if (!translateInt64(env, expr->nth(2), value)) return NULL;

        return LLVMConstInt(type, value, 1);

    } else if (matchSpecialForm(env, expr, "real", 2, 2)) {

        ValueRef expr_type = expr->nth(1);

        LLVMTypeRef type = translateType(env, expr_type);
        if (!type) return NULL;

        double value;
        if (!translateDouble(env, expr->nth(2), value)) return NULL;

        return LLVMConstReal(type, value);

    } else if (matchSpecialForm(env, expr, "dump-module", 0, 0)) {

        LLVMDumpModule(env->getModule());

        return NULL;

    } else if (matchSpecialForm(env, expr, "dump", 1, 1)) {

        ValueRef expr_arg = expr->nth(1);

        LLVMValueRef value = translateValue(env, expr_arg);
        if (value) {
            LLVMDumpValue(value);
        }

        return value;
    } else if (matchSpecialForm(env, expr, "dumptype", 1, 1)) {

        ValueRef expr_arg = expr->nth(1);

        LLVMTypeRef type = translateType(env, expr_arg);
        if (type) {
            LLVMDumpType(type);
        }

        return NULL;

    } else if (matchSpecialForm(env, expr, "global", 2, 2)) {

        const char *name = translateString(env, expr->nth(1));
        if (!name) return NULL;

        ValueRef expr_value = expr->nth(2);

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
        if (LLVMIsConstant(value)) {
            LLVMSetGlobalConstant(result, true);
        }

        if (inlined)
            LLVMSetLinkage(result, LLVMLinkOnceAnyLinkage);

        if (llvm::isa<Symbol>(expr->nth(1)))
            env->values[name] = result;

        return result;

    } else if (matchSpecialForm(env, expr, "quote", 2, 2)) {

        LLVMTypeRef type = translateType(env, expr->nth(1));
        if (!type) return NULL;

        LLVMValueRef result = LLVMAddGlobal(env->getModule(), type, "quote");
        env->addQuote(result, expr->nth(2));

        return result;

    } else if (matchSpecialForm(env, expr, "bitcast", 2, 2)) {

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;

        LLVMTypeRef type = translateType(env, expr->nth(2));
        if (!type) return NULL;

        if (LLVMIsConstant(value)) {
            return LLVMConstBitCast(value, type);

        } else {
            if (!verifyInBlock(env)) return NULL;
            return LLVMBuildBitCast(env->getBuilder(), value, type, "");
        }

    } else if (matchSpecialForm(env, expr, "ptrtoint", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;
        LLVMTypeRef type = translateType(env, expr->nth(2));
        if (!type) return NULL;

        return LLVMBuildPtrToInt(env->getBuilder(), value, type, "");

    } else if (matchSpecialForm(env, expr, "inttoptr", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;
        LLVMTypeRef type = translateType(env, expr->nth(2));
        if (!type) return NULL;

        return LLVMBuildIntToPtr(env->getBuilder(), value, type, "");

    } else if (matchSpecialForm(env, expr, "and", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef lhs = translateValue(env, expr->nth(1));
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr->nth(2));
        if (!rhs) return NULL;

        return LLVMBuildAnd(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "or", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef lhs = translateValue(env, expr->nth(1));
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr->nth(2));
        if (!rhs) return NULL;

        return LLVMBuildOr(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "add", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef lhs = translateValue(env, expr->nth(1));
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr->nth(2));
        if (!rhs) return NULL;

        return LLVMBuildAdd(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "sub", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef lhs = translateValue(env, expr->nth(1));
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr->nth(2));
        if (!rhs) return NULL;

        return LLVMBuildSub(env->getBuilder(), lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "icmp", 3, 3)) {
        if (!verifyInBlock(env)) return NULL;

        const char *opname = translateString(env, expr->nth(1));
        if (!opname) return NULL;
        LLVMValueRef lhs = translateValue(env, expr->nth(2));
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr->nth(3));
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
            auto _ = env->with_expr(expr->nth(1));
            translateError(env,
                "illegal operand. Try one of == != u> u>= u< u<= i> i>= i< i<=.");
            return NULL;
        }

        return LLVMBuildICmp(env->getBuilder(), op, lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "fcmp", 3, 3)) {
        if (!verifyInBlock(env)) return NULL;

        const char *opname = translateString(env, expr->nth(1));
        if (!opname) return NULL;
        LLVMValueRef lhs = translateValue(env, expr->nth(2));
        if (!lhs) return NULL;
        LLVMValueRef rhs = translateValue(env, expr->nth(3));
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
            auto _ = env->with_expr(expr->nth(1));
            translateError(env,
                "illegal operand. Try one of false true ord uno o== o!= o> o>= o< o<= u== u!= u> u>= u< u<=.");
            return NULL;
        }

        return LLVMBuildFCmp(env->getBuilder(), op, lhs, rhs, "");

    } else if (matchSpecialForm(env, expr, "getelementptr", 1, -1)) {

        ValueRef expr_array = expr->nth(1);
        LLVMValueRef ptr = translateValue(env, expr_array);
        if (!ptr) return NULL;

        auto _ = env->with_expr(expr_array);

        bool all_const = LLVMIsConstant(ptr);

        int valuecount = (int)expr->size() - 2;
        LLVMValueRef indices[valuecount];
        for (int i = 0; i < valuecount; ++i) {
            indices[i] = translateValue(env, expr->nth(i + 2));
            if (indices[i] == NULL) {
                return NULL;
            }
            all_const = all_const && LLVMIsConstant(indices[i]);
        }

        if (all_const) {
            return LLVMConstInBoundsGEP(ptr, indices, valuecount);
        } else {
            if (!verifyInBlock(env)) return NULL;
            return LLVMBuildGEP(env->getBuilder(), ptr, indices, valuecount, "");
        }

    } else if (matchSpecialForm(env, expr, "extractelement", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;


        LLVMValueRef index = translateValue(env, expr->nth(2));
        if (!index) return NULL;

        LLVMValueRef result = LLVMBuildExtractElement(env->getBuilder(), value, index, "");
        if (!result) {
            translateError(env, "can not use extract on this value");
        }
        return result;

    } else if (matchSpecialForm(env, expr, "extractvalue", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;

        int64_t index;
        if (!translateInt64(env, expr->nth(2), index)) return NULL;

        LLVMTypeRef valuetype = LLVMTypeOf(value);
        LLVMTypeKind kind = LLVMGetTypeKind(valuetype);
        switch(kind) {
            case LLVMStructTypeKind: {
                auto _ = env->with_expr(expr->nth(2));
                unsigned count = LLVMCountStructElementTypes(valuetype);
                if ((unsigned)index >= count) {
                    translateError(env,
                        "struct field index is out of bounds.");
                }
            } break;
            case LLVMArrayTypeKind: {
                auto _ = env->with_expr(expr->nth(2));
                unsigned count = LLVMGetArrayLength(valuetype);
                if ((unsigned)index >= count) {
                    translateError(env,
                        "array offset is out of bounds.");
                }
            } break;
            default: {
                auto _ = env->with_expr(expr->nth(1));
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
        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;

        int64_t bytes;
        if (!translateInt64(env, expr->nth(2), bytes)) return NULL;

        LLVMSetAlignment(value, bytes);
        return value;

    } else if (matchSpecialForm(env, expr, "load", 1, 1)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;

        return LLVMBuildLoad(env->getBuilder(), value, "");

    } else if (matchSpecialForm(env, expr, "store", 2, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;
        LLVMValueRef ptr = translateValue(env, expr->nth(2));
        if (!ptr) return NULL;

        return LLVMBuildStore(env->getBuilder(), value, ptr);

    } else if (matchSpecialForm(env, expr, "alloca", 1, 2)) {
        if (!verifyInBlock(env)) return NULL;

        LLVMTypeRef type = translateType(env, expr->nth(1));
        if (!type) return NULL;
        if (expr->nth(2)) {
            LLVMValueRef value = translateValue(env, expr->nth(2));
            return LLVMBuildArrayAlloca(env->getBuilder(), type, value, "");
        } else {
            return LLVMBuildAlloca(env->getBuilder(), type, "");
        }

    } else if (matchSpecialForm(env, expr, "defvalue", 2, 2)) {

        const Symbol *expr_name = translateKind<Symbol>(env, expr->nth(1));
        ValueRef expr_value = expr->nth(2);
        LLVMValueRef result = translateValue(env, expr_value);
        if (!result) return NULL;

        const char *name = expr_name->c_str();
        env->values[name] = result;

        return result;
    } else if (matchSpecialForm(env, expr, "deftype", 2, 2)) {

        const Symbol *expr_name = translateKind<Symbol>(env, expr->nth(1));
        ValueRef expr_value = expr->nth(2);
        LLVMTypeRef result = translateType(env, expr_value);
        if (!result) return NULL;

        const char *name = expr_name->c_str();
        env->types[name] = result;

        return NULL;
    } else if (matchSpecialForm(env, expr, "struct", -1, -1)) {

        translateType(env, expr);
        return NULL;

    } else if (matchSpecialForm(env, expr, "label", 1, -1)) {

        if (!verifyInFunction(env)) return NULL;

        ValueRef expr_name = expr->nth(1);

        const char *name = translateString(env, expr_name);
        if (!name) return NULL;

        LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(env->getBuilder());

        // continue existing label
        LLVMBasicBlockRef block = NULL;
        if (llvm::isa<Symbol>(expr_name)) {
            LLVMValueRef maybe_block = env->values[name];
            if (maybe_block) {
                block = verifyBasicBlock(env, maybe_block);
                if (!block)
                    return NULL;
            }
        }

        if (!block) {
            block = LLVMAppendBasicBlock(env->function, name);
            if (llvm::isa<Symbol>(expr_name))
                env->values[name] = LLVMBasicBlockAsValue(block);
        }

        LLVMValueRef blockvalue = LLVMBasicBlockAsValue(block);

        LLVMPositionBuilderAtEnd(env->getBuilder(), block);

        LLVMValueRef oldblockvalue = env->block;
        env->block = blockvalue;
        translateValueList(env, expr, 2);
        env->block = oldblockvalue;

        if (env->hasErrors()) return NULL;

        if (oldblock)
            LLVMPositionBuilderAtEnd(env->getBuilder(), oldblock);

        return blockvalue;

    } else if (matchSpecialForm(env, expr, "null", 1, 1)) {

        LLVMTypeRef type = translateType(env, expr->nth(1));
        if (!type) return NULL;

        return LLVMConstNull(type);

    } else if (matchSpecialForm(env, expr, "do", 0, -1)) {

        Environment subenv(env);
        translateValueList(&subenv, expr, 1);

        return NULL;
    } else if (matchSpecialForm(env, expr, "do-splice", 0, -1)) {

        translateValueList(env, expr, 1);

        return NULL;
    } else if (matchSpecialForm(env, expr, "define", 4, -1)) {

        const char *name = translateString(env, expr->nth(1));
        if (!name) return NULL;
        Table *expr_params = translateKind<Table>(env, expr->nth(2));
        if (!expr_params)
            return NULL;
        ValueRef expr_type = expr->nth(3);
        ValueRef body_expr = expr->nth(4);

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

        if (llvm::isa<Symbol>(expr->nth(1)))
            env->values[name] = func;
        if (inlined)
            LLVMSetLinkage(func, LLVMLinkOnceAnyLinkage);

        Environment subenv(env);
        subenv.function = func;

        {
            auto _ = env->with_expr(expr_params);

            int argcount = (int)expr_params->size();
            int paramcount = LLVMCountParams(func);
            if (argcount == paramcount) {
                LLVMValueRef params[paramcount];
                LLVMGetParams(func, params);
                for (int i = 0; i < argcount; ++i) {
                    const Symbol *expr_param = translateKind<Symbol>(env, expr_params->nth(i));
                    if (expr_param) {
                        const char *name = expr_param->c_str();
                        LLVMSetValueName(params[i], name);
                        subenv.values[name] = params[i];
                    }
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

            translateValueList(&subenv, expr, 4);
            if (env->hasErrors()) return NULL;
        }

        return func;

    } else if (matchSpecialForm(env, expr, "phi", 1, -1)) {

        if (!verifyInBlock(env)) return NULL;

        LLVMTypeRef type = translateType(env, expr->nth(1));
        if (!type) return NULL;

        int branchcount = expr->size() - 2;
        LLVMValueRef values[branchcount];
        LLVMBasicBlockRef blocks[branchcount];
        for (int i = 0; i < branchcount; ++i) {
            Table *expr_pair = translateKind<Table>(env, expr->nth(2 + i));
            auto _ = env->with_expr(expr_pair);
            if (!expr_pair)
                return NULL;
            if (expr_pair->size() != 2) {
                translateError(env, "exactly 2 parameters expected");
                return NULL;
            }
            LLVMValueRef value = translateValue(env, expr_pair->nth(0));
            if (!value) return NULL;
            LLVMBasicBlockRef block = verifyBasicBlock(env, translateValue(env, expr_pair->nth(1)));
            if (!block) return NULL;
            values[i] = value;
            blocks[i] = block;
        }

        LLVMValueRef result =
            LLVMBuildPhi(env->getBuilder(), type, "");
        LLVMAddIncoming(result, values, blocks, branchcount);
        return result;

    } else if (matchSpecialForm(env, expr, "br", 1, 1)) {

        if (!verifyInBlock(env)) return NULL;

        LLVMBasicBlockRef value = verifyBasicBlock(env, translateValue(env, expr->nth(1)));
        if (!value) return NULL;

        return LLVMBuildBr(env->getBuilder(), value);

    } else if (matchSpecialForm(env, expr, "cond-br", 3, 3)) {

        if (!verifyInBlock(env)) return NULL;

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;
        LLVMBasicBlockRef then_block = verifyBasicBlock(env, translateValue(env, expr->nth(2)));
        if (!then_block) return NULL;
        LLVMBasicBlockRef else_block = verifyBasicBlock(env, translateValue(env, expr->nth(3)));
        if (!else_block) return NULL;

        return LLVMBuildCondBr(env->getBuilder(), value, then_block, else_block);

    } else if (matchSpecialForm(env, expr, "ret", 0, 1)) {

        if (!verifyInBlock(env)) return NULL;

        ValueRef expr_value = expr->nth(1);
        if (!expr_value) {
            LLVMBuildRetVoid(env->getBuilder());
        } else {
            LLVMValueRef value = translateValue(env, expr_value);
            if (!value) return NULL;
            LLVMBuildRet(env->getBuilder(), value);
        }

        return NULL;
    } else if (matchSpecialForm(env, expr, "declare", 2, 2)) {

        const char *name = translateString(env, expr->nth(1));
        if (!name) return NULL;
        ValueRef expr_type = expr->nth(2);

        LLVMTypeRef functype = translateType(env, expr_type);

        auto _ = env->with_expr(expr_type);

        if (!functype)
            return NULL;

        LLVMValueRef result = LLVMAddFunction(
            env->getModule(), name, functype);

        if (llvm::isa<Symbol>(expr->nth(1)))
            env->values[name] = result;

        return result;

    } else if (matchSpecialForm(env, expr, "call", 1, -1)) {

        if (!verifyInBlock(env)) return NULL;

        int argcount = (int)expr->size() - 2;

        ValueRef expr_func = expr->nth(1);
        LLVMValueRef callee = translateValue(env, expr_func);
        if (!callee) return NULL;
        LLVMTypeRef functype = LLVMTypeOf(callee);
        LLVMTypeKind kind = LLVMGetTypeKind(functype);
        if (kind == LLVMPointerTypeKind) {
            functype = LLVMGetElementType(functype);
            kind = LLVMGetTypeKind(functype);
        }
        if (kind != LLVMFunctionTypeKind) {
            auto _ = env->with_expr(expr->nth(1));
            translateError(env, "callee is not a function.");
            return NULL;
        }

        int count = (int)LLVMCountParamTypes(functype);
        bool vararg = LLVMIsFunctionVarArg(functype);
        int mincount = count;
        int maxcount = vararg?-1:count;
        if (!verifyParameterCount(env, expr, mincount, maxcount, 2))
            return NULL;

        LLVMTypeRef ptypes[count];
        LLVMGetParamTypes(functype, ptypes);
        LLVMValueRef args[argcount];
        for (int i = 0; i < argcount; ++i) {
            args[i] = translateValue(env, expr->nth(i + 2));
            if (!args[i]) return NULL;
            if ((i < count) && (LLVMTypeOf(args[i]) != ptypes[i])) {
                auto _ = env->with_expr(expr->nth(i + 2));

                translateError(env, "call parameter type (%s) does not match function signature (%s)",
                    getTypeString(LLVMTypeOf(args[i])).c_str(),
                    getTypeString(ptypes[i]).c_str());
                return NULL;
            }
        }

        return LLVMBuildCall(env->getBuilder(), callee, args, argcount, "");

    } else if (matchSpecialForm(env, expr, "nop", 0, 0)) {

        // do nothing
        return NULL;

    } else if (matchSpecialForm(env, expr, "run", 1, 1)) {

        LLVMValueRef callee = translateValue(env, expr->nth(1));
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
            LLVMAddGlobalMapping(engine, it.first, it.second);
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
        Table *args_expr = translateKind<Table>(env, expr->nth(3));

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
    } else {
        auto _ = env->with_expr(head);
        translateError(env, "unhandled special form: %s", head->c_str());
        return NULL;
    }
}

static LLVMValueRef translateValue (Environment *env, ValueRef expr) {
    if (env->hasErrors()) return NULL;
    assert(expr);
    auto _ = env->with_expr(expr);

    if (auto table = llvm::dyn_cast<Table>(expr)) {
        return translateValueFromList(env, table);
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
            valueKindName(expr->getKind()));
        return NULL;
    }
}

static LLVMTypeRef translateTypeFromList (Environment *env, Table *expr) {
    if (expr->size() == 0) {
        translateError(env, "type expected");
        return NULL;
    }
    auto head = llvm::dyn_cast<Symbol>(expr->nth(0));
    if (!head) {
        auto _ = env->with_expr(expr->nth(0));
        translateError(env, "first element of table must be symbol, not %s",
            valueKindName(expr->nth(0)->getKind()));
        return NULL;
    }
    if (matchSpecialForm(env, expr, "function", 1, -1)) {

        ValueRef tail = expr->nth(-1);
        bool vararg = false;
        int argcount = (int)expr->size() - 2;
        if (isSymbol(tail, "...")) {
            vararg = true;
            --argcount;
        }

        if (argcount < 0) {
            translateError(env, "vararg function is missing return type");
            return NULL;
        }

        LLVMTypeRef rettype = translateType(env, expr->nth(1));
        if (!rettype) return NULL;

        LLVMTypeRef paramtypes[argcount];
        for (int i = 0; i < argcount; ++i) {
            paramtypes[i] = translateType(env, expr->nth(2 + i));
            if (!paramtypes[i]) {
                return NULL;
            }
        }

        return LLVMFunctionType(rettype, paramtypes, argcount, vararg);
    } else if (matchSpecialForm(env, expr, "dump", 1, 1)) {

        ValueRef expr_arg = expr->nth(1);

        LLVMTypeRef type = translateType(env, expr_arg);
        if (type) {
            LLVMDumpType(type);
        }

        return type;
    } else if (matchSpecialForm(env, expr, "*", 1, 1)) {

        LLVMTypeRef type = translateType(env, expr->nth(1));
        if (!type) return NULL;

        return LLVMPointerType(type, 0);

    } else if (matchSpecialForm(env, expr, "typeof", 1, 1)) {

        LLVMValueRef value = translateValue(env, expr->nth(1));
        if (!value) return NULL;

        return LLVMTypeOf(value);

    } else if (matchSpecialForm(env, expr, "array", 2, 2)) {

        LLVMTypeRef type = translateType(env, expr->nth(1));
        if (!type) return NULL;

        int64_t count;
        if (!translateInt64(env, expr->nth(2), count)) return NULL;

        return LLVMArrayType(type, count);
    } else if (matchSpecialForm(env, expr, "vector", 2, 2)) {

        LLVMTypeRef type = translateType(env, expr->nth(1));
        if (!type) return NULL;

        int64_t count;
        if (!translateInt64(env, expr->nth(2), count)) return NULL;

        return LLVMVectorType(type, count);
    } else if (matchSpecialForm(env, expr, "struct", 1, -1)) {

        const char *name = translateString(env, expr->nth(1));
        if (!name) return NULL;

        LLVMTypeRef result = LLVMStructCreateNamed(LLVMGetGlobalContext(), name);
        if (llvm::isa<Symbol>(expr->nth(1)))
            env->types[name] = result;

        int offset = 2;
        bool packed = false;
        if (isSymbol(expr->nth(offset), "packed")) {
            offset++;
            packed = true;
        }

        int elemcount = expr->size() - offset;
        LLVMTypeRef elemtypes[elemcount];
        for (int i = 0; i < elemcount; ++i) {
            elemtypes[i] = translateType(env, expr->nth(offset + i));
            if (!elemtypes[i]) return NULL;
        }

        if (elemcount)
            LLVMStructSetBody(result, elemtypes, elemcount, packed);

        return result;
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

    if (auto table = llvm::dyn_cast<Table>(expr)) {
        return translateTypeFromList(env, table);
    } else if (auto sym = llvm::dyn_cast<Symbol>(expr)) {
        LLVMTypeRef result = resolveType(env, sym->getValue());
        if (!result) {
            translateError(env, "no such type: %s", sym->c_str());
            return NULL;
        }

        return result;
    } else {
        translateError(env, "expected type, not %s",
            valueKindName(expr->getKind()));
        return NULL;
    }
}

static void init() {
    if (!gc_root)
        gc_root = new Table();

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

static bool translateRootValueList (Environment *env, Table *expr, int offset) {
    int argcount = (int)expr->size() - offset;
    for (int i = 0; i < argcount; ++i) {
        Value *stmt = expr->nth(i + offset);
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
    }
    return true;
}

static void compileModule (Environment *env, ValueRef expr, int offset) {
    assert(expr);
    assert(env->getBuilder());
    assert(env->getModule());

    auto _ = env->with_expr(expr);
    if (auto table = llvm::dyn_cast<Table>(expr)) {
        translateRootValueList (env, table, offset);
    } else {
        translateError(env, "unexpected %s",
            valueKindName(expr->getKind()));
    }
}

static void compileMain (ValueRef expr) {
    Environment env;
    TranslationGlobals globals;

    env.globals = &globals;

    {
        auto _ = env.with_expr(expr);
        std::string header = expr->getHeader();
        if (header != "IR") {
            translateError(&env, "unrecognized header: '%s'; try 'IR' instead.", header.c_str());
            return;
        }
    }

    setupRootEnvironment(&env, "main");

    compileModule(&env, expr, 1);

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
        bang::gc_root->append(expr);
        if (expr) {
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
    if (expr) {
        return expr->getKind();
    }
    return bang::V_None;
}

int bang_size(ValueRef expr) {
    if (expr) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            return table->size();
        }
    }
    return 0;
}

ValueRef bang_at(ValueRef expr, int index) {
    if (expr) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            return table->nth(index);
        }
    }
    return NULL;
}

ValueRef bang_set_at(ValueRef expr, int index, ValueRef value) {
    if (expr && value) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            bang::Table *newlist = new bang::Table();
            newlist->style = table->style;
            newlist->anchor = table->anchor;
            for (int i = 0; i < index; ++i) {
                ValueRef elem = table->nth(i);
                if (!elem) break;
                newlist->append(elem);
            }
            newlist->append(value);
            for (size_t i = index + 1; i < table->size(); ++i) {
                newlist->append(table->nth(i));
            }
            return newlist;
        }
    }
    return NULL;
}

ValueRef bang_slice(ValueRef expr, int start, int end) {
    if (expr) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            bang::Table *newlist = new bang::Table();
            newlist->style = table->style;
            newlist->anchor = table->anchor;
            if (end == -1)
                end = (int)table->size();
            for (int i = start; i < end; ++i) {
                ValueRef elem = table->nth(i);
                if (!elem) break;
                newlist->append(elem);
            }
            return newlist;
        }
    }
    return NULL;
}

ValueRef bang_merge(ValueRef left, ValueRef right) {
    if (left && right) {
        auto ltable = llvm::dyn_cast<bang::Table>(left);
        auto rtable = llvm::dyn_cast<bang::Table>(right);
        if (ltable && rtable) {
            bang::Table *newlist = new bang::Table();
            newlist->style = ltable->style;
            newlist->anchor = ltable->anchor;
            int size1 = (int)ltable->size();
            for (int i = 0; i < size1; ++i) {
                newlist->append(ltable->nth(i));
            }
            int size2 = (int)rtable->size();
            for (int i = 0; i < size2; ++i) {
                newlist->append(rtable->nth(i));
            }
            return newlist;
        }
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
    bang::gc_root->append(handle);
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

void bang_set_key(ValueRef expr, const char *key, ValueRef value) {
    if (expr && key && value) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            table->setKey(key, value);
        }
    }
}

ValueRef bang_get_key(ValueRef expr, const char *key) {
    if (expr && key) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            return table->getKey(key);
        }
    }
    return NULL;
}

ValueRef bang_map(ValueRef expr, bang_mapper map, void *ctx) {
    if (expr && map) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            bang::Table *newlist = new bang::Table();
            newlist->style = table->style;
            newlist->anchor = table->anchor;
            for (int i = 0; i < (int)table->size(); ++i) {
                ValueRef elem = table->nth(i);
                elem = map(elem, i, ctx);
                if (elem)
                    newlist->append(elem);
            }
            return newlist;
        }
    }
    return NULL;
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

ValueRef bang_wrap(ValueRef expr) {
    if (expr) {
        bang::Table *newlist = new bang::Table();
        newlist->style = '(';
        newlist->anchor = expr->anchor;
        newlist->append(expr);
        return newlist;
    }
    return NULL;
}

ValueRef bang_prepend(ValueRef expr, ValueRef left) {
    if (expr && left) {
        if (auto table = llvm::dyn_cast<bang::Table>(expr)) {
            bang::Table *newlist = new bang::Table();
            newlist->style = table->style;
            newlist->anchor = table->anchor;
            newlist->append(left);
            for (int i = 0; i < (int)table->size(); ++i) {
                ValueRef elem = table->nth(i);
                newlist->append(elem);
            }
            return newlist;
        }
    }
    return expr;
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
