
__all__ = [
    "compile",
    "compilefile",
    "ast_type",
    "ast_islist",
    "ast_issymbol",
    "ast_isnumber",
    "ast_isstring",
    "ASTList",
]

import re
import os

MODULEPATH = os.path.dirname(os.path.abspath(__file__))

LIST_COMMENT = "///"
SYMBOL_COMMENT = "#"

def ast_type(x):
    t = type(x)
    if t == ASTList: return "list"
    elif t == str:
        if x[0:1] == '"':
            return "string"
        else:
            return "symbol"
    elif t == float: return "number"
    else:
        return None

def ast_islist(x):
    return type(x) == ASTList
def ast_issymbol(x):
    return type(x) == str and x[0:1] != '"'
def ast_isnumber(x):
    return type(x) == float
def ast_isstring(x):
    return type(x) == str and x[0:1] == '"'

def islistcomment(x):
    return (ast_islist(x) and len(x) and ast_issymbol(x[0]) and x[0].startswith(LIST_COMMENT))

def islinecomment(x):
    return (ast_issymbol(x) and x[0].startswith(SYMBOL_COMMENT))

def iscomment(x):
    return islistcomment(x) or islinecomment(x)

def isnocomment(x):
    return not iscomment(x)

def stripiflist(x):
    if ast_islist(x):
        return x.strip()
    else:
        return x

def trygetanchor(x):
    if ast_islist(x):
        return x.anchor

def trysetanchor(x, a):
    if a and ast_islist(x):
        x.setanchor(a)
    return x

class ASTList(list):
    def setanchor(self, anchor):
        self.anchor = anchor
        return self

    def append(self, obj):
        assert(ast_type(obj))
        list.append(self, obj)

    def fold(self, z, f):
        result = z
        for e in self:
            result = f(result, e)
        return result

    def filter_map(self, f, m):
        def pred(a,b):
            if f(b):
                a.append(trysetanchor(m(b),trygetanchor(b)))
            return a
        return ASTList.fold(self, ASTList(), pred).setanchor(self.anchor)

    def strip (self):
        return ASTList.filter_map(self, isnocomment, stripiflist)

class ParserAnchor(object):
    origin = "parser"

    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)
        assert self.path
        assert self.lineno != None
        assert self.offset != None
        assert self.column != None

    def __repr__(self):
        return '<Anchor %s:%i:%i>' % (self.path, self.lineno, self.column)

    @classmethod
    def empty(cls):
        return cls(path='?',lineno=1,offset=0,column=1)

class TokenStream(object):
    def __init__(self, s, anchor=None):
        assert type(s) == str
        self.str = s
        self.anchor = anchor or \
            ParserAnchor.empty()

    def __str__(self):
        return self.str

    def __repr__(self):
        return '<TokenStream %s>' % (repr(self.str,))

    def find (self, pattern, start=0):
        m = re.search(pattern, self.str[start:])
        if m:
            return m.start()+start, m.end()+start
        else:
            return None, None

    def match(self, pattern):
        m = re.match(pattern, self.str)
        if m:
            return m.group(0)
        else:
            return None

    def __len__(self):
        return len(self.str)

    def __add__(self, other):
        return str(self) + str(other)

    def equals(self, other):
        return str(self) == str(other)

    def __getitem__(self, *args, **kargs):
        return self.str.__getitem__(*args, **kargs)

    def count_newlines(self, i=None, column=0):
        count = 0
        s = self.str
        if i is None:
            i = len(s)
        for k in xrange(0,i):
            if s[k] == '\n':
                count = count + 1
                column = 1
            else:
                column = column + 1
        return count, column

    def subtoken(self, i):
        assert(i >= 0)
        anchor = self.anchor
        count, column = self.count_newlines(i, anchor.column)
        if count > 0 or i > 0:
            anchor = ParserAnchor(
                path=anchor.path,
                lineno=anchor.lineno+count,
                offset=anchor.offset+max(0,i-1),
                column=column)
        return TokenStream(self.str[i:], anchor)

class ParserException(Exception):
    pass

def error(msg):
    raise ParserException, msg

