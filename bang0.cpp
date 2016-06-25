
#include <sys/types.h>

//------------------------------------------------------------------------------

extern "C" {

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

}

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
        }
        *dst = *src++;
        if (*dst == 0)
            break;
        dst++;
    }
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

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;
static std::map<std::string, Type *> NamedTypes;
static int compile_errors = 0;

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
    printf("%s:%i:%i: ", anchor->path, anchor->lineno, anchor->column);
    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}

static bool bang_eq_symbol (bang_expr *expr, const char *sym) {
    return expr && (expr->type == bang_expr_type_symbol) &&
        !strncmp((const char *)expr->buf, sym, expr->len);
}

/*
(__functype <returnval> <argtype> ... ['...'])
(__extern <name> <type>)
(__call <name> <arg> ...)
*/

typedef struct {
    Type *type;
    Value *value;
} bang_type_or_value;

static bang_type_or_value bang_translate (bang_expr *expr);

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

static Type *bang_translate_type (bang_expr *expr) {
    if (expr) {
        bang_type_or_value result = bang_translate(expr);
        if (!result.type && result.value) {
            bang_error(&expr->anchor, "type expected, not value\n");
        }
        return result.type;
    }
    return NULL;
}

static Value *bang_translate_value (bang_expr *expr) {
    if (expr) {
        bang_type_or_value result = bang_translate(expr);
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

static bang_type_or_value bang_translate (bang_expr *expr) {
    bang_type_or_value result;
    result.type = NULL;
    result.value = NULL;

    if (expr) {
        if (expr->type == bang_expr_type_list) {
            if (expr->len >= 1) {
                bang_expr *head = bang_nth(expr, 0);
                if (head->type == bang_expr_type_symbol) {
                    if (bang_match_expr(expr, "__functype", 1, -1)) {
                        bang_expr *tail = bang_nth(expr, -1);
                        bool vararg = false;
                        if (bang_eq_symbol(tail, "...")) {
                            vararg = true;
                        } else {
                            ++tail;
                        }

                        ++head;
                        Type *rettype = bang_translate_type(head);
                        if (rettype) {
                            ++head;
                            std::vector<Type *> argtypes;

                            bool failed = false;
                            while (head != tail) {
                                Type *argtype = bang_translate_type(head);
                                if (!argtype) {
                                    failed = true;
                                    break;
                                }
                                argtypes.push_back(argtype);

                                ++head;
                            }

                            if (!failed) {
                                result.type = FunctionType::get(rettype, argtypes, vararg);
                            }
                        }
                    } else if (bang_match_expr(expr, "__function", 2, 3)) {

                        bang_expr *expr_type = bang_nth(expr, 2);

                        bang_expr *expr_name = bang_verify_type(bang_nth(expr, 1), bang_expr_type_symbol);
                        Type *type = bang_translate_type(expr_type);

                        if (expr_name && type) {
                            FunctionType *functype = dyn_cast<FunctionType>(type);

                            if (functype) {
                                Function *func =
                                    Function::Create(functype, Function::ExternalLinkage,
                                        (const char *)expr_name->buf, TheModule.get());
                                result.value = func;

                                Value *body = bang_translate_value(bang_nth(expr, 3));
                                if (body) {

                                    // Create a new basic block to start insertion into.
                                    BasicBlock *BB = BasicBlock::Create(TheContext, "entry", func);
                                    Builder.SetInsertPoint(BB);

                                    Builder.CreateRet(body);

                                    // Validate the generated code, checking for consistency.
                                    if (!verifyFunction(*func)) {
                                        bang_error(&expr->anchor, "error validating function\n");
                                    }

                                    // Error reading body, remove function.
                                    // func->eraseFromParent();
                                }

                            } else {
                                bang_error(&expr_type->anchor, "not a function type\n");
                            }
                        }

                        /*
                        // Set names for all arguments.
                        unsigned Idx = 0;
                        for (auto &Arg : F->args())
                            Arg.setName(Args[Idx++]);
                        */
                    } else if (bang_match_expr(expr, "__call", 1, -1)) {

                        bang_expr *expr_name = bang_verify_type(bang_nth(expr, 1), bang_expr_type_symbol);
                        if (expr_name) {
                            int argcount = (int)expr->len - 2;
                            // Look up the name in the global module table.
                            Function *callee = TheModule->getFunction((const char *)expr_name->buf);
                            if (callee) {
                                if ((callee->isVarArg() && (callee->arg_size() <= (size_t)argcount))
                                    || (callee->arg_size() == (size_t)argcount)) {
                                    bool failed = false;
                                    std::vector<Value *> args;
                                    for (int i = 0; i < argcount; ++i) {
                                        Value *value = bang_translate_value(bang_nth(expr, i + 2));
                                        if (!value) {
                                            failed = true;
                                            break;
                                        }
                                        args.push_back(value);
                                    }

                                    if (!failed) {
                                        result.value = Builder.CreateCall(callee, args, "calltmp");
                                        result.value->dump();
                                        assert(result.value);
                                    }
                                } else {
                                    bang_error(&expr_name->anchor, "incorrect number of arguments\n");
                                }
                            } else {
                                bang_error(&expr_name->anchor, "unknown function\n");
                            }
                        }

                    } else {
                        bang_error(&head->anchor, "unknown special form: %s\n",
                            (const char *)head->buf);
                    }
                } else {
                    bang_error(&head->anchor, "symbol expected\n");
                }
            } else {
                bang_error(&expr->anchor, "expression is empty\n");
            }
        } else if (expr->type == bang_expr_type_symbol) {
            const char *name = (const char *)expr->buf;

            result.type = NamedTypes[name];
            result.value = NamedValues[name];

            if (!result.type && !result.value) {
                bang_error(&expr->anchor, "no such type or value: %s\n", name);
            }

        } else if (expr->type == bang_expr_type_string) {

            const char *name = (const char *)expr->buf;

            result.value = ConstantDataArray::getString(TheContext, name);
            assert(result.value);

        } else {

            bang_error(&expr->anchor, "unexpected %s\n",
                bang_expr_type_name(expr->type));
        }
    }

    return result;
}

static void bang_compile (bang_expr *expr) {
    TheModule = llvm::make_unique<Module>("bang", TheContext);

    NamedTypes["void"] = Type::getVoidTy(TheContext);
    NamedTypes["float"] = Type::getFloatTy(TheContext);
    NamedTypes["double"] = Type::getDoubleTy(TheContext);
    NamedTypes["bool"] = Type::getInt1Ty(TheContext);
    NamedTypes["int8"] = Type::getInt8Ty(TheContext);
    NamedTypes["int16"] = Type::getInt16Ty(TheContext);
    NamedTypes["int32"] = Type::getInt32Ty(TheContext);
    NamedTypes["int64"] = Type::getInt64Ty(TheContext);
    NamedTypes["int"] = Type::getInt32Ty(TheContext);
    NamedTypes["rawstring"] = Type::getInt8Ty(TheContext)->getPointerTo();

    if (expr->type == bang_expr_type_list) {
        if (expr->len >= 1) {
            bang_expr *head = bang_nth(expr, 0);
            if (bang_eq_symbol(head, "bang")) {
                for (size_t i = 1; i != expr->len; ++i) {
                    bang_expr *stmt = bang_nth(expr, (int)i);
                    bang_translate(stmt);
                    if (compile_errors)
                        break;
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

    if (!compile_errors) {

        TheModule->dump();
    }


}


//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
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
