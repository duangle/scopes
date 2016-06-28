#ifndef BANG0_CPP
#define BANG0_CPP

#include <sys/types.h>

//------------------------------------------------------------------------------

#if defined __cplusplus
extern "C" {
#endif

typedef enum {
    bang_expr_type_none,
    bang_expr_type_list,
    bang_expr_type_string,
    bang_expr_type_symbol,
    bang_expr_type_comment
} bang_expr_type;

typedef struct {
    const char *path;
    int lineno;
    int column;
    int offset;
} bang_anchor;

typedef struct {
    int type;
    size_t len;
    void *buf;
    bang_anchor anchor;
} bang_expr;

bang_expr *bang_parse_file (const char *path);

#ifdef BANG_HEADER_ONLY
long imul(long a, long b) {
    return a * b;
}
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
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>

//------------------------------------------------------------------------------

void bang_init_list(bang_expr *expr) {
    expr->type = bang_expr_type_list;
    expr->len = 0;
    expr->buf = NULL;
}

void bang_init_blob(bang_expr *expr, const char *s, size_t len) {
    expr->len = len;
    expr->buf = malloc(len + 1);
    memcpy(expr->buf, s, len);
    ((char*)expr->buf)[len] = 0;
}

void bang_init_string(bang_expr *expr, const char *s, size_t len) {
    expr->type = bang_expr_type_string;
    bang_init_blob(expr, s, len);
}

void bang_init_symbol(bang_expr *expr, const char *s, size_t len) {
    expr->type = bang_expr_type_symbol;
    bang_init_blob(expr, s, len);
}

void bang_init_comment(bang_expr *expr, const char *s, size_t len) {
    expr->type = bang_expr_type_comment;
    bang_init_blob(expr, s, len);
}

bang_expr *bang_append(bang_expr *expr) {
    assert(expr->type == bang_expr_type_list);
    expr->buf = realloc(expr->buf, sizeof(bang_expr) * (++expr->len));
    return (bang_expr *)expr->buf + (expr->len - 1);
}

bang_expr *bang_nth(bang_expr *expr, int i) {
    assert(expr->type == bang_expr_type_list);
    if (i < 0)
        i = expr->len + i;
    if ((i < 0) || ((size_t)i >= expr->len))
        return NULL;
    else
        return (bang_expr *)expr->buf + i;
}

void bang_expr_clear(bang_expr *expr) {
    free(expr->buf);
    expr->type = bang_expr_type_none;
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
} bang_token;

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
} bang_lexer;