def anchor_error(anchor, msg):
    error(("\n%s:%s:%s: %s") % (
        anchor.path,
        anchor.lineno or "?",
        anchor.column or "?",
        msg))

def anchor_warning(anchor, msg):
    if true:
        anchor_error(anchor, msg)
    else:
        print(("\n%s:%s:%s: warning: %s") % (
            anchor.path,
            anchor.lineno or "?",
            anchor.column or "?",
            msg))

def tokenstring(expr):
    i = 1
    r = ""
    while True:
        s, e = expr.find(r'[\\"]', i)
        if s is None:
            anchor_error(expr.anchor, "string is missing closing quote")
        if expr[s:e] == '\\':
            if expr[e] == '"':
                r = r + expr[i:s] + expr[e]
            else:
                r = r + expr[i:e+1]
            i = e+1
        else:
            r = r + expr[i:s]
            return '"' + r, expr.subtoken(e), expr

coatedreserved = {
    "[" : True,
    "]" : True,
    "{" : True,
    "}" : True,
}

def tokencoated(expr):
    assert type(expr) == TokenStream
    s, e = expr.find(r"^\s+")
    if not (s is None):
        expr = expr.subtoken(e)

    ch = expr[0:1]
    if ch == "(":
        return "(", expr.subtoken(1), expr
    elif ch == ")":
        return ")", expr.subtoken(1), expr
    elif ch == "\"":
        return tokenstring(expr)
    elif ch == ";":
        s, e = expr.find("\n")
        if s is None:
            s, e = 0, len(expr)
        return expr[:e], expr.subtoken(e), expr
    elif coatedreserved.get(ch,False):
        error(ch + " is a reserved character")
    else:
        t = expr.match("^[^][{(\s)}]+")
        if t:
            return t, expr.subtoken(len(t)), expr
        else:
            return None, expr, expr

nakedreserved = {
    "," : True,
    "[" : True,
    "]" : True,
    "{" : True,
    "}" : True,
}

def tokennaked(expr):
    assert type(expr) == TokenStream
    s, e = expr.find(r"^\s+")
    if not (s is None):
        expr = expr.subtoken(e)

    ch = expr[0:1]
    if ch == "(":
        return "(", expr.subtoken(1), expr
    elif ch == ")":
        return ")", expr.subtoken(1), expr
    elif ch == "\"":
        return tokenstring(expr)
    elif ch == ";":
        s, e = expr.find("\n")
        if s is None:
            s, e = 0, len(expr)
        return expr[:e], expr.subtoken(e), expr
    elif ch == "\\":
        return "\\", expr.subtoken(1), expr
    elif nakedreserved.get(ch,False):
        error(ch + " is a reserved character")
    else:
        t = expr.match("^[^][\\\\{(\s)}@,]+")
        if t:
            return t, expr.subtoken(len(t)), expr
        else:
            return None, expr, expr

def void ():
    pass

def tokenize(iter, path, opts=None):
    opts = opts or {}
    iter = iter or ""
    if callable(iter):
        part = iter()
    elif type(iter) == unicode:
        part = iter.encode('utf-8')
        iter = void
    elif type(iter) == str:
        part = iter
        iter = void
    else:
        error("Unexpected stream type: " + str(type(iter)))

    part = TokenStream(part, ParserAnchor(path=path,lineno=1,offset=0,column=1))

    if opts.get('strip_hashbang',False):
        s, e = part.find("^\s+")
        if not (s is None):
            part = part.subtoken(e)

        if part[0:1] == "#!":
            # hashbang, skip
            s, e = part.find("\n")
            assert s != None, "newline expected"
            part = part.subtoken(e)

    class _(object):
        _part = part

    def tokenize_part (tokenfunc):
        t, _._part, lastt = tokenfunc(_._part)

        if t:
            return t, (lastt and lastt.anchor)

        next_part = iter()

        if not next_part:
            if len(_._part) > 0:
                error("Syntax error: Unparsed tail: '" + str(_._part) + "'")
            return None, None

        _._part = _._part + next_part

        return tokenize_part()

    return tokenize_part

tok_closed = '(closed)'
tok_eof = '(eof)'
tok_wrap = '(wrap)'

