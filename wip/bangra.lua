--[[
Bangra Interpreter
Copyright (c) 2017 Leonard Ritter

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
--]]

--------------------------------------------------------------------------------
-- STRICT
--------------------------------------------------------------------------------

local mt = getmetatable(_G)
if mt == nil then
  mt = {}
  setmetatable(_G, mt)
end

__STRICT = true
mt.__declared = {}

mt.__newindex = function (t, n, v)
  if __STRICT and not mt.__declared[n] then
    local w = debug.getinfo(2, "S").what
    if w ~= "main" and w ~= "C" then
      error("assign to undeclared variable '"..n.."'", 2)
    end
    mt.__declared[n] = true
  end
  rawset(t, n, v)
end

mt.__index = function (t, n)
  if not mt.__declared[n] and debug.getinfo(2, "S").what ~= "C" then
    error("variable '"..n.."' is not declared", 2)
  end
  return rawget(t, n)
end

function global(...)
   for _, v in ipairs{...} do mt.__declared[v] = true end
end

--------------------------------------------------------------------------------
-- IMPORTS
--------------------------------------------------------------------------------

local ord = string.byte
local tochar = string.char
local format = string.format
local null = nil

local ffi = require 'ffi'
local typeof = ffi.typeof
local new = ffi.new
local cdef = ffi.cdef
local C = ffi.C
local cstr = ffi.string
local cast = ffi.cast
local copy = ffi.copy

local void = typeof('void')
local voidp = typeof('$ *', void)
local voidpp = typeof('$ *', voidp)

local int8_t = typeof('int8_t')
local int16_t = typeof('int16_t')
local int32_t = typeof('int32_t')
local int64_t = typeof('int64_t')

local uint8_t = typeof('uint8_t')
local uint16_t = typeof('uint16_t')
local uint32_t = typeof('uint32_t')
local uint64_t = typeof('uint64_t')

local int = typeof('int')

local size_t = typeof('size_t')

local p_int8_t = typeof('$ *', int8_t)
local vla_int8_t = typeof('$[?]', int8_t)

local rawstring = typeof('const char *')

-- import embedded header
cdef[[
typedef union {
    uintptr_t uintptr;
    void *ptr;
} cast_t;

unsigned char wip_bangra_lua_h[];
unsigned int wip_bangra_lua_h_len;
]]

local cast_t = typeof('cast_t')

local MAP_FAILED = new(cast_t, -1).ptr
local NULL = new(cast_t, 0).ptr

local function zstr_from_buffer(ptr, size)
    local s = new(typeof('$[$]', int8_t, size + 1))
    copy(s, ptr, size)
    s[size] = 0
    return s
end

do
    cdef(cstr(zstr_from_buffer(C.wip_bangra_lua_h, C.wip_bangra_lua_h_len)))
end

local off_t = typeof('__off_t')

local function stderr_writer(x)
    C.fputs(x, C.stderr)
end

local function stdout_writer(x)
    C.fputs(x, C.stdout)
end

local function make_get_enum_name(T)
    local revtable ={}
    for k,v in pairs(T) do
        revtable[v] = k
    end
    return function (k)
        return revtable[k]
    end
end

--------------------------------------------------------------------------------
-- ANSI COLOR FORMATTING
--------------------------------------------------------------------------------

local ANSI = {
RESET           = "\027[0m",
COLOR_BLACK     = "\027[30m",
COLOR_RED       = "\027[31m",
COLOR_GREEN     = "\027[32m",
COLOR_YELLOW    = "\027[33m",
COLOR_BLUE      = "\027[34m",
COLOR_MAGENTA   = "\027[35m",
COLOR_CYAN      = "\027[36m",
COLOR_GRAY60    = "\027[37m",

COLOR_GRAY30    = "\027[30;1m",
COLOR_XRED      = "\027[31;1m",
COLOR_XGREEN    = "\027[32;1m",
COLOR_XYELLOW   = "\027[33;1m",
COLOR_XBLUE     = "\027[34;1m",
COLOR_XMAGENTA  = "\027[35;1m",
COLOR_XCYAN     = "\027[36;1m",
COLOR_WHITE     = "\027[37;1m",
}

