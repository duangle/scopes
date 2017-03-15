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
-- reflect.lua
--------------------------------------------------------------------------------

local function reflect()
    --[[ LuaJIT FFI reflection Library ]]--
    --[[ Copyright (C) 2014 Peter Cawley <lua@corsix.org>. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
    --]]
    local ffi = require "ffi"
    local bit = require "bit"
    local reflect = {}

    local CTState, init_CTState
    local miscmap, init_miscmap

    local function gc_str(gcref) -- Convert a GCref (to a GCstr) into a string
      if gcref ~= 0 then
        local ts = ffi.cast("uint32_t*", gcref)
        return ffi.string(ts + 4, ts[3])
      end
    end

    local typeinfo = ffi.typeinfo or function(id)
      -- ffi.typeof is present in LuaJIT v2.1 since 8th Oct 2014 (d6ff3afc)
      -- this is an emulation layer for older versions of LuaJIT
      local ctype = (CTState or init_CTState()).tab[id]
      return {
        info = ctype.info,
        size = bit.bnot(ctype.size) ~= 0 and ctype.size,
        sib = ctype.sib ~= 0 and ctype.sib,
        name = gc_str(ctype.name),
      }
    end

    local function memptr(gcobj)
      return tonumber(tostring(gcobj):match"%x*$", 16)
    end

    init_CTState = function()
      -- Relevant minimal definitions from lj_ctype.h
      ffi.cdef [[
        typedef struct CType {
          uint32_t info;
          uint32_t size;
          uint16_t sib;
          uint16_t next;
          uint32_t name;
        } CType;

        typedef struct CTState {
          CType *tab;
          uint32_t top;
          uint32_t sizetab;
          void *L;
          void *g;
          void *finalizer;
          void *miscmap;
        } CTState;
      ]]

      -- Acquire a pointer to this Lua universe's CTState
      local co = coroutine.create(function()end) -- Any live coroutine will do.
      local uint32_ptr = ffi.typeof("uint32_t*")
      local G = ffi.cast(uint32_ptr, ffi.cast(uint32_ptr, memptr(co))[2])
      -- In global_State, `MRef ctype_state` is immediately before `GCRef gcroot[GCROOT_MAX]`.
      -- We first find (an entry in) gcroot by looking for a metamethod name string.
      local anchor = ffi.cast("uint32_t", ffi.cast("const char*", "__index"))
      local i = 0
      while math.abs(tonumber(G[i] - anchor)) > 64 do
        i = i + 1
      end
      -- We then work backwards looking for something resembling ctype_state.
      repeat
        i = i - 1
        CTState = ffi.cast("CTState*", G[i])
      until ffi.cast(uint32_ptr, CTState.g) == G

      return CTState
    end

    init_miscmap = function()
      -- Acquire the CTState's miscmap table as a Lua variable
      local t = {}; t[0] = t
      local tvalue = ffi.cast("uint32_t*", memptr(t))[2]
      ffi.cast("uint32_t*", tvalue)[ffi.abi"le" and 0 or 1] = ffi.cast("uint32_t", ffi.cast("uintptr_t", (CTState or init_CTState()).miscmap))
      miscmap = t[0]
      return miscmap
    end

    -- Information for unpacking a `struct CType`.
    -- One table per CT_* constant, containing:
    -- * A name for that CT_
    -- * Roles of the cid and size fields.
    -- * Whether the sib field is meaningful.
    -- * Zero or more applicable boolean flags.
    local CTs = {[0] =
      {"int",
        "", "size", false,
        {0x08000000, "bool"},
        {0x04000000, "float", "subwhat"},
        {0x02000000, "const"},
        {0x01000000, "volatile"},
        {0x00800000, "unsigned"},
        {0x00400000, "long"},
      },
      {"struct",
        "", "size", true,
        {0x02000000, "const"},
        {0x01000000, "volatile"},
        {0x00800000, "union", "subwhat"},
        {0x00100000, "vla"},
      },
      {"ptr",
        "element_type", "size", false,
        {0x02000000, "const"},
        {0x01000000, "volatile"},
        {0x00800000, "ref", "subwhat"},
      },
      {"array",
        "element_type", "size", false,
        {0x08000000, "vector"},
        {0x04000000, "complex"},
        {0x02000000, "const"},
        {0x01000000, "volatile"},
        {0x00100000, "vla"},
      },
      {"void",
        "", "size", false,
        {0x02000000, "const"},
        {0x01000000, "volatile"},
      },
      {"enum",
        "type", "size", true,
      },
      {"func",
        "return_type", "nargs", true,
        {0x00800000, "vararg"},
        {0x00400000, "sse_reg_params"},
      },
      {"typedef", -- Not seen
        "element_type", "", false,
      },
      {"attrib", -- Only seen internally
        "type", "value", true,
      },
      {"field",
        "type", "offset", true,
      },
      {"bitfield",
        "", "offset", true,
        {0x08000000, "bool"},
        {0x02000000, "const"},
        {0x01000000, "volatile"},
        {0x00800000, "unsigned"},
      },
      {"constant",
        "type", "value", true,
        {0x02000000, "const"},
      },
      {"extern", -- Not seen
        "CID", "", true,
      },
      {"kw", -- Not seen
        "TOK", "size",
      },
    }

    -- Set of CType::cid roles which are a CTypeID.
    local type_keys = {
      element_type = true,
      return_type = true,
      value_type = true,
      type = true,
    }

    -- Create a metatable for each CT.
    local metatables = {
    }
    for _, CT in ipairs(CTs) do
      local what = CT[1]
      local mt = {__index = {}}
      metatables[what] = mt
    end

    -- Logic for merging an attribute CType onto the annotated CType.
    local CTAs = {[0] =
      function(a, refct) error("TODO: CTA_NONE") end,
      function(a, refct) error("TODO: CTA_QUAL") end,
      function(a, refct)
        a = 2^a.value
        refct.alignment = a
        refct.attributes.align = a
      end,
      function(a, refct)
        refct.transparent = true
        refct.attributes.subtype = refct.typeid
      end,
      function(a, refct) refct.sym_name = a.name end,
      function(a, refct) error("TODO: CTA_BAD") end,
    }

    -- C function calling conventions (CTCC_* constants in lj_refct.h)
    local CTCCs = {[0] =
      "cdecl",
      "thiscall",
      "fastcall",
      "stdcall",
    }

    local function refct_from_id(id) -- refct = refct_from_id(CTypeID)
      local ctype = typeinfo(id)
      local CT_code = bit.rshift(ctype.info, 28)
      local CT = CTs[CT_code]
      local what = CT[1]
      local refct = setmetatable({
        what = what,
        typeid = id,
        name = ctype.name,
      }, metatables[what])

      -- Interpret (most of) the CType::info field
      for i = 5, #CT do
        if bit.band(ctype.info, CT[i][1]) ~= 0 then
          if CT[i][3] == "subwhat" then
            refct.what = CT[i][2]
          else
            refct[CT[i][2]] = true
          end
        end
      end
      if CT_code <= 5 then
        refct.alignment = bit.lshift(1, bit.band(bit.rshift(ctype.info, 16), 15))
      elseif what == "func" then
        refct.convention = CTCCs[bit.band(bit.rshift(ctype.info, 16), 3)]
      end

      if CT[2] ~= "" then -- Interpret the CType::cid field
        local k = CT[2]
        local cid = bit.band(ctype.info, 0xffff)
        if type_keys[k] then
          if cid == 0 then
            cid = nil
          else
            cid = refct_from_id(cid)
          end
        end
        refct[k] = cid
      end

      if CT[3] ~= "" then -- Interpret the CType::size field
        local k = CT[3]
        refct[k] = ctype.size or (k == "size" and "none")
      end

      if what == "attrib" then
        -- Merge leading attributes onto the type being decorated.
        local CTA = CTAs[bit.band(bit.rshift(ctype.info, 16), 0xff)]
        if refct.type then
          local ct = refct.type
          ct.attributes = {}
          CTA(refct, ct)
          ct.typeid = refct.typeid
          refct = ct
        else
          refct.CTA = CTA
        end
      elseif what == "bitfield" then
        -- Decode extra bitfield fields, and make it look like a normal field.
        refct.offset = refct.offset + bit.band(ctype.info, 127) / 8
        refct.size = bit.band(bit.rshift(ctype.info, 8), 127) / 8
        refct.type = {
          what = "int",
          bool = refct.bool,
          const = refct.const,
          volatile = refct.volatile,
          unsigned = refct.unsigned,
          size = bit.band(bit.rshift(ctype.info, 16), 127),
        }
        refct.bool, refct.const, refct.volatile, refct.unsigned = nil
      end

      if CT[4] then -- Merge sibling attributes onto this type.
        while ctype.sib do
          local entry = typeinfo(ctype.sib)
          if CTs[bit.rshift(entry.info, 28)][1] ~= "attrib" then break end
          if bit.band(entry.info, 0xffff) ~= 0 then break end
          local sib = refct_from_id(ctype.sib)
          sib:CTA(refct)
          ctype = entry
        end
      end

      return refct
    end

    local function sib_iter(s, refct)
      repeat
        local ctype = typeinfo(refct.typeid)
        if not ctype.sib then return end
        refct = refct_from_id(ctype.sib)
      until refct.what ~= "attrib" -- Pure attribs are skipped.
      return refct
    end

    local function siblings(refct)
      -- Follow to the end of the attrib chain, if any.
      while refct.attributes do
        refct = refct_from_id(refct.attributes.subtype or typeinfo(refct.typeid).sib)
      end

      return sib_iter, nil, refct
    end

    metatables.struct.__index.members = siblings
    metatables.func.__index.arguments = siblings
    metatables.enum.__index.values = siblings

    local function find_sibling(refct, name)
      local num = tonumber(name)
      if num then
        for sib in siblings(refct) do
          if num == 1 then
            return sib
          end
          num = num - 1
        end
      else
        for sib in siblings(refct) do
          if sib.name == name then
            return sib
          end
        end
      end
    end

    metatables.struct.__index.member = find_sibling
    metatables.func.__index.argument = find_sibling
    metatables.enum.__index.value = find_sibling

    function reflect.typeof(x) -- refct = reflect.typeof(ct)
      return refct_from_id(tonumber(ffi.typeof(x)))
    end

    function reflect.getmetatable(x) -- mt = reflect.getmetatable(ct)
      return (miscmap or init_miscmap())[-tonumber(ffi.typeof(x))]
    end

    return reflect
