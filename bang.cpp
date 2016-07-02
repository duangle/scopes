#ifndef BANG0_CPP
#define BANG0_CPP

//------------------------------------------------------------------------------

#if defined __cplusplus
extern "C" {
#endif


const char *return_test_string ();

int bang_main(int argc, char ** argv);

#if defined __cplusplus
}
#endif

#endif // BANG0_CPP
#ifdef BANG_CPP_IMPL

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
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>

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

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args ) {
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... );
    std::string str;
    str.resize(size);
    snprintf( &str[0], size, format.c_str(), args ... );
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

enum ExpressionKind {
    E_None,
    E_List,
    E_String,
    E_Symbol,
    E_Comment,
    E_Atom
};

struct Anchor {
    const char *path;
    int lineno;
    int column;
    int offset;
};

struct Expression {
private:
    const ExpressionKind kind;
protected:
    Expression(ExpressionKind kind_) :
        kind(kind_) {}
public:
    Anchor anchor;

    ExpressionKind getKind() const {
        return kind;
    }

    bool isListComment() const;
    bool isLineComment() const;
    bool isComment() const;

    std::string getHeader() const;
};

typedef std::shared_ptr<Expression> ExpressionRef;

//------------------------------------------------------------------------------

struct List : Expression {
    std::vector< ExpressionRef > values;

    List() :
        Expression(E_List)
        {}

    Expression *append(ExpressionRef expr) {
        assert(expr);
        Expression *ptr = expr.get();
        values.push_back(std::move(expr));
        return ptr;
    }

    size_t size() const {
        return values.size();
    };

    ExpressionRef &getElement(size_t i) {
        return values[i];
    }

    const ExpressionRef &getElement(size_t i) const {
        return values[i];
    }

    const Expression *nth(int i) const {
        if (i < 0)
            i = (int)values.size() + i;
        if ((i < 0) || ((size_t)i >= values.size()))
            return NULL;
        else
            return values[i].get();
    }

    Expression *nth(int i) {
        if (i < 0)
            i = (int)values.size() + i;
        if ((i < 0) || ((size_t)i >= values.size()))
            return NULL;
        else
            return values[i].get();
    }

    static bool classof(const Expression *expr) {
        return expr->getKind() == E_List;
    }

    static ExpressionKind kind() {
        return E_List;
    }
};

//------------------------------------------------------------------------------

struct Atom : Expression {
protected:
    Atom(ExpressionKind kind, const char *s, size_t len) :
        Expression(kind),
        value(s, len)
        {}

    std::string value;
public:

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

    static bool classof(const Expression *expr) {
        auto kind = expr->getKind();
        return (kind != E_List) && (kind != E_None);
    }

    void unescape() {
        value.resize(inplace_unescape(&value[0]));
    }

    static ExpressionKind kind() {
        return E_Atom;
    }
};

//------------------------------------------------------------------------------

struct String : Atom {
    String(const char *s, size_t len) :
        Atom(E_String, s, len) {}

    static bool classof(const Expression *expr) {
        return expr->getKind() == E_String;
    }

    static ExpressionKind kind() {
        return E_String;
    }

};

//------------------------------------------------------------------------------

struct Symbol : Atom {
    Symbol(const char *s, size_t len) :
        Atom(E_Symbol, s, len) {}

    static bool classof(const Expression *expr) {
        return expr->getKind() == E_Symbol;
    }

    static ExpressionKind kind() {
        return E_Symbol;
    }

};

//------------------------------------------------------------------------------

struct Comment : Atom {
    Comment(const char *s, size_t len) :
        Atom(E_Comment, s, len) {}

    static bool classof(const Expression *expr) {
        return expr->getKind() == E_Comment;
    }

    static ExpressionKind kind() {
        return E_Comment;
    }

};

//------------------------------------------------------------------------------

bool Expression::isListComment() const {
    if (auto list = llvm::dyn_cast<List>(this)) {
        if (list->size() > 0) {
            if (auto head = llvm::dyn_cast<Symbol>(list->nth(0))) {
                if (head->getValue().substr(0, 3) == "###")
                    return true;
            }
        }
    }
    return false;
}

bool Expression::isLineComment() const {
    return llvm::isa<Comment>(this);
}

bool Expression::isComment() const {
    return isLineComment() || isListComment();
}

std::string Expression::getHeader() const {
    if (auto list = llvm::dyn_cast<List>(this)) {
        if (list->size() >= 1) {
            if (auto head = llvm::dyn_cast<Symbol>(list->nth(0))) {
                return head->getValue();
            }
        }
    }
    return "";
}

//------------------------------------------------------------------------------

std::shared_ptr<Expression> strip(std::shared_ptr<Expression> expr) {
    assert(expr);
    if (expr->isComment()) {
        return nullptr;
    } else if (expr->getKind() == E_List) {
        auto list = std::static_pointer_cast<List>(expr);
        auto result = std::make_shared<List>();
        bool changed = false;
        for (size_t i = 0; i < list->size(); ++i) {
            auto oldelem = list->getElement(i);
            auto newelem = strip(oldelem);
            if (oldelem.get() != newelem.get())
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

typedef enum {
    token_eof,
    token_open,
    token_close,
    token_string,
    token_symbol,
    token_comment,
    token_escape
} Token;

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
        vsnprintf( &error_string[0], size, format, args );
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
            } else if (isspace(c) || (c == '(') || (c == ')') || (c == '"')) {
                -- next_cursor;
                break;
            }
        }
        string = cursor;
        string_len = next_cursor - cursor;
    }

    void readString (char terminator) {
        bool escape = false;
        while (true) {
            if (next_cursor == eof) {
                error("unterminated sequence\n");
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
            } else if (c == '(') {
                token = token_open;
                break;
            } else if (c == ')') {
                token = token_close;
                break;
            } else if (c == '\\') {
                token = token_escape;
                break;
            } else if (c == '"') {
                token = token_string;
                readString(c);
                break;
            } else if (c == ';') {
                token = token_comment;
                readString('\n');
                break;
            } else {
                token = token_symbol;
                readSymbol();
                break;
            }
        }
        return token;
    }


};



//------------------------------------------------------------------------------

struct Parser {
    Lexer lexer;

    std::string error_string;

    Parser() {}

    void init() {
        error_string.clear();
    }

    void error( const char *format, ... ) {
        va_list args;
        va_start (args, format);
        size_t size = vsnprintf(nullptr, 0, format, args);
        va_end (args);
        error_string.resize(size);
        va_start (args, format);
        vsnprintf( &error_string[0], size, format, args );
        va_end (args);
    }