local STYLE = {
STRING = ANSI.COLOR_XMAGENTA,
NUMBER = ANSI.COLOR_XGREEN,
KEYWORD = ANSI.COLOR_XBLUE,
OPERATOR = ANSI.COLOR_XCYAN,
INSTRUCTION = ANSI.COLOR_YELLOW,
TYPE = ANSI.COLOR_XYELLOW,
COMMENT = ANSI.COLOR_GRAY30,
ERROR = ANSI.COLOR_XRED,
LOCATION = ANSI.COLOR_XCYAN,
}

local is_tty = (C.isatty(C.fileno(C.stdout)) == 1)
local ansi
if is_tty then
    local reset = ANSI.RESET
    ansi = function(style, x) return style .. x .. reset end
else
    ansi = function(style, x) return x end
end

--------------------------------------------------------------------------------
-- S-EXPR LEXER / TOKENIZER
--------------------------------------------------------------------------------

local Type = {
    I32 = 0,
    I64 = 1,

    U32 = 2,
    U64 = 3,

    R32 = 4,
}

local Token = {
    none = -1,
    eof = 0,
    open = ord('('),
    close = ord(')'),
    square_open = ord('['),
    square_close = ord(']'),
    curly_open = ord('{'),
    curly_close = ord('}'),
    string = ord('"'),
    symbol = ord('S'),
    escape = ord('\\'),
    statement = ord(';'),
    number = ord('N'),
}

local get_token_name = make_get_enum_name(Token)

local token_terminators = new(rawstring, "()[]{}\"';#")

cdef[[
typedef struct _Any {
    size_t type;
    union {
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        float r32;
    };
} Any;
]]
local Any = typeof('Any')

cdef[[
typedef struct _Lexer {
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

    int base_offset;

    int token;
    const char *string;
    int string_len;
    Any number;
} Lexer;
]]