end
reflect = reflect()

--------------------------------------------------------------------------------
-- IMPORTS
--------------------------------------------------------------------------------

local ord = string.byte
local tochar = string.char
local format = string.format
local null = nil

local ffi = require 'ffi'
local typeof = ffi.typeof
local istype = ffi.istype
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
-- TYPE
--------------------------------------------------------------------------------

cdef[[
typedef struct _Type {
    int value;
} Type;
]]
local Type

local function define_types(def)
    def('Void')
    def('I32')
    def('I64')

    def('U32')
    def('U64')

    def('R32')

    def('Symbol')
    def('List')
    def('String')
end

do
    local typename = {}
    local cls = {}
    function eq(self, other)
        return self.value == other.value
    end
    function repr(self)
        return "type:" .. typename[self.value]
    end
    Type = ffi.metatype('Type', {
        __index = cls,
        __eq = eq,
        __tostring = repr })

    local idx = 0
    define_types(function(name)
        cls[name] = Type(idx)
        typename[idx] = string.lower(name)
        idx = idx + 1
    end)
end

--------------------------------------------------------------------------------
-- SYMBOL
--------------------------------------------------------------------------------

local name_symbol_map = {}
local symbol_name_map = {}
local get_symbol_name

cdef[[
typedef struct _Symbol {
    int value;
} Symbol;
]]
local Symbol
do
    local cls = {}
    function eq(self, other)
        return self.value == other.value
    end
    function repr(self)
        return "symbol<" .. get_symbol_name(self) .. ">"
    end
    Symbol = ffi.metatype('Symbol', {
        __index = cls,
        __eq = eq,
        __tostring = repr })