def parseany(tokens, t, anchor):
    if t == "(":
        result = ASTList()
        while True:
            tt, rest = parsecoated(tokens)
            if tt == tok_closed:
                break
            elif tt == tok_eof:
                anchor_warning(anchor, 'missing closing parens')
                break
            result.append(tt)
        return result.setanchor(anchor)
    elif t == ")":
        anchor_error(anchor, 'stray closing parens')
    elif t == None:
        return tok_eof
    elif t[0] == "\"":
        return '"' + t[1:].decode('string_escape')
    else:
        if not t.startswith("0x"):
            try:
                n = float(t)
                return n
            except:
                pass
        return t

def parsecoated(tokens):
    t, anchor = tokens(tokencoated)
    if t == ")":
        return tok_closed, anchor
    else:
        return parseany(tokens, t, anchor), anchor

def wraplist(anchor, exprs):
    return ASTList(exprs).setanchor(anchor)

def parsenaked(tokens, t, anchor):
    column = anchor.column
    lineno = anchor.lineno
    result = ASTList()
    mainanchor = anchor
    escaped = False
    tt = None
    subcolumn = None
    while True:
        if t == "\\":
            escaped = True
            prevanchor = anchor
            t, anchor = tokens(tokennaked)
            if anchor.lineno <= lineno:
                anchor_error(prevanchor, "escape character is not at end of line")
            lineno = anchor.lineno
        elif anchor.lineno > lineno:
            escaped = False
            if subcolumn != None:
                if anchor.column != subcolumn:
                    anchor_error(anchor, "indentation mismatch")
            else:
                subcolumn = anchor.column
            lineno = anchor.lineno
            tt, anchor, t = parsenaked(tokens, t, anchor)
            result.append(tt)
        elif t == "@":
            anchor_error(anchor, "illegal character")
        else:
            tt = parseany(tokens, t, anchor)
            t, anchor = tokens(tokennaked)
            result.append(tt)
        if t == None or ((not escaped
                    or anchor.lineno > lineno)
                and anchor.column <= column):
            break
    assert len(result) > 0
    if len(result) == 1:
        return result[0], anchor, t
    else:
        result = result.setanchor(mainanchor)
        return result, anchor, t

def parseroot(tokens):
    t, anchor = tokens(tokennaked)
    if t == None:
        return tok_eof, anchor

    lineno = anchor.lineno

    result = ASTList()
    mainanchor = anchor
    escaped = False
    tt = None
    while True:
        if t == "\\":
            escaped = True
            t, anchor = tokens(tokennaked)
            if anchor.lineno <= lineno:
                anchor_error(anchor, "escape character is not at end of line")
            lineno = anchor.lineno
        elif anchor.lineno > lineno:
            escaped = False
            lineno = anchor.lineno
            tt, anchor, t = parsenaked(tokens, t, anchor)
            result.append(tt)
        elif t == "@":
            anchor_error(anchor, "illegal character")
        else:
            tt = parseany(tokens, t, anchor)
            t, anchor = tokens(tokennaked)
            result.append(tt)
        if t == None:
            break
    assert len(result) > 0
    if len(result) == 1:
        return result[0], mainanchor
    else:
        result = result.setanchor(mainanchor)
        return result, mainanchor

def compile(tokens, path, opts=None):
    result = None
    opts = opts or {}
    tokens = tokenize(tokens, path, opts)
    while True:
        form, anchor = parseroot(tokens)
        if form == tok_eof:
            break
        elif result == None:
            result = form
        else:
            anchor_warning(anchor, 'extra characters after end of root expression')
            break
    return result

def compilefile(fileobj, path=None, opts=None):
    if type(fileobj) == str:
        f = file(fileobj, 'rb')
        obj = f.read()
        f.close()
        f = obj
    else:
        assert(type(path) == str)
        f = fileobj
    opts = opts or {}
    opts['strip_hashbang'] = True
    return compile(f, path or fileobj, opts)

if __name__ == "__main__":
    from pprint import pprint
    d = compilefile(os.path.join(MODULEPATH, "..", "testing", "test_naked.n"))
    d = d.strip() # remove comments
    pprint(d, indent=4)
