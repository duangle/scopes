#ifndef BANG0_CPP
#define BANG0_CPP

#include <sys/types.h>

//------------------------------------------------------------------------------

#if defined __cplusplus
extern "C" {
#endif


#if defined __cplusplus
}
#endif

#endif // BANG0_CPP
#ifndef BANG_HEADER_ONLY

//------------------------------------------------------------------------------

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

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

//------------------------------------------------------------------------------

typedef enum {
    E_None,
    E_List,
    E_String,
    E_Symbol,
    E_Comment
} ExpressionKind;

typedef struct {
    const char *path;
    int lineno;
    int column;
    int offset;
} Anchor;

typedef struct {
    int type;
    size_t len;
    void *buf;
    Anchor anchor;
} Expression;

Expression *parseFile (const char *path);

//------------------------------------------------------------------------------

void initList(Expression *expr) {
    expr->type = E_List;
    expr->len = 0;
    expr->buf = NULL;
}

void initBlob(Expression *expr, const char *s, size_t len) {
    expr->len = len;
    expr->buf = malloc(len + 1);
    memcpy(expr->buf, s, len);
    ((char*)expr->buf)[len] = 0;
}

void initString(Expression *expr, const char *s, size_t len) {
    expr->type = E_String;
    initBlob(expr, s, len);
}

void initSymbol(Expression *expr, const char *s, size_t len) {
    expr->type = E_Symbol;
    initBlob(expr, s, len);
}

void initComment(Expression *expr, const char *s, size_t len) {
    expr->type = E_Comment;
    initBlob(expr, s, len);
}

Expression *append(Expression *expr) {
    assert(expr->type == E_List);
    expr->buf = realloc(expr->buf, sizeof(Expression) * (++expr->len));
    return (Expression *)expr->buf + (expr->len - 1);
}

Expression *nth(Expression *expr, int i) {
    assert(expr->type == E_List);
    if (i < 0)
        i = expr->len + i;
    if ((i < 0) || ((size_t)i >= expr->len))
        return NULL;
    else
        return (Expression *)expr->buf + i;
}

