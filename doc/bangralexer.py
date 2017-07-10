#from setuptools import setup

from pygments.lexer import Lexer
from pygments.token import *

class TOK:
    none = -1
    eof = 0
    open = '('
    close = ')'
    square_open = '['
    square_close = ']'
    curly_open = '{'
    curly_close = '}'
    string = '"'
    quote = '\''
    symbol = 'S'
    escape = '\\'
    statement = ';'
    number = 'N'

TOKEN_TERMINATORS = "()[]{}\"';#,"

types = set("""bool i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 Scope Label Parameter
string list Symbol Syntax Nothing type Any
""".strip().split())
keywords = set("""fn let if elseif else label return syntax-extend
""".strip().split())
builtins = set("""print import-from branch icmp== icmp!= icmp<s icmp<=s icmp>s
icmp>=s icmp<u icmp<=u icmp>u icmp>=u 
fcmp==o fcmp!=o fcmp<o fcmp<=o fcmp>o fcmp>=o
fcmp==u fcmp!=u fcmp<u fcmp<=u fcmp>u fcmp>=u
extractvalue insertvalue compiler-error! io-write abort! unreachable! undef
ptrtoint inttoptr bitcast typeof string-join va-countof va@ type@ Scope@
set-type-symbol! set-scope-symbol! 
band bor bxor add sub mul udiv sdiv urem srem zext sext trunc shl ashr lshr
fadd fsub fmul fdiv frem fpext fptrunc
fptosi fptoui sitofp uitofp signed? bitcountof getelementptr
Any-repr Any-wrap integer-type unconst purify compile typify Any-extract-constant
string->Symbol dump dump-label constant? load store
""".strip().split())
builtin_constants = set("""true false none syntax-scope
""".strip().split())
operators = set("""+ - * / << >> & | ^ ~ % . < > <= >= != == = ..
""".strip().split())
word_operators = set("""not
""".strip().split())

integer_literal_suffixes = set("i8 i16 i32 i64 u8 u16 u32 u64 usize".strip().split())
real_literal_suffixes = set("f32 f64".strip().split())

print(keywords)

def isspace(c):
    return c in ' \t\n\r'

class BangraLexer(Lexer):
    def __init__(self, **options):
        Lexer.__init__(self, **options)

    def get_tokens_unprocessed(self, text):
        state = type('state', (), {})
        state.cursor = 0
        state.next_cursor = 0
        state.lineno = 1
        state.next_lineno = 1
        state.line = 0
        state.next_line = 0
        def is_eof():
            return state.next_cursor == len(text)
        def next():
            x = text[state.next_cursor]
            state.next_cursor = state.next_cursor + 1
            return x
        def next_token():
            state.lineno = state.next_lineno
            state.line = state.next_line
            state.cursor = state.next_cursor
        def newline():
            state.next_lineno = state.next_lineno + 1
            state.next_line = state.next_cursor
        def select_string():
            state.value = text[state.cursor:state.next_cursor]
        def reset_cursor():
            state.next_cursor = state.cursor
        def try_fmt_split(s):
            l = s.split(':')
            if len(l) == 2:
                return l
            else:
                return s,None

        def is_integer(s):
            tail = None
            if ':' in s:
                s,tail = try_fmt_split(s)
            if tail and not (tail in integer_literal_suffixes):
                return False
            if not s:
                return False
            if s[0] in '+-':
                s = s[1:]
            nums = '0123456789'
            if s.startswith('0x'):
                nums = nums + 'ABCDEFabcdef'
                s = s[2:]
            elif len(s) > 1 and s[0] == '0':
                return False
            if len(s) == 0:
                return False
            for k,c in enumerate(s):
                if not c in nums:
                    return False
            return True
        def is_real(s):
            tail = None
            if ':' in s:
                s,tail = try_fmt_split(s)
            if tail and not (tail in real_literal_suffixes):
                return False
            if not s: return False
            if s[0] in '+-':
                s = s[1:]
            if s == 'inf' or s == 'nan':
                return True
            nums = '0123456789'
            if s.startswith('0x'):
                nums = nums + 'ABCDEFabcdef'
                s = s[2:]
            if len(s) == 0:
                return False
            for k,c in enumerate(s):
                if c == 'e':
                    return is_integer(s[k + 1:])
                if c == '.':
                    s = s[k + 1:]
                    for k,c in enumerate(s):
                        if c == 'e':
                            return is_integer(s[k + 1:])
                        if not c in nums:
                            return False
                    break
                if not c in nums:
                    return False
            return True
        def read_symbol():
            escape = False
            while True:
                if is_eof():
                    break
                c = next()
                if escape:
                    if c == '\n':
                        newline()
                    escape = False
                elif c == '\\':
                    escape = True
                elif isspace(c) or (c in TOKEN_TERMINATORS):
                    state.next_cursor = state.next_cursor - 1
                    break
            select_string()
        def read_string(terminator):
            escape = False
            while True:
                if is_eof():
                    raise Exception("unterminated sequence")
                    break
                c = next()
                if c == '\n':
                    newline()
                if escape:
                    escape = False
                elif c == '\\':
                    escape = True
                elif c == terminator:
                    break
            select_string()
        def column():
            return state.cursor - state.line + 1
        def next_column():
            return state.next_cursor - state.next_line + 1
        def read_comment():
            col = column()
            while True:
                if is_eof():
                    break
                next_col = next_column()
                c = next()
                if c == '\n':
                    newline()
                elif not isspace(c) and (next_col <= col):
                    state.next_cursor = state.next_cursor - 1
                    break
            select_string()
        def read_whitespace():
            while True:
                if is_eof():
                    break
                c = next()
                if c == '\n':
                    newline()
                elif not isspace(c):
                    state.next_cursor = state.next_cursor - 1
                    break
            select_string()
        while True:
            next_token()
            if is_eof():
                return
            c = next()
            cur = state.cursor
            if c == '\n':
                newline()
            if isspace(c):
                token = Token.Whitespace
                read_whitespace()
            elif c == '#':
                token = Token.Comment
                read_comment()
            elif c == '(':
                token = Token.Punctuation.Open
                select_string()
            elif c == ')':
                token = Token.Punctuation.Close
                select_string()
            elif c == '[':
                token = Token.Punctuation.Square.Open
                select_string()
            elif c == ']':
                token = Token.Punctuation.Square.Close
                select_string()
            elif c == '{':
                token = Token.Punctuation.Curly.Open
                select_string()
            elif c == '}':
                token = Token.Punctuation.Curly.Close
                select_string()
            elif c == '\\':
                token = Token.Punctuation.Escape
                select_string()
            elif c == '"': 
                token = Token.String
                read_string(c)
            elif c == ';':
                token = Token.Punctuation.Statement.Separator
                select_string()
            elif c == '\'':
                token = Token.Punctuation.Quote
                select_string()
            elif c == ',':
                token = Token.Punctuation.Comma
                select_string()
            else:
                read_symbol()
                if state.value in keywords:
                    token = Token.Keyword.Declaration
                elif state.value in builtins:
                    token = Token.Name.Builtin
                elif state.value in types:
                    token = Token.Keyword.Type
                elif state.value in builtin_constants:
                    token = Token.Keyword.Constant
                elif state.value in operators:
                    token = Token.Operator
                elif state.value in word_operators:
                    token = Token.Operator.Word
                elif is_integer(state.value):
                    token = Token.Number.Integer
                elif is_real(state.value):
                    token = Token.Number.Float
                else:
                    token = Token.Name
            #print state.cursor, token, repr(state.value)
            yield state.cursor, token, state.value

def setup(app):
    app.add_lexer("bangra", BangraLexer())