end

local next_symbol_id = 0
local function get_symbol(name)
    assert(type(name) == 'string')
    local sym = name_symbol_map[name]
    if (sym == null) then
        sym = next_symbol_id
        next_symbol_id = next_symbol_id + 1
        name_symbol_map[name] = sym
        symbol_name_map[sym] = name
    end
    return Symbol(sym)
end

get_symbol_name = function(sym)
    local name = symbol_name_map[sym.value]
    assert(name ~= null)
    return name
end

local SYM_Unnamed = get_symbol("")

--------------------------------------------------------------------------------
-- ANY
--------------------------------------------------------------------------------

local List

cdef[[
typedef struct _List List;

typedef struct _String {
    const char *ptr;
    int count;
} String;

typedef struct _Any {
    Type type;
    union {
        uint8_t embedded[8];

        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        float r32;
        Symbol symbol;
        List *list;
        String *str;
    };
} Any;
]]
local String = typeof('String')
local Any = typeof('Any')
do
    local cls = {}
    function repr(self)
        if self.type == Type.Void then
            return 'none'
        else
            return '<value of ' .. tostring(self.type) .. '>'
        end
    end
    Any = ffi.metatype('Any', {
        __index = cls,
        __tostring = repr })
end
local none = new(Any, {Type.Void})