static void bang_lexer_init (bang_lexer *lexer,
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

static void bang_lexer_free (bang_lexer *lexer) {
    free(lexer->error);
    lexer->error = NULL;
}

static int bang_lexer_column (bang_lexer *lexer) {
    return lexer->cursor - lexer->line + 1;
}

static void bang_lexer_init_anchor(bang_lexer *lexer, bang_anchor *anchor) {
    anchor->path = lexer->path;
    anchor->lineno = lexer->lineno;
    anchor->column = bang_lexer_column(lexer);
    anchor->offset = lexer->cursor - lexer->input_stream;
}

static void bang_lexer_error (bang_lexer *lexer, const char *format, ...) {
    if (lexer->error == NULL) {
        lexer->error = (char *)malloc(1024);
    }
    va_list args;
    va_start (args, format);
    vsnprintf (lexer->error, 1024, format, args);
    va_end (args);
    lexer->token = token_eof;
}

static void bang_lexer_symbol (bang_lexer *lexer) {
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

static void bang_lexer_string (bang_lexer *lexer, char terminator) {
    bool escape = false;
    while (true) {
        if (lexer->next_cursor == lexer->eof) {
            bang_lexer_error(lexer, "unterminated sequence");
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

static int bang_lexer_next_token (bang_lexer *lexer) {
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
            bang_lexer_string(lexer, c);
            break;
        } else if (c == ';') {
            lexer->token = token_comment;
            bang_lexer_string(lexer, '\n');
            break;
        } else {
            lexer->token = token_symbol;
            bang_lexer_symbol(lexer);
            break;
        }
    }
    return lexer->token;
}

/*
static void bang_lexer_test (bang_lexer *lexer) {
    while (bang_lexer_next_token(lexer) != token_eof) {
        int lineno = lexer->lineno;
        int column = bang_lexer_column(lexer);
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
    bang_lexer lexer;

    char *error;
} bang_parser;

static void bang_parser_error (bang_parser *parser, const char *format, ...) {
    if (parser->error == NULL) {
        parser->error = (char *)malloc(1024);
    }
    va_list args;
    va_start (args, format);
    vsnprintf (parser->error, 1024, format, args);
    va_end (args);
}

static void bang_parser_init (bang_parser *parser) {
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

static bool bang_parser_parse_any (bang_parser *parser, bang_expr *result) {
    assert(parser->lexer.token != token_eof);
    if (parser->lexer.token == token_open) {
        bang_init_list(result);
        bang_lexer_init_anchor(&parser->lexer, &result->anchor);
        while (true) {
            bang_lexer_next_token(&parser->lexer);
            if (parser->lexer.token == token_close) {
                break;
            } else if (parser->lexer.token == token_eof) {
                bang_parser_error(parser, "missing closing parens\n");
                break;
            } else {
                if (!bang_parser_parse_any(parser, bang_append(result)))
                    break;
            }
        }
    } else if (parser->lexer.token == token_close) {
        bang_parser_error(parser, "stray closing parens\n");
    } else if (parser->lexer.token == token_string) {
        bang_init_string(result, parser->lexer.string + 1, parser->lexer.string_len - 2);
        result->len = inplace_unescape((char *)result->buf);
        bang_lexer_init_anchor(&parser->lexer, &result->anchor);
    } else if (parser->lexer.token == token_symbol) {
        bang_init_symbol(result, parser->lexer.string, parser->lexer.string_len);
        result->len = inplace_unescape((char *)result->buf);
        bang_lexer_init_anchor(&parser->lexer, &result->anchor);
    } else if (parser->lexer.token == token_comment) {
        bang_init_comment(result, parser->lexer.string + 1, parser->lexer.string_len - 2);
        result->len = inplace_unescape((char *)result->buf);
        bang_lexer_init_anchor(&parser->lexer, &result->anchor);
    } else {
        bang_parser_error(parser, "unexpected token: %c (%i)\n",
            *parser->lexer.cursor, (int)*parser->lexer.cursor);
    }

    if (parser->error)
        bang_expr_clear(result);

    return (parser->error == NULL);
}

static bool bang_parser_parse_naked (bang_parser *parser, bang_expr *result) {
    int lineno = parser->lexer.lineno;
    int column = bang_lexer_column(&parser->lexer);

    bool escape = false;
    int subcolumn = 0;

    bang_init_list(result);
    bang_lexer_init_anchor(&parser->lexer, &result->anchor);

    while (parser->lexer.token != token_eof) {
        if (parser->lexer.token == token_escape) {
            escape = true;
            bang_lexer_next_token(&parser->lexer);
            if (parser->lexer.lineno <= lineno) {
                bang_parser_error(parser, "escape character is not at end of line\n");
                break;
            }
            lineno = parser->lexer.lineno;
        } else if (parser->lexer.lineno > lineno) {
            escape = false;
            if (subcolumn != 0) {
                if (bang_lexer_column(&parser->lexer) != subcolumn) {
                    bang_parser_error(parser, "indentation mismatch\n");
                    break;
                }
            } else {
                subcolumn = bang_lexer_column(&parser->lexer);
            }
            lineno = parser->lexer.lineno;
            if (!bang_parser_parse_naked(parser, bang_append(result)))
                break;
        } else {
            if (!bang_parser_parse_any(parser, bang_append(result)))
                break;
            bang_lexer_next_token(&parser->lexer);
            //bang_parser_error(parser, "unexpected token: %c (%i)\n", *parser->lexer.cursor, (int)*parser->lexer.cursor);
        }

        if ((!escape || (parser->lexer.lineno > lineno)) && (bang_lexer_column(&parser->lexer) <= column))
            break;
    }

    if (parser->error)
        bang_expr_clear(result);
    else {
        assert(result->len > 0);
        if (result->len == 1) {
            // remove list
            bang_expr *tmp = (bang_expr *)result->buf;
            memcpy(result, tmp, sizeof(bang_expr));
            free(tmp);
        }
    }

    return (parser->error == NULL);
}

static bang_expr *bang_parser_parse (bang_parser *parser,
    const char *input_stream, const char *eof, const char *path) {
    bang_lexer_init(&parser->lexer, input_stream, eof, path);
    bang_lexer_next_token(&parser->lexer);
    bool escape = false;

    bang_expr result;
    bang_init_list(&result);
    bang_lexer_init_anchor(&parser->lexer, &result.anchor);

    int lineno = parser->lexer.lineno;
    while (parser->lexer.token != token_eof) {

        if (parser->lexer.token == token_escape) {
            escape = true;
            bang_lexer_next_token(&parser->lexer);
            if (parser->lexer.lineno <= lineno) {
                bang_parser_error(parser, "escape character is not at end of line\n");
                break;
            }
            lineno = parser->lexer.lineno;
        } else if (parser->lexer.lineno > lineno) {
            escape = false;
            lineno = parser->lexer.lineno;
            if (!bang_parser_parse_naked(parser, bang_append(&result)))
                break;
        } else {
            if (!bang_parser_parse_any(parser, bang_append(&result)))
                break;
            bang_lexer_next_token(&parser->lexer);
        }

    }

    if ((parser->lexer.error != NULL) && (parser->error == NULL)) {
        bang_parser_error(parser, "%s", parser->lexer.error);
    }

    bang_lexer_free(&parser->lexer);

    assert(result.len > 0);

    if (parser->error != NULL) {
        bang_expr_clear(&result);
        return NULL;
    } else if (result.len == 0) {
        return NULL;
    } else if (result.len == 1) {
        return (bang_expr *)result.buf;
    } else {
        bang_expr *newresult = (bang_expr *)malloc(sizeof(bang_expr));
        memcpy(newresult, &result, sizeof(bang_expr));
        return newresult;
    }
}

static void bang_parser_free (bang_parser *parser) {
    free(parser->error);
    parser->error = NULL;
}

bang_expr *bang_parse_file (const char *path) {
    int fd = open(path, O_RDONLY);
    off_t length = lseek(fd, 0, SEEK_END);
    void *ptr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (ptr != MAP_FAILED) {
        bang_parser parser;
        bang_parser_init(&parser);
        bang_expr *expr = bang_parser_parse(&parser,
            (const char *)ptr, (const char *)ptr + length,
            path);
        if (parser.error) {
            int lineno = parser.lexer.lineno;
            int column = bang_lexer_column(&parser.lexer);
            printf("%i:%i:%s\n", lineno, column, parser.error);
            assert(expr == NULL);
        }
        bang_parser_free(&parser);

        munmap(ptr, length);
        close(fd);

        return expr;
    } else {
        fprintf(stderr, "unable to open file: %s\n", path);
        return NULL;
    }
}

//------------------------------------------------------------------------------

void print_expr(bang_expr *e, size_t depth)
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
	case bang_expr_type_list:
		sep();
		puts("(");
		for (i = 0; i < e->len; i++)
			print_expr(((bang_expr*)e->buf) + i, depth + 1);
		sep();
		puts(")");
		return;
	case bang_expr_type_symbol:
	case bang_expr_type_string:
    case bang_expr_type_comment:
		sep();
        if (e->type == bang_expr_type_comment) putchar(';');
		else if (e->type == bang_expr_type_string) putchar('"');
		for (i = 0; i < e->len; i++) {
			switch(((char*)e->buf)[i]) {
			case '"':
			case '\\':
				putchar('\\');
				break;
			case ')': case '(':
				if (e->type == bang_expr_type_symbol)
					putchar('\\');
			}

			putchar(((char*)e->buf)[i]);
		}
		if (e->type == bang_expr_type_string) putchar('"');
		putchar('\n');
		return;
    default:
        assert (false); break;
	}
#undef sep
}


//------------------------------------------------------------------------------

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>

#include <map>
#include <string>
#include <vector>
#include <memory>

//------------------------------------------------------------------------------

static LLVMModuleRef bang_module;
static LLVMBuilderRef bang_builder;
static LLVMExecutionEngineRef bang_engine;
static LLVMValueRef bang_nopfunc;

typedef std::map<std::string, LLVMValueRef> NameValueMap;
typedef std::map<std::string, LLVMTypeRef> NameTypeMap;
typedef std::map<std::string, LLVMModuleRef> NameModuleMap;

static NameValueMap NamedValues;
static NameTypeMap NamedTypes;
static NameModuleMap NamedModules;
static int compile_errors = 0;
static bool bang_dump_module = false;

//------------------------------------------------------------------------------

struct bang_field_def {
    std::string name;
    LLVMTypeRef type;
};

struct bang_struct_def {
    std::vector<bang_field_def> fields;
    LLVMTypeRef type;
};

typedef std::map<std::string, std::unique_ptr<bang_struct_def> > NameStructMap;
static NameStructMap NamedStructs;
static NameStructMap TaggedNamedStructs;

//------------------------------------------------------------------------------

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/MultiplexConsumer.h"

#include <sstream>

using namespace clang;
//using namespace llvm;

// createInvocationFromCommandLine()
// attach custom observers
// - diagnostic consumer
// pp callbacks (for module import)
// manually run most of ExecuteAction (?)
// - set up several compiler components
// - parse a single decl from a dummy file
// - finalize the AST

// CompilerInstance::loadModuleFile
// look up decls we want (not possible?)
// use TU-wide lookup and filter

static void bang_error (bang_anchor *anchor, const char *format, ...);

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

/*
    -I/usr/local/include
    -I./clang/lib/clang/3.8.0/include
    -I/usr/include/x86_64-linux-gnu
    -I/usr/include

*/

static int bang_argc;
static char **bang_argv;

class BangCVisitor : public RecursiveASTVisitor<BangCVisitor> {
public:
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

class CodeGenProxy : public ASTConsumer {
public:
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
class BangEmitLLVMOnlyAction : public EmitLLVMOnlyAction {
public:
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

static LLVMModuleRef bang_import_c_module (bang_anchor *anchor,
    const char *modulename, const char *path, const char **args, int argcount) {

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
        bang_error(anchor, "compiler failed\n");
    }

    return M;
}

//------------------------------------------------------------------------------

static const char *bang_expr_type_name(int type) {
    switch(type) {
    case bang_expr_type_none: return "?";
    case bang_expr_type_list: return "list";
    case bang_expr_type_string: return "string";
    case bang_expr_type_symbol: return "symbol";
    case bang_expr_type_comment: return "comment";
    default: return "<corrupted>";
    }
}

static void bang_error (bang_anchor *anchor, const char *format, ...) {
    ++compile_errors;
    printf("%s:%i:%i: error: ", anchor->path, anchor->lineno, anchor->column);
    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}

static bool bang_eq_symbol (bang_expr *expr, const char *sym) {
    return expr && (expr->type == bang_expr_type_symbol) &&
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
    LLVMTypeRef type;
    LLVMValueRef value;
} bang_type_or_value;

typedef struct _bang_env {
    // currently active function
    LLVMValueRef function;
    // local names
    NameValueMap *names;
    // parent env
    struct _bang_env *parent;
} bang_env;

static bang_type_or_value bang_translate (bang_env env, bang_expr *expr);

static bang_expr *bang_verify_type(bang_expr *expr, int type) {
    if (expr) {
        if (expr->type == type) {
            return expr;
        } else {
            bang_error(&expr->anchor, "%s expected\n",
                bang_expr_type_name(type));
        }
    }
    return NULL;
}

static const char *bang_get_string(bang_expr *expr) {
    if (expr) {
        if ((expr->type == bang_expr_type_symbol) || (expr->type == bang_expr_type_string)) {
            return (const char *)expr->buf;
        } else {
            bang_error(&expr->anchor, "string or symbol expected\n");
        }
    }
    return NULL;
}

static LLVMTypeRef bang_translate_type (bang_env env, bang_expr *expr) {
    if (expr) {
        bang_type_or_value result = bang_translate(env, expr);
        if (!result.type && result.value) {
            bang_error(&expr->anchor, "type expected, not value\n");
        }
        return result.type;
    }
    return NULL;
}

static LLVMValueRef bang_translate_value (bang_env env, bang_expr *expr) {
    if (expr) {
        bang_type_or_value result = bang_translate(env, expr);
        if (!result.value && result.type) {
            bang_error(&expr->anchor, "value expected, not type\n");
        }
        return result.value;
    }
    return NULL;
}

static bool bang_verify_arg_range (bang_expr *expr, int mincount, int maxcount) {
    if (expr) {
        if (expr->type == bang_expr_type_list) {
            int argcount = (int)expr->len - 1;
            if ((mincount >= 0) && (argcount < mincount)) {
                bang_error(&expr->anchor, "at least %i arguments expected\n", mincount);
                return false;
            }
            if ((maxcount >= 0) && (argcount > maxcount)) {
                bang_error(&expr->anchor, "at most %i arguments expected\n", maxcount);
                return false;
            }
            return true;
        } else {
            bang_error(&expr->anchor, "list expected\n");
            return false;
        }
    }
    return false;
}

static bool bang_match_expr (bang_expr *expr, const char *name, int mincount, int maxcount) {
    return bang_eq_symbol(bang_nth(expr, 0), name) && bang_verify_arg_range(expr, mincount, maxcount);
}

static LLVMValueRef bang_nopcall () {
    return LLVMBuildCall(bang_builder, bang_nopfunc, NULL, 0, "");
}

static LLVMValueRef bang_translate_exprlist (bang_env env, bang_expr *expr, int offset) {
    int argcount = (int)expr->len - offset;
    bool success = true;
    LLVMValueRef stmt = NULL;
    for (int i = 0; i < argcount; ++i) {
        stmt = bang_translate_value(env, bang_nth(expr, i + offset));
        if (!stmt || compile_errors) {
            success = false;
            stmt = NULL;
            break;
        }
    }

    return stmt;
}

static bang_type_or_value bang_translate (bang_env env, bang_expr *expr) {
    bang_type_or_value result;
    result.type = NULL;
    result.value = NULL;

    if (expr) {
        if (expr->type == bang_expr_type_list) {
            if (expr->len >= 1) {
                bang_expr *head = bang_nth(expr, 0);
                if (head->type == bang_expr_type_symbol) {
                    if (bang_match_expr(expr, "function-type", 2, 2)) {
                        bang_expr *args_expr = bang_verify_type(bang_nth(expr, 2), bang_expr_type_list);

                        if (args_expr) {
                            bang_expr *tail = bang_nth(args_expr, -1);
                            bool vararg = false;
                            int argcount = (int)args_expr->len;
                            if (bang_eq_symbol(tail, "...")) {
                                vararg = true;
                                --argcount;
                            }

                            if (argcount >= 0) {
                                LLVMTypeRef rettype = bang_translate_type(env, bang_nth(expr, 1));
                                if (rettype) {
                                    LLVMTypeRef argtypes[argcount];

                                    bool success = true;
                                    for (int i = 0; i < argcount; ++i) {
                                        LLVMTypeRef argtype = bang_translate_type(env, bang_nth(args_expr, i));
                                        if (!argtype) {
                                            success = false;
                                            break;
                                        }
                                        argtypes[i] = argtype;
                                    }

                                    if (success) {
                                        result.type = LLVMFunctionType(rettype, argtypes, argcount, vararg);
                                    }
                                }
                            } else {
                                bang_error(&expr->anchor, "vararg function is missing return type\n");
                            }
                        }
                    } else if (bang_match_expr(expr, "bitcast", 2, 2)) {

                        LLVMTypeRef casttype = bang_translate_type(env, bang_nth(expr, 1));
                        LLVMValueRef castvalue = bang_translate_value(env, bang_nth(expr, 2));

                        if (casttype && castvalue) {
                            result.value = LLVMBuildBitCast(bang_builder, castvalue, casttype, "ptrcast");
                        }

                    } else if (bang_match_expr(expr, "extract", 2, 2)) {

                        LLVMValueRef value = bang_translate_value(env, bang_nth(expr, 1));
                        LLVMValueRef index = bang_translate_value(env, bang_nth(expr, 2));

                        if (value && index) {
                            result.value = LLVMBuildExtractElement(bang_builder, value, index, "extractelem");
                        }

                    } else if (bang_match_expr(expr, "const-int", 2, 2)) {
                        bang_expr *expr_type = bang_nth(expr, 1);
                        bang_expr *expr_value = bang_verify_type(bang_nth(expr, 2), bang_expr_type_symbol);

                        LLVMTypeRef type;
                        if (expr_type) {
                            type = bang_translate_type(env, expr_type);
                        } else {
                            type = LLVMInt32Type();
                        }

                        if (type && expr_value) {
                            char *end;
                            long long value = strtoll((const char *)expr_value->buf, &end, 10);
                            if (end != ((char *)expr_value->buf + expr_value->len)) {
                                bang_error(&expr_value->anchor, "not a valid integer constant\n");
                            } else {
                                result.value = LLVMConstInt(type, value, 1);
                            }
                        }

                    } else if (bang_match_expr(expr, "const-real", 2, 2)) {
                        bang_expr *expr_type = bang_nth(expr, 1);
                        bang_expr *expr_value = bang_verify_type(bang_nth(expr, 2), bang_expr_type_symbol);

                        LLVMTypeRef type;
                        if (expr_type) {
                            type = bang_translate_type(env, expr_type);
                        } else {
                            type = LLVMDoubleType();
                        }

                        if (type && expr_value) {
                            char *end;
                            double value = strtod((const char *)expr_value->buf, &end);
                            if (end != ((char *)expr_value->buf + expr_value->len)) {
                                bang_error(&expr_value->anchor, "not a valid real constant\n");
                            } else {
                                result.value = LLVMConstReal(type, value);
                            }
                        }

                    } else if (bang_match_expr(expr, "typeof", 1, 1)) {

                        LLVMValueRef value = bang_translate_value(env, bang_nth(expr, 1));
                        if (value) {
                            result.type = LLVMTypeOf(value);
                        }

                    } else if (bang_match_expr(expr, "dump-module", 0, 0)) {

                        bang_dump_module = true;

                    } else if (bang_match_expr(expr, "dump", 1, 1)) {

                        bang_expr *expr_arg = bang_nth(expr, 1);

                        bang_type_or_value tov = bang_translate(env, expr_arg);
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

                    } else if (bang_match_expr(expr, "getelementptr", 2, -1)) {

                        LLVMValueRef ptr = bang_translate_value(env, bang_nth(expr, 1));
                        if (ptr) {
                            int argcount = (int)expr->len - 2;

                            LLVMValueRef indices[argcount];
                            bool success = true;
                            for (int i = 0; i < argcount; ++i) {
                                LLVMValueRef index = bang_translate_value(env, bang_nth(expr, i + 2));
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

                    } else if (bang_match_expr(expr, "?", 3, 3)) {

                        LLVMValueRef cond_value = bang_translate_value(env, bang_nth(expr, 1));
                        if (cond_value) {
                            LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(bang_builder);

                            LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(env.function, "then");
                            LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(env.function, "else");
                            LLVMBasicBlockRef br_block = LLVMAppendBasicBlock(env.function, "br");

                            bang_expr *then_expr = bang_nth(expr, 2);
                            bang_expr *else_expr = bang_nth(expr, 3);

                            LLVMPositionBuilderAtEnd(bang_builder, then_block);
                            LLVMValueRef then_result = bang_translate_value(env, then_expr);
                            LLVMBuildBr(bang_builder, br_block);

                            LLVMPositionBuilderAtEnd(bang_builder, else_block);
                            LLVMValueRef else_result = bang_translate_value(env, else_expr);
                            LLVMBuildBr(bang_builder, br_block);

                            LLVMTypeRef then_type = then_result?LLVMTypeOf(then_result):NULL;
                            LLVMTypeRef else_type = else_result?LLVMTypeOf(else_result):NULL;

                            if ((then_type == LLVMVoidType()) || (else_type == LLVMVoidType())) {
                                LLVMPositionBuilderAtEnd(bang_builder, br_block);
                                result.value = bang_nopcall();
                            } else if (then_type == else_type) {
                                LLVMPositionBuilderAtEnd(bang_builder, br_block);
                                result.value = LLVMBuildPhi(bang_builder, then_type, "select");
                                LLVMValueRef values[] = { then_result, else_result };
                                LLVMBasicBlockRef blocks[] = { then_block, else_block };
                                LLVMAddIncoming(result.value, values, blocks, 2);
                            } else {
                                LLVMDumpType(then_type);
                                LLVMDumpType(else_type);
                                bang_error(&then_expr->anchor, "then/else type evaluation mismatch\n");
                                bang_error(&else_expr->anchor, "then-expression must evaluate to same type as else-expression\n");
                            }

                            LLVMPositionBuilderAtEnd(bang_builder, oldblock);
                            LLVMBuildCondBr(bang_builder, cond_value, then_block, else_block);

                            LLVMPositionBuilderAtEnd(bang_builder, br_block);
                        }

                    } else if (bang_match_expr(expr, "do", 1, -1)) {

                        bang_env subenv = env;
                        subenv.parent = &env;
                        NameValueMap names;
                        subenv.names = NULL;

                        result.value = bang_translate_exprlist(subenv, expr, 1);

                    } else if (bang_match_expr(expr, "function", 3, -1)) {

                        bang_expr *expr_type = bang_nth(expr, 3);

                        bang_expr *expr_name = bang_verify_type(bang_nth(expr, 1), bang_expr_type_symbol);
                        LLVMTypeRef functype = bang_translate_type(env, expr_type);

                        if (expr_name && functype) {
                            if (LLVMGetTypeKind(functype) == LLVMFunctionTypeKind) {
                                // todo: external linkage?
                                LLVMValueRef func = LLVMAddFunction(bang_module, (const char *)expr_name->buf, functype);

                                bang_expr *expr_params = bang_nth(expr, 2);
                                bang_expr *body_expr = bang_nth(expr, 4);

                                NameValueMap names;

                                if (bang_eq_symbol(expr_params, "...")) {
                                    if (body_expr) {
                                        bang_error(&expr_params->anchor, "cannot declare function body without parameter list\n");
                                    }
                                } else if (expr_params->type == bang_expr_type_list) {
                                    int argcount = (int)expr_params->len;
                                    int paramcount = LLVMCountParams(func);
                                    if (argcount == paramcount) {
                                        LLVMValueRef params[paramcount];
                                        LLVMGetParams(func, params);
                                        for (int i = 0; i < argcount; ++i) {
                                            bang_expr *expr_param = bang_verify_type(bang_nth(expr_params, i), bang_expr_type_symbol);
                                            if (expr_param) {
                                                const char *name = (const char *)expr_param->buf;
                                                LLVMSetValueName(params[i], name);
                                                names[name] = params[i];
                                            }
                                        }
                                    } else {
                                        bang_error(&expr_params->anchor, "parameter name count mismatch (%i != %i); must name all parameter types\n",
                                            argcount, paramcount);
                                    }
                                } else {
                                    bang_error(&expr_params->anchor, "parameter list or ... expected\n");
                                }

                                if (!compile_errors) {
                                    result.value = func;

                                    if (body_expr) {
                                        env.function = func;
                                        env.names = &names;

                                        LLVMBasicBlockRef oldblock = LLVMGetInsertBlock(bang_builder);

                                        LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
                                        LLVMPositionBuilderAtEnd(bang_builder, entry);

                                        LLVMValueRef result = bang_translate_exprlist(env, expr, 4);

                                        if (LLVMGetReturnType(functype) == LLVMVoidType()) {
                                            LLVMBuildRetVoid(bang_builder);
                                        } else if (result) {
                                            LLVMBuildRet(bang_builder, result);
                                        } else {
                                            bang_error(&expr->anchor, "function returns no value\n");
                                        }

                                        LLVMPositionBuilderAtEnd(bang_builder, oldblock);

                                    }
                                }
                            } else {
                                bang_error(&expr_type->anchor, "not a function type\n");
                            }
                        }

                    } else if (bang_match_expr(expr, "extern", 2, 2)) {

                        bang_expr *expr_type = bang_nth(expr, 2);

                        bang_expr *expr_name = bang_verify_type(bang_nth(expr, 1), bang_expr_type_symbol);

                        LLVMTypeRef functype = bang_translate_type(env, expr_type);

                        if (expr_name && functype) {
                            if (LLVMGetTypeKind(functype) == LLVMFunctionTypeKind) {
                                // todo: external linkage?
                                LLVMValueRef func = LLVMAddFunction(bang_module, (const char *)expr_name->buf, functype);

                                result.value = func;
                            } else {
                                bang_error(&expr_type->anchor, "not a function type\n");
                            }
                        }

                    } else if (bang_match_expr(expr, "call", 1, -1)) {

                        int argcount = (int)expr->len - 2;

                        bang_expr *expr_func = bang_nth(expr, 1);
                        LLVMValueRef callee = bang_translate_value(env, expr_func);

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
                                        LLVMValueRef value = bang_translate_value(env, bang_nth(expr, i + 2));
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
                                    bang_error(&expr->anchor, "incorrect number of call arguments (got %i, need %s%i)\n",
                                        argcount, isvararg?"at least ":"", arg_size);
                                }
                            } else {
                                bang_error(&expr_func->anchor, "cannot call object\n");
                            }
                        }

                    } else if (bang_match_expr(expr, "import-c", 3, 3)) {
                        const char *modulename = bang_get_string(bang_nth(expr, 1));
                        const char *name = bang_get_string(bang_nth(expr, 2));
                        bang_expr *args_expr = bang_verify_type(bang_nth(expr, 3), bang_expr_type_list);

                        if (modulename && name && args_expr) {
                            int argcount = (int)args_expr->len;
                            const char *args[argcount];
                            bool success = true;
                            for (int i = 0; i < argcount; ++i) {
                                const char *arg = bang_get_string(bang_nth(args_expr, i));
                                if (arg) {
                                    args[i] = arg;
                                } else {
                                    success = false;
                                    break;
                                }
                            }
                            LLVMModuleRef module = bang_import_c_module(&expr->anchor, modulename, name, args, argcount);
                            if (module) {
                                NamedModules[modulename] = module;
                            }
                        }

                    } else if (bang_match_expr(expr, "pointer-type", 1, 1)) {

                        LLVMTypeRef type = bang_translate_type(env, bang_nth(expr, 1));

                        if (type) {
                            result.type = LLVMPointerType(type, 0);
                        }
                    } else {
                        bang_error(&head->anchor, "unhandled special form: %s\n",
                            (const char *)head->buf);
                    }
                } else {
                    bang_error(&head->anchor, "symbol expected\n");
                }
            } else {
                result.value = bang_nopcall();
            }
        } else if (expr->type == bang_expr_type_symbol) {
            const char *name = (const char *)expr->buf;

            result.type = NamedTypes[name];
            result.value = NULL;

            bang_env *penv = &env;
            while (penv) {
                if (penv->names) {
                    result.value = (*penv->names)[name];
                    if (result.value)
                        break;
                }
                penv = penv->parent;
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
                bang_error(&expr->anchor, "no such name: %s\n", name);
            }

        } else if (expr->type == bang_expr_type_string) {

            const char *name = (const char *)expr->buf;
            result.value = LLVMBuildGlobalString(bang_builder, name, "str");
            //result.value = LLVMConstString(name, expr->len, 0);
            assert(result.value);

        } else {

            bang_error(&expr->anchor, "unexpected %s\n",
                bang_expr_type_name(expr->type));
        }
    }

    return result;
}

static void export_external (const char *name, void *addr) {
    LLVMValueRef func = LLVMGetNamedFunction(bang_module, name);
    if (func) {
        LLVMAddGlobalMapping(bang_engine, func, addr);
    }
}

static void bang_compile (bang_expr *expr) {
    bang_module = LLVMModuleCreateWithName("bang");
    NamedModules["bang"] = bang_module;
    bang_builder = LLVMCreateBuilder();

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

    bang_env env;
    env.function = entryfunc;
    env.names = NULL;
    env.parent = NULL;

    if (expr->type == bang_expr_type_list) {
        if (expr->len >= 1) {
            bang_expr *head = bang_nth(expr, 0);
            if (bang_eq_symbol(head, "bang")) {

                LLVMBasicBlockRef entry = LLVMAppendBasicBlock(entryfunc, "entry");
                LLVMPositionBuilderAtEnd(bang_builder, entry);

                for (size_t i = 1; i != expr->len; ++i) {
                    bang_expr *stmt = bang_nth(expr, (int)i);
                    bang_type_or_value tov = bang_translate(env, stmt);
                    if (compile_errors)
                        break;
                    if (tov.value) {
                        lastvalue = tov.value;
                    }
                }
            } else {
                bang_error(&head->anchor, "'bang' expected\n");
            }
        } else {
            bang_error(&expr->anchor, "expression is empty\n");
        }
    } else {
        bang_error(&expr->anchor, "unexpected %s\n",
            bang_expr_type_name(expr->type));
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

        export_external("bang_parse_file", (void *)bang_parse_file);
        export_external("LLVMVoidType", (void *)LLVMVoidType);


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
        bang_expr *expr = bang_parse_file(*argv);
        if (expr) {
            //print_expr(expr, 0);
            bang_compile(expr);

            free(expr);
        } else {
            result = 255;
        }

        ++argv;
    }

    return result;
}

#endif // BANG_HEADER_ONLY