    ExpressionRef parseAny () {
        assert(lexer.token != token_eof);
        if (lexer.token == token_open) {
            auto result = llvm::make_unique<List>();
            lexer.initAnchor(result->anchor);
            while (true) {
                lexer.readToken();
                if (lexer.token == token_close) {
                    break;
                } else if (lexer.token == token_eof) {
                    error("missing closing parens\n");
                    return nullptr;
                } else {
                    if (auto elem = parseAny())
                        result->append(std::move(elem));
                    else
                        return nullptr;
                }
            }
            return std::move(result);
        } else if (lexer.token == token_close) {
            error("stray closing parens\n");
        } else if (lexer.token == token_string) {
            auto result = llvm::make_unique<String>(lexer.string + 1, lexer.string_len - 2);
            lexer.initAnchor(result->anchor);
            result->unescape();
            return std::move(result);
        } else if (lexer.token == token_symbol) {
            auto result = llvm::make_unique<Symbol>(lexer.string, lexer.string_len);
            lexer.initAnchor(result->anchor);
            result->unescape();
            return std::move(result);
        } else if (lexer.token == token_comment) {
            auto result = llvm::make_unique<Comment>(lexer.string + 1, lexer.string_len - 2);
            lexer.initAnchor(result->anchor);
            result->unescape();
            return std::move(result);
        } else {
            error("unexpected token: %c (%i)\n", *lexer.cursor, (int)*lexer.cursor);
        }

        return nullptr;
    }

    ExpressionRef parseNaked () {
        int lineno = lexer.lineno;
        int column = lexer.column();

        bool escape = false;
        int subcolumn = 0;

        auto result = llvm::make_unique<List>();
        lexer.initAnchor(result->anchor);

        while (lexer.token != token_eof) {
            if (lexer.token == token_escape) {
                escape = true;
                lexer.readToken();
                if (lexer.lineno <= lineno) {
                    error("escape character is not at end of line\n");
                    return nullptr;
                }
                lineno = lexer.lineno;
            } else if (lexer.lineno > lineno) {
                escape = false;
                if (subcolumn != 0) {
                    if (lexer.column() != subcolumn) {
                        error("indentation mismatch\n");
                        return nullptr;
                    }
                } else {
                    subcolumn = lexer.column();
                }
                lineno = lexer.lineno;
                if (auto elem = parseNaked())
                    result->append(std::move(elem));
                else
                    return nullptr;
            } else {
                if (auto elem = parseAny())
                    result->append(std::move(elem));
                else
                    return nullptr;
                lexer.readToken();
            }

            if ((!escape || (lexer.lineno > lineno)) && (lexer.column() <= column))
                break;
        }

        assert(result->size() > 0);
        if (result->size() == 1) {
            return std::move(result->getElement(0));
        } else {
            return std::move(result);
        }
    }

    ExpressionRef parseRoot (
        const char *input_stream, const char *eof, const char *path) {
        lexer.init(input_stream, eof, path);

        lexer.readToken();

        auto result = llvm::make_unique<List>();
        lexer.initAnchor(result->anchor);

        int lineno = lexer.lineno;
        while (lexer.token != token_eof) {

            if (lexer.token == token_escape) {
                lexer.readToken();
                if (lexer.lineno <= lineno) {
                    error("escape character is not at end of line\n");
                    return nullptr;
                }
                lineno = lexer.lineno;
            } else if (lexer.lineno > lineno) {
                lineno = lexer.lineno;
                if (auto elem = parseNaked())
                    result->append(std::move(elem));
                else
                    return nullptr;
            } else {
                if (auto elem = parseAny())
                    result->append(std::move(elem));
                else
                    return nullptr;
                lexer.readToken();
            }

        }

        if (!error_string.empty() && !lexer.error_string.empty()) {
            error("%s", lexer.error_string.c_str());
            return nullptr;
        }

        assert(result->size() > 0);
        assert(error_string.empty());

        if (result->size() == 0) {
            return nullptr;
        } else if (result->size() == 1) {
            return std::move(result->getElement(0));
        } else {
            return std::move(result);
        }
    }

    ExpressionRef parseFile (const char *path) {
        int fd = open(path, O_RDONLY);
        off_t length = lseek(fd, 0, SEEK_END);
        void *ptr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
        if (ptr != MAP_FAILED) {
            init();
            auto expr = parseRoot(
                (const char *)ptr, (const char *)ptr + length,
                path);
            if (!error_string.empty()) {
                int lineno = lexer.lineno;
                int column = lexer.column();
                printf("%i:%i:%s\n", lineno, column, error_string.c_str());
                assert(expr == NULL);
            }

            munmap(ptr, length);
            close(fd);

            if (expr)
                expr = strip(expr);
            return expr;
        } else {
            fprintf(stderr, "unable to open file: %s\n", path);
            return NULL;
        }
    }


};

//------------------------------------------------------------------------------

void printExpression(const Expression *e, size_t depth=0)
{
#define sep() for(i = 0; i < depth; i++) printf("    ")
	size_t i;
	if (!e) return;

    sep();
    printf("%s:%i:%i [%i]\n",
        e->anchor.path,
        e->anchor.lineno,
        e->anchor.column,
        e->anchor.offset);

	switch(e->getKind()) {
	case E_List: {
        const List *l = llvm::cast<List>(e);
		sep();
		puts("(");
		for (i = 0; i < l->size(); i++)
			printExpression(l->nth(i), depth + 1);
		sep();
		puts(")");
    } return;
	case E_Symbol:
	case E_String:
    case E_Comment: {
		sep();
        const Atom *a = llvm::cast<Atom>(e);
        if (a->getKind() == E_Comment) putchar(';');
		else if (a->getKind() == E_String) putchar('"');
		for (i = 0; i < a->size(); i++) {
			switch((*a)[i]) {
			case '"':
			case '\\':
				putchar('\\');
				break;
			case ')': case '(':
				if (a->getKind() == E_Symbol)
					putchar('\\');
			}

			putchar((*a)[i]);
		}
		if (a->getKind() == E_String) putchar('"');
		putchar('\n');
    } return;
    default:
        assert (false); break;
	}
#undef sep
}

//------------------------------------------------------------------------------

typedef uint64_t TypeId;

static TypeId next_type_ref = 0;

// handle around TypeIds
class Type {
protected:
    TypeId id;
public:

    struct TypeHash {
      size_t operator() (const Type &x) const {
        return std::hash<int>()(x.getId());
      }
    };