local wrap
do
    local typeid_char = reflect.typeof(int8_t).typeid
    wrap = function(value)
        local t = type(value)
        if t == 'cdata' then
            local ct = typeof(value)
            if istype(Symbol, ct) then
                local any = Any()
                any.type = Type.Symbol
                any.symbol = value
                return any
            elseif istype(List, ct) then
                local any = Any()
                any.type = Type.List
                any.list = value
                return any
            end
            local refct = reflect.typeof(value)
            if refct.what == 'array'
                and refct.element_type.typeid == typeid_char then
                local str = String(value, refct.size - 1)
                local any = Any()
                any.type = Type.String
                any.str = str
                return any
            end
        end
        error("unable to wrap " .. tostring(value))
    end
end

--------------------------------------------------------------------------------
-- S-EXPR LEXER / TOKENIZER
--------------------------------------------------------------------------------

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
    function cls.init(input_stream, eof, path, offset)
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
        return wrap(get_symbol(cstr(zstr_from_buffer(dest, size))))
    end
    function cls:get_string()
        local dest = zstr_from_buffer(self.string + 1, self.string_len - 2)
        local size = C.unescape_string(dest)
        return wrap(zstr_from_buffer(dest, size))
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
        -- return copy instead of reference
        return Any(self.number)
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
-- S-EXPR PARSER
--------------------------------------------------------------------------------

cdef[[
struct _List {
    Any at;
    List *next;
    int count;
};
]]

local EOL = cast(typeof('List*'), NULL)
do
    local cls = {}
    function cls.create(at, next)
        local self = List()
        self.at = at
        self.next = next
        if (next ~= EOL) then
            self.count = next.count + 1
        else
            self.count = 1
        end
        return self
    end
    List = ffi.metatype('List', { __index = cls })
end

-- (a . (b . (c . (d . NIL)))) -> (d . (c . (b . (a . NIL))))
-- this is the mutating version; input lists are modified, direction is inverted
local function reverse_list_inplace(l, eol, cat_to)
    eol = eol or EOL
    cat_to = cat_to or EOL
    local next = cat_to
    local count = 0
    if (cat_to ~= EOL) then
        count = cat_to.count
    end
    while (l ~= eol) do
        count = count + 1
        local iternext = l.next
        l.next = next
        l.count = count
        next = l
        l = iternext
    end
    return next
end

local function ListBuilder(lexer)
    local prev = EOL
    local eol = EOL
    local cls = {}
    function cls.append(value)
        prev = List.create(value, prev)
        assert(prev)
    end
    function cls.reset_start()
        eol = prev
    end
    function cls.split()
        -- if we haven't appended anything, that's an error
        if (prev == EOL) then
            error("can't split empty expression")
        end
        -- reverse what we have, up to last split point and wrap result
        -- in cell
        prev = List.create(wrap(reverse_list_inplace(prev, eol)), eol)
        assert(prev)
        cls.reset_start()
    end
    function cls.is_single_result()
        return (prev ~= EOL) and (prev.next == EOL)
    end
    function cls.get_single_result()
        if (prev ~= EOL) then
            return prev.at
        else
            return none
        end
    end
    function cls.get_result()
        return reverse_list_inplace(prev)
    end
    return cls
end