local Lexer
do
    local TAB = ord('\t')
    local CR = ord('\n')
    local BS = ord('\\')

    function verify_good_taste(c)
        if (c == TAB) then
            error("please use spaces instead of tabs.")
        end
    end

    local cls = {}
    function cls.open(input_stream, eof, path, offset)
        offset = offset or 0
        eof = eof or (input_stream + C.strlen(input_stream))

        local self = Lexer()
        self.base_offset = offset
        self.path = path
        self.input_stream = input_stream
        self.eof = eof
        self.next_cursor = input_stream
        self.next_lineno = 1
        self.next_line = input_stream
        return self
    end
    function cls:offset()
        return self.base_offset + (self.cursor - self.input_stream)
    end
    function cls:column()
        return self.cursor - self.line + 1
    end
    function cls:anchor()
        return {
            path = self.path,
            lineno = self.lineno,
            column = self:column(),
            offset = self:offset(),
        }
    end
    function cls:next()
        local c = self.next_cursor[0]
        self.next_cursor = self.next_cursor + 1
        return c
    end
    function cls:is_eof()
        return self.next_cursor == self.eof
    end
    function cls:newline()
        self.next_lineno = self.next_lineno + 1
        self.next_line = self.next_cursor
    end
    function cls:read_symbol()
        local escape = false
        while (true) do
            if (self:is_eof()) then
                break
            end
            local c = self:next()
            if (escape) then
                if (c == CR) then
                    self:newline()
                end
                escape = false
            elseif (c == BS) then
                escape = true
            elseif (0 ~= C.isspace(c)) or (NULL ~= C.strchr(token_terminators, c)) then
                self.next_cursor = self.next_cursor - 1
                break
            end
        end
        self.string = self.cursor
        self.string_len = self.next_cursor - self.cursor
    end
    function cls:read_string(terminator)
        local escape = false
        while (true) do
            if (self:is_eof()) then
                error("unterminated sequence")
                break
            end
            local c = self:next()
            if (c == CR) then
                self:newline()
            end
            if (escape) then
                escape = false
            elseif (c == BS) then
                escape = true
            elseif (c == terminator) then
                break
            end
        end
        self.string = self.cursor
        self.string_len = self.next_cursor - self.cursor
    end
    local pp_int8_t = typeof('$*[1]', int8_t)
    local function make_read_number(desttype, destattr, f)
        return function (self)
            local cendp = new(pp_int8_t)
            local errno = 0
            self.number.type = desttype
            self.number[destattr] = f(self.cursor, cendp, 0)
            local cend = cendp[0]
            if ((cend == self.cursor)
                or (errno == C._ERANGE)
                or (cend >= self.eof)
                or ((0 == C.isspace(cend[0]))
                    and (NULL == C.strchr(token_terminators, cend[0])))) then
                return false
            end
            self.next_cursor = cend
            return true
        end
    end
    cls.read_int64 = make_read_number(Type.I64, "i64", C.strtoll)
    cls.read_uint64 = make_read_number(Type.U64, "u64", C.strtoull)
    cls.read_real32 = make_read_number(Type.R32, "r32",
        function (cursor, cendp, base)
            return C.strtof(cursor, cendp)
        end)
    function cls:next_token()
        self.lineno = self.next_lineno
        self.line = self.next_line
        self.cursor = self.next_cursor
        --self.active_anchor = get_anchor()
    end
    function cls:read_token ()
        local c
        local cc
    ::skip::
        self:next_token()
        if (self:is_eof()) then
            self.token = Token.eof
            goto done
        end
        c = self:next()
        verify_good_taste(c)
        if (c == CR) then
            self:newline()
        end
        if (0 ~= C.isspace(c)) then
            goto skip
        end
        cc = tochar(c)
        if (cc == '#') then
            self:read_string('\n')
            goto skip
        elseif (cc == '(') then self.token = Token.open
        elseif (cc == ')') then self.token = Token.close
        elseif (cc == '[') then self.token = Token.square_open
        elseif (cc == ']') then self.token = Token.square_close
        elseif (cc == '{') then self.token = Token.curly_open
        elseif (cc == '}') then self.token = Token.curly_close
        elseif (cc == '\\') then self.token = Token.escape
        elseif (cc == '"') then self.token = Token.string; self:read_string(c)
        elseif (cc == ';') then self.token = Token.statement
        else
            if (self:read_int64()
                or self:read_uint64()
                or self:read_real32()) then self.token = Token.number
            else self.token = Token.symbol; self:read_symbol() end
        end
    ::done::
        return self.token
    end
    function cls:get_symbol()
        local dest = zstr_from_buffer(self.string, self.string_len)
        local size = C.unescape_string(dest)
        return zstr_from_buffer(dest, size)
    end
    function cls:get_string()
        local dest = zstr_from_buffer(self.string + 1, self.string_len - 2)
        local size = C.unescape_string(dest)
        return zstr_from_buffer(dest, size)
    end
    function cls:get_number()
        if ((self.number.type == Type.I64)
            and (self.number.i64 <= 0x7fffffffll)
            and (self.number.i64 >= -0x80000000ll)) then
            self.number.type = Type.I32
            self.number.i32 = self.number.i64
        elseif ((self.number.type == Type.U64)
            and (self.number.u64 <= 0xffffffffull)) then
            self.number.type = Type.U32
            self.number.u32 = self.number.u64
        end
        return self.number
    end
    function cls:get()
        if (self.token == Token.number) then
            return self:get_number()
        elseif (self.token == Token.symbol) then
            return self:get_symbol()
        elseif (self.token == Token.string) then
            return self:get_string()
        end
    end

    Lexer = ffi.metatype('Lexer', { __index = cls })
end

--[[
    int readToken () {
        lineno = next_lineno;
        line = next_line;
        cursor = next_cursor;
        while (true) {
            if (next_cursor == eof) {
                token = token_eof;
                break;
            }
            char c = next();
            if (!verifyGoodTaste(c)) break;
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
            } else if (readInteger() || readUInteger()) {
                token = token_integer;
                break;
            } else if (readReal()) {
                token = token_real;
                break;
            } else {
                token = token_symbol;
                readSymbol();
                break;
            }
        }
        return token;
    }

    Any getAsString() {
        // TODO: anchor
        auto result = make_any(TYPE_String);
        auto s = alloc_string(string + 1, string_len - 2);
        unescape(*s);
        result.str = s;
        return result;
    }

    Any getAsSymbol() {
        // TODO: anchor
        std::string s(string, string_len);
        inplace_unescape(const_cast<char *>(s.c_str()));
        return bangra::symbol(s);
    }

    Any getAsInteger() {
        // TODO: anchor
        size_t width;
        if (is_unsigned) {
            width = ((uint64_t)integer > (uint64_t)INT_MAX)?64:32;
        } else {
            width =
                ((integer < (int64_t)INT_MIN) || (integer > (int64_t)INT_MAX))?64:32;
        }
        auto type = Types::Integer(width, !is_unsigned);
        return bangra::integer(type, this->integer);
    }

    Any getAsReal() {
        return bangra::real(TYPE_R32, this->real);
    }

};
--]]