    // pretty names for types are here
    static std::unordered_map<Type, std::string, TypeHash> _pretty_name_map;
    // opaque types don't have an LLVM type
    static std::unordered_map<Type, LLVMTypeRef, TypeHash> _llvm_type_map;
    // return type for function types
    static std::unordered_map<Type, Type, TypeHash > _return_type_map;
    // varargs trait for function types
    static std::unordered_map<Type, bool, TypeHash> _varargs_map;
    // if a type has elements or parameters, their types are here
    static std::unordered_map<Type, std::vector<Type>, TypeHash > _element_type_map;
    // if a type's elements are named, their names are here
    static std::unordered_map<Type, std::vector<std::string>, TypeHash > _element_name_map;
    // if a type is an array, the array size is here
    static std::unordered_map<Type, size_t, TypeHash> _array_size_map;
    // if a type is a vector, the vector size is here
    static std::unordered_map<Type, size_t, TypeHash> _vector_size_map;

    Type() : id(0) {}
    Type(const Type &type) : id(type.id) {}
    Type(TypeId id_) : id(id_) {}

    static Type none() {
        return Type(0);
    }

    static Type create(std::string name, LLVMTypeRef llvmtype) {
        Type type(++next_type_ref);
        _pretty_name_map[type.id] = name;
        _llvm_type_map[type.id] = llvmtype;
        return type;
    }

    static Type createStruct(std::string name) {
        return Type::create(name,
            LLVMStructCreateNamed(LLVMGetGlobalContext(), name.c_str()));
    }

    static Type _pointer(Type type);
    static Type _array(Type type, size_t size);
    static Type _vector(Type type, size_t size);
    static Type _function(Type returntype, std::vector<Type> params, bool varargs);

    static std::function<Type (Type)> pointer;
    static std::function<Type (Type, size_t)> array;
    static std::function<Type (Type, size_t)> vector;
    static std::function<Type (Type, std::vector<Type>, bool)> function;

    bool operator == (const Type &other) const {
        return id == other.id;
    }

    bool operator != (const Type &other) const {
        return id != other.id;
    }

    bool operator < (const Type &other) const {
        return id < other.id;
    }

    bool isValid () const {
        return id != 0;
    }

    size_t getArraySize() const {
        return _array_size_map[*this];
    }

    size_t getVectorSize() const {
        return _vector_size_map[*this];
    }

    bool isArray() const {
        return (getArraySize() != 0);
    }

    bool isVector() const {
        return (getVectorSize() != 0);
    }

    bool isPointer() const {
        return (!isArray() && !isVector() && (_element_type_map[*this].size() == 1));
    }

    bool isFunction() const {
        return (_return_type_map[*this] != 0);
    }

    bool hasVarArgs() const {
        assert(isFunction());
        return _varargs_map[*this];
    }

    Type getReturnType() const {
        assert(isFunction());
        return _return_type_map[*this];
    }

    std::vector<Type> getElementTypes() const {
        assert(isFunction() || isArray() || isVector());
        return _element_type_map[*this];
    }

    Type getElementType() const {
        assert(isArray() || isVector() || isPointer());
        return _element_type_map[*this][0];
    }

    TypeId getId() const {
        return id;
    }

    LLVMTypeRef getLLVMType() const {
        return _llvm_type_map[*this];
    }

    std::string getString() const {
        if (!id) {
            return "<none>";
        } else {
            std::string result = _pretty_name_map[*this];
            if (!result.size())
                return string_format("<unnamed:" PRIu64 ">", id);
            return result;
        }
    }
};

std::unordered_map<Type, std::string, Type::TypeHash> Type::_pretty_name_map;
std::unordered_map<Type, LLVMTypeRef, Type::TypeHash> Type::_llvm_type_map;
std::unordered_map<Type, Type, Type::TypeHash > Type::_return_type_map;
std::unordered_map<Type, bool, Type::TypeHash> Type::_varargs_map;
std::unordered_map<Type, std::vector<Type>, Type::TypeHash > Type::_element_type_map;
std::unordered_map<Type, std::vector<std::string>, Type::TypeHash > Type::_element_name_map;
std::unordered_map<Type, size_t, Type::TypeHash> Type::_array_size_map;
std::unordered_map<Type, size_t, Type::TypeHash> Type::_vector_size_map;

std::function<Type (Type)> Type::pointer = memo(Type::_pointer);
std::function<Type (Type, size_t)> Type::array = memo(Type::_array);
std::function<Type (Type, size_t)> Type::vector = memo(Type::_vector);
std::function<Type (Type, std::vector<Type>, bool)> Type::function = memo(Type::_function);

} // namespace bang

namespace std {
    template <>
    struct hash<bang::Type> {
        public :
        size_t operator()(const bang::Type &x ) const{
            return hash<int>()(x.getId());
        }
    };
}