local function parse(lexer)
    local parse_naked
    local parse_any

    -- parses a list to its terminator and returns a handle to the first cell
    local function parse_list(end_token)
        local builder = ListBuilder(lexer)
        lexer:read_token()
        while (true) do
            if (lexer.token == end_token) then
                break
            elseif (lexer.token == Token.escape) then
                local column = lexer:column()
                lexer:read_token()
                builder.append(parse_naked(column, end_token))
            elseif (lexer.token == Token.eof) then
                error("missing closing bracket")
                -- point to beginning of list
                --error_origin = builder.getAnchor();
            elseif (lexer.token == Token.statement) then
                builder.split()
                lexer:read_token()
            else
                builder.append(parse_any())
                lexer:read_token()
            end
        end
        return builder.get_result()
    end

    -- parses the next sequence and returns it wrapped in a cell that points
    -- to prev
    parse_any = function()
        assert(lexer.token ~= Token.eof)
        --auto anchor = lexer.newAnchor();
        if (lexer.token == Token.open) then
            return wrap(parse_list(Token.close))
        elseif (lexer.token == Token.square_open) then
            local list = parse_list(Token.square_close)
            local sym = get_symbol("[")
            return wrap(List.create(sym, list))
        elseif (lexer.token == Token.curly_open) then
            local list = parse_list(Token.curly_close)
            local sym = get_symbol("{")
            return wrap(List.create(sym, list))
        elseif ((lexer.token == Token.close)
            or (lexer.token == Token.square_close)
            or (lexer.token == Token.curly_close)) then
            error("stray closing bracket")
        elseif (lexer.token == Token.string) then
            return lexer:get_string()
        elseif (lexer.token == Token.symbol) then
            return lexer:get_symbol()
        elseif (lexer.token == Token.number) then
            return lexer:get_number()
        else
            error("unexpected token: %c (%i)",
                tochar(lexer.cursor[0]), lexer.cursor[0])
        end
    end

    parse_naked = function(column, end_token)
        local lineno = lexer.lineno

        local escape = false
        local subcolumn = 0

        local builder = ListBuilder(lexer)

        while (lexer.token ~= Token.eof) do
            if (lexer.token == end_token) then
                break
            elseif (lexer.token == Token.escape) then
                escape = true
                lexer:read_token()
                if (lexer.lineno <= lineno) then
                    error("escape character is not at end of line")
                end
                lineno = lexer.lineno
            elseif (lexer.lineno > lineno) then
                if (subcolumn == 0) then
                    subcolumn = lexer:column()
                elseif (lexer:column() ~= subcolumn) then
                    error("indentation mismatch")
                end
                if (column ~= subcolumn) then
                    if ((column + 4) ~= subcolumn) then
                        error("indentations must nest by 4 spaces.")
                    end
                end

                escape = false
                builder.reset_start()
                lineno = lexer.lineno
                -- keep adding elements while we're in the same line
                while ((lexer.token ~= Token.eof)
                        and (lexer.token ~= end_token)
                        and (lexer.lineno == lineno)) do
                    builder.append(parse_naked(subcolumn, end_token))
                end
            elseif (lexer.token == Token.statement) then
                builder.split()
                lexer:read_token()
                -- if we are in the same line, continue in parent
                if (lexer.lineno == lineno) then
                    break
                end
            else
                builder.append(parse_any())
                lineno = lexer.next_lineno
                lexer:read_token()
            end

            if (((not escape) or (lexer.lineno > lineno))
                and (lexer:column() <= column)) then
                break
            end
        end

        if (builder.is_single_result()) then
            return builder.get_single_result()
        else
            return wrap(builder.get_result())
        end
    end

    local function parse_root()
        local builder = ListBuilder(lexer)
        lexer:read_token()
        while (lexer.token ~= Token.eof) do
            if (lexer.token == Token.none) then
                break
            end
            builder.append(parse_naked(1, Token.none))
        end
        return wrap(builder.get_result())
    end

    return parse_root()
end

--------------------------------------------------------------------------------
-- TESTING
--------------------------------------------------------------------------------

local function test_lexer()
    local s = "test\n    test test\n(-1 0x7fffffff 0xffffffff 0xffffffffff 0x7fffffffffffffff 0xffffffffffffffff 0.00012345 1 2 3.5 10.0 1001.0 1001.1 1001.001 1. .1 0.1 .01 0.01 1e-22 3.1415914159141591415914159 inf nan 1.33 1.0 0.0 \"te\\\"st\\n\\ttest!\")test\ntest((1 2; 3 4))\n"

    local lexer = Lexer.init(new(rawstring, s), null, "path")

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




