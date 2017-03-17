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
-- verify luajit was built with the right flags

do

    local t = setmetatable({count=10}, {
        __len = function(x)
            return x.count
        end})
    -- the flag can be uncommented in luajit/src/Makefile
    assert(#t == 10, "luajit must be built with -DLUAJIT_ENABLE_LUA52COMPAT")
end

--------------------------------------------------------------------------------
-- strict.lua
--------------------------------------------------------------------------------

local mt = getmetatable(_G)
if mt == nil then
  mt = {}
  setmetatable(_G, mt)
end

__STRICT = true
mt.__declared = {
    global = true
}

mt.__newindex = function (t, n, v)
  if __STRICT and not mt.__declared[n] then
    --local w = debug.getinfo(2, "S").what
    --if w ~= "main" and w ~= "C" then
    error("assign to undeclared variable '"..n.."'", 2)
    --end
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
-- 30log.lua
--------------------------------------------------------------------------------

local function class()
    local assert, pairs, type, tostring, setmetatable = assert, pairs, type, tostring, setmetatable
    local baseMt, _instances, _classes, _class = {}, setmetatable({},{__mode='k'}), setmetatable({},{__mode='k'})
    local function assert_class(class, method) assert(_classes[class], ('Wrong method call. Expected class:%s.'):format(method)) end
    local function deep_copy(t, dest, aType) t = t or {}; local r = dest or {}
      for k,v in pairs(t) do
        if aType and type(v)==aType then r[k] = v elseif not aType then
          if type(v) == 'table' and k ~= "__index" then r[k] = deep_copy(v) else r[k] = v end
        end
      end; return r
    end
    local function instantiate(self,...)
      assert_class(self, 'new(...) or class(...)'); local instance = {class = self}; _instances[instance] = tostring(instance); setmetatable(instance,self)
      if self.init then if type(self.init) == 'table' then deep_copy(self.init, instance) else self.init(instance, ...) end; end; return instance
    end
    local function extend(self, name, extra_params)
      assert_class(self, 'extend(...)'); local heir = {}; _classes[heir] = tostring(heir); deep_copy(extra_params, deep_copy(self, heir));
      heir.name, heir.__index, heir.super = extra_params and extra_params.name or name, heir, self; return setmetatable(heir,self)
    end
    baseMt = { __call = function (self,...) return self:new(...) end, __tostring = function(self,...)
      if _instances[self] then return ("instance of '%s' (%s)"):format(rawget(self.class,'name') or '?', _instances[self]) end
      return _classes[self] and ("class '%s' (%s)"):format(rawget(self,'name') or '?',_classes[self]) or self
    end}; _classes[baseMt] = tostring(baseMt); setmetatable(baseMt, {__tostring = baseMt.__tostring})
    local class = {isClass = function(class, ofsuper) local isclass = not not _classes[class]; if ofsuper then return isclass and (class.super == ofsuper) end; return isclass end, isInstance = function(instance, ofclass)
        local isinstance = not not _instances[instance]; if ofclass then return isinstance and (instance.class == ofclass) end; return isinstance end}; _class = function(name, attr)
      local c = deep_copy(attr); c.mixins=setmetatable({},{__mode='k'}); _classes[c] = tostring(c); c.name, c.__tostring, c.__call = name or c.name, baseMt.__tostring, baseMt.__call
      c.include = function(self,mixin) assert_class(self, 'include(mixin)'); self.mixins[mixin] = true; return deep_copy(mixin, self, 'function') end
      c.new, c.extend, c.__index, c.includes = instantiate, extend, c, function(self,mixin) assert_class(self,'includes(mixin)') return not not (self.mixins[mixin] or (self.super and self.super:includes(mixin))) end
      c.extends = function(self, class) assert_class(self, 'extends(class)') local super = self; repeat super = super.super until (super == class or super == nil); return class and (super == class) end
        return setmetatable(c, baseMt) end; class._DESCRIPTION = '30 lines library for object orientation in Lua'; class._VERSION = '30log v1.0.0'; class._URL = 'http://github.com/Yonaba/30log'; class._LICENSE = 'MIT LICENSE <http://www.opensource.org/licenses/mit-license.php>'
    return setmetatable(class,{__call = function(_,...) return _class(...) end })
end
class = class()

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

local WIN32 = (jit.os == "Windows")

local ord = string.byte
local tochar = string.char
local format = string.format
local substr = string.sub
local null = nil

local traceback = debug.traceback

local lshift = bit.lshift
local rshift = bit.rshift
local band = bit.band

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
local bool = typeof('bool')

local float = typeof('float')
local double = typeof('double')

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

local typeid_char = reflect.typeof(int8_t).typeid
local function is_char_array_ctype(refct)
    return refct.what == 'array' and refct.element_type.typeid == typeid_char
end

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

local function set(keys)
    local r = {}
    for i,k in ipairs(keys) do
        r[tostring(k)] = i
    end
    return r
end

local function update(a, b)
    for k,v in pairs(b) do
        a[k] = v
    end
end

local function split(str)
    local result = {}
    for s in string.gmatch(str, "%S+") do
      table.insert(result, s)
    end
    return result
end

local function cformat(fmt, ...)
    local size = C.stb_snprintf(null, 0, fmt, ...)
    local s = vla_int8_t(size + 1)
    C.stb_snprintf(s, size + 1, fmt, ...)
    return cstr(s)
end

local function escape_string(s, quote_chars)
    local len = #s
    local size = C.escape_string(null, s, len, quote_chars)
    local es = vla_int8_t(size + 1)
    C.escape_string(es, s, len, quote_chars)
    return cstr(es)
end

local function endswith(str,tail)
   return tail=='' or substr(str,-(#tail))==tail
end

local function assert_luatype(ltype, x)
    if type(x) == ltype then
        return x
    else
        error(ltype .. " expected, got " .. repr(x))
    end
end

local function assert_number(x) assert_luatype("number", x) end
local function assert_string(x) assert_luatype("string", x) end
local function assert_table(x) assert_luatype("table", x) end
local function assert_boolean(x) assert_luatype("boolean", x) end
local function assert_cdata(x) assert_luatype("cdata", x) end


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
COLOR_RGB       = function(hexcode, isbg)
    local r = band(rshift(hexcode, 16), 0xff)
    local g = band(rshift(hexcode, 8), 0xff)
    local b = band(hexcode, 0xff)
    local ctrlcode
    if isbg then
        ctrlcode = "\027[48;2;"
    else
        ctrlcode = "\027[38;2;"
    end
    return ctrlcode
        .. tostring(r) .. ";"
        .. tostring(g) .. ";"
        .. tostring(b) .. "m"
end
}

local SUPPORT_ISO_8613_3 = not WIN32
local STYLE
if SUPPORT_ISO_8613_3 then
local BG = ANSI.COLOR_RGB(0x2D2D2D, true)
STYLE = {
FOREGROUND = ANSI.COLOR_RGB(0xCCCCCC),
BACKGROUND = ANSI.COLOR_RGB(0x2D2D2D, true),
SYMBOL = ANSI.COLOR_RGB(0xCCCCCC),
STRING = ANSI.COLOR_RGB(0xCC99CC),
NUMBER = ANSI.COLOR_RGB(0x99CC99),
KEYWORD = ANSI.COLOR_RGB(0x6699CC),
FUNCTION = ANSI.COLOR_RGB(0xFFCC66),
SFXFUNCTION = ANSI.COLOR_RGB(0xCC6666),
OPERATOR = ANSI.COLOR_RGB(0x66CCCC),
INSTRUCTION = ANSI.COLOR_YELLOW,
TYPE = ANSI.COLOR_RGB(0xF99157),
COMMENT = ANSI.COLOR_RGB(0x999999),
ERROR = ANSI.COLOR_XRED,
LOCATION = ANSI.COLOR_XCYAN,
}
else
STYLE = {
FOREGROUND = ANSI.COLOR_WHITE,
BACKGROUND = ANSI.RESET,
STRING = ANSI.COLOR_XMAGENTA,
NUMBER = ANSI.COLOR_XGREEN,
KEYWORD = ANSI.COLOR_XBLUE,
FUNCTION = ANSI.COLOR_GREEN,
SFXFUNCTION = ANSI.COLOR_RED,
OPERATOR = ANSI.COLOR_XCYAN,
INSTRUCTION = ANSI.COLOR_YELLOW,
TYPE = ANSI.COLOR_XYELLOW,
COMMENT = ANSI.COLOR_GRAY30,
ERROR = ANSI.COLOR_XRED,
LOCATION = ANSI.COLOR_XCYAN,
}
end

local is_tty = (C.isatty(C.fileno(C.stdout)) == 1)
local support_ansi = is_tty
local ansi
if is_tty then
    local reset = ANSI.RESET
    ansi = function(style, x) return style .. x .. reset end
else
    ansi = function(style, x) return x end
end

local function repr(x)
    local visited = {}
    local function _repr(x, maxd)
        if type(x) == "table" then
            if visited[x] then
                maxd = 0
            end
            local mt = getmetatable(x)
            if mt and mt.__tostring then
                return mt.__tostring(x)
            end
            visited[x] = x
            local s = ansi(STYLE.OPERATOR,"{")
            if maxd <= 0 then
                s = s .. ansi(STYLE.COMMENT, "...")
            else
                local n = ''
                for k,v in pairs(x) do
                    if n ~= '' then
                        n = n .. ansi(STYLE.OPERATOR,",")
                    end
                    k = _repr(k, maxd - 1)
                    n = n .. k .. ansi(STYLE.OPERATOR, "=") .. _repr(v, maxd - 1)
                end
                if mt then
                    if n ~= '' then
                        n = n .. ansi(STYLE.OPERATOR,",")
                    end
                    if mt.__class then
                        n = n .. ansi(STYLE.KEYWORD, "class")
                            .. ansi(STYLE.OPERATOR, "=")
                            .. tostring(mt.__class)
                    else
                        n = n .. ansi(STYLE.KEYWORD, "meta")
                            .. ansi(STYLE.OPERATOR, "=")
                            .. _repr(mt, maxd - 1)
                    end
                end
                s = s .. n
            end
            s = s .. ansi(STYLE.OPERATOR,"}")
            return s
        elseif type(x) == "number" then
            return ansi(STYLE.NUMBER, tostring(x))
        elseif type(x) == "boolean" then
            return ansi(STYLE.KEYWORD, tostring(x))
        elseif type(x) == "string" then
            return ansi(STYLE.STRING, format("%q", x))
        elseif type(x) == "nil" then
            return ansi(STYLE.KEYWORD, "null")
        end
        return tostring(x)
    end
    return _repr(x, 10)
end

--------------------------------------------------------------------------------
-- TYPE
--------------------------------------------------------------------------------

local Type = {}
local function assert_type(x)
    if type(x) == "table" and getmetatable(x) == Type then
        return x
    else
        error("type expected, got " .. repr(x))
    end
end
local function define_types(def)
    def('Void')
    def('Any')

    def('Bool')

    def('I8')
    def('I16')
    def('I32')
    def('I64')

    def('U8')
    def('U16')
    def('U32')
    def('U64')

    def('R32')
    def('R64')

    def('Symbol')
    def('List')
    def('String')

    def('Parameter')
    def('Flow')
    def('Table')
end

do
    Type.__index = Type
    local idx = 0

    local cls = Type
    setmetatable(Type, {
        __call = function(cls, name)
            local k = idx
            idx = idx + 1
            return setmetatable({
                name = name,
                index = idx
            }, Type)
        end
    })

    function cls:__tostring()
        return ansi(STYLE.KEYWORD, "type")
            .. ansi(STYLE.COMMENT, "<")
            .. ansi(STYLE.TYPE, self.name)
            .. ansi(STYLE.COMMENT, ">")
    end

    define_types(function(name)
        cls[name] = Type(string.lower(name))
    end)
end

--------------------------------------------------------------------------------
-- SYMBOL
--------------------------------------------------------------------------------

local SYMBOL_ESCAPE_CHARS = "[]{}()\""

local Symbol = {__class="Symbol"}
local function assert_symbol(x)
    if type(x) == "table" and getmetatable(x) == Symbol then
        return x
    else
        error("symbol expected, got " .. repr(x))
    end
end
do
    Symbol.__index = Symbol

    local next_symbol_id = 0
    local name_symbol_map = {}
    local cls = Symbol
    setmetatable(Symbol, {
        __call = function(cls, name)
            assert_string(name)
            local sym = name_symbol_map[name]
            if (sym == null) then
                sym = setmetatable({name=name, index=next_symbol_id},Symbol)
                next_symbol_id = next_symbol_id + 1
                name_symbol_map[name] = sym
            end
            return sym
        end
    })
    function cls:__tostring()
        return
            ansi(STYLE.KEYWORD, "symbol")
            .. ansi(STYLE.COMMENT, "<")
            .. ansi(STYLE.SYMBOL, self.name)
            .. ansi(STYLE.COMMENT, ">")
    end
end

local function define_symbols(def)
    def({Unnamed=''})
    def({DoForm='form:do'})
    def({ContinuationForm='form:fn/cc'})

    def({ListWildcard='#list'})
    def({SymbolWildcard='#symbol'})

    def({ScopeParent='#parent'})
end

do
    define_symbols(function(kv)
        local key, value = next(kv)
        Symbol[key] = Symbol(value)
    end)
end

--------------------------------------------------------------------------------
-- ANY
--------------------------------------------------------------------------------

local wrap
local format_any_value
local Any = {}
setmetatable(Any, {
    __call = function(cls, arg1, arg2)
        local _type = arg1
        local value = arg2
        if type(arg2) == "nil" then
            -- wrap syntax
            _type, value = wrap(arg1)
        else
            local _type = arg1
            local value = arg2
        end
        assert_type(_type)
        return setmetatable({
            type = _type,
            value = value
        }, cls)
    end
})
function Any.__tostring(self)
    return ansi(STYLE.KEYWORD, "any")
        .. ansi(STYLE.COMMENT, "<")
        .. format_any_value(self.type, self.value)
        .. ansi(STYLE.OPERATOR, ":")
        .. ansi(STYLE.TYPE, self.type.name)
        .. ansi(STYLE.COMMENT, ">")
end

local function assert_any(x)
    if type(x) == "table" and getmetatable(x) == Any then
        return x
    else
        error("any expected, got " .. tostring(x))
    end
end

local function assert_any_type(_type, value)
    assert_any(value)
    if (value.type == _type) then
        return value.value
    else
        error("type "
            .. ansi(STYLE.TYPE,_type.name)
            .. " expected, got "
            .. ansi(STYLE.TYPE,value.type.name)
            )
    end
end

local none = Any(Type.Void, NULL)
local function is_none(value)
    return value.type == Type.Void
end

--------------------------------------------------------------------------------
-- ANCHOR
--------------------------------------------------------------------------------

cdef[[
typedef struct _Anchor {
    const char *path;
    int lineno;
    int column;
    int offset;
} Anchor;
]]
local Anchor

do
    local cls = {}
    local function anchor_tostring(self)
        return
            ansi(STYLE.LOCATION, ffi.string(self.path))
            .. ansi(STYLE.OPERATOR, ":")
            .. format("%i", self.lineno)
            .. ansi(STYLE.OPERATOR, ":")
            .. format("%i", self.column)
    end
    Anchor = ffi.metatype('Anchor', {
        __index = cls,
        __tostring = anchor_tostring })
end

local active_anchor
local function location_error(msg)
    if type(msg) == "string" then
        msg = {msg = msg, anchor = active_anchor}
    end
    error(msg)
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

local Lexer = {}
do
    Lexer.__index = Lexer

    local TAB = ord('\t')
    local CR = ord('\n')
    local BS = ord('\\')

    local function verify_good_taste(c)
        if (c == TAB) then
            location_error("please use spaces instead of tabs.")
        end
    end

    local cls = Lexer
    function cls.init(input_stream, eof, path, offset)
        offset = offset or 0
        eof = eof or (input_stream + C.strlen(input_stream))

        local self = setmetatable({}, Lexer)
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
        return Anchor(self.path, self.lineno, self:column(), self:offset())
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
                location_error("unterminated sequence")
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
    local function make_read_number(srctype, f)
        local atype = typeof('$[$]', srctype, 1)
        local rtype = typeof('$&', srctype)
        return function (self)
            local cendp = new(pp_int8_t)
            local errno = 0
            local srcval = new(atype)
            f(srcval, self.cursor, cendp, 0)
            self.number = Any(cast(srctype, cast(rtype, srcval)))
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
    cls.read_int64 = make_read_number(int64_t, C.bangra_strtoll)
    cls.read_uint64 = make_read_number(uint64_t, C.bangra_strtoull)
    cls.read_real32 = make_read_number(float,
        function (dest, cursor, cendp, base)
            return C.bangra_strtof(dest, cursor, cendp)
        end)
    function cls:next_token()
        self.lineno = self.next_lineno
        self.line = self.next_line
        self.cursor = self.next_cursor
        active_anchor = self:anchor()
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
            self:read_string(CR)
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
        return Any(Symbol(cstr(zstr_from_buffer(dest, size))))
    end
    function cls:get_string()
        local dest = zstr_from_buffer(self.string + 1, self.string_len - 2)
        local size = C.unescape_string(dest)
        return Any(zstr_from_buffer(dest, size))
    end
    function cls:get_number()
        if ((self.number.type == Type.I64)
            and (self.number.value <= 0x7fffffffll)
            and (self.number.value >= -0x80000000ll)) then
            return Any(int32_t(self.number.value))
        elseif ((self.number.type == Type.U64)
            and (self.number.value <= 0xffffffffull)) then
            return Any(uint32_t(self.number.value))
        end
        -- return copy instead of reference
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

local EOL = {count=0}
local List = {}
setmetatable(EOL, List)
local function assert_list(x)
    if type(x) == "table" and getmetatable(x) == List then
        return x
    else
        error("expected List or null, got " .. repr(x))
    end
end
do
    List.__class = "List"

    function List:__len()
        return self.count
    end

    function List:__index(key)
        local value = rawget(self, key)
        if value == null and self == EOL then
            error("attempting to access list terminator")
        end
    end

    function List.from_args(...)
        local l = EOL
        for i=select('#',...),1,-1 do
            l = List(select(i, ...), l)
        end
        return l
    end

    setmetatable(List, {
        __call = function (cls, at, next)
            assert_any(at)
            assert_list(next)
            local count
            if (next ~= EOL) then
                count = next.count + 1
            else
                count = 1
            end
            return setmetatable({
                at = at,
                next = next,
                count = count
            }, List)
        end
    })
end

-- (a . (b . (c . (d . NIL)))) -> (d . (c . (b . (a . NIL))))
-- this is the mutating version; input lists are modified, direction is inverted
local function reverse_list_inplace(l, eol, cat_to)
    assert_list(l)
    eol = eol or EOL
    cat_to = cat_to or EOL
    assert_list(eol)
    assert_list(cat_to)
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
        prev = List(value, prev)
        assert(prev)
    end
    function cls.reset_start()
        eol = prev
    end
    function cls.is_expression_empty()
        return (prev == EOL)
    end
    function cls.split()
        -- if we haven't appended anything, that's an error
        if (cls.is_expression_empty()) then
            error("can't split empty expression")
        end
        -- reverse what we have, up to last split point and wrap result
        -- in cell
        prev = List(Any(reverse_list_inplace(prev, eol)), eol)
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
                location_error("missing closing bracket")
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
            return Any(parse_list(Token.close))
        elseif (lexer.token == Token.square_open) then
            local list = parse_list(Token.square_close)
            local sym = get_symbol("[")
            return Any(List(wrap(sym), list))
        elseif (lexer.token == Token.curly_open) then
            local list = parse_list(Token.curly_close)
            local sym = get_symbol("{")
            return Any(List(wrap(sym), list))
        elseif ((lexer.token == Token.close)
            or (lexer.token == Token.square_close)
            or (lexer.token == Token.curly_close)) then
            location_error("stray closing bracket")
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
                    location_error("escape character is not at end of line")
                end
                lineno = lexer.lineno
            elseif (lexer.lineno > lineno) then
                if (subcolumn == 0) then
                    subcolumn = lexer:column()
                elseif (lexer:column() ~= subcolumn) then
                    location_error("indentation mismatch")
                end
                if (column ~= subcolumn) then
                    if ((column + 4) ~= subcolumn) then
                        location_error("indentations must nest by 4 spaces.")
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
                if builder.is_expression_empty() then
                    lexer:read_token()
                else
                    builder.split()
                    lexer:read_token()
                    -- if we are in the same line, continue in parent
                    if (lexer.lineno == lineno) then
                        break
                    end
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
            return Any(builder.get_result())
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
        return Any(builder.get_result())
    end

    return xpcall(
        function()
            return parse_root()
        end,
        function(err)
            if (type(err) == "table") and err.anchor then
                local msg = err.msg
                if err.anchor then
                    msg = tostring(err.anchor)
                        .. ansi(STYLE.OPERATOR,":") .. " " .. msg
                    local sf = SourceFile.open(err.anchor.path)
                    if sf then
                        msg = msg .. "\n"
                        sf:dump_line(err.anchor.offset,
                            function (s)
                                msg = msg .. s
                            end)
                    end
                end
                return msg
            else
                print(traceback(err,3))
                os.exit(1)
            end
        end
    )
end

--------------------------------------------------------------------------------
-- VALUE PRINTING
--------------------------------------------------------------------------------

local function is_nested(e)
    if (e.type == Type.List) then
        local it = e.value
        while (it ~= EOL) do
            if (it.at.type == Type.List) then
                return true
            end
            it = it.next
        end
    end
    return false
end

local function stream_anchor(writer, e, depth)
    depth = depth or 0
    --[[
    const Anchor *anchor = find_valid_anchor(e);
    if (anchor) {
        stream <<
            format("%s:%i:%i: ",
                anchor->path,
                anchor->lineno,
                anchor->column);
    }
    --]]
    for i=1,depth do
        writer("    ")
    end
end

-- keywords and macros
local KEYWORDS = set(split(
    "let true false fn quote with ::* ::@ call escape do dump-syntax"
        .. " syntax-extend if else elseif loop repeat none assert qquote"
        .. " unquote unquote-splice globals return splice continuation"
        .. " try except define in for empty-list empty-tuple raise"
        .. " yield xlet cc/call fn/cc null"
    ))

    -- builtin and global functions
local FUNCTIONS = set(split(
    "external branch print repr tupleof import-c eval structof typeof"
        .. " macro block-macro block-scope-macro cons expand empty?"
        .. " dump list-head? countof tableof slice none? list-atom?"
        .. " list-load list-parse load require cstr exit hash min max"
        .. " va-arg va-countof range zip enumerate bitcast element-type"
        .. " qualify disqualify iter iterator? list? symbol? parse-c"
        .. " get-exception-handler xpcall error sizeof prompt null?"
        .. " extern-library arrayof"
    ))

-- builtin and global functions with side effects
local SFXFUNCTIONS = set(split(
    "set-key! set-globals! set-exception-handler! bind! set!"
    ))

-- builtin operator functions that can also be used as infix
local OPERATORS = set(split(
    "+ - ++ -- * / % == != > >= < <= not and or = @ ** ^ & | ~ , . .. : += -="
        .. " *= /= %= ^= &= |= ~= <- ? := // << >>"
    ))

local TYPES = set(split(
    "int int8 int16 int32 int64 uint8 uint16 uint32 uint64 void string"
        .. " rawstring opaque half float double symbol list parameter"
        .. " frame closure flow integer real cfunction array tuple vector"
        .. " pointer struct enum bool uint real16 real32 real64 tag qualifier"
        .. " iterator type table size_t usize_t ssize_t void*"
    ))

local function StreamValueFormat(naked, depth, opts)
    if type(naked) == "table" then
        local obj = naked
        return {
            depth = obj.depth,
            naked = obj.naked,
            maxdepth = obj.maxdepth,
            maxlength = obj.maxlength,
            keywords = obj.keywords,
            functions = obj.functions,
            sfxfunctions = obj.sfxfunctions,
            operators = obj.operators,
            types = obj.types
        }
    else
        opts = opts or {}
        opts.depth = depth or 0
        opts.naked = naked or false
        opts.maxdepth = opts.maxdepth or lshift(1,30)
        opts.maxlength = opts.maxlength or lshift(1,30)
        opts.keywords = opts.keywords or KEYWORDS
        opts.functions = opts.functions or FUNCTIONS
        opts.sfxfunctions = opts.sfxfunctions or SFXFUNCTIONS
        opts.operators = opts.operators or OPERATORS
        opts.types = opts.types or TYPES
        return opts
    end
end

local function stream_value(writer, e, format)
    local depth = format.depth
    local maxdepth = format.maxdepth
    local naked = format.naked

    if (naked) then
        stream_anchor(writer, e, depth)
    end

    if (e.type == Type.List) then
        if (maxdepth == 0) then
            writer(ansi(STYLE.OPERATOR,"("))
            writer(ansi(STYLE.COMMENT,"<...>"))
            writer(ansi(STYLE.OPERATOR,")"))
            if (naked) then
                writer('\n')
            end
        else
            local subfmt = StreamValueFormat(format)
            subfmt.maxdepth = subfmt.maxdepth - 1

            local it = e.value
            if (it == EOL) then
                writer(ansi(STYLE.OPERATOR,"()"))
                if (naked) then
                    writer('\n')
                end
                return
            end
            local offset = 0
            if (naked) then
                local single = (it.next == EOL)
                if is_nested(it.at) then
                    writer(";\n")
                    goto print_sparse
                end
            ::print_terse::
                subfmt.depth = depth
                subfmt.naked = false
                stream_value(writer, it.at, subfmt)
                it = it.next
                offset = offset + 1
                while (it ~= EOL) do
                    if (is_nested(it.at)) then
                        break
                    end
                    writer(' ')
                    stream_value(writer, it.at, subfmt)
                    offset = offset + 1
                    it = it.next
                end
                if single then
                    writer(";\n")
                else
                    writer("\n")
                end
            ::print_sparse::
                while (it ~= EOL) do
                    subfmt.depth = depth + 1
                    subfmt.naked = true
                    local value = it.at
                    if ((value.type ~= Type.List) -- not a list
                        and (offset >= 1) -- not first element in list
                        and (it.next ~= EOL) -- not last element in list
                        and not(is_nested(it.next.at))) then -- next element can be terse packed too
                        single = false
                        stream_anchor(writer, value, depth + 1)
                        writer("\\ ")
                        goto print_terse
                    end
                    if (offset >= subfmt.maxlength) then
                        stream_anchor(writer, value, depth + 1)
                        writer("<...>\n")
                        return
                    end
                    stream_value(writer, value, subfmt)
                    offset = offset + 1
                    it = it.next
                end

            else
                subfmt.depth = depth + 1
                subfmt.naked = false
                writer(ansi(STYLE.OPERATOR,'('))
                while (it ~= EOL) do
                    if (offset > 0) then
                        writer(' ')
                    end
                    if (offset >= subfmt.maxlength) then
                        writer(ansi(STYLE.COMMENT,"..."))
                        break
                    end
                    stream_value(writer, it.at, subfmt)
                    offset = offset + 1
                    it = it.next
                end
                writer(ansi(STYLE.OPERATOR,')'))
                if (naked) then
                    writer('\n')
                end
            end
        end
    else
        if (e.type == Type.Symbol) then
            local name = e.value.name
            local style =
                (format.keywords[name] and STYLE.KEYWORD)
                or (format.functions[name] and STYLE.FUNCTION)
                or (format.sfxfunctions[name] and STYLE.SFXFUNCTION)
                or (format.operators[name] and STYLE.OPERATOR)
                or (format.types[name] and STYLE.TYPE)
                or STYLE.SYMBOL
            if (style and support_ansi) then writer(style) end
            writer(escape_string(name, SYMBOL_ESCAPE_CHARS))
            if (style and support_ansi) then writer(ANSI.RESET) end
        else
            writer(format_any_value(e.type, e.value))
        end
        if (naked) then
            writer('\n')
        end
    end
end

function List.__tostring(self)
    local s = ""
    stream_value(
        function (x)
            s = s .. x
        end,
        {type=Type.List, value=self}, StreamValueFormat(false))
    return s
end

--------------------------------------------------------------------------------
-- IL OBJECTS
--------------------------------------------------------------------------------

local Parameter = class("Parameter")
do
    local cls = Parameter
    function cls:init(name)
        assert_symbol(name)
        self.flow = null
        self.index = -1
        self.name = name
        self.type = Type.Any
        self.vararg = endswith(name.name, "...")
    end
    function cls:__tostring()
        return
            (function()
                if self.vararg then
                    return ansi(STYLE.KEYWORD, "parameter...")
                else
                    return ansi(STYLE.KEYWORD, "parameter")
                end
            end)()
            .. ansi(STYLE.COMMENT, "<")
            .. ansi(STYLE.SYMBOL, self.name.name)
            .. ansi(STYLE.OPERATOR, ":")
            .. ansi(STYLE.TYPE, self.type.name)
            .. ansi(STYLE.COMMENT, ">")
    end
end

--------------------------------------------------------------------------------
-- SCOPES
--------------------------------------------------------------------------------

local function assert_scope(x)
    if type(x) == "table" and getmetatable(x) == null then
        return x
    else
        error("plain table expected, not " .. repr(x))
    end
end

local function new_scope(scope)
    local self = {}
    if scope ~= nil then
        assert_scope(scope)
        self[Symbol.ScopeParent] = Any(scope)
    end
    return self
end

local function set_local(scope, name, value)
    assert_scope(scope)
    assert_symbol(name)
    assert_any(value)
    scope[name] = value
end

local function get_parent(scope)
    assert_scope(scope)
    local parent = scope[Symbol.ScopeParent]
    if parent then
        return parent.value
    end
end

local function get_local(scope, name)
    assert_scope(scope)
    assert_symbol(name)
    while scope do
        local result = scope[name]
        if result then
            return result
        end
        scope = get_parent(scope)
    end
end

--[[
template<BuiltinFlowFunction func>
static void setBuiltinMacro(Table *scope, const std::string &name) {
    assert(scope);
    auto sym = get_symbol(name);
    set_ptr_symbol((void *)func, sym);
    setLocal(scope, sym, macro(wrap_ptr(TYPE_BuiltinFlow, (void *)func)));
}

template<SpecialFormFunction func>
static void setBuiltin(Table *scope, const std::string &name) {
    assert(scope);
    auto sym = get_symbol(name);
    set_ptr_symbol((void *)func, sym);
    setLocal(scope, sym,
        wrap_ptr(TYPE_SpecialForm, (void *)func));
}

template<BuiltinFlowFunction func>
static void setBuiltin(Table *scope, const std::string &name) {
    assert(scope);
    auto sym = get_symbol(name);
    set_ptr_symbol((void *)func, sym);
    setLocal(scope, sym,
        wrap_ptr(TYPE_BuiltinFlow, (void *)func));
}

template<BuiltinFunction func>
static void setBuiltin(Table *scope, const std::string &name) {
    setBuiltin< builtin_call<func> >(scope, name);
}
]]

--------------------------------------------------------------------------------
-- MACRO EXPANDER
--------------------------------------------------------------------------------

local function verify_any_type(_type, value)
    assert_any(value)
    if (value.type == _type) then
        return value.value
    else
        location_error("type "
            .. ansi(STYLE.TYPE,_type.name)
            .. " expected, got "
            .. ansi(STYLE.TYPE,value.type.name)
            )
    end
end

local function verify_list_parameter_count(expr, mincount, maxcount)
    assert_list(expr)
    if ((mincount <= 0) and (maxcount == -1)) then
        return true
    end
    local argcount = #expr - 1

    if ((maxcount >= 0) and (argcount > maxcount)) then
        location_error(
            format("excess argument. At most %i arguments expected", maxcount))
        return false
    end
    if ((mincount >= 0) and (argcount < mincount)) then
        location_error(
            format("at least %i arguments expected", mincount))
        return false
    end
    return true;
end

local function verify_at_parameter_count(topit, mincount, maxcount)
    assert_list(expr)
    assert(topit ~= EOL)
    local val = topit.at
    verify_any_type(Type.List, val)
    verify_list_parameter_count(val.value, mincount, maxcount)
end

--------------------------------------------------------------------------------

-- new descending approach for compiler to optimize tail calls:
-- 1. each function is called with a flow node as target argument; it represents
--    a continuation that should be called with the resulting value.

local globals = new_scope()

--static Cursor expand (const Table *env, const List *topit);
--static Any compile (const Any &expr, const Any &dest);
local expand
local compile

local function toparameter(env, value)
    assert_scope(env)
    local sym
    if getmetatable(value) == Symbol then
        sym = value
    elseif getmetatable(value) == Parameter then
        return value
    elseif getmetatable(value) == Any then
        if (value.type == Type.Parameter) then
            return value
        else
            assert_any_type(Type.Symbol)
            sym = value.value
        end
    end
    assert_symbol(sym)
    local param = Any(Parameter(sym))
    set_local(env, sym, param)
    return param
end

local function expand_expr_list(env, it)
    assert_scope(env)
    assert_list(it)
    local l = EOL
    while (it ~= EOL) do
        local nextlist,nextscope = expand(env, it)
        if (nextlist == EOL) then
            break
        end
        l = List(nextlist.at, l)
        it = nextlist.next
        env = nextscope
    end
    return reverse_list_inplace(l)
end

--[[
template <Cursor (*ExpandFunc)(const Table *, const List *)>
B_FUNC(wrap_expand_call) {
    builtin_checkparams(B_RCOUNT(S), 2, 2, 2);
    auto topit = extract_list(B_GETARG(S, 0));
    auto env = extract_table(B_GETARG(S, 1));
    auto cur = ExpandFunc(env, topit);
    Any out[] = { wrap(cur.list), wrap(cur.scope) };
    B_CALL(S,
        const_none,
        B_GETCONT(S),
        wrap(out, 2));
}
]]

local function expand_do(env, topit)
    assert_scope(env)
    assert_list(topit)

    local it = verify_any_type(Type.List, topit.at)

    local subenv = new_scope(env)
    return
        List(
            quote(Any(List(
                get_local(globals, Symbol.DoForm) or none,
                expand_expr_list(subenv, it)))),
            topit.next), env
end

local function expand_continuation(env, topit)
    assert_scope(env)
    assert_list(topit)
    verify_at_parameter_count(topit, 1, -1)

    local it = verify_any_type(Type.List, topit.at)
    it = it.next

    local sym
    assert(it ~= EOL)
    if (it.at.type == Type.Symbol) then
        sym = it.at
        it = it.next
        assert(it ~= EOL)
    else
        sym = Any(Symbol.Unnamed)
    end

    local expr_parameters = it.at
    it = it.next

    local subenv = new_scope(env)

    local outargs = EOL
    local params = verify_any_type(Type.List, expr_parameters)
    local param = params
    while (param ~= EOL) do
        outargs = List(toparameter(subenv, param.at), outargs)
        param = param.next
    end

    return
        List(
            quote(Any(
                List(
                    get_local(globals, Symbol.ContinuationForm) or none,
                    List(
                        sym,
                        List(
                            Any(reverse_list_inplace(outargs)),
                            expand_expr_list(subenv, it)))))),
            topit.next), env
end

local function expand_syntax_extend(env, topit)
    local cur_list, cur_env = expand_continuation(env, topit)

    local fun = compile(unquote(cur_list.at), none)
    return cur_list.next, execute(function(expr_env)
        return verify_any_type(Type.Table, expr_env)
    end, fun, Any(env))
end

local function expand_wildcard(env, handler, topit)
    assert_scope(env)
    assert_any(handler)
    assert_list(topit)
    return execute(function(result)
        if (is_none(result)) then
            return EOL
        end
        return verify_any_type(Type.List, result)
    end, handler, Any(topit), Any(env))
end

local function expand_macro(env, handler, topit)
    assert_scope(env)
    assert_any(handler)
    assert_list(topit)
    return execute(function(result_list, result_scope)
        if (is_none(result_list)) then
            return EOL
        end
        return verify_any_type(Type.List, result_list),
            result_scope and verify_any_type(Type.Table, result_scope)
    end, handler,  Any(topit), Any(env))
end

local function expand(env, topit)
    assert_scope(env)
    assert_list(topit)
    local result = none
::process::
    assert(topit ~= EOL)
    local expr = topit.at
    if (is_quote_type(expr.type)) then
        -- remove qualifier and return as-is
        return List(unquote(expr), topit.next), env
    elseif (expr.type == Type.List) then
        local list = expr.value
        if (list == EOL) then
            location_error("expression is empty")
        end

        local head = list.at

        -- resolve symbol
        if (head.type == Type.Symbol) then
            head = get_local(env, head.value) or none
        end

        if (is_macro_type(head.type)) then
            local result_list,result_env = expand_macro(env, unmacro(head), topit)
            if (result_list ~= EOL) then
                topit = result_list
                env = result_env
                goto process
            elseif result_scope then
                return EOL, env
            end
        end

        local default_handler = get_local(env, Symbol.ListWildcard)
        if not is_none(default_handler) then
            local result = expand_wildcard(env, default_handler, topit)
            if result then
                topit = result
                goto process
            end
        end

        local it = verify_any_type(Type.List, topit.at)
        result = Any(expand_expr_list(env, it))
        topit = topit.next
    elseif expr.type == Type.Symbol then
        local value = expr.value
        result = get_local(env, value)
        if result == null then
            local default_handler = get_local(env, Symbol.SymbolWildcard)
            if (default_handler.type ~= Type.Void) then
                local result = expand_wildcard(env, default_handler, topit)
                if result then
                    topit = result
                    goto process
                end
            end
            location_error("no such symbol in scope: '%s'", value.name)
        end
        topit = topit.next
    else
        result = expr
        topit = topit.next
    end
    return List(result, topit), env
end

--[[
static Any builtin_expand(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 2, 2);
    auto expr_eval = extract_list(args[0]);
    auto scope = args[1];
    verifyValueKind(TYPE_Table, scope);

    auto retval = expand(scope.table, expr_eval);
    Any out[] = {wrap(retval.list), wrap(retval.scope)};
    return wrap(out, 2);
}

static Any builtin_set_globals(const Any *args, size_t argcount) {
    builtin_checkparams(argcount, 1, 1);
    auto scope = args[0];
    verifyValueKind(TYPE_Table, scope);

    globals = scope.table;
    return const_none;
}

static B_FUNC(builtin_set_exception_handler) {
    builtin_checkparams(B_ARGCOUNT(S), 1, 1);
    auto old_exception_handler = S->exception_handler;
    auto func = B_GETARG(S, 0);
    S->exception_handler = func;
    B_CALL(S, const_none, B_GETCONT(S), old_exception_handler);
}

static B_FUNC(builtin_get_exception_handler) {
    builtin_checkparams(B_ARGCOUNT(S), 0, 0);
    B_CALL(S, const_none, B_GETCONT(S), S->exception_handler);
}
]]

--------------------------------------------------------------------------------
-- Any wrapping
--------------------------------------------------------------------------------
-- define this one after all other types have been defined

wrap = function(value)
    local t = type(value)
    if t == 'table' then
        local mt = getmetatable(value)
        if mt == List then
            return Type.List, value
        elseif mt == Parameter then
            return Type.Parameter, value
        elseif mt == Symbol then
            return Type.Symbol, value
        end
    elseif t == 'cdata' then
        local ct = typeof(value)
        if istype(int8_t, ct) then
            return Type.I8, value
        elseif istype(int16_t, ct) then
            return Type.I16, value
        elseif istype(int32_t, ct) then
            return Type.I32, value
        elseif istype(int64_t, ct) then
            return Type.I64, value
        elseif istype(uint8_t, ct) then
            return Type.U8, value
        elseif istype(uint16_t, ct) then
            return Type.U16, value
        elseif istype(uint32_t, ct) then
            return Type.U32, value
        elseif istype(uint64_t, ct) then
            return Type.U64, value
        elseif istype(float, ct) then
            return Type.R32, value
        elseif istype(double, ct) then
            return Type.R64, value
        end
        local refct = reflect.typeof(value)
        if is_char_array_ctype(refct) then
            return Type.String, cstr(value)
        end
    end
    error("unable to wrap " .. repr(value))
end

format_any_value = function(self, x)
    if self == Type.I8 then
        return ansi(STYLE.NUMBER, cformat("%d", x))
    elseif self == Type.I16 then
        return ansi(STYLE.NUMBER, cformat("%d", x))
    elseif self == Type.I32 then
        return ansi(STYLE.NUMBER, cformat("%d", x))
    elseif self == Type.I64 then
        return ansi(STYLE.NUMBER, cformat("%lld", x))
    elseif self == Type.U8 then
        return ansi(STYLE.NUMBER, cformat("%u", x))
    elseif self == Type.U16 then
        return ansi(STYLE.NUMBER, cformat("%u", x))
    elseif self == Type.U32 then
        return ansi(STYLE.NUMBER, cformat("%u", x))
    elseif self == Type.U64 then
        return ansi(STYLE.NUMBER, cformat("%llu", x))
    elseif self == Type.R32 then
        return ansi(STYLE.NUMBER, cformat("%g", x))
    elseif self == Type.R64 then
        return ansi(STYLE.NUMBER, cformat("%g", x))
    elseif self == Type.Bool then
        if x == bool(true) then
            return ansi(STYLE.KEYWORD, "true")
        else
            return ansi(STYLE.KEYWORD, "false")
        end
    elseif self == Type.Void then
        return ansi(STYLE.KEYWORD, "none")
    elseif self == Type.Symbol then
        return ansi(STYLE.SYMBOL,
            escape_string(get_symbol_name(x), SYMBOL_ESCAPE_CHARS))
    elseif self == Type.String then
        return ansi(STYLE.STRING,
            '"' .. escape_string(x, "\"") .. '"')
    end
    return repr(x)
end

--------------------------------------------------------------------------------
-- TESTING
--------------------------------------------------------------------------------

local lexer_test = [[
test
    test test
numbers -1 0x7fffffff 0xffffffff 0xffffffffff 0x7fffffffffffffff
    \ 0xffffffffffffffff 0.00012345 1 2 3.5 10.0 1001.0 1001.1 1001.001
    \ 1. .1 0.1 .01 0.01 1e-22 3.1415914159141591415914159 inf nan 1.33
    \ 1.0 0.0 "te\"st\n\ttest!" test
test (1 2; 3 4; 5 6)

cond
    i == 0;
        print "yes"
    i == 1;
        print "no"

function test (a b)
    assert (a != b)
    + a b

; more more
    one
    two
    three
; test test test



]]

local function test_lexer()
    collectgarbage("stop")
    local lexer = Lexer.init(new(rawstring, lexer_test), null, "path")
    local result,expr = parse(lexer)
    if result then
        stream_value(stdout_writer, expr, StreamValueFormat(true))
    else
        print(expr)
    end
end

local function test_bangra()
    collectgarbage("stop")
    local src = SourceFile.open("bangra.b")
    local ptr = src:strptr()
    local lexer = Lexer.init(ptr, ptr + src.length, "bangra.b")
    local result,expr = parse(lexer)
    if result then
        stream_value(stdout_writer, expr, StreamValueFormat(true))
    else
        print(expr)
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

local function test_ansicolors()
    for k,v in pairs(ANSI) do
        if type(v) == "string" then
            print(ansi(v, k))
        end
    end
    print(ansi(ANSI.COLOR_RGB(0x4080ff), "yes"))

end

local function test_list()
    local l = List(Any(int(5)), EOL)
    print(l.next)
    --print(EOL.next)
end

--test_list()
--testf()
--test_lexer()
test_bangra()
--test_ansicolors()