namespace bang {

// etc.

static Type T_void;
static Type T_opaque;
static Type T_bool;

static Type T_int8;
static Type T_int16;
static Type T_int32;
static Type T_int64;

static Type T_uint8;
static Type T_uint16;
static Type T_uint32;
static Type T_uint64;

static Type T_half;
static Type T_float;
static Type T_double;

static void setupTypes () {
    T_void = Type::create("void", LLVMVoidType());
    T_opaque = Type::create("opaque",
        LLVMStructType(NULL, 0, false));
    T_bool = Type::create("bool", LLVMInt1Type());

    T_int8 = Type::create("int8", LLVMInt8Type());
    T_int16 = Type::create("int16", LLVMInt16Type());
    T_int32 = Type::create("int32", LLVMInt32Type());
    T_int64 = Type::create("int64", LLVMInt64Type());

    T_uint8 = Type::create("uint8", LLVMInt8Type());
    T_uint16 = Type::create("uint16", LLVMInt16Type());
    T_uint32 = Type::create("uint32", LLVMInt32Type());
    T_uint64 = Type::create("uint64", LLVMInt64Type());

    T_half = Type::create("half", LLVMHalfType());
    T_float = Type::create("float", LLVMFloatType());
    T_double = Type::create("double", LLVMDoubleType());
}

Type Type::_pointer(Type type) {
    assert(type.getLLVMType());
    // cannot reference void
    assert(type != T_void);
    Type ptr = Type::create(string_format("(pointer-type %s)",
        type.getString().c_str()),
        LLVMPointerType(type.getLLVMType(), 0) );
    std::vector<Type> etypes;
    etypes.push_back(type.id);
    _element_type_map[ptr] = etypes;
    return ptr;
}

Type Type::_array(Type type, size_t size) {
    assert(type.getLLVMType());
    assert(type != T_void);
    assert(size >= 1);
    Type arraytype = Type::create(string_format("(array-type %s %zu)",
        type.getString().c_str(), size),
        LLVMArrayType(type.getLLVMType(), size) );
    std::vector<Type> etypes;
    etypes.push_back(type.id);
    _element_type_map[arraytype] = etypes;
    _array_size_map[arraytype] = size;
    return arraytype;
}

Type Type::_vector(Type type, size_t size) {
    assert(type.getLLVMType());
    assert(type != T_void);
    assert(size >= 1);
    Type vectortype = Type::create(string_format("(vector-type %s %zu)",
        type.getString().c_str(), size),
        LLVMVectorType(type.getLLVMType(), size) );
    std::vector<Type> etypes;
    etypes.push_back(type.id);
    _element_type_map[vectortype] = etypes;
    _vector_size_map[vectortype] = size;
    return vectortype;
}

Type Type::_function(Type returntype, std::vector<Type> params, bool varargs) {
    assert(returntype.getLLVMType());
    std::stringstream ss;
    std::vector<LLVMTypeRef> llvmparamtypes;
    ss << "(function-type " << returntype.getString() << " (";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i != 0)
            ss << " ";
        ss << params[i].getString();
        assert(params[i] != T_void);
        LLVMTypeRef llvmtype = params[i].getLLVMType();
        assert(llvmtype);
        llvmparamtypes.push_back(llvmtype);
    }
    if (varargs)
        ss << " ...";
    ss << "))";

    Type functype = Type::create(ss.str(),
        LLVMFunctionType(returntype.getLLVMType(),
            &llvmparamtypes[0], llvmparamtypes.size(), varargs));
    _element_type_map[functype] = params;
    _return_type_map[functype] = returntype;
    _varargs_map[functype] = varargs;
    assert (functype.isFunction());
    return functype;
}

//------------------------------------------------------------------------------

class TypedValue {
protected:
    Type type;
    LLVMValueRef value;
public:

    TypedValue() :
        type(Type::none()),
        value(NULL)
        {}

    TypedValue(Type type_) :
        type(type_),
        value(NULL)
        {}

    TypedValue(Type type_, LLVMValueRef value_) :
        type(type_),
        value(value_) {
        assert(!value_ || type.isValid());
        }

    Type getType() const { return type; }
    LLVMValueRef getValue() const { return value; }
    LLVMTypeRef getLLVMType() const { return type.getLLVMType(); }

    operator bool () const { return (value != NULL) || (type != Type::none()); }
};

//------------------------------------------------------------------------------

typedef std::map<std::string, TypedValue> NameValueMap;
typedef std::map<std::string, LLVMModuleRef> NameModuleMap;
typedef std::map<std::string, Type> NameTypeMap;

//------------------------------------------------------------------------------

struct Environment;

struct TranslationGlobals {
    int compile_errors;
    bool dump_module;
    LLVMValueRef nopfunc;
    // execution engine for this translation
    LLVMExecutionEngineRef engine;
    // module for this translation
    LLVMModuleRef module;
    // builder for this translation
    LLVMBuilderRef builder;
    // meta env; only valid for proto environments
    const Environment *meta;

    TranslationGlobals() :
        compile_errors(0),
        dump_module(false),
        nopfunc(NULL),
        engine(NULL),
        module(NULL),
        builder(NULL),
        meta(NULL)
        {}
};

//------------------------------------------------------------------------------

struct Environment {
    TranslationGlobals *globals;
    // currently active function
    LLVMValueRef function;
    // currently evaluated expression
    const Expression *expr;
    // type of active function
    Type function_type;
    // local names
    NameValueMap names;
    // parent env
    Environment *parent;

    struct WithExpression {
        const Expression *prevexpr;
        Environment *env;

        WithExpression(Environment *env_, const Expression *expr_) :
            prevexpr(env_->expr),
            env(env_) {
            if (expr_)
                env_->expr = expr_;
        }
        ~WithExpression() {
            env->expr = prevexpr;
        }
    };

    Environment() :
        globals(NULL),
        function(NULL),
        expr(NULL),
        parent(NULL)
        {}

    Environment(Environment *parent_) :
        globals(parent_->globals),
        function(parent_->function),
        expr(parent_->expr),
        parent(parent_)
        {}

    WithExpression with_expr(const Expression *expr) {
        return WithExpression(this, expr);
    }

    bool hasErrors() const {
        return globals->compile_errors != 0;
    };

    LLVMBuilderRef getBuilder() const {
        return globals->builder;
    }

};

