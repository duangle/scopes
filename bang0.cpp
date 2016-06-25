
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

static LLVMModuleRef bang_module;
static LLVMBuilderRef bang_builder;
static LLVMExecutionEngineRef bang_engine;

static std::map<std::string, LLVMValueRef> NamedValues;
static std::map<std::string, LLVMTypeRef> NamedTypes;
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
    printf("%s:%i:%i: error: ", anchor->path, anchor->lineno, anchor->column);
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
(__functype returntype [argtype [...]] [\...])
(__function name type [value])
(__call name value ...)
(__bitcast type value)
(__extract value indexvalue)
(__const-int <value> [<type>])
(__const-real <value> [<type>])
(__gep value index [index [...]])
(__pointer-type type)
(__typeof value)
(__dump value)
*/

typedef struct {
    LLVMTypeRef type;
    LLVMValueRef value;
} bang_type_or_value;

typedef struct {
    // currently active function
    LLVMValueRef function;
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

static bang_type_or_value bang_translate (bang_env env, bang_expr *expr) {
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
                        int argcount = (int)expr->len - 2;
                        if (bang_eq_symbol(tail, "...")) {
                            vararg = true;
                            --argcount;
                        }

                        if (argcount >= 0) {
                            LLVMTypeRef rettype = bang_translate_type(env, bang_nth(expr, 1));
                            if (rettype) {
                                LLVMTypeRef *argtypes = (LLVMTypeRef *)alloca(sizeof(LLVMTypeRef) * argcount);

                                bool success = true;
                                for (int i = 0; i < argcount; ++i) {
                                    LLVMTypeRef argtype = bang_translate_type(env, bang_nth(expr, i + 2));
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
                    } else if (bang_match_expr(expr, "__bitcast", 2, 2)) {

                        LLVMTypeRef casttype = bang_translate_type(env, bang_nth(expr, 1));
                        LLVMValueRef castvalue = bang_translate_value(env, bang_nth(expr, 2));

                        if (casttype && castvalue) {
                            result.value = LLVMBuildBitCast(bang_builder, castvalue, casttype, "ptrcast");
                        }

                    } else if (bang_match_expr(expr, "__extract", 2, 2)) {

                        LLVMValueRef value = bang_translate_value(env, bang_nth(expr, 1));
                        LLVMValueRef index = bang_translate_value(env, bang_nth(expr, 2));

                        if (value && index) {
                            result.value = LLVMBuildExtractElement(bang_builder, value, index, "extractelem");
                        }

                    } else if (bang_match_expr(expr, "__const-int", 2, 2)) {
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

                    } else if (bang_match_expr(expr, "__const-real", 2, 2)) {
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

                    } else if (bang_match_expr(expr, "__typeof", 1, 1)) {

                        LLVMValueRef value = bang_translate_value(env, bang_nth(expr, 1));
                        if (value) {
                            result.type = LLVMTypeOf(value);
                        }

                    } else if (bang_match_expr(expr, "__dump", 1, 1)) {

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

                    } else if (bang_match_expr(expr, "__gep", 2, -1)) {

                        LLVMValueRef ptr = bang_translate_value(env, bang_nth(expr, 1));
                        if (ptr) {
                            int argcount = (int)expr->len - 2;

                            LLVMValueRef *indices = (LLVMValueRef *)alloca(sizeof(LLVMValueRef) * argcount);
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

                    } else if (bang_match_expr(expr, "__block", 0, -1)) {

                    } else if (bang_match_expr(expr, "__function", 2, 3)) {

                        bang_expr *expr_type = bang_nth(expr, 2);

                        bang_expr *expr_name = bang_verify_type(bang_nth(expr, 1), bang_expr_type_symbol);
                        LLVMTypeRef functype = bang_translate_type(env, expr_type);

                        if (expr_name && functype) {
                            if (LLVMGetTypeKind(functype) == LLVMFunctionTypeKind) {
                                // todo: external linkage?
                                LLVMValueRef func = LLVMAddFunction(bang_module, (const char *)expr_name->buf, functype);

                                result.value = func;

                                bang_expr *body_expr = bang_nth(expr, 3);

                                if (body_expr) {
                                    env.function = func;

                                    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
                                    LLVMPositionBuilderAtEnd(bang_builder, entry);

                                    LLVMValueRef body = bang_translate_value(env, body_expr);
                                    if (body) {
                                        LLVMBuildRet(bang_builder, body);

                                        // Error reading body, remove function.
                                        // func->eraseFromParent();
                                    }
                                }
                            } else {
                                bang_error(&expr_type->anchor, "not a function type\n");
                            }
                        }
                    } else if (bang_match_expr(expr, "__call", 1, -1)) {

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

                                    LLVMValueRef *args = (LLVMValueRef *)alloca(sizeof(LLVMValueRef) * argcount);

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
                                        result.value = LLVMBuildCall(bang_builder, callee, args, argcount, "calltmp");
                                    }
                                } else {
                                    bang_error(&expr->anchor, "incorrect number of call arguments (got %i, need %s%i)\n",
                                        argcount, isvararg?"at least ":"", arg_size);
                                }
                            } else {
                                bang_error(&expr_func->anchor, "cannot call object\n");
                            }
                        }

                    } else if (bang_match_expr(expr, "__pointer-type", 1, 1)) {

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
                bang_error(&expr->anchor, "expression is empty\n");
            }
        } else if (expr->type == bang_expr_type_symbol) {
            const char *name = (const char *)expr->buf;

            result.type = NamedTypes[name];
            result.value = NamedValues[name];

            if (!result.value) {
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

static void bang_compile (bang_expr *expr) {
    bang_module = LLVMModuleCreateWithName("bang");
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
    LLVMValueRef entryfunc = LLVMAddFunction(bang_module, "__anon_expr", entryfunctype);
    LLVMValueRef lastvalue = NULL;

    bang_env env;
    env.function = entryfunc;

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
        LLVMDumpModule(bang_module);

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

        printf("running:\n");

        LLVMRunFunction(bang_engine, entryfunc, 0, NULL);

    }

    LLVMDisposeBuilder(bang_builder);
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