void clearExpression(Expression *expr) {
    free(expr->buf);
    expr->type = E_None;
    expr->len = 0;
    expr->buf = NULL;
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

typedef struct {
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

    char *error;
} Lexer;

static void lexerInit (Lexer *lexer,
    const char *input_stream, const char *eof, const char *path) {
    if (eof == NULL) {
        eof = input_stream + strlen(input_stream);
    }

    lexer->path = path;
    lexer->input_stream = input_stream;
    lexer->eof = eof;
    lexer->next_cursor = input_stream;
    lexer->next_lineno = 1;
    lexer->next_line = input_stream;
    lexer->error = NULL;
}

static void lexerFree (Lexer *lexer) {
    free(lexer->error);
    lexer->error = NULL;
}

static int lexerColumn (Lexer *lexer) {
    return lexer->cursor - lexer->line + 1;
}

static void lexerInitAnchor(Lexer *lexer, Anchor *anchor) {
    anchor->path = lexer->path;
    anchor->lineno = lexer->lineno;
    anchor->column = lexerColumn(lexer);
    anchor->offset = lexer->cursor - lexer->input_stream;
}

static void lexerError (Lexer *lexer, const char *format, ...) {
    if (lexer->error == NULL) {
        lexer->error = (char *)malloc(1024);
    }
    va_list args;
    va_start (args, format);
    vsnprintf (lexer->error, 1024, format, args);
    va_end (args);
    lexer->token = token_eof;
}

static void lexerSymbol (Lexer *lexer) {
    bool escape = false;
    while (true) {
        if (lexer->next_cursor == lexer->eof) {
            break;
        }
        char c = *lexer->next_cursor++;
        if (escape) {
            if (c == '\n') {
                ++lexer->next_lineno;
                lexer->next_line = lexer->next_cursor;
            }
            // ignore character
            escape = false;
        } else if (c == '\\') {
            // escape
            escape = true;
        } else if (isspace(c) || (c == '(') || (c == ')') || (c == '"')) {
            -- lexer->next_cursor;
            break;
        }
    }
    lexer->string = lexer->cursor;
    lexer->string_len = lexer->next_cursor - lexer->cursor;
}

static void lexerString (Lexer *lexer, char terminator) {
    bool escape = false;
    while (true) {
        if (lexer->next_cursor == lexer->eof) {
            lexerError(lexer, "unterminated sequence");
            break;
        }
        char c = *lexer->next_cursor++;
        if (c == '\n') {
            ++lexer->next_lineno;
            lexer->next_line = lexer->next_cursor;
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
    lexer->string = lexer->cursor;
    lexer->string_len = lexer->next_cursor - lexer->cursor;
}

static int lexerNextToken (Lexer *lexer) {
    lexer->lineno = lexer->next_lineno;
    lexer->line = lexer->next_line;
    lexer->cursor = lexer->next_cursor;
    while (true) {
        if (lexer->next_cursor == lexer->eof) {
            lexer->token = token_eof;
            break;
        }
        char c = *lexer->next_cursor++;
        if (c == '\n') {
            ++lexer->next_lineno;
            lexer->next_line = lexer->next_cursor;
        }
        if (isspace(c)) {
            lexer->lineno = lexer->next_lineno;
            lexer->line = lexer->next_line;
            lexer->cursor = lexer->next_cursor;
        } else if (c == '(') {
            lexer->token = token_open;
            break;
        } else if (c == ')') {
            lexer->token = token_close;
            break;
        } else if (c == '\\') {
            lexer->token = token_escape;
            break;
        } else if (c == '"') {
            lexer->token = token_string;
            lexerString(lexer, c);
            break;
        } else if (c == ';') {
            lexer->token = token_comment;
            lexerString(lexer, '\n');
            break;
        } else {
            lexer->token = token_symbol;
            lexerSymbol(lexer);
            break;
        }
    }
    return lexer->token;
}

/*
static void lexerTest (Lexer *lexer) {
    while (lexerNextToken(lexer) != token_eof) {
        int lineno = lexer->lineno;
        int column = lexerColumn(lexer);
        if (lexer->error != NULL) {
            printf("%i:%i:%s\n", lineno, column, lexer->error);
            break;
        }
        switch(lexer->token) {
            case token_eof: printf("%i:%i:(eof)\n", lineno, column); break;
            case token_open: printf("%i:%i:(open)\n", lineno, column); break;
            case token_close: printf("%i:%i:(close)\n", lineno, column); break;
            case token_string: printf("%i:%i:(string) '%.*s'\n", lineno, column, lexer->string_len, lexer->string); break;
            case token_comment: printf("%i:%i:(comment) %.*s", lineno, column, lexer->string_len, lexer->string); break;
            case token_symbol: printf("%i:%i:(symbol) '%.*s'\n", lineno, column, lexer->string_len, lexer->string); break;
            case token_escape: printf("%i:%i:(escape)\n", lineno, column); break;
            default: assert(false); break;
        }
    }
}
*/

//------------------------------------------------------------------------------

typedef struct {
    Lexer lexer;

    char *error;
} Parser;

static void parserError (Parser *parser, const char *format, ...) {
    if (parser->error == NULL) {
        parser->error = (char *)malloc(1024);
    }
    va_list args;
    va_start (args, format);
    vsnprintf (parser->error, 1024, format, args);
    va_end (args);
}

static void parserInit (Parser *parser) {
    parser->error = NULL;
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

static bool parserParseAny (Parser *parser, Expression *result) {
    assert(parser->lexer.token != token_eof);
    if (parser->lexer.token == token_open) {
        initList(result);
        lexerInitAnchor(&parser->lexer, &result->anchor);
        while (true) {
            lexerNextToken(&parser->lexer);
            if (parser->lexer.token == token_close) {
                break;
            } else if (parser->lexer.token == token_eof) {
                parserError(parser, "missing closing parens\n");
                break;
            } else {
                if (!parserParseAny(parser, append(result)))
                    break;
            }
        }
    } else if (parser->lexer.token == token_close) {
        parserError(parser, "stray closing parens\n");
    } else if (parser->lexer.token == token_string) {
        initString(result, parser->lexer.string + 1, parser->lexer.string_len - 2);
        result->len = inplace_unescape((char *)result->buf);
        lexerInitAnchor(&parser->lexer, &result->anchor);
    } else if (parser->lexer.token == token_symbol) {
        initSymbol(result, parser->lexer.string, parser->lexer.string_len);
        result->len = inplace_unescape((char *)result->buf);
        lexerInitAnchor(&parser->lexer, &result->anchor);
    } else if (parser->lexer.token == token_comment) {
        initComment(result, parser->lexer.string + 1, parser->lexer.string_len - 2);
        result->len = inplace_unescape((char *)result->buf);
        lexerInitAnchor(&parser->lexer, &result->anchor);
    } else {
        parserError(parser, "unexpected token: %c (%i)\n",
            *parser->lexer.cursor, (int)*parser->lexer.cursor);
    }

    if (parser->error)
        clearExpression(result);

    return (parser->error == NULL);
}

static bool parserParseNaked (Parser *parser, Expression *result) {
    int lineno = parser->lexer.lineno;
    int column = lexerColumn(&parser->lexer);

    bool escape = false;
    int subcolumn = 0;

    initList(result);
    lexerInitAnchor(&parser->lexer, &result->anchor);

    while (parser->lexer.token != token_eof) {
        if (parser->lexer.token == token_escape) {
            escape = true;
            lexerNextToken(&parser->lexer);
            if (parser->lexer.lineno <= lineno) {
                parserError(parser, "escape character is not at end of line\n");
                break;
            }
            lineno = parser->lexer.lineno;
        } else if (parser->lexer.lineno > lineno) {
            escape = false;
            if (subcolumn != 0) {
                if (lexerColumn(&parser->lexer) != subcolumn) {
                    parserError(parser, "indentation mismatch\n");
                    break;
                }
            } else {
                subcolumn = lexerColumn(&parser->lexer);
            }
            lineno = parser->lexer.lineno;
            if (!parserParseNaked(parser, append(result)))
                break;
        } else {
            if (!parserParseAny(parser, append(result)))
                break;
            lexerNextToken(&parser->lexer);
            //parserError(parser, "unexpected token: %c (%i)\n", *parser->lexer.cursor, (int)*parser->lexer.cursor);
        }

        if ((!escape || (parser->lexer.lineno > lineno)) && (lexerColumn(&parser->lexer) <= column))
            break;
    }

    if (parser->error)
        clearExpression(result);
    else {
        assert(result->len > 0);
        if (result->len == 1) {
            // remove list
            Expression *tmp = (Expression *)result->buf;
            memcpy(result, tmp, sizeof(Expression));
            free(tmp);
        }
    }

    return (parser->error == NULL);
}

static Expression *parserParse (Parser *parser,
    const char *input_stream, const char *eof, const char *path) {
    lexerInit(&parser->lexer, input_stream, eof, path);
    lexerNextToken(&parser->lexer);
    bool escape = false;

    Expression result;
    initList(&result);
    lexerInitAnchor(&parser->lexer, &result.anchor);

    int lineno = parser->lexer.lineno;
    while (parser->lexer.token != token_eof) {

        if (parser->lexer.token == token_escape) {
            escape = true;
            lexerNextToken(&parser->lexer);
            if (parser->lexer.lineno <= lineno) {
                parserError(parser, "escape character is not at end of line\n");
                break;
            }
            lineno = parser->lexer.lineno;
        } else if (parser->lexer.lineno > lineno) {
            escape = false;
            lineno = parser->lexer.lineno;
            if (!parserParseNaked(parser, append(&result)))
                break;
        } else {
            if (!parserParseAny(parser, append(&result)))
                break;
            lexerNextToken(&parser->lexer);
        }

    }

    if ((parser->lexer.error != NULL) && (parser->error == NULL)) {
        parserError(parser, "%s", parser->lexer.error);
    }

    lexerFree(&parser->lexer);

    assert(result.len > 0);

    if (parser->error != NULL) {
        clearExpression(&result);
        return NULL;
    } else if (result.len == 0) {
        return NULL;
    } else if (result.len == 1) {
        return (Expression *)result.buf;
    } else {
        Expression *newresult = (Expression *)malloc(sizeof(Expression));
        memcpy(newresult, &result, sizeof(Expression));
        return newresult;
    }
}

static void parserFree (Parser *parser) {
    free(parser->error);
    parser->error = NULL;
}

Expression *parseFile (const char *path) {
    int fd = open(path, O_RDONLY);
    off_t length = lseek(fd, 0, SEEK_END);
    void *ptr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (ptr != MAP_FAILED) {
        Parser parser;
        parserInit(&parser);
        Expression *expr = parserParse(&parser,
            (const char *)ptr, (const char *)ptr + length,
            path);
        if (parser.error) {
            int lineno = parser.lexer.lineno;
            int column = lexerColumn(&parser.lexer);
            printf("%i:%i:%s\n", lineno, column, parser.error);
            assert(expr == NULL);
        }
        parserFree(&parser);

        munmap(ptr, length);
        close(fd);

        return expr;
    } else {
        fprintf(stderr, "unable to open file: %s\n", path);
        return NULL;
    }
}

//------------------------------------------------------------------------------

void printExpression(Expression *e, size_t depth)
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

	switch(e->type) {
	case E_List:
		sep();
		puts("(");
		for (i = 0; i < e->len; i++)
			printExpression(((Expression*)e->buf) + i, depth + 1);
		sep();
		puts(")");
		return;
	case E_Symbol:
	case E_String:
    case E_Comment:
		sep();
        if (e->type == E_Comment) putchar(';');
		else if (e->type == E_String) putchar('"');
		for (i = 0; i < e->len; i++) {
			switch(((char*)e->buf)[i]) {
			case '"':
			case '\\':
				putchar('\\');
				break;
			case ')': case '(':
				if (e->type == E_Symbol)
					putchar('\\');
			}

			putchar(((char*)e->buf)[i]);
		}
		if (e->type == E_String) putchar('"');
		putchar('\n');
		return;
    default:
        assert (false); break;
	}
#undef sep
}

//------------------------------------------------------------------------------

typedef uint64_t TypeId;

static TypeId next_type_ref = 0;

// pretty names for types are here
static std::map<TypeId, std::string> _pretty_name_map;
// opaque types don't have an LLVM type
static std::map<TypeId, LLVMTypeRef> _llvm_type_map;
// if a type has elements, their types are here
static std::map<TypeId, std::vector<TypeId> > _element_type_map;
// if a type's elements are named, their names are here
static std::map<TypeId, std::vector<std::string> > _element_name_map;
// if a type is a pointer, its pointee type is here
static std::map<TypeId, TypeId> _pointee_map;
// if a type is an array, the array size is here
static std::map<TypeId, size_t> _array_size_map;
// if a type is a vector, the vector size is here
static std::map<TypeId, size_t> _vector_size_map;
// cached pointer types go here
static std::map<TypeId, TypeId> _pointer_map;

// handle around TypeIds
class Type {
protected:
    TypeId id;
public:

    Type() : id(0) {}
    Type(const Type &type) : id(type.id) {}
    Type(uint64_t id_) : id(id_) {}

    static Type create(std::string name, LLVMTypeRef llvmtype) {
        Type type(++next_type_ref);
        _pretty_name_map[type.id] = name;
        _llvm_type_map[type.id] = llvmtype;
        return type;
    }

    static Type pointer(Type type);

    bool operator == (const Type &other) const {
        return id == other.id;
    }

    operator bool () const {
        return id != 0;
    }

    TypeId getId() const {
        return id;
    }

    LLVMTypeRef getLLVMType() const {
        return _llvm_type_map[id];
    }

    std::string getPrettyName() const {
        if (!id) {
            return "<none>";
        } else {
            std::string result = _pretty_name_map[id];
            if (!result.size())
                return string_format("<unnamed:" PRIu64 ">", id);
            return result;
        }
    }
};

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
    T_opaque = Type::create("opaque", NULL);
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

Type Type::pointer(Type type) {
    // cannot reference void
    assert(type != T_void);
    Type ptr = Type(_pointer_map[type.id]);
    if (!ptr) {
        ptr = Type::create(string_format("&%s", getPrettyName().c_str()));
        _pointer_map[type.id] = ptr.id;
        _pointee_map[ptr.id] = type.id;
    }
    return ptr;
}

//------------------------------------------------------------------------------

static LLVMModuleRef bang_module;
static LLVMBuilderRef bang_builder;
static LLVMExecutionEngineRef bang_engine;
static LLVMValueRef bang_nopfunc;

typedef std::map<std::string, LLVMValueRef> NameValueMap;
typedef std::map<std::string, LLVMModuleRef> NameModuleMap;

static NameValueMap NamedValues;
static NameModuleMap NamedModules;
static int compile_errors = 0;
static bool bang_dump_module = false;

//------------------------------------------------------------------------------

struct Environment {
    // currently active function
    LLVMValueRef function;
    // types in scope
    NameTypeMap types;
    // local names
    NameValueMap names;
    // parent env
    const Environment *parent;

    Environment() :
        function(NULL),
        parent(NULL)
    {}

    Environment(const Environment *parent) :
        function(parent->function),
        parent(parent) {
    }

};

//------------------------------------------------------------------------------

static void translateError (Anchor *anchor, const char *format, ...);

class BangCVisitor : public clang::RecursiveASTVisitor<clang::BangCVisitor> {
public:
    using namespace clang;

    ASTContext *Context;

    BangCVisitor() : Context(NULL) {
    }

    void SetContext(ASTContext * ctx) {
        Context = ctx;
    }

    bool GetFields(RecordDecl * rd) {
        // ASTContext::getASTRecordLayout(const RecordDecl *D)

        //check the fields of this struct, if any one of them is not understandable, then this struct becomes 'opaque'
        //that is, we insert the type, and link it to its llvm type, so it can be used in terra code
        //but none of its fields are exposed (since we don't understand the layout)
        bool opaque = false;
        int anonname = 0;
        for(RecordDecl::field_iterator it = rd->field_begin(), end = rd->field_end(); it != end; ++it) {
            DeclarationName declname = it->getDeclName();

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
            QualType FT = it->getType();
            LLVMTypeRef fieldtype = TranslateType(FT);
            if(!fieldtype) {
                opaque = true;
                continue;
            }
            LLVMDumpType(fieldtype);
            printf("%s\n", declstr.c_str());
        }
        return !opaque;

    }

    LLVMTypeRef TranslateRecord(RecordDecl * rd) {
        if(rd->isStruct() || rd->isUnion()) {
            std::string name = rd->getName();

            bool tagged = true;

            if(name == "") {
                TypedefNameDecl * decl = rd->getTypedefNameForAnonDecl();
                if(decl) {
                    tagged = false;
                    name = decl->getName();
                }
            }
            bang_struct_def *structdef =
                ((tagged)?TaggedNamedStructs[name]:NamedStructs[name]).get();

            if (!structdef) {
                structdef = new bang_struct_def();

                structdef->type = LLVMStructCreateNamed(
                    LLVMGetGlobalContext(), name.c_str());

                printf("record: %s tagged=%s\n",
                    name.c_str(), tagged?"true":"false");

                if (tagged)
                    TaggedNamedStructs[name].reset(structdef);
                else
                    NamedStructs[name].reset(structdef);
            }

            // size_t argpos = RegisterRecordType(Context->getRecordType(rd));
            // thenamespace->setfield(name.c_str()); //register the type

            RecordDecl * defn = rd->getDefinition();
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

            return structdef->type;
        } else {
            //return ImportError("non-struct record types are not supported");
            return NULL;
        }
    }

    LLVMTypeRef TranslateType(QualType T) {
        T = Context->getCanonicalType(T);
        const Type *Ty = T.getTypePtr();

        switch (Ty->getTypeClass()) {
        case Type::Record: {
            const RecordType *RT = dyn_cast<RecordType>(Ty);
            RecordDecl * rd = RT->getDecl();
            //return GetRecordTypeFromDecl(rd, tt);
            return TranslateRecord(rd);
        }  break; //TODO
        case Type::Builtin:
            switch (cast<BuiltinType>(Ty)->getKind()) {
            case BuiltinType::Void: {
                //InitType("opaque",tt);
                return LLVMVoidType();
            } break;
            case BuiltinType::Bool: {
                //InitType("bool",tt);
                return LLVMInt1Type();
            } break;
            case BuiltinType::Char_S:
            case BuiltinType::Char_U:
            case BuiltinType::SChar:
            case BuiltinType::UChar:
            case BuiltinType::Short:
            case BuiltinType::UShort:
            case BuiltinType::Int:
            case BuiltinType::UInt:
            case BuiltinType::Long:
            case BuiltinType::ULong:
            case BuiltinType::LongLong:
            case BuiltinType::ULongLong:
            case BuiltinType::WChar_S:
            case BuiltinType::WChar_U:
            case BuiltinType::Char16:
            case BuiltinType::Char32: {
                /*
                std::stringstream ss;
                if (Ty->isUnsignedIntegerType())
                    ss << "u";
                ss << "int";
                int sz = Context->getTypeSize(T);
                ss << sz;
                InitType(ss.str().c_str(),tt);
                */
                int sz = Context->getTypeSize(T);
                if (sz == 8)
                    return LLVMInt8Type();
                else if (sz == 16)
                    return LLVMInt16Type();
                else if (sz == 32)
                    return LLVMInt32Type();
                else if (sz == 64)
                    return LLVMInt64Type();
            } break;
            case BuiltinType::Half: {
                return LLVMHalfType();
            } break;
            case BuiltinType::Float: {
                return LLVMFloatType();
            } break;
            case BuiltinType::Double: {
                return LLVMDoubleType();
            } break;
            case BuiltinType::LongDouble:
            case BuiltinType::NullPtr:
            case BuiltinType::UInt128:
            default:
                break;
            }
        case Type::Complex:
        case Type::LValueReference:
        case Type::RValueReference:
            break;
        case Type::Pointer: {
            const PointerType *PTy = cast<PointerType>(Ty);
            QualType ETy = PTy->getPointeeType();
            LLVMTypeRef pointee = TranslateType(ETy);
            if (pointee) {
                if (pointee == LLVMVoidType())
                    pointee = LLVMInt8Type();
                return LLVMPointerType(pointee, 0);
            }
        } break;
        case Type::VariableArray:
        case Type::IncompleteArray:
            break;
        case Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            LLVMTypeRef at = TranslateType(ATy->getElementType());
            if(at) {
                int sz = ATy->getSize().getZExtValue();
                return LLVMArrayType(at, sz);
            }
        } break;
        case Type::ExtVector:
        case Type::Vector: {
                const VectorType *VT = cast<VectorType>(T);
                LLVMTypeRef at = TranslateType(VT->getElementType());
                if(at) {
                    int n = VT->getNumElements();
                    return LLVMVectorType(at, n);
                }
        } break;
        case Type::FunctionNoProto: /* fallthrough */
        case Type::FunctionProto: {
            const FunctionType *FT = cast<FunctionType>(Ty);
            if (FT) {
                return TranslateFuncType(FT);
            }
        } break;
        case Type::ObjCObject: break;
        case Type::ObjCInterface: break;
        case Type::ObjCObjectPointer: break;
        case Type::Enum: {
            return LLVMInt32Type();
        } break;
        case Type::BlockPointer:
        case Type::MemberPointer:
        case Type::Atomic:
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
        return NULL;
    }

    LLVMTypeRef TranslateFuncType(const FunctionType * f) {

        bool valid = true; // decisions about whether this function can be exported or not are delayed until we have seen all the potential problems
        QualType RT = f->getReturnType();

        LLVMTypeRef returntype = TranslateType(RT);

        if (!returntype)
            valid = false;

        const FunctionProtoType * proto = f->getAs<FunctionProtoType>();
        std::vector<LLVMTypeRef> argtypes;
        //proto is null if the function was declared without an argument list (e.g. void foo() and not void foo(void))
        //we don't support old-style C parameter lists, we just treat them as empty
        if(proto) {
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                QualType PT = proto->getParamType(i);
                LLVMTypeRef paramtype = TranslateType(PT);
                if(!paramtype) {
                    valid = false; //keep going with attempting to parse type to make sure we see all the reasons why we cannot support this function
                } else if(valid) {
                    argtypes.push_back(paramtype);
                }
            }
        }

        if(valid) {
            return LLVMFunctionType(returntype, &argtypes[0], argtypes.size(), proto ? proto->isVariadic() : false);
        }

        return NULL;
    }

    bool TraverseFunctionDecl(FunctionDecl *f) {
         // Function name
        DeclarationName DeclName = f->getNameInfo().getName();
        std::string FuncName = DeclName.getAsString();
        const FunctionType * fntyp = f->getType()->getAs<FunctionType>();

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
        LLVMTypeRef functype = TranslateFuncType(fntyp);
        if (!functype)
            return true;

        std::string InternalName = FuncName;
        AsmLabelAttr * asmlabel = f->getAttr<AsmLabelAttr>();
        if(asmlabel) {
            InternalName = asmlabel->getLabel();
            #ifndef __linux__
                //In OSX and Windows LLVM mangles assembler labels by adding a '\01' prefix
                InternalName.insert(InternalName.begin(), '\01');
            #endif
        }

        printf("%s -> %s\n", FuncName.c_str(), InternalName.c_str());
        //CreateFunction(FuncName,InternalName,&typ);

        //LLVMDumpType(functype);

        LLVMAddFunction(bang_module, InternalName.c_str(), functype);


        //KeepLive(f);//make sure this function is live in codegen by creating a dummy reference to it (void) is to suppress unused warnings

        return true;
    }
};

class CodeGenProxy : public clang::ASTConsumer {
public:
    using namespace clang;

    BangCVisitor visitor;

    CodeGenProxy() {}
    virtual ~CodeGenProxy() {}

    virtual void Initialize(ASTContext &Context) {
        visitor.SetContext(&Context);
    }

    virtual bool HandleTopLevelDecl(DeclGroupRef D) {
        for (DeclGroupRef::iterator b = D.begin(), e = D.end(); b != e; ++b)
            visitor.TraverseDecl(*b);
        return true;
    }
};

// see ASTConsumers.h for more utilities
class BangEmitLLVMOnlyAction : public clang::EmitLLVMOnlyAction {
public:
    using namespace clang;

    BangEmitLLVMOnlyAction() :
        EmitLLVMOnlyAction((llvm::LLVMContext *)LLVMGetGlobalContext())
    {
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {

        std::vector< std::unique_ptr<ASTConsumer> > consumers;
        consumers.push_back(EmitLLVMOnlyAction::CreateASTConsumer(CI, InFile));
        consumers.push_back(llvm::make_unique<CodeGenProxy>());
        return llvm::make_unique<MultiplexConsumer>(std::move(consumers));
    }
};

static LLVMModuleRef importCModule (Anchor *anchor,
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
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction());
    if (compiler.ExecuteAction(*Act)) {
        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        LLVMDumpModule(M);
    } else {
        translateError(anchor, "compiler failed\n");
    }

    return M;
}

//------------------------------------------------------------------------------

static const char *expressionKindName(int type) {
    switch(type) {
    case E_None: return "?";
    case E_List: return "list";
    case E_String: return "string";
    case E_Symbol: return "symbol";
    case E_Comment: return "comment";
    default: return "<corrupted>";
    }
}

static void translateError (Anchor *anchor, const char *format, ...) {
    ++compile_errors;
    printf("%s:%i:%i: error: ", anchor->path, anchor->lineno, anchor->column);
    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}

static bool isSymbol (Expression *expr, const char *sym) {
    return expr && (expr->type == E_Symbol) &&
        !strcmp((const char *)expr->buf, sym);
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
(getelementptr value index [index [...]])
(pointer-type type)
(typeof value)
(dump value)
(? if-expr then-block else-block)
(do expr ...)
(dump-module)
(import-c <filename> (<compiler-arg> ...))
*/

typedef struct {
    TypeId type;
    LLVMValueRef value;
} TypedValue;

static TypedValue translate (const Environment *env, Expression *expr);

static Expression *translateKind(Expression *expr, int type) {
    if (expr) {
        if (expr->type == type) {
            return expr;
        } else {
            translateError(&expr->anchor, "%s expected\n",
                expressionKindName(type));
        }
    }
    return NULL;
}

static const char *translateString(Expression *expr) {
    if (expr) {
        if ((expr->type == E_Symbol) || (expr->type == E_String)) {
            return (const char *)expr->buf;
        } else {
            translateError(&expr->anchor, "string or symbol expected\n");
        }
    }
    return NULL;
}

static TypeId translateType (const Environment *env, Expression *expr) {
    if (expr) {
        TypedValue result = translate(env, expr);
        if (!result.type && result.value) {
            translateError(&expr->anchor, "type expected, not value\n");
        }
        return result.type;
    }
    return NULL;
}

static LLVMValueRef translateValue (const Environment *env, Expression *expr) {
    if (expr) {
        TypedValue result = translate(env, expr);
        if (!result.value && result.type) {
            translateError(&expr->anchor, "value expected, not type\n");
        }
        return result.value;
    }
    return NULL;
}

static bool verifyParameterCount (Expression *expr, int mincount, int maxcount) {
    if (expr) {
        if (expr->type == E_List) {
            int argcount = (int)expr->len - 1;
            if ((mincount >= 0) && (argcount < mincount)) {
                translateError(&expr->anchor, "at least %i arguments expected\n", mincount);
                return false;
            }
            if ((maxcount >= 0) && (argcount > maxcount)) {
                translateError(&expr->anchor, "at most %i arguments expected\n", maxcount);
                return false;
            }
            return true;
        } else {
            translateError(&expr->anchor, "list expected\n");
            return false;
        }
    }
    return false;
}

static bool matchSpecialForm (Expression *expr, const char *name, int mincount, int maxcount) {
    return isSymbol(nth(expr, 0), name) && verifyParameterCount(expr, mincount, maxcount);
}

static LLVMValueRef nopcall () {
    return LLVMBuildCall(bang_builder, bang_nopfunc, NULL, 0, "");
}

static LLVMValueRef translateExpressionList (const Environment *env, Expression *expr, int offset) {
    int argcount = (int)expr->len - offset;
    bool success = true;
    LLVMValueRef stmt = NULL;
    for (int i = 0; i < argcount; ++i) {
        stmt = translateValue(env, nth(expr, i + offset));
        if (!stmt || compile_errors) {
            success = false;
            stmt = NULL;
            break;
        }
    }

    return stmt;
}

static TypedValue translate (const Environment *env, Expression *expr) {
    TypedValue result;
    result.type = NULL;
    result.value = NULL;

    if (expr) {
        if (expr->type == E_List) {
            if (expr->len >= 1) {
                Expression *head = nth(expr, 0);
                if (head->type == E_Symbol) {
                    if (matchSpecialForm(expr, "function-type", 2, 2)) {
                        Expression *args_expr = translateKind(nth(expr, 2), E_List);

                        if (args_expr) {
                            Expression *tail = nth(args_expr, -1);
                            bool vararg = false;
                            int argcount = (int)args_expr->len;
                            if (isSymbol(tail, "...")) {
                                vararg = true;
                                --argcount;
                            }

                            if (argcount >= 0) {
                                TypeId rettype = translateType(env, nth(expr, 1));
                                if (rettype) {
                                    TypeId argtypes[argcount];

                                    bool success = true;
                                    for (int i = 0; i < argcount; ++i) {
                                        TypeId argtype = translateType(env, nth(args_expr, i));
                                        if (!argtype) {
                                            success = false;
                                            break;
                                        }
                                        argtypes[i] = argtype;
                                    }

                                    if (success) {
                                        result.type = bang_function_type(rettype, argtypes, argcount, vararg);
                                    }
                                }
                            } else {
                                translateError(&expr->anchor, "vararg function is missing return type\n");
                            }
                        }
                    } else if (matchSpecialForm(expr, "bitcast", 2, 2)) {

                        LLVMTypeRef casttype = translateType(env, nth(expr, 1));
                        LLVMValueRef castvalue = translateValue(env, nth(expr, 2));

                        if (casttype && castvalue) {
                            result.value = LLVMBuildBitCast(bang_builder, castvalue, casttype, "ptrcast");
                        }

                    } else if (matchSpecialForm(expr, "extract", 2, 2)) {

                        LLVMValueRef value = translateValue(env, nth(expr, 1));
                        LLVMValueRef index = translateValue(env, nth(expr, 2));

                        if (value && index) {
                            result.value = LLVMBuildExtractElement(bang_builder, value, index, "extractelem");
                        }

                    } else if (matchSpecialForm(expr, "const-int", 2, 2)) {
                        Expression *expr_type = nth(expr, 1);
                        Expression *expr_value = translateKind(nth(expr, 2), E_Symbol);

                        LLVMTypeRef type;
                        if (expr_type) {
                            type = translateType(env, expr_type);
                        } else {
                            type = LLVMInt32Type();
                        }

                        if (type && expr_value) {
                            char *end;
                            long long value = strtoll((const char *)expr_value->buf, &end, 10);
                            if (end != ((char *)expr_value->buf + expr_value->len)) {
                                translateError(&expr_value->anchor, "not a valid integer constant\n");
                            } else {
                                result.value = LLVMConstInt(type, value, 1);
                            }
                        }

                    } else if (matchSpecialForm(expr, "const-real", 2, 2)) {
                        Expression *expr_type = nth(expr, 1);
                        Expression *expr_value = translateKind(nth(expr, 2), E_Symbol);

                        LLVMTypeRef type;
                        if (expr_type) {
                            type = translateType(env, expr_type);
                        } else {
                            type = LLVMDoubleType();
                        }

                        if (type && expr_value) {
                            char *end;
                            double value = strtod((const char *)expr_value->buf, &end);
                            if (end != ((char *)expr_value->buf + expr_value->len)) {
                                translateError(&expr_value->anchor, "not a valid real constant\n");
                            } else {
                                result.value = LLVMConstReal(type, value);
                            }
                        }

                    } else if (matchSpecialForm(expr, "typeof", 1, 1)) {

                        LLVMValueRef value = translateValue(env, nth(expr, 1));
                        if (value) {
                            result.type = LLVMTypeOf(value);
                        }

                    } else if (matchSpecialForm(expr, "dump-module", 0, 0)) {

                        bang_dump_module = true;

                    } else if (matchSpecialForm(expr, "dump", 1, 1)) {

                        Expression *expr_arg = nth(expr, 1);

                        TypedValue tov = translate(env, expr_arg);
                        if (tov.type) {
                            LLVMDumpType(tov.type);
                        }
                        if (tov.value) {
                            LLVMDumpValue(tov.value);
                        }
                        if (!tov.type && !tov.value) {
                            printf("no expression\n");
                        }

                        result = tov;

                    } else if (matchSpecialForm(expr, "getelementptr", 2, -1)) {

                        LLVMValueRef ptr = translateValue(env, nth(expr, 1));
                        if (ptr) {
                            int argcount = (int)expr->len - 2;

                            LLVMValueRef indices[argcount];
                            bool success = true;
                            for (int i = 0; i < argcount; ++i) {
                                LLVMValueRef index = translateValue(env, nth(expr, i + 2));
                                if (!index) {
                                    success = false;
                                    break;
                                }
                                indices[i] = index;
                            }

                            if (success) {
                                result.value = LLVMBuildGEP(bang_builder, ptr, indices, (unsigned)argcount, "gep");
                            }
                        }

                    } else if (matchSpecialForm(expr, "?", 3, 3)) {

                        LLVMValueRef cond_value = translateValue(env, nth(expr, 1));
                        if (cond_value) {
                            LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(bang_builder);

                            LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(env->function, "then");
                            LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(env->function, "else");
                            LLVMBasicBlockRef br_block = LLVMAppendBasicBlock(env->function, "br");

                            Expression *then_expr = nth(expr, 2);
                            Expression *else_expr = nth(expr, 3);

                            LLVMPositionBuilderAtEnd(bang_builder, then_block);
                            LLVMValueRef then_result = translateValue(env, then_expr);
                            LLVMBuildBr(bang_builder, br_block);

                            LLVMPositionBuilderAtEnd(bang_builder, else_block);
                            LLVMValueRef else_result = translateValue(env, else_expr);
                            LLVMBuildBr(bang_builder, br_block);

                            LLVMTypeRef then_type = then_result?LLVMTypeOf(then_result):NULL;
                            LLVMTypeRef else_type = else_result?LLVMTypeOf(else_result):NULL;

                            if ((then_type == LLVMVoidType()) || (else_type == LLVMVoidType())) {
                                LLVMPositionBuilderAtEnd(bang_builder, br_block);
                                result.value = nopcall();
                            } else if (then_type == else_type) {
                                LLVMPositionBuilderAtEnd(bang_builder, br_block);
                                result.value = LLVMBuildPhi(bang_builder, then_type, "select");
                                LLVMValueRef values[] = { then_result, else_result };
                                LLVMBasicBlockRef blocks[] = { then_block, else_block };
                                LLVMAddIncoming(result.value, values, blocks, 2);
                            } else {
                                LLVMDumpType(then_type);
                                LLVMDumpType(else_type);
                                translateError(&then_expr->anchor, "then/else type evaluation mismatch\n");
                                translateError(&else_expr->anchor, "then-expression must evaluate to same type as else-expression\n");
                            }

                            LLVMPositionBuilderAtEnd(bang_builder, oldblock);
                            LLVMBuildCondBr(bang_builder, cond_value, then_block, else_block);

                            LLVMPositionBuilderAtEnd(bang_builder, br_block);
                        }

                    } else if (matchSpecialForm(expr, "do", 1, -1)) {

                        Environment subenv(env);

                        result.value = translateExpressionList(&subenv, expr, 1);

                    } else if (matchSpecialForm(expr, "function", 3, -1)) {

                        Expression *expr_type = nth(expr, 3);

                        Expression *expr_name = translateKind(nth(expr, 1), E_Symbol);
                        LLVMTypeRef functype = translateType(env, expr_type);

                        if (expr_name && functype) {
                            if (LLVMGetTypeKind(functype) == LLVMFunctionTypeKind) {
                                // todo: external linkage?
                                LLVMValueRef func = LLVMAddFunction(bang_module, (const char *)expr_name->buf, functype);

                                Expression *expr_params = nth(expr, 2);
                                Expression *body_expr = nth(expr, 4);

                                Environment subenv(env);
                                subenv.function = func;

                                if (isSymbol(expr_params, "...")) {
                                    if (body_expr) {
                                        translateError(&expr_params->anchor, "cannot declare function body without parameter list\n");
                                    }
                                } else if (expr_params->type == E_List) {
                                    int argcount = (int)expr_params->len;
                                    int paramcount = LLVMCountParams(func);
                                    if (argcount == paramcount) {
                                        LLVMValueRef params[paramcount];
                                        LLVMGetParams(func, params);
                                        for (int i = 0; i < argcount; ++i) {
                                            Expression *expr_param = translateKind(nth(expr_params, i), E_Symbol);
                                            if (expr_param) {
                                                const char *name = (const char *)expr_param->buf;
                                                LLVMSetValueName(params[i], name);
                                                subenv.names[name] = params[i];
                                            }
                                        }
                                    } else {
                                        translateError(&expr_params->anchor, "parameter name count mismatch (%i != %i); must name all parameter types\n",
                                            argcount, paramcount);
                                    }
                                } else {
                                    translateError(&expr_params->anchor, "parameter list or ... expected\n");
                                }

                                if (!compile_errors) {
                                    result.value = func;

                                    if (body_expr) {
                                        LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(bang_builder);

                                        LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
                                        LLVMPositionBuilderAtEnd(bang_builder, entry);

                                        LLVMValueRef result = translateExpressionList(&subenv, expr, 4);

                                        if (LLVMGetReturnType(functype) == LLVMVoidType()) {
                                            LLVMBuildRetVoid(bang_builder);
                                        } else if (result) {
                                            LLVMBuildRet(bang_builder, result);
                                        } else {
                                            translateError(&expr->anchor, "function returns no value\n");
                                        }

                                        LLVMPositionBuilderAtEnd(bang_builder, oldblock);

                                    }
                                }
                            } else {
                                translateError(&expr_type->anchor, "not a function type\n");
                            }
                        }

                    } else if (matchSpecialForm(expr, "extern", 2, 2)) {

                        Expression *expr_type = nth(expr, 2);

                        Expression *expr_name = translateKind(nth(expr, 1), E_Symbol);

                        LLVMTypeRef functype = translateType(env, expr_type);

                        if (expr_name && functype) {
                            if (LLVMGetTypeKind(functype) == LLVMFunctionTypeKind) {
                                // todo: external linkage?
                                LLVMValueRef func = LLVMAddFunction(bang_module, (const char *)expr_name->buf, functype);

                                result.value = func;
                            } else {
                                translateError(&expr_type->anchor, "not a function type\n");
                            }
                        }

                    } else if (matchSpecialForm(expr, "call", 1, -1)) {

                        int argcount = (int)expr->len - 2;

                        Expression *expr_func = nth(expr, 1);
                        LLVMValueRef callee = translateValue(env, expr_func);

                        if (callee) {
                            unsigned arg_size = LLVMCountParams(callee);

                            LLVMTypeRef functype = LLVMGetElementType(LLVMTypeOf(callee));

                            if ((LLVMGetTypeKind(functype) == LLVMFunctionTypeKind)) {

                                int isvararg = LLVMIsFunctionVarArg(functype);

                                if ((isvararg && (arg_size <= (unsigned)argcount))
                                    || (arg_size == (unsigned)argcount)) {

                                    LLVMValueRef args[argcount];

                                    bool success = true;
                                    for (int i = 0; i < argcount; ++i) {
                                        LLVMValueRef value = translateValue(env, nth(expr, i + 2));
                                        if (!value) {
                                            success = false;
                                            break;
                                        }
                                        args[i] = value;
                                    }

                                    if (success) {
                                        if (LLVMGetReturnType(functype) == LLVMVoidType()) {
                                            result.value = LLVMBuildCall(bang_builder, callee, args, argcount, "");
                                        } else {
                                            result.value = LLVMBuildCall(bang_builder, callee, args, argcount, "calltmp");
                                        }
                                    }
                                } else {
                                    translateError(&expr->anchor, "incorrect number of call arguments (got %i, need %s%i)\n",
                                        argcount, isvararg?"at least ":"", arg_size);
                                }
                            } else {
                                translateError(&expr_func->anchor, "cannot call object\n");
                            }
                        }

                    } else if (matchSpecialForm(expr, "import-c", 3, 3)) {
                        const char *modulename = translateString(nth(expr, 1));
                        const char *name = translateString(nth(expr, 2));
                        Expression *args_expr = translateKind(nth(expr, 3), E_List);

                        if (modulename && name && args_expr) {
                            int argcount = (int)args_expr->len;
                            const char *args[argcount];
                            bool success = true;
                            for (int i = 0; i < argcount; ++i) {
                                const char *arg = translateString(nth(args_expr, i));
                                if (arg) {
                                    args[i] = arg;
                                } else {
                                    success = false;
                                    break;
                                }
                            }
                            LLVMModuleRef module = importCModule(&expr->anchor, modulename, name, args, argcount);
                            if (module) {
                                NamedModules[modulename] = module;
                            }
                        }

                    } else if (matchSpecialForm(expr, "pointer-type", 1, 1)) {

                        LLVMTypeRef type = translateType(env, nth(expr, 1));

                        if (type) {
                            result.type = LLVMPointerType(type, 0);
                        }
                    } else {
                        translateError(&head->anchor, "unhandled special form: %s\n",
                            (const char *)head->buf);
                    }
                } else {
                    translateError(&head->anchor, "symbol expected\n");
                }
            } else {
                result.value = nopcall();
            }
        } else if (expr->type == E_Symbol) {
            const char *name = (const char *)expr->buf;

            result.type = NamedTypes[name];
            result.value = NULL;

            Environment *penv = (Environment *)env;
            while (penv) {
                result.value = (*penv).names[name];
                if (result.value)
                    break;
                penv = (Environment *)penv->parent;
            }

            if (!result.value) {
                result.value = NamedValues[name];
            }

            if (!result.value) {
                /*
                for (auto it: NamedModules) {
                    result.value = LLVMGetNamedFunction(it.second, name);
                    if (result.value)
                        break;
                }
                */
                result.value = LLVMGetNamedFunction(bang_module, name);
            }

            if (!result.type && !result.value) {
                translateError(&expr->anchor, "no such name: %s\n", name);
            }

        } else if (expr->type == E_String) {

            const char *name = (const char *)expr->buf;
            result.value = LLVMBuildGlobalString(bang_builder, name, "str");
            //result.value = LLVMConstString(name, expr->len, 0);
            assert(result.value);

        } else {

            translateError(&expr->anchor, "unexpected %s\n",
                expressionKindName(expr->type));
        }
    }

    return result;
}

static void exportExternal (const char *name, void *addr) {
    LLVMValueRef func = LLVMGetNamedFunction(bang_module, name);
    if (func) {
        LLVMAddGlobalMapping(bang_engine, func, addr);
    }
}

static void compile (Expression *expr) {
    bang_module = LLVMModuleCreateWithName("bang");
    NamedModules["bang"] = bang_module;
    bang_builder = LLVMCreateBuilder();

    setupTypes();

    NamedTypes["void"] = LLVMVoidType();
    NamedTypes["half"] = LLVMHalfType();
    NamedTypes["float"] = LLVMFloatType();
    NamedTypes["double"] = LLVMDoubleType();
    NamedTypes["i1"] = LLVMInt1Type();
    NamedTypes["i8"] = LLVMInt8Type();
    NamedTypes["i16"] = LLVMInt16Type();
    NamedTypes["i32"] = LLVMInt32Type();
    NamedTypes["i64"] = LLVMInt64Type();

    LLVMTypeRef entryfunctype = LLVMFunctionType(LLVMVoidType(), NULL, 0, 0);
    bang_nopfunc = LLVMAddFunction(bang_module, "__nop", entryfunctype);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(bang_nopfunc, "entry");
    LLVMPositionBuilderAtEnd(bang_builder, entry);
    LLVMBuildRetVoid(bang_builder);

    LLVMValueRef entryfunc = LLVMAddFunction(bang_module, "__anon_expr", entryfunctype);
    LLVMValueRef lastvalue = NULL;

    Environment env;
    env.function = entryfunc;

    if (expr->type == E_List) {
        if (expr->len >= 1) {
            Expression *head = nth(expr, 0);
            if (isSymbol(head, "bang")) {

                LLVMBasicBlockRef entry = LLVMAppendBasicBlock(entryfunc, "entry");
                LLVMPositionBuilderAtEnd(bang_builder, entry);

                for (size_t i = 1; i != expr->len; ++i) {
                    Expression *stmt = nth(expr, (int)i);
                    TypedValue tov = translate(&env, stmt);
                    if (compile_errors)
                        break;
                    if (tov.value) {
                        lastvalue = tov.value;
                    }
                }
            } else {
                translateError(&head->anchor, "'bang' expected\n");
            }
        } else {
            translateError(&expr->anchor, "expression is empty\n");
        }
    } else {
        translateError(&expr->anchor, "unexpected %s\n",
            expressionKindName(expr->type));
    }

    LLVMBuildRetVoid(bang_builder);

    if (!compile_errors) {
        if (bang_dump_module) {
            LLVMDumpModule(bang_module);
            printf("\n\noutput:\n");
        }

        char *error = NULL;
        LLVMVerifyModule(bang_module, LLVMAbortProcessAction, &error);
        LLVMDisposeMessage(error);

        error = NULL;
        LLVMLinkInMCJIT();
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmParser();
        LLVMInitializeNativeAsmPrinter();
        LLVMInitializeNativeDisassembler();
        int result = LLVMCreateExecutionEngineForModule(
            &bang_engine, bang_module, &error);

        if (error) {
            fprintf(stderr, "error: %s\n", error);
            LLVMDisposeMessage(error);
            exit(EXIT_FAILURE);
        }

        if (result != 0) {
            fprintf(stderr, "failed to create execution engine\n");
            abort();
        }

        for (auto it: NamedModules)
            LLVMAddModule(bang_engine, it.second);

        exportExternal("parseFile", (void *)parseFile);
        exportExternal("LLVMVoidType", (void *)LLVMVoidType);


        LLVMRunFunction(bang_engine, entryfunc, 0, NULL);

    }

    LLVMDisposeBuilder(bang_builder);
}


//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
    bang_argc = argc;
    bang_argv = argv;

    int result = 0;

    ++argv;
    while (argv && *argv) {
        Expression *expr = parseFile(*argv);
        if (expr) {
            //printExpression(expr, 0);
            compile(expr);

            free(expr);
        } else {
            result = 255;
        }

        ++argv;
    }

    return result;
}

} // namespace bang

#endif // BANG_HEADER_ONLY