//------------------------------------------------------------------------------

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
            if(!fieldtype.isValid()) {
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

            if (!structtype.isValid()) {
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
            return Type::none();
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
                return T_void;
            } break;
            case clang::BuiltinType::Bool: {
                return T_bool;
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
                        return T_uint8;
                    else if (sz == 16)
                        return T_uint16;
                    else if (sz == 32)
                        return T_uint32;
                    else if (sz == 64)
                        return T_uint64;
                } else {
                    if (sz == 8)
                        return T_int8;
                    else if (sz == 16)
                        return T_int16;
                    else if (sz == 32)
                        return T_int32;
                    else if (sz == 64)
                        return T_int64;
                }
            } break;
            case clang::BuiltinType::Half: {
                return T_half;
            } break;
            case clang::BuiltinType::Float: {
                return T_float;
            } break;
            case clang::BuiltinType::Double: {
                return T_double;
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
            if (pointee.isValid()) {
                if (pointee == T_void)
                    pointee = T_opaque;
                return Type::pointer(pointee);
            }
        } break;
        case clang::Type::VariableArray:
        case clang::Type::IncompleteArray:
            break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            Type at = TranslateType(ATy->getElementType());
            if(at.isValid()) {
                int sz = ATy->getSize().getZExtValue();
                return Type::array(at, sz);
            }
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
                const VectorType *VT = cast<VectorType>(T);
                Type at = TranslateType(VT->getElementType());
                if(at.isValid()) {
                    int n = VT->getNumElements();
                    return Type::vector(at, n);
                }
        } break;
        case clang::Type::FunctionNoProto: /* fallthrough */
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
            return T_int32;
        } break;
        case clang::Type::BlockPointer:
        case clang::Type::MemberPointer:
        case clang::Type::Atomic:
        default:
            break;
        }
        fprintf(stderr, "type not understood: %s (%i)\n", T.getAsString().c_str(), Ty->getTypeClass());
        /*
        std::stringstream ss;
        ss << "type not understood: " << T.getAsString().c_str() << " " << Ty->getTypeClass();
        return ImportError(ss.str().c_str());
        */
        // TODO: print error
        return Type::none();
    }

    Type TranslateFuncType(const clang::FunctionType * f) {

        bool valid = true; // decisions about whether this function can be exported or not are delayed until we have seen all the potential problems
        clang::QualType RT = f->getReturnType();

        Type returntype = TranslateType(RT);

        if (!returntype.isValid())
            valid = false;

        const clang::FunctionProtoType * proto = f->getAs<clang::FunctionProtoType>();
        std::vector<Type> argtypes;
        //proto is null if the function was declared without an argument list (e.g. void foo() and not void foo(void))
        //we don't support old-style C parameter lists, we just treat them as empty
        if(proto) {
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                clang::QualType PT = proto->getParamType(i);
                Type paramtype = TranslateType(PT);
                if(!paramtype.isValid()) {
                    valid = false; //keep going with attempting to parse type to make sure we see all the reasons why we cannot support this function
                } else if(valid) {
                    argtypes.push_back(paramtype);
                }
            }
        }

        if(valid) {
            return Type::function(returntype, argtypes, proto ? proto->isVariadic() : false);
        }

        return Type::none();
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

        /*
        //Obj typ;
        if(!GetFuncType(fntyp,&typ)) {
            SetErrorReport(FuncName.c_str());
            return true;
        }
        */
        Type functype = TranslateFuncType(fntyp);
        if (!functype.isValid())
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

        env->names[FuncName] = TypedValue(Type::pointer(functype),
            LLVMAddFunction(env->globals->module, InternalName.c_str(), functype.getLLVMType()));

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

    /*
    // Infer the builtin include path if unspecified.
    if (compiler.getHeaderSearchOpts().UseBuiltinIncludes &&
        compiler.getHeaderSearchOpts().ResourceDir.empty())
        compiler.getHeaderSearchOpts().ResourceDir =
            CompilerInvocation::GetResourcesPath(bang_argv[0], MainAddr);
    */

    LLVMModuleRef M = NULL;

    // Create and execute the frontend to generate an LLVM bitcode module.
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction(env));
    if (compiler.ExecuteAction(*Act)) {
        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        //LLVMDumpModule(M);
        LLVMAddModule(env->globals->engine, M);
    } else {
        translateError(env, "compiler failed\n");
    }

    return M;
}

//------------------------------------------------------------------------------

static const char *expressionKindName(int kind) {
    switch(kind) {
    case E_None: return "?";
    case E_List: return "list";
    case E_String: return "string";
    case E_Symbol: return "symbol";
    case E_Comment: return "comment";
    case E_Atom: return "string or symbol";
    default: return "<corrupted>";
    }
}

static void translateError (Environment *env, const char *format, ...) {
    ++env->globals->compile_errors;
    if (env->expr) {
        Anchor anchor = env->expr->anchor;
        printf("%s:%i:%i: error: ", anchor.path, anchor.lineno, anchor.column);
    } else {
        printf("error: ");
    }
    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}

static bool isSymbol (const Expression *expr, const char *sym) {
    if (expr) {
        if (auto symexpr = llvm::dyn_cast<Symbol>(expr))
            return (symexpr->getValue() == sym);
    }
    return false;
}

/*
() = nop
(function-type returntype ([argtype [...]] [\...]))
(function name type [value])
(extern name type)
(call name value ...)
(bitcast type value)
(extract value indexvalue)
(const-int <value> [<type>])
(const-real <value> [<type>])
(array-ref <value>)
(pointer-type type)
(typeof value)
(dump value)
(? if-expr then-block else-block)
(do expr ...)
(dump-module)
(import-c <filename> (<compiler-arg> ...))
(proto-eval expr ...)
*/

static TypedValue translate (Environment *env, const Expression *expr);

template<typename T>
static const T *translateKind(Environment *env, const Expression *expr) {
    if (expr) {
        auto _ = env->with_expr(expr);
        const T *co = llvm::dyn_cast<T>(expr);
        if (co) {
            return co;
        } else {
            translateError(env, "%s expected, not %s\n",
                expressionKindName(T::kind()),
                expressionKindName(expr->getKind()));
        }
    }
    return nullptr;
}

static const char *translateString (Environment *env, const Expression *expr) {
    if (expr) {
        if (auto str = translateKind<Atom>(env, expr))
            return str->c_str();
    }
    return nullptr;
}

static Type translateType (Environment *env, const Expression *expr) {
    if (expr) {
        auto _ = env->with_expr(expr);
        TypedValue result = translate(env, expr);
        if (!result.getType().isValid() && result.getValue()) {
            translateError(env, "type expected, not value\n");
        }
        return result.getType();
    }
    return Type::none();
}

static TypedValue translateValue (Environment *env, const Expression *expr) {
    if (expr) {
        auto _ = env->with_expr(expr);
        TypedValue result = translate(env, expr);
        if (!result.getValue() && result.getType().isValid()) {
            translateError(env, "value expected, not type\n");
        }
        return result;
    }
    return TypedValue();
}

static bool verifyParameterCount (Environment *env, const List *expr, int mincount, int maxcount) {
    if (expr) {
        auto _ = env->with_expr(expr);
        int argcount = (int)expr->size() - 1;
        if ((mincount >= 0) && (argcount < mincount)) {
            translateError(env, "at least %i arguments expected\n", mincount);
            return false;
        }
        if ((maxcount >= 0) && (argcount > maxcount)) {
            translateError(env, "at most %i arguments expected\n", maxcount);
            return false;
        }
        return true;
    }
    return false;
}

static bool matchSpecialForm (Environment *env, const List *expr, const char *name, int mincount, int maxcount) {
    return isSymbol(expr->nth(0), name) && verifyParameterCount(env, expr, mincount, maxcount);
}

static TypedValue nopcall (Environment *env) {
    return TypedValue(T_void, LLVMBuildCall(env->getBuilder(), env->globals->nopfunc, NULL, 0, ""));
}

static TypedValue translateExpressionList (Environment *env, const List *expr, int offset) {
    int argcount = (int)expr->size() - offset;
    TypedValue stmt;
    for (int i = 0; i < argcount; ++i) {
        stmt = translateValue(env, expr->nth(i + offset));
        if (!stmt.getValue() || env->hasErrors()) {
            stmt = TypedValue();
            break;
        }
    }

    return stmt;
}