--------------------------------------------------------------------------------
-- SOURCE FILE
--------------------------------------------------------------------------------

cdef[[
typedef struct _SourceFile {
    int fd;
    off_t length;
    void *ptr;
} SourceFile;
]]

local SourceFile
do
    local cls = {}
    function cls.open(path)
        local file = ffi.gc(SourceFile(-1, 0, null), cls.close)
        file.fd = C.open(path, C._O_RDONLY)
        if (file.fd >= 0) then
            file.length = C.lseek(file.fd, 0, C._SEEK_END)
            file.ptr = C.mmap(null,
                file.length, C._PROT_READ, C._MAP_PRIVATE, file.fd, 0)
            if (file.ptr ~= MAP_FAILED) then
                return file
            end
        end
        file:close()
    end
    function cls.close(self)
        if (self.ptr ~= MAP_FAILED) then
            C.munmap(self.ptr, self.length)
            self.ptr = MAP_FAILED
            self.length = 0
        end
        if (self.fd >= 0) then
            C.close(self.fd)
            self.fd = -1
        end
    end
    function cls.is_open(self)
        return (self.fd ~= -1)
    end
    function cls.strptr(self)
        assert(self:is_open())
        return cast(rawstring, self.ptr);
    end
    local CR = ord('\n')
    function cls.dump_line(self, offset, writer)
        local str = self:strptr()
        if (offset >= self.length) then
            return
        end
        local start = offset
        local send = offset
        while (start > 0) do
            if (str[start-1] == CR) then
                break
            end
            start = start - 1
        end
        while (send < self.length) do
            if (str[send] == CR) then
                break
            end
            send = send + 1
        end
        local line = zstr_from_buffer(str + start, send - start)
        writer(cstr(line))
        writer("\n")
        local column = offset - start
        for i=1,column do
            writer(' ')
        end
        writer(ansi(STYLE.OPERATOR, '^'))
        writer("\n")
    end

    SourceFile = ffi.metatype('SourceFile', { __index = cls })
end

--------------------------------------------------------------------------------
-- TESTING
--------------------------------------------------------------------------------

local function test_lexer()
    local s = "test\n    test test\n(-1 0x7fffffff 0xffffffff 0xffffffffff 0x7fffffffffffffff 0xffffffffffffffff 0.00012345 1 2 3.5 10.0 1001.0 1001.1 1001.001 1. .1 0.1 .01 0.01 1e-22 3.1415914159141591415914159 inf nan 1.33 1.0 0.0 \"te\\\"st\\n\\ttest!\")test\ntest((1 2; 3 4))\n"

    local lexer = Lexer.open(new(rawstring, s), null, "path")
    local token = lexer:read_token()
    while (token ~= Token.eof) do
        local s = lexer:get()
        if (token == Token.symbol or token == Token.string) then
            s = cstr(s)
        elseif (token == Token.number) then
            if (s.type == Type.I32) then s = s.i32
            elseif (s.type == Type.I64) then s = s.i64
            elseif (s.type == Type.U32) then s = s.u32
            elseif (s.type == Type.U64) then s = s.u64
            elseif (s.type == Type.R32) then s = s.r32
            end
        end
        print(get_token_name(token), s)

        token = lexer:read_token()
    end

end

local function testf()
    local q = "hello " .. "\\n\\\"world!"
    local s = new(c_int8vla_t, #q+1, q);
    C.unescape_string(s)
    print(cstr(s), C.strlen(s))
    print(q)

    local sz = C.escape_string(null, s, C.strlen(s), '"')
    local ss = new(c_int8vla_t, sz+1)
    C.escape_string(ss, s, C.strlen(s), '"')
    print(cstr(ss))
end

--testf()
test_lexer()




