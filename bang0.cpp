
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
    int type;
    size_t len;
    void *buf;
} bang_expr;

bang_expr *bang_parse_file (const char *path);

}

//------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <assert.h>
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

static void bang_lexer_init (bang_lexer *lexer, const char *input_stream, const char *eof) {
    if (eof == NULL) {
        eof = input_stream + strlen(input_stream);
    }

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
    } else if (parser->lexer.token == token_symbol) {
        bang_init_symbol(result, parser->lexer.string, parser->lexer.string_len);
        result->len = inplace_unescape((char *)result->buf);
    } else if (parser->lexer.token == token_comment) {
        bang_init_comment(result, parser->lexer.string + 1, parser->lexer.string_len - 2);
        result->len = inplace_unescape((char *)result->buf);
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

static bang_expr *bang_parser_parse (bang_parser *parser, const char *input_stream, const char *eof) {
    bang_lexer_init(&parser->lexer, input_stream, eof);
    bang_lexer_next_token(&parser->lexer);
    bool escape = false;

    bang_expr result;
    bang_init_list(&result);

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
        bang_expr *expr = bang_parser_parse(&parser, (const char *)ptr, (const char *)ptr + length);
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

void print_expr(bang_expr *e, int depth)
{
#define sep() for(i = 0; i < depth; i++) printf("    ")
	int i;
	if (!e) return;

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
	}
#undef sep
}

//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
    int result = 0;

    ++argv;
    while (argv && *argv) {
        bang_expr *expr = bang_parse_file(*argv);
        if (expr) {
            print_expr(expr, 0);
            free(expr);
        } else {
            result = 255;
        }

        ++argv;
    }

    return result;
}