static void compileAndRun (Environment *env, const char *modulename, const Expression *expr);

static TypedValue translateList (Environment *env, const List *expr) {
    TypedValue result;

    if (expr->size() >= 1) {
        if (auto head = llvm::dyn_cast<Symbol>(expr->nth(0))) {
            if (matchSpecialForm(env, expr, "function-type", 2, 2)) {
                if (auto args_expr = translateKind<List>(env, expr->nth(2))) {
                    const Expression *tail = args_expr->nth(-1);
                    bool vararg = false;
                    int argcount = (int)args_expr->size();
                    if (isSymbol(tail, "...")) {
                        vararg = true;
                        --argcount;
                    }

                    if (argcount >= 0) {
                        Type rettype = translateType(env, expr->nth(1));
                        if (rettype.isValid()) {
                            std::vector<Type> argtypes;

                            bool success = true;
                            for (int i = 0; i < argcount; ++i) {
                                Type argtype = translateType(env, args_expr->nth(i));
                                if (!argtype.isValid()) {
                                    success = false;
                                    break;
                                }
                                argtypes.push_back(argtype);
                            }

                            if (success) {
                                result = TypedValue(Type::function(rettype, argtypes, vararg));
                            }
                        }
                    } else {
                        translateError(env, "vararg function is missing return type\n");
                    }
                }
            /*
            } else if (matchSpecialForm(env, expr, "bitcast", 2, 2)) {

                Type casttype = translateType(env, expr->nth(1));
                LLVMValueRef castvalue = translateValue(env, expr->nth(2));

                if (casttype && castvalue) {
                    result = TypedValue(casttype,
                        LLVMBuildBitCast(env->getBuilder(), castvalue, casttype.getLLVMType(), "ptrcast"));
                }

            } else if (matchSpecialForm(env, expr, "extract", 2, 2)) {

                LLVMValueRef value = translateValue(env, expr->nth(1));
                LLVMValueRef index = translateValue(env, expr->nth(2));

                if (value && index) {
                    result = TypedValue(

                        LLVMBuildExtractElement(env->getBuilder(), value, index, "extractelem"));
                }
            */
            } else if (matchSpecialForm(env, expr, "const-int", 2, 2)) {
                const Expression *expr_type = expr->nth(1);
                const Symbol *expr_value = translateKind<Symbol>(env, expr->nth(2));

                Type type;
                if (expr_type) {
                    type = translateType(env, expr_type);
                } else {
                    type = T_int32;
                }

                if (type.isValid() && expr_value) {
                    auto _ = env->with_expr(expr_value);
                    char *end;
                    long long value = strtoll(expr_value->c_str(), &end, 10);
                    if (end != (expr_value->c_str() + expr_value->size())) {
                        translateError(env, "not a valid integer constant\n");
                    } else {
                        result = TypedValue(type,
                            LLVMConstInt(type.getLLVMType(), value, 1));
                    }
                }

            } else if (matchSpecialForm(env, expr, "const-real", 2, 2)) {
                const Expression *expr_type = expr->nth(1);
                const Symbol *expr_value = translateKind<Symbol>(env, expr->nth(2));

                Type type;
                if (expr_type) {
                    type = translateType(env, expr_type);
                } else {
                    type = T_double;
                }

                if (type.isValid() && expr_value) {
                    auto _ = env->with_expr(expr_value);
                    char *end;
                    double value = strtod(expr_value->c_str(), &end);
                    if (end != (expr_value->c_str() + expr_value->size())) {
                        translateError(env, "not a valid real constant\n");
                    } else {
                        result = TypedValue(type,
                            LLVMConstReal(type.getLLVMType(), value));
                    }
                }

            } else if (matchSpecialForm(env, expr, "typeof", 1, 1)) {

                TypedValue tmpresult = translate(env, expr->nth(1));

                result = TypedValue(tmpresult.getType());

            } else if (matchSpecialForm(env, expr, "dump-module", 0, 0)) {

                env->globals->dump_module = true;

            } else if (matchSpecialForm(env, expr, "dump", 1, 1)) {

                const Expression *expr_arg = expr->nth(1);

                TypedValue tov = translate(env, expr_arg);
                if (tov.getType().isValid()) {
                    printf("type: %s\n", tov.getType().getString().c_str());
                    LLVMDumpType(tov.getLLVMType());
                }
                if (tov.getValue()) {
                    printf("value:\n");
                    LLVMDumpValue(tov.getValue());
                }
                if (!tov) {
                    printf("no expression or type\n");
                }

                result = tov;

            } else if (matchSpecialForm(env, expr, "array-ref", 1, 1)) {
                const Expression *expr_array = expr->nth(1);
                TypedValue ptr = translateValue(env, expr_array);
                if (ptr) {
                    auto _ = env->with_expr(expr_array);

                    Type etype = ptr.getType();
                    if (etype.isArray()) {
                        etype = Type::pointer(etype.getElementType());

                        LLVMValueRef indices[] = {
                            LLVMConstInt(LLVMInt32Type(), 0, 1),
                            LLVMConstInt(LLVMInt32Type(), 0, 1)
                        };

                        result = TypedValue(etype,
                            LLVMBuildGEP(env->getBuilder(), ptr.getValue(), indices, 2, "gep"));
                    } else {
                        translateError(env, "array value expected");
                    }
                }

            } else if (matchSpecialForm(env, expr, "?", 3, 3)) {

                TypedValue cond_value = translateValue(env, expr->nth(1));
                if (cond_value) {
                    LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(env->getBuilder());

                    LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(env->function, "then");
                    LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(env->function, "else");
                    LLVMBasicBlockRef br_block = LLVMAppendBasicBlock(env->function, "br");

                    const Expression *then_expr = expr->nth(2);
                    const Expression *else_expr = expr->nth(3);

                    LLVMPositionBuilderAtEnd(env->getBuilder(), then_block);
                    TypedValue then_result = translateValue(env, then_expr);
                    LLVMBuildBr(env->getBuilder(), br_block);

                    LLVMPositionBuilderAtEnd(env->getBuilder(), else_block);
                    TypedValue else_result = translateValue(env, else_expr);
                    LLVMBuildBr(env->getBuilder(), br_block);

                    Type then_type = then_result.getType();
                    Type else_type = else_result.getType();

                    auto _ = env->with_expr(then_expr);

                    if ((then_type == T_void) || (else_type == T_void)) {
                        LLVMPositionBuilderAtEnd(env->getBuilder(), br_block);
                        result = nopcall(env);
                    } else if (then_type == else_type) {
                        LLVMPositionBuilderAtEnd(env->getBuilder(), br_block);
                        result = TypedValue(then_type, LLVMBuildPhi(env->getBuilder(), then_type.getLLVMType(), "select"));
                        LLVMValueRef values[] = { then_result.getValue(), else_result.getValue() };
                        LLVMBasicBlockRef blocks[] = { then_block, else_block };
                        LLVMAddIncoming(result.getValue(), values, blocks, 2);
                    } else {
                        translateError(env, "then/else type evaluation mismatch\n");
                        translateError(env, "then-expression must evaluate to same type as else-expression\n");
                    }

                    LLVMPositionBuilderAtEnd(env->getBuilder(), oldblock);
                    LLVMBuildCondBr(env->getBuilder(), cond_value.getValue(), then_block, else_block);

                    LLVMPositionBuilderAtEnd(env->getBuilder(), br_block);
                }

            } else if (matchSpecialForm(env, expr, "do", 1, -1)) {

                Environment subenv(env);

                result = translateExpressionList(&subenv, expr, 1);

            } else if (matchSpecialForm(env, expr, "proto-eval", 1, -1)) {

                Environment proto_env;
                TranslationGlobals globals;
                globals.meta = env;

                proto_env.globals = &globals;

                compileAndRun(&proto_env, "proto", expr);

                env->globals->compile_errors += globals.compile_errors;

                if (!env->hasErrors()) {
                    result = TypedValue(T_void);
                }

            } else if (matchSpecialForm(env, expr, "function", 3, -1)) {

                const Expression *expr_type = expr->nth(3);

                const Symbol *expr_name = translateKind<Symbol>(env, expr->nth(1));
                Type functype = translateType(env, expr_type);

                if (expr_name && functype.isValid()) {
                    auto _ = env->with_expr(expr_type);

                    if (functype.isFunction()) {
                        const char *name = expr_name->c_str();

                        // todo: external linkage?
                        LLVMValueRef func = LLVMAddFunction(env->globals->module, name, functype.getLLVMType());

                        const Expression *expr_params = expr->nth(2);
                        const Expression *body_expr = expr->nth(4);

                        Environment subenv(env);
                        subenv.function = func;
                        subenv.function_type = functype;

                        {
                            auto _ = env->with_expr(expr_params);

                            if (isSymbol(expr_params, "...")) {
                                if (body_expr) {
                                    translateError(env, "cannot declare function body without parameter list\n");
                                }
                            } else if (auto list = llvm::dyn_cast<List>(expr_params)) {
                                std::vector<Type> etypes = functype.getElementTypes();

                                int argcount = (int)list->size();
                                int paramcount = LLVMCountParams(func);
                                if (argcount == paramcount) {
                                    LLVMValueRef params[paramcount];
                                    LLVMGetParams(func, params);
                                    for (int i = 0; i < argcount; ++i) {
                                        const Symbol *expr_param = translateKind<Symbol>(env, list->nth(i));
                                        if (expr_param) {
                                            const char *name = expr_param->c_str();
                                            LLVMSetValueName(params[i], name);
                                            subenv.names[name] = TypedValue(etypes[i], params[i]);
                                        }
                                    }
                                } else {
                                    translateError(env, "parameter name count mismatch (%i != %i); must name all parameter types\n",
                                        argcount, paramcount);
                                }
                            } else {
                                translateError(env, "parameter list or ... expected\n");
                            }
                        }

                        if (!env->hasErrors()) {
                            result = TypedValue(Type::pointer(functype), func);
                            env->names[name] = result;

                            if (body_expr) {
                                auto _ = env->with_expr(body_expr);

                                LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(env->getBuilder());

                                LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
                                LLVMPositionBuilderAtEnd(env->getBuilder(), entry);

                                TypedValue bodyvalue = translateExpressionList(&subenv, expr, 4);

                                if (!env->hasErrors()) {

                                    if (functype.getReturnType() == T_void) {
                                        LLVMBuildRetVoid(env->getBuilder());
                                    } else if (bodyvalue.getValue()) {
                                        LLVMBuildRet(env->getBuilder(), bodyvalue.getValue());
                                    } else {
                                        translateError(env, "function returns no value\n");
                                    }

                                    LLVMPositionBuilderAtEnd(env->getBuilder(), oldblock);

                                }

                            }
                        }
                    } else {
                        translateError(env, "not a function type: %s\n",
                            functype.getString().c_str());
                    }
                }

            } else if (matchSpecialForm(env, expr, "extern", 2, 2)) {

                const Expression *expr_type = expr->nth(2);

                const Symbol *expr_name = translateKind<Symbol>(env, expr->nth(1));

                Type functype = translateType(env, expr_type);

                auto _ = env->with_expr(expr_type);

                if (expr_name && functype.isValid()) {
                    if (functype.isFunction()) {
                        const char *name = (const char *)expr_name->c_str();
                        // todo: external linkage?
                        LLVMValueRef func = LLVMAddFunction(env->globals->module,
                            name, functype.getLLVMType());

                        result = TypedValue(Type::pointer(functype), func);
                        env->names[name] = result;
                    } else {
                        translateError(env, "not a function type: %s\n",
                            functype.getString().c_str());
                    }
                }

            } else if (matchSpecialForm(env, expr, "call", 1, -1)) {

                int argcount = (int)expr->size() - 2;

                const Expression *expr_func = expr->nth(1);
                TypedValue callee = translateValue(env, expr_func);

                if (callee && callee.getType().isPointer()) {
                    Type functype = callee.getType().getElementType();

                    if (functype.isFunction()) {
                        std::vector<Type> params = functype.getElementTypes();
                        unsigned arg_size = params.size();

                        int isvararg = functype.hasVarArgs();

                        if ((isvararg && (arg_size <= (unsigned)argcount))
                            || (arg_size == (unsigned)argcount)) {

                            LLVMValueRef args[argcount];
                            bool success = true;
                            for (int i = 0; i < argcount; ++i) {
                                TypedValue value = translateValue(env, expr->nth(i + 2));
                                if (!value) {
                                    success = false;
                                    break;
                                }
                                args[i] = value.getValue();
                            }

                            if (success) {
                                Type returntype = functype.getReturnType();
                                result = TypedValue(returntype,
                                    LLVMBuildCall(env->getBuilder(), callee.getValue(), args, argcount, (returntype == T_void)?"":"calltmp"));
                            }
                        } else {
                            translateError(env, "incorrect number of call arguments (got %i, need %s%i)\n",
                                argcount, isvararg?"at least ":"", arg_size);
                        }
                    } else {
                        auto _ = env->with_expr(expr_func);
                        translateError(env, "cannot call object\n");
                    }
                }

            } else if (matchSpecialForm(env, expr, "import-c", 3, 3)) {
                const char *modulename = translateString(env, expr->nth(1));
                const char *name = translateString(env, expr->nth(2));
                const List *args_expr = translateKind<List>(env, expr->nth(3));

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

            } else if (matchSpecialForm(env, expr, "pointer-type", 1, 1)) {

                Type type = translateType(env, expr->nth(1));

                if (type.isValid()) {
                    result = TypedValue(Type::pointer(type));
                }
            } else {
                auto _ = env->with_expr(head);
                translateError(env, "unhandled special form: %s\n", head->c_str());
            }
        } else {
            auto _ = env->with_expr(expr->nth(0));
            translateError(env, "first element of expression must be symbol, not %s\n",
                expressionKindName(expr->nth(0)->getKind()));
        }
    } else {
        result = nopcall(env);
    }

    return result;
}

static TypedValue translate (Environment *env, const Expression *expr) {
    TypedValue result;

    if (expr) {
        auto _ = env->with_expr(expr);

        if (auto list = llvm::dyn_cast<List>(expr)) {
            return translateList(env, list);
        } else if (auto sym = llvm::dyn_cast<Symbol>(expr)) {
            Environment *penv = (Environment *)env;
            while (penv) {
                result = (*penv).names[sym->getValue()];
                if (result) {
                    break;
                }
                penv = penv->parent;
            }

            if (!result) {
                translateError(env, "no such name: %s\n", sym->c_str());
            }

        } else if (auto str = llvm::dyn_cast<String>(expr)) {
            result = TypedValue(
                Type::array(T_int8, str->size() + 1),
                LLVMBuildGlobalString(env->getBuilder(), str->c_str(), "str"));

        } else {
            translateError(env, "unexpected %s\n",
                expressionKindName(expr->getKind()));
        }
    }

    return result;
}

static void init() {
    setupTypes();

    LLVMEnablePrettyStackTrace();
    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeDisassembler();
}

static void compileAndRun (Environment *env, const char *modulename, const Expression *expr) {
    LLVMModuleRef module = LLVMModuleCreateWithName(modulename);

    char *error = NULL;
    LLVMExecutionEngineRef engine;
    int result = LLVMCreateExecutionEngineForModule(
        &engine, module, &error);

    if (error) {
        fprintf(stderr, "error: %s\n", error);
        LLVMDisposeMessage(error);
        exit(EXIT_FAILURE);
    }

    if (result != 0) {
        fprintf(stderr, "failed to create execution engine\n");
        abort();
    }

    LLVMBuilderRef builder = LLVMCreateBuilder();

    env->globals->engine = engine;
    env->globals->module = module;
    env->globals->builder = builder;

    env->names["void"] = T_void;
    env->names["half"] = T_half;
    env->names["float"] = T_float;
    env->names["double"] = T_double;
    env->names["bool"] = T_bool;
    env->names["int8"] = T_int8;
    env->names["int16"] = T_int16;
    env->names["int32"] = T_int32;
    env->names["int64"] = T_int64;
    env->names["uint8"] = T_uint8;
    env->names["uint16"] = T_uint16;
    env->names["uint32"] = T_uint32;
    env->names["uint64"] = T_uint64;
    env->names["opaque"] = T_opaque;

    Type entryfunctype = Type::function(T_void, std::vector<Type>(), false);

    env->globals->nopfunc = LLVMAddFunction(module, "__nop", entryfunctype.getLLVMType());
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(env->globals->nopfunc, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMBuildRetVoid(builder);

    LLVMValueRef entryfunc = LLVMAddFunction(module, "", entryfunctype.getLLVMType());

    env->function = entryfunc;
    env->function_type = entryfunctype;

    {
        auto _ = env->with_expr(expr);
        if (auto list = llvm::dyn_cast<List>(expr)) {
            if (list->size() >= 1) {
                LLVMBasicBlockRef entry = LLVMAppendBasicBlock(entryfunc, "entry");
                LLVMPositionBuilderAtEnd(builder, entry);

                for (size_t i = 1; i != list->size(); ++i) {
                    const Expression *stmt = list->nth((int)i);
                    translate(env, stmt);
                    if (env->hasErrors())
                        break;
                }
            } else {
                translateError(env, "expression is empty\n");
            }
        } else {
            translateError(env, "unexpected %s\n",
                expressionKindName(expr->getKind()));
        }
    }

    if (!env->hasErrors()) {
        LLVMBuildRetVoid(builder);

        if (env->globals->dump_module) {
            LLVMDumpModule(module);
            printf("\n\noutput:\n");
        }

        error = NULL;
        LLVMVerifyModule(module, LLVMAbortProcessAction, &error);
        LLVMDisposeMessage(error);

        typedef void (*MainFunctionType)();

        MainFunctionType fn = (MainFunctionType)
            LLVMGetPointerToGlobal(engine, entryfunc);
            //LLVMRecompileAndRelinkFunction(engine, entryfunc);
        assert(fn);

        fn();

        //LLVMRunFunction(engine, entryfunc, 0, NULL);

    }

    LLVMDisposeBuilder(builder);
}

static void compileMain (const Expression *expr) {
    Environment env;
    TranslationGlobals globals;

    env.globals = &globals;

    {
        auto _ = env.with_expr(expr);

        std::string header = expr->getHeader();
        if (header != "bang") {
            translateError(&env, "unrecognized header: '%s'; try 'bang' instead.\n", header.c_str());
            return;
        }
    }

    compileAndRun(&env, "main", expr);

}

} // namespace bang

// C API
//------------------------------------------------------------------------------

const char *return_test_string () {
    return "hi!";
}

int bang_main(int argc, char ** argv) {
    bang::init();

    int result = 0;

    if (argv && argv[1]) {
        bang::Parser parser;
        auto expr = parser.parseFile(argv[1]);
        if (expr) {
            bang::compileMain(expr.get());
        } else {
            result = 1;
        }
    }

    return result;
}

//------------------------------------------------------------------------------

#endif // BANG_CPP_IMPL
#ifdef BANG_MAIN_CPP_IMPL
int main(int argc, char ** argv) {
    return bang_main(argc, argv);
}
#endif
