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

local global_opts = {
    -- semver style versioning
    version_major = 0,
    version_minor = 7,
    version_patch = 0,

    trace_execution = false, -- print each statement being executed
    print_lua_traceback = false, -- print lua traceback on any error
    validate_macros = false, -- validate each macro result
    stack_limit = 65536, -- recursion limit

    debug = false, -- updated by bangra_is_debug() further down
}

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

do
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

unsigned char bangra_h[];
unsigned int bangra_h_len;
unsigned char bangra_b[];
unsigned int bangra_b_len;
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
    cdef(cstr(zstr_from_buffer(C.bangra_h, C.bangra_h_len)))
end

global_opts.debug = C.bangra_is_debug()
local off_t = typeof('__off_t')

local function stderr_writer(x)
    C.fputs(x, C.stderr)
end

local function stdout_writer(x)
    C.fputs(x, C.stdout)
end

local function string_writer(s)
    s = s or ""
    return function (x)
        if x == null then
            return s
        else
            s = s .. x
        end
    end
end

local function min(a,b)
    if (a < b) then
        return a
    else
        return b
    end
end

local function max(a,b)
    if (a > b) then
        return a
    else
        return b
    end
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
        r[k] = i
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

local repr
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
local function assert_function(x) assert_luatype("function", x) end

--------------------------------------------------------------------------------
-- CC EXCEPTIONS
--------------------------------------------------------------------------------

local xpcallcc
local errorcc
local _xpcallcc_handler
do
    errorcc = function (exc)
        if _xpcallcc_handler then
            return _xpcallcc_handler(exc)
        else
            print(traceback("uncaught error: " .. tostring(exc), 2))
            os.exit(1)
        end
    end

    xpcallcc = function (func, xfunc, ffinally)
        assert(func)
        assert(xfunc)
        assert(ffinally)
        local old_handler = _xpcallcc_handler
        local function cleanup ()
            _xpcallcc_handler = old_handler
        end
        local function finally (...)
            cleanup()
            return ffinally(...)
        end
        local function except (exc)
            cleanup()
            return xfunc(exc, ffinally)
        end
        _xpcallcc_handler = except
        return func(finally)
    end
end

rawset(_G, "error", errorcc)
rawset(_G, "xpcall", null)
rawset(_G, "pcall", null)

--------------------------------------------------------------------------------

local function protect(obj)
    local mt = getmetatable(obj)
    assert(mt)
    assert(mt.__index == null)
    function mt.__index(cls, name)
        error("no such attribute in "
            .. tostring(cls)
            .. ": " .. tostring(name))
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
local Style
if SUPPORT_ISO_8613_3 then
local BG = ANSI.COLOR_RGB(0x2D2D2D, true)
Style = {
Foreground = ANSI.COLOR_RGB(0xCCCCCC),
Background = ANSI.COLOR_RGB(0x2D2D2D, true),
Symbol = ANSI.COLOR_RGB(0xCCCCCC),
String = ANSI.COLOR_RGB(0xCC99CC),
Number = ANSI.COLOR_RGB(0x99CC99),
Keyword = ANSI.COLOR_RGB(0x6699CC),
Function = ANSI.COLOR_RGB(0xFFCC66),
SfxFunction = ANSI.COLOR_RGB(0xCC6666),
Operator = ANSI.COLOR_RGB(0x66CCCC),
Instruction = ANSI.COLOR_YELLOW,
Type = ANSI.COLOR_RGB(0xF99157),
Comment = ANSI.COLOR_RGB(0x999999),
Error = ANSI.COLOR_XRED,
Location = ANSI.COLOR_RGB(0x999999),
}
else
Style = {
Foreground = ANSI.COLOR_WHITE,
Background = ANSI.RESET,
String = ANSI.COLOR_XMAGENTA,
Number = ANSI.COLOR_XGREEN,
Keyword = ANSI.COLOR_XBLUE,
Function = ANSI.COLOR_GREEN,
SfxFunction = ANSI.COLOR_RED,
Operator = ANSI.COLOR_XCYAN,
Instruction = ANSI.COLOR_YELLOW,
Type = ANSI.COLOR_XYELLOW,
Comment = ANSI.COLOR_GRAY30,
Error = ANSI.COLOR_XRED,
Location = ANSI.COLOR_GRAY30,
}
end

local function ansi_styler(style, x)
    return style .. x .. ANSI.RESET
end

local function plain_styler(style, x)
    return x
end

local is_tty = (C.isatty(C.fileno(C.stdout)) == 1)
local support_ansi = is_tty
local default_styler
if support_ansi then
    default_styler = ansi_styler
else
    default_styler = plain_styler
end

repr = function(x, styler)
    styler = styler or default_styler
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
            local s = styler(Style.Operator,"{")
            if maxd <= 0 then
                s = s .. styler(Style.Comment, "...")
            else
                local n = ''
                for k,v in pairs(x) do
                    if n ~= '' then
                        n = n .. styler(Style.Operator,",")
                    end
                    k = _repr(k, maxd - 1)
                    n = n .. k .. styler(Style.Operator, "=") .. _repr(v, maxd - 1)
                end
                if mt then
                    if n ~= '' then
                        n = n .. styler(Style.Operator,",")
                    end
                    if mt.__class then
                        n = n .. styler(Style.Keyword, "class")
                            .. styler(Style.Operator, "=")
                            .. tostring(mt.__class)
                    else
                        n = n .. styler(Style.Keyword, "meta")
                            .. styler(Style.Operator, "=")
                            .. _repr(mt, maxd - 1)
                    end
                end
                s = s .. n
            end
            s = s .. styler(Style.Operator,"}")
            return s
        elseif type(x) == "number" then
            return styler(Style.Number, tostring(x))
        elseif type(x) == "boolean" then
            return styler(Style.Keyword, tostring(x))
        elseif type(x) == "string" then
            return styler(Style.String, format("%q", x))
        elseif type(x) == "nil" then
            return styler(Style.Keyword, "null")
        end
        return tostring(x)
    end
    return _repr(x, 10)
end

--------------------------------------------------------------------------------
--
--------------------------------------------------------------------------------

local builtins = {}

--------------------------------------------------------------------------------
-- Symbol
--------------------------------------------------------------------------------

local SYMBOL_ESCAPE_CHARS = "[]{}()\""

local Symbol = {__class="Symbol"}
local function assert_symbol(x)
    if getmetatable(x) == Symbol then
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
        return default_styler(Style.Symbol, self.name)
    end
end

local function define_symbols(def)
    def({Unnamed=''})
    def({FnCCForm='form-fn-body'})
    def({QuoteForm='form-quote'})
    def({DoForm='form-do'})
    def({SyntaxScope='syntax-scope'})

    def({ListWildcard='#list'})
    def({SymbolWildcard='#symbol'})
    def({ThisFnCC='#this-fn/cc'})

    def({Compare='compare'})
    def({CountOf='countof'})
    def({Slice='slice'})
    def({Cast='cast'})
    def({Size='size'})
    def({Alignment='alignment'})
    def({Unsigned='unsigned'})
    def({Bitwidth='bitwidth'})
    def({Super='super'})
    def({At='@'})
    def({ApplyType='apply-type'})
    def({ElementType="element-type"})
    def({Join='..'})
    def({Add='+'})
    def({Sub='-'})
    def({Mul='*'})
    def({Div='/'})
    def({Mod='%'})
    def({BitAnd='&'})
    def({BitOr='|'})
    def({BitXor='^'})
    def({BitNot='~'})
    def({LShift='<<'})
    def({RShift='>>'})
    def({Pow='**'})
    def({Repr='repr'})
    def({Styler='styler'})

    def({Equal='=='})
    def({NotEqual='!='})
    def({Greater='>'})
    def({GreaterEqual='>='})
    def({Less='<'})
    def({LessEqual='<='})

    -- ad-hoc builtin names
    def({ExecuteReturn='execute-return'})
    def({RCompare='rcompare'})
    def({CountOfForwarder='countof-forwarder'})
    def({SliceForwarder='slice-forwarder'})
    def({JoinForwarder='join-forwarder'})
    def({RCast='rcast'})
    def({ROp='rop'})
    def({CompareListNext="compare-list-next"})
    def({ReturnSafecall='return-safecall'})
    def({ReturnError='return-error'})
    def({XPCallReturn='xpcall-return'})

end

do
    define_symbols(function(kv)
        local key, value = next(kv)
        Symbol[key] = Symbol(value)
    end)
    protect(Symbol)
end

--------------------------------------------------------------------------------
-- ANCHOR
--------------------------------------------------------------------------------

local Anchor = class("Anchor")
local function assert_anchor(x)
    if getmetatable(x) == Anchor then
        return x
    else
        error("expected anchor, got " .. repr(x))
    end
end
do
    local cls = Anchor
    function cls:init(path, lineno, column, offset)
        assert_string(path)
        assert_number(lineno)
        assert_number(column)
        offset = offset or 0
        assert_number(offset)
        self.path = path
        self.lineno = lineno
        self.column = column
        self.offset = offset
    end
    -- defined elsewhere:
    -- function cls.stream_source_line
    function cls:format_plain()
        return self.path
            .. ':' .. format("%i", self.lineno)
            .. ':' .. format("%i", self.column)
    end
    function cls:stream_message_with_source(writer, msg, styler)
        styler = styler or default_styler
        writer(styler(Style.Location, self:format_plain() .. ":"))
        writer(" ")
        writer(msg)
        writer("\n")
        self:stream_source_line(writer, styler)
    end
    function cls:repr(styler)
        return styler(Style.Location, self:format_plain())
    end
    function cls:__tostring()
        return self:repr(default_styler)
    end
end

--------------------------------------------------------------------------------
-- SCOPES
--------------------------------------------------------------------------------

local Any = {}

local function assert_any(x)
    if getmetatable(x) == Any then
        return x
    else
        error("any expected, got " .. tostring(x))
    end
end

local function quote_error(msg)
    if type(msg) == "string" then
        msg = {msg = msg, quoted = true}
    end
    error(msg)
end

local set_active_anchor
local get_active_anchor
do
    local _active_anchor

    set_active_anchor = function(anchor)
        if anchor ~= null then
            assert_anchor(anchor)
        end
        _active_anchor = anchor
    end
    get_active_anchor = function()
        return _active_anchor
    end
end

local function with_anchor(anchor, f)
    local _anchor = get_active_anchor()
    set_active_anchor(anchor)
    f()
    set_active_anchor(_anchor)
end

local function exception(exc)
    if type(exc) ~= "table" then
        return {
            msg = tostring(exc),
            anchor = get_active_anchor()
        }
    else
        return exc
    end
end

local function location_error(exc)
    exc = exception(exc)
    exc.interpreter_error = true
    error(exc)
end

local function unwrap(_type, value)
    assert_any(value)
    if (value.type == _type) then
        return value.value
    else
        location_error("type "
            .. tostring(_type)
            .. " expected, got "
            .. tostring(value.type)
            )
    end
end

local maybe_unsyntax
local Type = {}

local Scope = class("Scope")
local function assert_scope(x)
    if getmetatable(x) == Scope then
        return x
    else
        error("scope expected, not " .. repr(x))
    end
end
do
    local cls = Scope
    function cls:init(scope)
        -- symbol -> any
        self.symbols = {}
        if scope ~= null then
            assert_scope(scope)
        end
        self.parent = scope
    end
    function cls:count()
        local count = 0
        for k,v in pairs(self.symbols) do
            count = count + 1
        end
        return count
    end
    function cls:totalcount()
        local count = 0
        while self do
            count = count + self:count()
            self = self.parent
        end
        return count
    end
    function cls:repr(styler)
        local totalcount = self:totalcount()
        local count = self:count()
        return
            styler(Style.Keyword, "scope")
            .. styler(Style.Comment, "<")
            .. format("%i+%i symbols", count, totalcount - count)
            .. styler(Style.Comment, ">")
    end
    function cls:__tostring()
        return self:repr(default_styler)
    end
    function cls:bind(sxname, value)
        local name = unwrap(Type.Symbol, maybe_unsyntax(sxname))
        assert_any(value)
        self.symbols[name] = { sxname, value }
    end
    function cls:del(sxname)
        local name = unwrap(Type.Symbol, maybe_unsyntax(sxname))
        self.symbols[name] = nil
    end
    function cls:lookup(name)
        assert_symbol(name)
        local entry = self.symbols[name]
        if entry then
            return entry[2]
        end
        if self.parent then
            return self.parent:lookup(name)
        end
    end

end

--------------------------------------------------------------------------------
-- Type
--------------------------------------------------------------------------------

local is_none

local function assert_type(x)
    if getmetatable(x) == Type then
        return x
    else
        error("type expected, got " .. repr(x))
    end
end
local function define_types(def)
    def('Nothing', 'Nothing')
    def('Any', 'Any')
    def('Type', 'type')
    def('Callable', 'Callable')

    def('Bool', 'bool')

    def('Integer', 'Integer')
    def('Real', 'Real')

    def('I8', 'i8')
    def('I16', 'i16')
    def('I32', 'i32')
    def('I64', 'i64')

    def('U8', 'u8')
    def('U16','u16')
    def('U32', 'u32')
    def('U64', 'u64')

    def('R32', 'r32')
    def('R64', 'r64')

    def('Builtin', 'Builtin')

    def('Scope', 'Scope')

    def('Symbol', 'Symbol')
    def('List', 'list')
    def('String', 'string')

    def('Form', 'Form')
    def('Parameter', 'Parameter')
    def('Label', 'Label')
    def('VarArgs', 'va-list')

    def('Ref', 'ref')

    def('Anchor', 'Anchor')

    def('BuiltinMacro', 'BuiltinMacro')
    def('Macro', 'Macro')

    def('Syntax', 'Syntax')

    def('Boxed', 'Boxed')
end

do
    Type.__index = Type
    local typemap = {}

    local cls = Type
    setmetatable(Type, {
        __call = function(cls, name)
            assert_symbol(name)
            local ty = typemap[name]
            if ty == null then
                ty = setmetatable({
                    name = name.name,
                    index = name.index,
                    symbol = name,
                    scope = Scope()
                }, Type)
                typemap[name] = ty
            end
            return ty
        end
    })
    protect(cls)
    function cls:bind(name, value)
        return self.scope:bind(name, value)
    end
    function cls:lookup(name)
        local value = self.scope:lookup(name)
        if value ~= null then
            return value
        end
        local super = self.scope:lookup(Symbol.Super)
        if super ~= null then
            return super.value:lookup(name)
        end
    end
    function cls:super()
        local super = self:lookup(Symbol.Super)
        return super and unwrap(Type.Type, super)
    end
    function cls:set_super(_type)
        assert_type(_type)
        self:bind(Any(Symbol.Super), Any(_type))
    end
    function cls:element_type()
        local et = self:lookup(Symbol.ElementType)
        return et and unwrap(Type.Type, et)
    end
    function cls:set_element_type(_type)
        assert_type(_type)
        self:bind(Any(Symbol.ElementType), Any(_type))
    end
    function cls:size()
        local sz = self:lookup(Symbol.Size)
        if sz then
            return sz and unwrap(Type.SizeT, sz)
        else
            return 0
        end
    end
    function cls:__call(...)
        return self:__call(...)
    end
    function cls:__tostring()
        return self:repr(default_styler)
    end
    function cls:repr(styler)
        return styler(Style.Type, self.name)
    end

    define_types(function(name, internalname)
        cls[name] = Type(Symbol(internalname))
    end)

    Type.SizeT = Type.U64

    function Type.TypedLabel(...)
        local typename = 'TypedLabel['
        for i=1,select('#', ...) do
            local argtype = select(i, ...)
            if i > 1 then
                typename = typename .. " "
            end
            typename = typename .. argtype.name
        end
        typename = typename .. "]"
        local _type = Type(Symbol(typename))
        if not rawget(_type, 'types') then
            _type:set_super(Type.Label)
            _type.types = { ... }
        end
        return _type
    end
end

local function is_macro_type(_type)
    assert_type(_type)
    if _type == Type.BuiltinMacro
        or _type == Type.Macro then
        return true
    end
    return false
end

local function each_numerical_type(f, opts)
    if opts == null then
        opts = {
            reals = true,
            ints = true,
        }
    end
    if opts.ints then
        opts.signed = true
        opts.unsigned = true
    end
    if opts.reals then
        f(Type.R32, float)
        f(Type.R64, double)
    end
    if opts.signed then
        f(Type.I8, int8_t)
        f(Type.I16, int16_t)
        f(Type.I32, int32_t)
        f(Type.I64, int64_t)
    end
    if opts.unsigned then
        f(Type.U8, uint8_t)
        f(Type.U16, uint16_t)
        f(Type.U32, uint32_t)
        f(Type.U64, uint64_t)
    end
end

--------------------------------------------------------------------------------
-- ANY
--------------------------------------------------------------------------------

local MT_TYPE_MAP = {
    [Symbol] = Type.Symbol,
    [Type] = Type.Type,
    [Scope] = Type.Scope,
    [Anchor] = Type.Anchor
}

do
    local function wrap(value)
        local t = type(value)
        if t == 'table' then
            local mt = getmetatable(value)
            local ty = MT_TYPE_MAP[mt]
            if ty ~= null then
                return ty, value
            end
        elseif t == 'string' then
            return Type.String, value
        elseif t == 'cdata' then
            local ct = typeof(value)
            if bool == ct then
                return Type.Bool, value
            elseif int8_t == ct then
                return Type.I8, value
            elseif int16_t == ct then
                return Type.I16, value
            elseif int32_t == ct then
                return Type.I32, value
            elseif int64_t == ct then
                return Type.I64, value
            elseif uint8_t == ct then
                return Type.U8, value
            elseif uint16_t == ct then
                return Type.U16, value
            elseif uint32_t == ct then
                return Type.U32, value
            elseif uint64_t == ct then
                return Type.U64, value
            elseif float == ct then
                return Type.R32, value
            elseif double == ct then
                return Type.R64, value
            end
            local refct = reflect.typeof(value)
            if is_char_array_ctype(refct) then
                return Type.String, cstr(value)
            end
        end
        error("unable to wrap " .. repr(value))
    end

    Any.__index = Any
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
end

local function is_expression_type(t)
    return t == Type.I32
        or t == Type.R32
        or t == Type.String
        or t == Type.Symbol
        or t == Type.Parameter
        or t == Type.List
end

function Any:repr(styler)
    if getmetatable(self.type) ~= Type then
        return styler(Style.Error, "corrupted value")
    else
        local s = self.type:format_value(self.value, styler) or "<format failed>"
        if not is_expression_type(self.type) then
            s = s .. styler(Style.Operator, ":") .. (self.type:repr(styler) or "<displayname failed>")
        end
        return s
    end
end

function Any.__tostring(self)
    return self:repr(default_styler)
end

local function assert_any_type(_type, value)
    assert_any(value)
    if (value.type == _type) then
        return value.value
    else
        error("type "
            .. tostring(_type)
            .. " expected, got "
            .. tostring(value.type)
            )
    end
end

local none = Any(Type.Nothing, NULL)
is_none = function(value)
    return value.type == Type.Nothing
end
local is_null_or_none = function(value)
    return value == null or value.type == Type.Nothing
end

--------------------------------------------------------------------------------
-- SYNTAX OBJECTS
--------------------------------------------------------------------------------

local function macro(x)
    assert_any(x)
    if x.type == Type.Builtin then
        return Any(Type.BuiltinMacro, x.value)
    elseif x.type == Type.Label then
        return Any(Type.Macro, x.value)
    else
        error("type " .. repr(x.type) .. " can not be a macro")
    end
end
local function unmacro(x)
    assert_any(x)
    if x.type == Type.BuiltinMacro then
        return Any(Type.Builtin, x.value)
    elseif x.type == Type.Macro then
        return Any(Type.Label, x.value)
    else
        error("type " .. repr(x.type) .. " is not a macro macro")
    end
end

local Syntax = {}
MT_TYPE_MAP[Syntax] = Type.Syntax
local assert_syntax = function(x)
    if getmetatable(x) == Syntax then
        return x
    else
        location_error("expected syntax, got " .. repr(x))
    end
end
do
    local cls = Syntax
    cls.__index = cls
    setmetatable(Syntax, {
        __call = function(cls, datum, anchor, quoted)
            assert_any(datum)
            assert_anchor(anchor)
            return setmetatable({
                datum = datum,
                anchor = anchor,
                quoted = quoted or false
            }, Syntax)
        end
    })
    function cls:__len()
        assert(false, "can't call len on syntax")
    end
    function cls:repr(styler)
        local prefix = self.anchor:format_plain() .. ":"
        if self.quoted then
            prefix = prefix .. "'"
        end
        return
            styler(Style.Comment, prefix)
            .. styler(Style.Comment, '[')
            .. self.datum:repr(styler)
            .. styler(Style.Comment, ']')
    end
    function cls:__tostring()
        return self:repr(default_styler)
    end
end

local function unsyntax(x)
    assert_any(x)
    x = unwrap(Type.Syntax, x)
    return x.datum, x.anchor
end

maybe_unsyntax = function(x)
    assert_any(x)
    if x.type == Type.Syntax then
        local anchor = x.value.anchor
        return x.value.datum, anchor
    else
        return x
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

local token_terminators = new(rawstring, "()[]{}\"';#,")

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
    function cls:next_column()
        return self.next_cursor - self.next_line + 1
    end
    function cls:anchor()
        return Anchor(self.path, self.lineno, self:column(), self:offset())
    end
    function cls:next()
        local c = self.next_cursor[0]
        verify_good_taste(c)
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
    function cls:read_single_symbol()
        self.string = self.cursor
        self.string_len = self.next_cursor - self.cursor
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
        while true do
            if self:is_eof() then
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
    function cls:read_comment()
        local col = self:column()
        while true do
            if self:is_eof() then
                break
            end
            local next_col = self:next_column()
            local c = self:next()
            if (c == CR) then
                self:newline()
            elseif C.isspace(c) == 0
                and next_col <= col then
                self.next_cursor = self.next_cursor - 1
                break
            end
        end
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
        set_active_anchor(self:anchor())
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
        if (c == CR) then
            self:newline()
        end
        if (0 ~= C.isspace(c)) then
            goto skip
        end
        cc = tochar(c)
        if (cc == '#') then
            self:read_comment()
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
        elseif (cc == ',') then
            self.token = Token.symbol
            self:read_single_symbol()
        else
            if (self:read_int64()
                or self:read_uint64()
                or self:read_real32()) then self.token = Token.number
            else self.token = Token.symbol; self:read_symbol() end
        end
    ::done::
        --print(get_token_name(self.token))
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

local SourceFile = class("SourceFile")
do
    local file_cache = setmetatable({}, {__mode = "v"})
    local gc_token = typeof('struct {}')

    local cls = SourceFile
    function cls:init(path)
        self.path = path
        self.fd = -1
        self.length = 0
        self.ptr = MAP_FAILED
    end

    function cls.open(path, str)
        assert_string(path)
        local file = file_cache[path]
        if file == null then
            if str ~= null then
                assert_string(str)
                local file = SourceFile(path)
                file.ptr = new(rawstring, str)
                file.length = #str
                -- keep reference
                file._str = str
                file_cache[path] = file
                return file
            else
                local file = SourceFile(path)
                file.fd = C.open(path, C._O_RDONLY)
                if (file.fd >= 0) then
                    file.length = C.lseek(file.fd, 0, C._SEEK_END)
                    file.ptr = C.mmap(null,
                        file.length, C._PROT_READ, C._MAP_PRIVATE, file.fd, 0)
                    if (file.ptr ~= MAP_FAILED) then
                        file_cache[path] = file
                        file._close_token = ffi.gc(new(gc_token),
                            function ()
                                file:close()
                            end)
                        return file
                    end
                end
                file:close()
            end
        else
            return file
        end
    end
    function cls:close()
        assert(not self._str)
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
    function cls:is_open()
        return (self.fd ~= -1)
    end
    function cls:strptr()
        assert(self:is_open() or self._str)
        return cast(rawstring, self.ptr)
    end
    local CR = ord('\n')
    function cls:stream_line(writer, offset, styler, indent)
        local str = self:strptr()
        if (offset >= self.length) then
            writer("<cannot display location in source file>\n")
            return
        end
        indent = indent or '    '
        local start = offset
        local send = offset
        while (start > 0) do
            local c = str[start-1]
            if (c == CR) then
                break
            end
            start = start - 1
        end
        while (start < offset) do
            local c = str[start]
            if C.isspace(c) == 0 then
                break
            end
            start = start + 1
        end
        while (send < self.length) do
            if (str[send] == CR) then
                break
            end
            send = send + 1
        end
        local line = zstr_from_buffer(str + start, send - start)
        writer(indent)
        writer(cstr(line))
        writer("\n")
        local column = offset - start
        if column > 0 then
            writer(indent)
            for i=1,column do
                writer(' ')
            end
            writer(styler(Style.Operator, '^'))
            writer("\n")
        end
    end
end

function Anchor:stream_source_line(writer, styler, indent)
    local sf = SourceFile.open(self.path)
    if sf then
        sf:stream_line(writer, self.offset, styler, indent)
    end
end


--------------------------------------------------------------------------------
-- S-EXPR PARSER
--------------------------------------------------------------------------------

local EOL = {count=0}
local List = {}
MT_TYPE_MAP[List] = Type.List
setmetatable(EOL, List)
local function assert_list(x)
    if getmetatable(x) == List then
        return x
    else
        error("expected list, got " .. repr(x))
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
            location_error("cannot index into empty list")
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
        assert_any(value)
        unwrap(Type.Syntax, value)
        prev = List(value, prev)
        assert(prev)
    end
    function cls.is_empty()
        return prev == EOL
    end
    function cls.reset_start()
        eol = prev
    end
    function cls.is_expression_empty()
        return (prev == EOL)
    end
    function cls.split(anchor)
        -- reverse what we have, up to last split point and wrap result
        -- in cell
        prev = List(Any(Syntax(Any(reverse_list_inplace(prev, eol)),anchor)), eol)
        assert(prev)
        cls.reset_start()
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
        local start_anchor = lexer:anchor()
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
                set_active_anchor(start_anchor)
                location_error("unclosed open bracket")
                -- point to beginning of list
                --error_origin = builder.getAnchor();
            elseif (lexer.token == Token.statement) then
                builder.split(lexer:anchor())
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
        local anchor = lexer:anchor()
        if (lexer.token == Token.open) then
            return Any(Syntax(Any(parse_list(Token.close)), anchor))
        elseif (lexer.token == Token.square_open) then
            local list = parse_list(Token.square_close)
            local sym = Symbol("square-list")
            return Any(Syntax(Any(List(Any(sym), list)), anchor))
        elseif (lexer.token == Token.curly_open) then
            local list = parse_list(Token.curly_close)
            local sym = Symbol("curly-list")
            return Any(Syntax(Any(List(Any(sym), list)), anchor))
        elseif ((lexer.token == Token.close)
            or (lexer.token == Token.square_close)
            or (lexer.token == Token.curly_close)) then
            location_error("stray closing bracket")
        elseif (lexer.token == Token.string) then
            return Any(Syntax(lexer:get_string(), anchor))
        elseif (lexer.token == Token.symbol) then
            return Any(Syntax(lexer:get_symbol(), anchor))
        elseif (lexer.token == Token.number) then
            return Any(Syntax(lexer:get_number(), anchor))
        else
            error("unexpected token: %c (%i)",
                tochar(lexer.cursor[0]), lexer.cursor[0])
        end
    end

    parse_naked = function(column, end_token)
        local lineno = lexer.lineno

        local escape = false
        local subcolumn = 0

        local anchor = lexer:anchor()
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
                lineno = lexer.lineno
                -- keep adding elements while we're in the same line
                while ((lexer.token ~= Token.eof)
                        and (lexer.token ~= end_token)
                        and (lexer.lineno == lineno)) do
                    builder.append(parse_naked(subcolumn, end_token))
                end
            elseif (lexer.token == Token.statement) then
                lexer:read_token()
                if not builder.is_empty() then
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

        return Any(Syntax(Any(builder.get_result()), anchor))
    end

    local function parse_root()
        lexer:read_token()
        local lineno = 0
        local escape = false

        local anchor = lexer:anchor()
        local builder = ListBuilder(lexer)

        while (lexer.token ~= Token.eof) do
            if lexer.token == Token.none then
                break
            elseif (lexer.token == Token.escape) then
                escape = true
                lexer:read_token()
                if (lexer.lineno <= lineno) then
                    location_error("escape character is not at end of line")
                end
                lineno = lexer.lineno
            elseif (lexer.lineno > lineno) then
                if (lexer:column() ~= 1) then
                    location_error("indentation mismatch")
                end

                escape = false
                lineno = lexer.lineno
                -- keep adding elements while we're in the same line
                while ((lexer.token ~= Token.eof)
                        and (lexer.token ~= Token.none)
                        and (lexer.lineno == lineno)) do
                    builder.append(parse_naked(1, Token.none))
                end
            elseif (lexer.token == Token.statement) then
                location_error("unexpected statement token")
            else
                builder.append(parse_any())
                lineno = lexer.next_lineno
                lexer:read_token()
            end
        end

        return Any(Syntax(Any(builder.get_result()), anchor))
    end

    return parse_root()
end

--------------------------------------------------------------------------------
-- VALUE PRINTER
--------------------------------------------------------------------------------

local StreamValueFormat
local stream_expr
do
local ANCHOR_SEP = ":"
local INDENT_SEP = "⁞"

-- keywords and macros
local KEYWORDS = set(split(
    "let true false fn xfn quote with ::* ::@ call escape do dump-syntax"
        .. " syntax-extend if else elseif loop continue none assert qquote-syntax"
        .. " unquote unquote-splice globals return splice"
        .. " try except define in loop-for empty-list empty-tuple raise"
        .. " yield xlet cc/call fn/cc null break quote-syntax recur"
        .. " fn-types"
    ))

    -- builtin and global functions
local FUNCTIONS = set(split(
    "external branch print repr tupleof import-c eval structof typeof"
        .. " macro block-macro block-scope-macro cons expand empty? type?"
        .. " dump syntax-head? countof slice none? list-atom? label?"
        .. " list-load list-parse load require cstr exit hash min max"
        .. " va@ va-countof range zip enumerate cast element-type"
        .. " qualify disqualify iter va-iter iterator? list? symbol? parse-c"
        .. " get-exception-handler xpcall error sizeof alignof prompt null?"
        .. " extern-library arrayof get-scope-symbol syntax-cons vectorof"
        .. " datum->syntax syntax->datum syntax->anchor syntax-do"
        .. " syntax-error ordered-branch alloc syntax-list syntax-quote"
        .. " syntax-unquote syntax-quoted? bitcast concat repeat product"
        .. " zip-fill integer? callable? extract-memory box unbox pointerof"
        .. " scopeof"
    ))

-- builtin and global functions with side effects
local SFXFUNCTIONS = set(split(
    "set-scope-symbol! set-type-symbol! set-globals! set-exception-handler!"
        .. " copy-memory! ref-set!"
    ))

-- builtin operator functions that can also be used as infix
local OPERATORS = set(split(
    "+ - ++ -- * / % == != > >= < <= not and or = @ ** ^ & | ~ , . .. : += -="
        .. " *= /= %= ^= &= |= ~= <- ? := // << >> <> <:"
    ))

local TYPES = set(split(
        "int i8 i16 i32 i64 u8 u16 u32 u64 Nothing string ref"
        .. " r16 r32 r64 half float double Symbol list Parameter"
        .. " Label Integer Real array tuple vector"
        .. " pointer struct enum bool uint Qualifier Syntax Anchor Scope"
        .. " Iterator type size_t usize_t ssize_t void* Callable Boxed Any"
    ))

StreamValueFormat = function(naked, depth, opts)
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
    opts.anchors = opts.anchors or "line"
    opts.styler = opts.styler or default_styler
    return opts
end

local simple_types = set({
    Type.Symbol, Type.String, Type.I32, Type.R32
})

local function is_nested(e)
    e = maybe_unsyntax(e)
    if (e.type == Type.List) then
        local it = e.value
        while (it ~= EOL) do
            local q = maybe_unsyntax(it.at)
            if simple_types[q.type] == null then
                return true
            end
            it = it.next
        end
    end
    return false
end

stream_expr = function(writer, e, format)
    format = format or StreamValueFormat()

    local depth = format.depth
    local maxdepth = format.maxdepth
    local maxlength = format.maxlength
    local naked = format.naked
    local line_anchors = (format.anchors == "line")
    local atom_anchors = (format.anchors == "all")
    local styler = format.styler

    local last_anchor

    local function stream_indent(writer, depth)
        depth = depth or 0
        if depth >= 1 then
            local indent = "    "
            for i=2,depth do
                indent = indent .. INDENT_SEP .. "   "
            end
            writer(styler(Style.Comment, indent))
        end
    end

    local function stream_anchor(anchor, quoted)
        if anchor then
            local str
            if not last_anchor or last_anchor.path ~= anchor.path then
                str = anchor.path
                    .. ":" .. tostring(anchor.lineno)
                    .. ":" .. tostring(anchor.column) .. ANCHOR_SEP
            elseif not last_anchor or last_anchor.lineno ~= anchor.lineno then
                str = ":" .. tostring(anchor.lineno)
                    .. ":" .. tostring(anchor.column) .. ANCHOR_SEP
            elseif not last_anchor or last_anchor.column ~= anchor.column then
                str = "::" .. tostring(anchor.column) .. ANCHOR_SEP
            else
                str = "::" .. ANCHOR_SEP
            end
            if quoted then
                str = "'" .. str
            end
            writer(styler(Style.Comment, str))
            last_anchor = anchor
        else
            --writer(styler(Style.Error, "?"))
        end
    end

    local function islist(value)
        value = maybe_unsyntax(value)
        return value.type == Type.List
    end

    local function walk(e, depth, maxdepth, naked)
        assert_any(e)

        local quoted = false

        local anchor
        if e.type == Type.Syntax then
            anchor = e.value.anchor
            quoted = e.value.quoted
            e = unsyntax(e)
        end

        local otype = e.type

        if (naked) then
            stream_indent(writer, depth)
        end
        if atom_anchors then
            stream_anchor(anchor, quoted)
        end

        if (e.type == Type.List) then
            if naked and line_anchors and not atom_anchors then
                stream_anchor(anchor, quoted)
            end

            maxdepth = maxdepth - 1

            local it = e.value
            if (it == EOL) then
                writer(styler(Style.Operator,"()"))
                if (naked) then
                    writer('\n')
                end
                return
            end
            if maxdepth == 0 then
                writer(styler(Style.Operator,"("))
                writer(styler(Style.Comment,"<...>"))
                writer(styler(Style.Operator,")"))
                if (naked) then
                    writer('\n')
                end
                return
            end
            local offset = 0
            --local numsublists = 0
            if (naked) then
                if islist(it.at) then
                    writer(";")
                    writer('\n')
                    goto print_sparse
                end
            ::print_terse::
                depth = depth
                naked = false
                walk(it.at, depth, maxdepth, naked)
                it = it.next
                offset = offset + 1
                while it ~= EOL do
                    --[[
                    if islist(it.at) then
                        numsublists = numsublists + 1
                        if numsublists >= 2 then
                            break
                        end
                    end
                    --]]
                    if is_nested(it.at) then
                        break
                    end
                    writer(' ')
                    walk(it.at, depth, maxdepth, naked)
                    offset = offset + 1
                    it = it.next
                end
                writer('\n')
            ::print_sparse::
                while (it ~= EOL) do
                    local depth = depth + 1
                    naked = true
                    local value = it.at
                    if not islist(value) -- not a list
                        and (offset >= 1) then -- not first element in list
                        stream_indent(writer, depth)
                        writer("\\ ")
                        goto print_terse
                    end
                    if (offset >= maxlength) then
                        stream_indent(writer, depth)
                        writer("<...>\n")
                        return
                    end
                    walk(value, depth, maxdepth, naked)
                    offset = offset + 1
                    it = it.next
                end

            else
                depth = depth + 1
                naked = false
                writer(styler(Style.Operator,'('))
                while (it ~= EOL) do
                    if (offset > 0) then
                        writer(' ')
                    end
                    if (offset >= maxlength) then
                        writer(styler(Style.Comment,"..."))
                        break
                    end
                    walk(it.at, depth, maxdepth, naked)
                    offset = offset + 1
                    it = it.next
                end
                writer(styler(Style.Operator,')'))
                if (naked) then
                    writer('\n')
                end
            end
        else
            if (e.type == Type.Symbol) then
                local name = e.value.name
                local style =
                    (format.keywords[name] and Style.Keyword)
                    or (format.functions[name] and Style.Function)
                    or (format.sfxfunctions[name] and Style.SfxFunction)
                    or (format.operators[name] and Style.Operator)
                    or (format.types[name] and Style.Type)
                    or Style.Symbol
                writer(styler(style, escape_string(name, SYMBOL_ESCAPE_CHARS)))
            else
                writer(e.type:format_value(e.value, styler))
            end
            if quoted or not is_expression_type(otype) then
                writer(styler(Style.Operator, ":"))
                if quoted then
                    writer(styler(Style.Comment, "'"))
                end
                writer(tostring(otype))
            end
            if (naked) then
                writer('\n')
            end
        end
    end
    walk(e, depth, maxdepth, naked)
end
end -- do

function List.repr(self, styler)
    local s = ""
    local fmt = StreamValueFormat(false)
    fmt.styler = styler
    fmt.maxdepth = 5
    fmt.maxlength = 10
    stream_expr(
        function (x)
            s = s .. x
        end,
        Any(self), fmt)
    return s
end

function List.__tostring(self)
    return List.repr(self, default_styler)
end

--------------------------------------------------------------------------------
-- IL OBJECTS
--------------------------------------------------------------------------------

-- CFF form implemented after
-- Leissa et al., Graph-Based Higher-Order Intermediate Representation
-- http://compilers.cs.uni-saarland.de/papers/lkh15_cgo.pdf

local Builtin = class("Builtin")
MT_TYPE_MAP[Builtin] = Type.Builtin
do
    local cls = Builtin
    function cls:init(func, name)
        assert_function(func)
        self.func = func
        name = name or Symbol.Unnamed
        assert_symbol(name)
        self.name = name
        -- self.type_func -- receives argument types and returns return type
        -- self.foldable -- if true, can be folded
    end
    function cls:__call(...)
        return self.func(...)
    end
    function cls:repr(styler)
        if self.name ~= Symbol.Unnamed then
            return styler(Style.Function, self.name.name)
        else
            return styler(Style.Error, tostring(self.func))
        end
    end
    function cls:__tostring()
        return self:repr(default_styler)
    end
end

local Form = class("Form")
MT_TYPE_MAP[Form] = Type.Form
do
    local cls = Form
    function cls:init(func, name)
        assert_function(func)
        assert_symbol(name)
        self.func = func
        self.name = name
    end
    function cls:__call(...)
        return self.func(...)
    end
    function cls:repr(styler)
        return styler(Style.Keyword, self.name.name)
    end
    function cls:__tostring()
        return self:repr(default_styler)
    end
end

local Parameter = class("Parameter")
MT_TYPE_MAP[Parameter] = Type.Parameter
local function assert_parameter(x)
    if getmetatable(x) == Parameter then
        return x
    else
        error("expected parameter, got " .. repr(x))
    end
end
do
    local cls = Parameter
    function cls:init(name, _type)
        local name,anchor = unsyntax(name)
        name = unwrap(Type.Symbol, name)
        assert_symbol(name)
        assert_anchor(anchor)
        _type = _type or Type.Any
        assert_type(_type)
        self.label = null
        self.index = -1
        self.name = name
        self.type = _type
        self.anchor = anchor
        self.vararg = endswith(name.name, "...")
        self.users = setmetatable({}, {__mode="k"})
    end
    function cls:add_user(label, argindex)
        if label ~= self.label then
            self.users[label] = true
        end
    end
    function cls:local_repr(styler)
        local name
        if self.name ~= Symbol.Unnamed or self.index <= 0 then
            name = styler(Style.Comment, "%")
                .. styler(Style.Symbol, self.name.name)
        else
            name = styler(Style.Operator, "@")
                .. styler(Style.Number, self.index - 1)
        end
        if self.vararg then
            name = name .. styler(Style.Keyword, "…")
        end
        if self.type ~= Type.Any then
            name = name .. styler(Style.Operator, ":")
                .. self.type:repr(styler)
        end
        return name
    end
    function cls:repr(styler)
        local name
        if self.label then
            name = self.label:short_repr(styler)
        else
            name = styler(Style.Comment, '<unbound>')
        end
        return name .. self:local_repr(styler)
    end
    function cls:__tostring()
        return self:repr(default_styler)
    end
    function cls.create_from_parameter(param)
        local pparam = Parameter(Any(Syntax(Any(param.name), param.anchor)))
        pparam.vararg = param.vararg
        pparam.type = param.type
        return pparam
    end
end

local ARG_Cont = 1
local ARG_Arg0 = 2

local PARAM_Cont = 1
local PARAM_Arg0 = 2

local Label = {}
MT_TYPE_MAP[Label] = Type.Label
local function assert_label(x)
    if getmetatable(x) == Label then
        return x
    else
        error("expected label, got " .. repr(x))
    end
end
do
    local cls = Label
    cls.__index = cls
    local unique_id_counter = 1

    setmetatable(cls, {
        __call = function(cls, name)
                local self = {}
                local name,anchor = unsyntax(name)
                name = unwrap(Type.Symbol, name)
                assert_symbol(name)
                self.uid = unique_id_counter
                unique_id_counter = unique_id_counter + 1
                -- next label/builtin
                -- self.enter = nil

                -- next.parameter index matches self.argument index
                -- unless a parameter is flagged as vararg
                -- first parameter/argument is reserved for return continuation
                self.parameters = {}
                --self.arguments = {}
                self.name = name
                self.anchor = anchor
                self.users = setmetatable({}, {__mode="k"})
                setmetatable(self, cls)
                return self
            end
    })

    function cls.__newindex(self, name, value)
        error("can't directly set attribute: " .. name)
    end

    function cls:add_user(label, argindex)
        if label ~= self then
            self.users[label] = true
        end
    end

    local function using(self, arg, i)
        if arg.type == Type.Parameter or arg.type == Type.Label then
            arg.value:add_user(self, i)
        end
    end

    function cls:set_enter(enter)
        assert_any(enter)
        using(self, enter, 0)
        rawset(self, 'enter', enter)
    end

    function cls:set_arguments(arguments)
        assert(not self.arguments)
        for i=1,#arguments do
            local arg = arguments[i]
            assert_any(arg)
            using(self, arg, i)
        end
        rawset(self, 'arguments', arguments)
    end

    function cls:set_body_anchor(anchor)
        assert_anchor(anchor)
        rawset(self, 'body_anchor', anchor)
    end

    function cls:short_repr(styler)
        return
            styler(Style.Keyword, "λ")
            .. styler(Style.Symbol, self.name.name)
            .. styler(Style.Operator, "#")
            .. styler(Style.Number, self.uid)
    end

    function cls:repr(styler)
        local name = self:short_repr(styler)
            .. styler(Style.Operator, "(")
        for _,param in ipairs(self.parameters) do
            if _ > 1 then
                name = name .. " "
            end
            name = name .. param:local_repr(styler)
        end
        name = name .. styler(Style.Operator, ")")
        return name
    end

    function cls:__tostring()
        return self:repr(default_styler)
    end

    function cls:append_parameter(param)
        assert_table(param)
        assert_parameter(param)
        assert(param.label == null)
        param.label = self
        table.insert(self.parameters, param)
        param.index = #self.parameters
        return param
    end

    -- an empty function
    -- you have to add the continuation argument manually
    function cls.create_empty_function(name)
        return Label(name)
    end

    -- a function that eventually returns
    function cls.create_function(name)
        local value = Label(name)
        local sym, anchor = maybe_unsyntax(name)
        sym = unwrap(Type.Symbol, sym)
        -- continuation is always first argument
        -- this argument will be called when the function is done
        value:append_parameter(
            Parameter(Any(Syntax(Any(Symbol("return-" .. sym.name)),anchor)),
                Type.Any))
        return value
    end

    -- only inherits name and anchor
    function cls.create_from_label(label)
        local ll = Label(Any(Syntax(Any(label.name), label.anchor)))
        ll:set_body_anchor(label.body_anchor)
        return ll
    end

    -- a continuation that never returns
    function cls.create_continuation(name)
        local value = Label(name)
        -- first argument is present, but unused
        value:append_parameter(
            Parameter(name, Type.Nothing))
        return value
    end
end

--------------------------------------------------------------------------------
-- IL PRINTER
--------------------------------------------------------------------------------

local stream_il
local CONT_SEP = " ⮕ "
do
    stream_il = function(writer, afunc, opts)
        opts = opts or {}
        local follow_labels = true
        local follow_params = true
        if opts.follow_labels ~= null then
            follow_labels = opts.follow_labels
        end
        if opts.follow_params ~= null then
            follow_params = opts.follow_params
        end
        local styler = opts.styler or default_styler
        local line_anchors = (opts.anchors == "line")
        local atom_anchors = (opts.anchors == "all")
        local users = opts.users or {}
        local scopes = opts.scopes or {}

        local last_anchor
        local function stream_anchor(anchor)
            if anchor then
                local str
                if not last_anchor or last_anchor.path ~= anchor.path then
                    str = anchor.path
                        .. ":" .. tostring(anchor.lineno)
                        .. ":" .. tostring(anchor.column) .. ANCHOR_SEP
                elseif not last_anchor or last_anchor.lineno ~= anchor.lineno then
                    str = ":" .. tostring(anchor.lineno)
                        .. ":" .. tostring(anchor.column) .. ANCHOR_SEP
                elseif not last_anchor or last_anchor.column ~= anchor.column then
                    str = "::" .. tostring(anchor.column) .. ANCHOR_SEP
                else
                    str = "::" .. ANCHOR_SEP
                end

                writer(styler(Style.Comment, str))
                last_anchor = anchor
            else
                --writer(styler(Style.Error, "?"))
            end
        end

        local visited = {}
        local stream_any
        local function stream_label_label(alabel)
            writer(styler(Style.Keyword, "λ"))
            writer(styler(Style.Symbol, alabel.name.name))
            writer(styler(Style.Operator, "#"))
            writer(styler(Style.Number, tostring(alabel.uid)))
        end
        local function stream_label_label_user(alabel)
            writer(styler(Style.Comment, "λ"))
            writer(styler(Style.Comment, alabel.name.name))
            writer(styler(Style.Comment, "#"))
            writer(styler(Style.Comment, tostring(alabel.uid)))
        end

        local function stream_param_label(param, alabel)
            if param.label ~= alabel then
                stream_label_label(param.label)
            end
            if param.name == Symbol.Unnamed then
                writer(styler(Style.Operator, "@"))
                writer(styler(Style.Number, tostring(param.index - 1)))
            else
                writer(styler(Style.Comment, "%"))
                writer(styler(Style.Symbol, param.name.name))
            end
            if param.type ~= Type.Any then
                writer(styler(Style.Comment, ":"))
                writer(param.type:repr(styler))
            end
            if param.vararg then
                writer(styler(Style.Keyword, "…"))
            end
        end

        local function stream_argument(arg, alabel)
            if arg.type == Type.Syntax then
                local anchor
                arg,anchor = unsyntax(arg)
                if atom_anchors then
                    stream_anchor(anchor)
                end
            end

            if arg.type == Type.Parameter then
                stream_param_label(arg.value, alabel)
            elseif arg.type == Type.Label then
                stream_label_label(arg.value)
            else
                writer(tostring(arg))
            end
        end

        local function stream_users(_users)
            if _users then
                writer(styler(Style.Comment, "{"))
                local k = 0
                for dest,_ in pairs(_users) do
                    if k > 0 then
                        writer(" ")
                    end
                    stream_label_label_user(dest)
                    k = k + 1
                end
                writer(styler(Style.Comment, "}"))
            end
        end

        local function stream_scope(_scope)
            if _scope then
                writer(" ")
                writer(styler(Style.Comment, "<"))
                local k = 0
                for dest,_ in pairs(_scope) do
                    if k > 0 then
                        writer(" ")
                    end
                    stream_label_label_user(dest)
                    k = k + 1
                end
                writer(styler(Style.Comment, ">"))
            end
        end

        local function stream_label (alabel)
            if visited[alabel] then
                return
            end
            visited[alabel] = true
            if line_anchors then
                stream_anchor(alabel.anchor)
            end
            writer(styler(Style.Symbol, alabel.name.name))
            writer(styler(Style.Operator, "#"))
            writer(styler(Style.Number, tostring(alabel.uid)))
            stream_users(users[alabel])
            writer(" ")
            writer(styler(Style.Operator, "("))
            for i,param in ipairs(alabel.parameters) do
                if i > 1 then
                    writer(" ")
                end
                stream_param_label(param, alabel)
                stream_users(users[param])
            end
            writer(styler(Style.Operator, "):"))
            stream_scope(scopes[alabel])
            writer("\n    ")
            if not alabel.enter then
                writer(styler(Style.Error, "empty"))
            else
                if line_anchors and alabel.body_anchor then
                    stream_anchor(alabel.body_anchor)
                    writer(' ')
                end
                stream_argument(alabel.enter, alabel)
                for i=2,#alabel.arguments do
                    writer(" ")
                    local arg = alabel.arguments[i]
                    stream_argument(arg, alabel)
                end
                local cont = alabel.arguments[1]
                if cont and not is_none(maybe_unsyntax(cont)) then
                    writer(styler(Style.Comment,CONT_SEP))
                    stream_argument(cont, alabel)
                end
            end
            writer("\n")

            for i,arg in ipairs(alabel.arguments) do
                arg = maybe_unsyntax(arg)
                stream_any(arg)
            end
            if alabel.enter then
                stream_any(maybe_unsyntax(alabel.enter))
            end
        end
        stream_any = function(afunc)
            if afunc.type == Type.Label and follow_labels then
                stream_label(afunc.value)
            end
        end
        if afunc.type == Type.Label then
            stream_label(afunc.value)
        else
            error("can't descend into type " .. tostring(afunc.type))
        end
    end
end

--------------------------------------------------------------------------------
-- DEBUG SERVICES
--------------------------------------------------------------------------------

local debugger = {}
do
    local stack = {}
    local cls = debugger
    local last_anchor
    function cls.dump_traceback()
        cls.stream_traceback(stderr_writer)
    end
    function cls.stack_level()
        return #stack
    end
    function cls.clear_traceback()
        stack = {}
    end
    function cls.stream_traceback(writer, opts)
        opts = opts or {}
        local styler = opts.styler or default_styler
        local start = opts.stack_start or 1
        local endn = max(0, opts.stack_end or 0)
        writer("Traceback (most recent call last):\n")
        for i=start,#stack - endn do
            local entry = stack[i]
            local anchor = entry[1]
            local cont = entry[2]
            local dest = entry[3]
            if dest.type == Type.Builtin then
                local builtin = dest.value
                writer('  in builtin ')
                writer(tostring(builtin))
                writer('\n')
            elseif dest.type == Type.Label then
                local label = dest.value
                if anchor == null then
                    anchor = label.body_anchor or label.anchor
                end
                if anchor then
                    writer('  File ')
                    writer(repr(anchor.path))
                    writer(', line ')
                    writer(styler(Style.Number, tostring(anchor.lineno)))
                    if label.name ~= Symbol.Unnamed then
                        writer(', in ')
                        writer(tostring(label.name))
                    end
                    writer('\n')
                    anchor:stream_source_line(writer, styler)
                end
            end
        end
    end
    local function is_eq(a,b)
        if a.type ~= b.type then
            return false
        end
        if is_none(a) then
            return true
        end
        if a.value ~= b.value then
            return false
        end
        return true
    end
    local function pop_stack(i)
        for k=#stack,i,-1 do
            stack[k] = null
        end
    end
    function cls.enter_call(dest, cont, ...)
        for i=1,#stack do
            local entry = stack[i]
            local _cont = entry[3]
            if is_none(_cont) or is_eq(_cont, dest) then
                pop_stack(i)
                break
            end
        end

        if #stack > 0 then
            stack[#stack][1] = last_anchor
        end
        local anchor
        if dest.type == Type.Label then
            local label = dest.value
            local label_anchor = label.body_anchor or label.anchor
            if label_anchor then
                last_anchor = label_anchor
                anchor = label.anchor
                set_active_anchor(last_anchor)
            end
        end
        if #stack >= global_opts.stack_limit then
            location_error("stack overflow")
        end
        table.insert(stack, { anchor, cont, dest, ... })
        if false then
            for i=1,#stack do
                local entry = stack[i]
                print(i,unpack(entry))
            end
            print("----")
        end
    end
end

--------------------------------------------------------------------------------
-- LAMBDA MANGLING
--------------------------------------------------------------------------------

local execute
local mangle
do

local function build_scope(entry)
    local visited = {}
    local scope = {}

    local function can_walk_label(label)
        if entry == label then
            return false
        end
        return not visited[label]
    end

    local function walk(scope_label)
        if scope_label ~= entry then
            visited[scope_label] = true
            table.insert(scope, scope_label)
            -- users of live_label are indirectly live in topscope_label
            for live_label,_ in pairs(scope_label.users) do
                if can_walk_label(live_label) then
                    walk(live_label)
                end
            end
        end
        for _,param in ipairs(scope_label.parameters) do
            -- every label using one of our parameters is live in scope
            for live_label,_ in pairs(param.users) do
                if can_walk_label(live_label) then
                    walk(live_label)
                end
            end
        end
    end

    --print("-----")
    --stream_il(stdout_writer, Any(entry))
    --print("-----")
    walk(entry, 1)

    return scope
end

local function fold_constants(enter, body)
    if enter.type == Type.Builtin then
        local ev = enter.value
        local ff = ev.fold_func
        if ff then
            return ff(enter, body)
        elseif ev.foldable then
            for i=2,#body do
                if body[i].type == Type.Parameter then
                    return enter, body
                end
            end

            local ret = body[1]
            table.remove(body, 1)

            local result

            execute(enter, function(...)
                result = { none, ... }
            end, unpack(body))

            return ret, result
        end
    end
    return enter, body
end

local function remap_body(ll, entry, map)
    local enter = entry.enter
    local narg = enter
    if enter.type == Type.Label or enter.type == Type.Parameter then
        enter = map[enter.value] or enter
        assert_any(enter)
        if (enter.type == Type.VarArgs) then
            if #enter.value == 0 then
                enter = none
            else
                enter = enter.value[1]
            end
        end
    end

    local args = entry.arguments
    local body = {}
    for i,arg in ipairs(args) do
        if arg.type == Type.Label or arg.type == Type.Parameter then
            arg = map[arg.value] or arg
            if arg.type == Type.VarArgs then
                if i == #args then
                    for _,subarg in ipairs(arg.value) do
                        table.insert(body, subarg)
                    end
                elseif #arg.value == 0 then
                    table.insert(body, none)
                else
                    table.insert(body, arg.value[1])
                end
            else
                assert_any(arg)
                table.insert(body, arg)
            end
        else
            table.insert(body, arg)
        end
    end

    -- constant folding produces errors when performed in in unreachable branches
    -- set_active_anchor(ll.body_anchor or ll.anchor)
    -- enter, body = fold_constants(enter, body)

    ll:set_enter(enter)
    ll:set_arguments(body)
end

mangle = function(entry, params, map)
    assert_label(entry)
    local entry_scope = build_scope(entry)
    -- remap entry point
    local le = Label.create_from_label(entry)
    for _,param in ipairs(params) do
        le:append_parameter(param)
    end
    -- create new labels and map new parameters
    for _,l in ipairs(entry_scope) do
        local ll = Label.create_from_label(l)
        assert(not map[l])
        map[l] = Any(ll)
        for _,param in ipairs(l.parameters) do
            local pparam = Parameter.create_from_parameter(param)
            assert(not map[param])
            map[param] = Any(pparam)
            ll:append_parameter(pparam)
        end
    end
    -- remap label bodies
    for _,l in ipairs(entry_scope) do
        local ll = map[l].value
        remap_body(ll, l, map)
    end
    remap_body(le, entry, map)

    return le
end

end

--------------------------------------------------------------------------------
-- INTERPRETER
--------------------------------------------------------------------------------

local function dump_trace(writer, dest, cont, ...)
    writer(repr(dest))
    for i=1,select('#', ...) do
        writer(' ')
        writer(repr(select(i, ...)))
    end
    if not is_none(cont) then
        writer(default_styler(Style.Comment, CONT_SEP))
        writer(repr(cont))
    end
end

local EOM = {}
local function load_val(map, keys)
    for i=1,#keys do
        local key = keys[i]
        map = map[key]
        if map == nil then
            return
        end
    end
    return map[EOM]
end
local function store_val(map, keys, val)
    for i=1,#keys do
        local key = keys[i]
        local submap = map[key]
        if submap == nil then
            submap = {}
            map[key] = submap
        end
        map = submap
    end
    map[EOM] = val
end

local label_cache = {}
local call
local function call_label(label, cont, ...)
    assert_any(cont)
    assert_label(label)

    local rbuf = { cont, ... }
    local rcount = #rbuf

    local pcount = #label.parameters
    --assert(pcount >= 1)

    if pcount > 0 then
        local map = {}

        local srci = 1
        for i=1,pcount do
            local param = label.parameters[i]
            local arg
            if param.vararg then
                if i == 1 then
                    location_error("continuation parameter can't be vararg")
                end
                if param.type ~= Type.Any then
                    location_error("vararg parameter can't be typed")
                end
                -- how many parameters after this one
                local remparams = pcount - i
                -- how many varargs to capture
                local vargsize = max(0, rcount - srci - remparams + 1)
                local argvalues = {}
                for k=srci,(srci + vargsize - 1) do
                    table.insert(argvalues, rbuf[k])
                end
                arg = Any(Type.VarArgs, argvalues)
                srci = srci + vargsize
            elseif srci <= rcount then
                local value = rbuf[srci]
                if param.type ~= Type.Any then
                    unwrap(param.type, value)
                end
                arg = value
                srci = srci + 1
            else
                if param.type ~= Type.Any then
                    unwrap(param.type, none)
                end
                arg = none
            end
            assert_any(arg)
            map[param] = arg
        end

        label = mangle(label, {}, map)
    end

    --print("before:")
    --stream_il(stdout_writer, Any(label))
    --print("----")

    if global_opts.trace_execution then
        local w = string_writer()
        w(default_styler(Style.Keyword, "label "))
        dump_trace(w, label.enter, unpack(label.arguments))
        if label.body_anchor then
            label.body_anchor:stream_message_with_source(stderr_writer, w())
        else
            stderr_writer("<unknown source location>: ")
            stderr_writer(w())
        end
        stderr_writer('\n')
    end

    if not label.enter then
        location_error("label is empty")
    end

    return call(label.enter, unpack(label.arguments))
end

call = function(dest, cont, ...)
    if global_opts.trace_execution then
        stderr_writer(default_styler(Style.Keyword, "trace "))
        dump_trace(stderr_writer, dest, cont, ...)
        stderr_writer('\n')
    end
    assert_any(cont)
    assert_any(dest)
    for i=1,select('#', ...) do
        assert_any(select(i, ...))
    end

    if dest.type == Type.Label then
        debugger.enter_call(dest, cont, ...)
        return call_label(dest.value, cont, ...)
    elseif dest.type == Type.Builtin then
        debugger.enter_call(dest, cont, ...)
        local func = dest.value.func
        return func(dest, cont, ...)
    elseif dest.type == Type.Type then
        local ty = dest.value
        local func = ty:lookup(Symbol.ApplyType)
        if func ~= null then
            return call(func, cont, ...)
        else
            location_error("can not apply type "
                .. tostring(ty))
        end
    else
        location_error("don't know how to apply value of type "
            .. tostring(dest.type))
    end
end

local function retcall (dest, ...)
    return call(dest, none, ...)
end

execute = function(dest, cont, ...)
    assert_any(dest)
    assert_function(cont)
    local ret = Any(Builtin(function(dest, _cont, ...)
        return cont(...)
    end, Symbol.ExecuteReturn))
    return call(dest, ret, ...)
end

function Type:format_value(value, styler)
    local reprf = self:lookup(Symbol.Repr)
    if reprf then
        local result
        execute(reprf, function(value)
            result = unwrap(Type.String, value)
        end, Any(self, value), Any(Builtin(function(self, cont, style, text)
            return retcall(cont,
                Any(styler(
                    unwrap(Type.String, style),
                    unwrap(Type.String, text))))
        end, Symbol.Styler)))
        return result
    end
    if type(value) == "table" then
        local mt = getmetatable(value)
        if mt and rawget(mt, 'repr') then
            return mt.repr(value, styler)
        end
    end
    return styler(Style.Operator, "[")
        .. tostring(self:size()) .. " bytes"
        .. styler(Style.Operator, "]")
end

--------------------------------------------------------------------------------
-- MACRO EXPANDER
--------------------------------------------------------------------------------

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
    assert_list(topit)
    assert(topit ~= EOL)
    local val = topit.at
    verify_list_parameter_count(
        unwrap(Type.List, unsyntax(val)), mincount, maxcount)
end

--------------------------------------------------------------------------------

local globals

local expand
local translate

local expand_fn_cc
local expand_syntax_extend

local expand_root

local function wrap_expand_builtin(f)
    return function(dest, cont, topit, env)
        return f(unwrap(Type.Scope, env), unwrap(Type.List, topit),
            function (cur_list, cur_env)
                assert(cur_env)
                return retcall(cont, Any(cur_list), Any(cur_env))
            end)
    end
end

do

local function expand_expr_list(env, it, cont)
    assert_scope(env)
    assert_list(it)

    local function process(env, it, l)
        if it == EOL then
            return cont(reverse_list_inplace(l))
        end
        return expand(env, it, function(nextlist,nextscope)
            assert_list(nextlist)
            if (nextlist == EOL) then
                return cont(reverse_list_inplace(l))
            end
            return process(
                nextscope,
                nextlist.next,
                List(nextlist.at, l))
        end)
    end
    return process(env, it, EOL)
end

expand_fn_cc = function(env, topit, cont)
    assert_scope(env)
    assert_list(topit)
    verify_at_parameter_count(topit, 1, -1)

    local it = topit.at

    local nit,anchor = unsyntax(it)
    it = unwrap(Type.List, nit)

    local _,anchor_kw = unsyntax(it.at)

    it = it.next

    local func_name
    assert(it ~= EOL)

    local scopekey

    local tryfunc_name = unsyntax(it.at)
    if (tryfunc_name.type == Type.Symbol) then
        func_name = it.at
        it = it.next
        scopekey = tryfunc_name
    elseif (tryfunc_name.type == Type.String) then
        func_name = Any(Syntax(Any(Symbol(tryfunc_name.value)), anchor_kw))
        it = it.next
    else
        func_name = Any(Syntax(Any(Symbol.Unnamed), anchor_kw))
    end

    local expr_parameters = it.at
    local params_anchor
    expr_parameters, params_anchor = unsyntax(expr_parameters)

    it = it.next

    local func = Label.create_empty_function(func_name, anchor)
    if scopekey then
        -- named self-binding
        env:bind(scopekey, Any(func))
    end
    -- hidden self-binding for subsequent macros
    env:bind(Any(Symbol.ThisFnCC), Any(func))

    local subenv = Scope(env)

    local function toparameter(env, value)
        assert_scope(env)
        local _value, anchor = unsyntax(value)
        if _value.type == Type.Parameter then
            return _value.value
        else
            local param = Parameter(value, Type.Any)
            env:bind(value, Any(param))
            return param
        end
    end

    local params = unwrap(Type.List, expr_parameters)
    while (params ~= EOL) do
        func:append_parameter(toparameter(subenv, params.at))
        params = params.next
    end
    if (#func.parameters == 0) then
        set_active_anchor(params_anchor)
        location_error("explicit continuation parameter missing")
    end

    return expand_expr_list(subenv, it, function(result)
        result = List(Any(Syntax(Any(func), anchor, true)), result)
        result = List(Any(Syntax(globals:lookup(Symbol.FnCCForm), anchor, true)), result)
        return cont(List(Any(Syntax(Any(result), anchor, true)), topit.next), env)
    end)
end

expand_syntax_extend = function(env, topit, cont)
    assert_scope(env)
    assert_list(topit)
    verify_at_parameter_count(topit, 1, -1)

    local it = topit.at

    local nit,anchor = unsyntax(it)
    it = unwrap(Type.List, nit)

    local _,anchor_kw = unsyntax(it.at)
    it = it.next

    local func_name = Any(Syntax(Any(Symbol.Unnamed), anchor))
    local func = Label.create_empty_function(func_name)
    func:append_parameter(Parameter(func_name, Type.Any))

    local subenv = Scope(env)
    subenv:bind(Any(Symbol.SyntaxScope), Any(env))

    return expand_expr_list(subenv, it, function(expr)
        expr = List(Any(Syntax(Any(func), anchor, true)), expr)
        expr = List(Any(Syntax(globals:lookup(Symbol.FnCCForm), anchor, true)), expr)
        expr = Any(Syntax(Any(expr), anchor, true))
        return translate(null, expr,
            function(_state, _anchor, enter, fun)
                assert(not enter)
                fun = maybe_unsyntax(fun)
                return execute(fun,
                    function(expr_env)
                        if expr_env == null or expr_env.type ~= Type.Scope then
                            set_active_anchor(anchor)
                            location_error("syntax-extend did not evaluate to scope")
                        end
                        return cont(topit.next, unwrap(Type.Scope, expr_env))
                    end)
            end)
    end)
end

local function expand_wildcard(label, env, handler, topit, cont)
    assert_string(label)
    assert_scope(env)
    assert_any(handler)
    assert_list(topit)
    assert(cont)
    return xpcallcc(function(cont)
        return execute(handler, function(result)
            if result == null or is_none(result) then
                return cont(EOL)
            end
            if result.type ~= Type.List then
                location_error(label
                    .. " macro returned unexpected value of type "
                    .. tostring(result.type))
            end
            return cont(unwrap(Type.List, result))
        end, Any(topit), Any(env))
    end,
    function (exc, cont)
        exc = exception(exc)
        local w = string_writer()
        local _,anchor = unsyntax(topit.at)
        anchor:stream_message_with_source(w,
            'while expanding ' .. label .. ' macro')
        local fmt = StreamValueFormat()
        fmt.naked = true
        fmt.maxdepth = 3
        fmt.maxlength = 5
        stream_expr(w, topit.at, fmt)
        exc.macros = w() .. (exc.macros or "")
        error(exc)
    end,
    cont)
end

local function expand_macro(env, handler, topit, cont)
    assert_scope(env)
    assert_any(handler)
    assert_list(topit)
    assert(cont)
    return xpcallcc(function(cont)
        return execute(handler, function(result_list, result_scope)
            --print(handler, result_list, result_scope)
            if (is_none(result_list)) then
                return cont(EOL)
            end
            result_list = unwrap(Type.List, result_list)
            result_scope = result_scope and unwrap(Type.Scope, result_scope)
            if result_list ~= EOL and result_scope == null then
                location_error(tostring(handler) .. " did not return a scope")
            end
            if global_opts.validate_macros then
                -- validate result completely wrapped in syntax
                local todo = {result_list.at}
                local k = 1
                while k <= #todo do
                    local elem = todo[k]
                    if elem.type ~= Type.Syntax then
                        location_error("syntax objects missing in expanded macro")
                    end
                    if not elem.value.quoted then
                        elem = unsyntax(elem)
                        if elem.type == Type.List then
                            elem = elem.value
                            while elem ~= EOL do
                                table.insert(todo, elem.at)
                                elem = elem.next
                            end
                        end
                    end
                    k = k + 1
                    assert(k < global_opts.stack_limit, "possible circular reference encountered")
                end
            end
            return cont(result_list, result_scope)
        end, Any(topit), Any(env))
    end,
    function (exc, cont)
        exc = exception(exc)
        local w = string_writer()
        local _, anchor = unsyntax(topit.at)
        anchor:stream_message_with_source(w, 'while expanding macro')
        local fmt = StreamValueFormat()
        fmt.naked = true
        fmt.maxdepth = 3
        fmt.maxlength = 5
        stream_expr(w, topit.at, fmt)
        exc.macros = w() .. (exc.macros or "")
        error(exc)
    end,
    cont)
end

expand = function(env, topit, cont)
    assert_scope(env)
    assert_list(topit)
    local result = none
    assert(topit ~= EOL)

    local function process(env, topit)
        local expr = topit.at
        local sx = unwrap(Type.Syntax, expr)
        if sx.quoted then
            -- return as-is
            return cont(List(expr, topit.next), env)
        end
        local anchor
        expr,anchor = unsyntax(expr)
        set_active_anchor(anchor)
        if (expr.type == Type.List) then
            local list = expr.value
            if (list == EOL) then
                location_error("expression is empty")
            end

            local head = list.at
            local head_anchor
            head, head_anchor = unsyntax(head)

            -- resolve symbol
            if (head.type == Type.Symbol) then
                head = env:lookup(head.value) or none
            end

            local function expand_list()
                return expand_expr_list(env,
                    unwrap(Type.List, expr),
                    function (result)
                        return cont(List(Any(Syntax(Any(result), anchor, true)),
                            topit.next), env)
                    end)
            end

            local function expand_wildcard_list()
                local default_handler = env:lookup(Symbol.ListWildcard)
                if default_handler then
                    return expand_wildcard("wildcard list",
                        env, default_handler, topit,
                        function (result)
                            if result ~= EOL then
                                return process(env, result)
                            end
                            return expand_list()
                        end)
                end
                return expand_list()
            end

            if (is_macro_type(head.type)) then
                return expand_macro(env, unmacro(head), topit,
                    function (result_list,result_env)
                        if (result_list ~= EOL) then
                            assert_scope(result_env)
                            assert(result_list ~= EOL)
                            return process(result_env, result_list)
                        elseif result_env then
                            return cont(EOL, env)
                        else
                            return expand_wildcard_list()
                        end
                    end)
            end

            return expand_wildcard_list()
        elseif expr.type == Type.Symbol then
            local value = expr.value
            local result = env:lookup(value)
            if result == null then
                local function missing_symbol_error()
                    set_active_anchor(anchor)
                    location_error(
                        format("no value bound to name '%s' in scope", value.name))
                end
                local default_handler = env:lookup(Symbol.SymbolWildcard)
                if default_handler then
                    return expand_wildcard("wildcard symbol",
                        env, default_handler, topit, function(result)
                        if result ~= EOL then
                            return process(env, result)
                        end
                        return missing_symbol_error()
                    end)
                else
                    return missing_symbol_error()
                end
            end
            if result.type == Type.List then
                -- quote lists
                result = List(Any(Syntax(result, anchor, true)), EOL)
                result = List(Any(Syntax(globals:lookup(Symbol.QuoteForm), anchor, true)), result)
                result = Any(result)
            end
            result = Any(Syntax(result, anchor, true))
            return cont(List(result, topit.next), env)
        else
            return cont(List(Any(Syntax(expr, anchor, true)), topit.next), env)
        end
    end
    return process(env, topit)
end

expand_root = function(expr, scope, cont)
    local anchor
    if expr.type == Type.Syntax then
        expr, anchor = unsyntax(expr)
    end
    expr = unwrap(Type.List, expr)
    return expand_expr_list(scope or globals, expr, function(result)
        result = Any(result)
        if anchor then
            result = Any(Syntax(result, anchor))
        end
        return cont(result)
    end)
end

end -- do

--------------------------------------------------------------------------------
-- IL TRANSLATOR
--------------------------------------------------------------------------------

local translate_root

do

-- arguments must include continuation
local function br(state, enter, arguments, anchor)
    assert_any(enter)
    assert_table(arguments)
    assert_anchor(anchor)
    for i=1,#arguments do
        local arg = maybe_unsyntax(arguments[i])
        assert_any(arg)
        arguments[i] = arg
    end
    assert(#arguments >= 1)
    if (state == null) then
        location_error("can not define body: continuation already exited.")
        --print("warning: can not define body: continuation already exited.")
        return
    end
    assert(not state.enter)
    assert(not state.arguments)
    enter = maybe_unsyntax(enter)
    state:set_enter(enter)
    state:set_arguments(arguments)
    state:set_body_anchor(anchor)
end

--------------------------------------------------------------------------------

local function is_return_callable(callable, args)
    local contarg = args[1]
    local argcount = #args - 1
    local is_return = false
    local is_forward_return = false
    local ncallable = maybe_unsyntax(callable)
    if contarg then
        contarg = maybe_unsyntax(contarg)
        if contarg.type == Type.Nothing then
            is_return = true
        end
    end
    if ncallable.type == Type.Parameter then
        local param = ncallable.value
        if param.index == 1 then
            -- return continuation is being called
            is_return = true
            if argcount == 1 then -- only one argument?
                -- can be forwarded
                is_forward_return = true
            end
        end
    end
    return is_return, is_forward_return
end

-- used 2 times
local function make_callable_dest(dest, cont)
    return function(state, anchor, enter, value)
        assert_anchor(anchor)
        if enter then
            local args = value
            local is_return = is_return_callable(enter, args)
            if not args[1] then
                -- todo: must not override existing cont arg
                if is_return then
                    args[1] = none
                else
                    -- return continuations are terminal
                    args[1] = dest
                end
            end
            br(state, enter, args, anchor)
            return cont()
        elseif value == null then
            br(state, dest, {none}, anchor)
            return cont()
        else
            local _,anchor = maybe_unsyntax(value)
            br(state, dest, {none, value}, anchor)
            return cont()
        end
    end
end

local function translate_expr_list(state, it, cont, anchor)
    assert_function(cont)
    assert_list(it)
    assert_anchor(anchor)
    if (it == EOL) then
        return cont(state, anchor)
    else
        local function loop(state, it)
            if it.next == EOL then -- last element goes to cont
                return translate(state, it.at, cont)
            else
                local sxvalue = it.at
                local value, anchor = unsyntax(sxvalue)
                return translate(state, sxvalue,
                    function(state, _anchor, enter, value)
                        assert_anchor(_anchor)
                        if enter then
                            local args = value
                            local is_return = is_return_callable(enter, args)
                            -- complex expression
                            -- continuation and results are ignored
                            local next = Label.create_continuation(
                                Any(Syntax(Any(Symbol.Unnamed), _anchor)))
                            if is_return then
                                set_active_anchor(anchor)
                                location_error("return call is not last expression")
                            else
                                args[1] = Any(next)
                            end
                            br(state, enter, args, _anchor)
                            state = next
                        end
                        return loop(state, it.next)
                    end)
            end
        end
        return loop(state, it)
    end
end

local function translate_quote(state, it, cont)
    assert_function(cont)
    local anchor
    it, anchor = unsyntax(it)
    it = unwrap(Type.List, it)
    it = it.next
    local sx = unwrap(Type.Syntax, it.at)
    sx.quoted = true
    return cont(state, anchor, nil, it.at)
end

-- (fn/cc <label without body> expr ...)
local function translate_fn_body(state, it, cont, anchor)
    assert_function(cont)

    local anchor
    it, anchor = unsyntax(it)
    it = unwrap(Type.List, it)

    it = it.next
    local func = unwrap(Type.Label, unsyntax(it.at))
    it = it.next

    local dest = Any(func.parameters[1])

    return translate_expr_list(func, it,
    function(_state, _anchor, enter, value)
        assert_anchor(_anchor)
        if enter then
            local args = value
            local is_return = is_return_callable(enter, args)
            local next
            if not args[1] then
                if is_return then
                    args[1] = none
                else
                    args[1] = dest
                end
            end
            br(_state, enter, args, _anchor)
        elseif value == null then
            br(_state, dest, {none}, _anchor)
        else
            local _,_anchor = maybe_unsyntax(value)
            br(_state, dest, {none, value}, _anchor)
        end
        assert(#func.arguments > 0)
        return cont(state, anchor, nil, Any(Syntax(Any(func), anchor)))
    end, anchor)
end

local function translate_argument_list(state, it, cont, anchor, explicit_ret)
    local args = {}
    local enter
    if not explicit_ret then
        table.insert(args, false)
    end
    local function loop(state, it)
        if (it == EOL) then
            return cont(state, anchor, enter, args)
        else
            local sxvalue = it.at
            local value = maybe_unsyntax(sxvalue)
            -- complex expression
            return translate(state, sxvalue,
                function(state, anchor, _enter, value)
                    assert_anchor(anchor)
                    local arg
                    if _enter then
                        local _args = value
                        local is_return = is_return_callable(_enter, _args)
                        if is_return then
                            set_active_anchor(anchor)
                            location_error("unexpected return in argument list")
                        end

                        local sxdest = Any(Syntax(Any(Symbol.Unnamed), anchor))
                        local next = Label.create_continuation(sxdest)
                        local param = Parameter(sxdest, Type.Any)
                        param.vararg = true
                        next:append_parameter(param)
                        _args[1] = Any(next)
                        br(state, _enter, _args, anchor)
                        state = next
                        arg = Any(next.parameters[PARAM_Arg0])
                    else
                        -- a known value is returned - no need to generate code
                        arg = value
                    end
                    if not enter then
                        enter = arg
                    else
                        table.insert(args, arg)
                    end
                    return loop(state, it.next)
                end)
        end
    end
    return loop(state, it)
end

local function translate_implicit_call(state, it, cont, anchor)
    assert_list(it)
    assert_function(cont)

    local count = it.count
    if count < 1 then
        location_error("callable expected")
    end
    return translate_argument_list(state, it, cont, anchor, false)
end

local function translate_call(state, it, cont, anchor)
    assert_function(cont)

    local anchor
    it, anchor = unsyntax(it)
    it = unwrap(Type.List, it)

    it = it.next
    return translate_implicit_call(state, it, cont, anchor)
end

local function translate_contcall(state, it, cont, anchor)
    local anchor
    it, anchor = unsyntax(it)
    it = unwrap(Type.List, it)

    it = it.next
    local count = it.count
    if count < 1 then
        location_error("callable expected")
    elseif count < 2 then
        location_error("continuation expected")
    end
    return translate_argument_list(state, it, cont, anchor, true)
end

--------------------------------------------------------------------------------

translate = function(state, sxexpr, destcont)
    assert_function(destcont)
    assert_any(sxexpr)
    return xpcallcc(function(cont)
        local sx = unwrap(Type.Syntax, sxexpr)
        local expr, anchor = unsyntax(sxexpr)

        set_active_anchor(anchor)

        if (expr.type == Type.List) then
            local slist = expr.value
            if (slist == EOL) then
                location_error("empty expression")
            end
            local head = unsyntax(slist.at)
            if (head.type == Type.Form) then
                return head.value(state, sxexpr, cont)
            else
                return translate_implicit_call(state, slist, cont, anchor)
            end
        else
            return cont(state, anchor, nil, sxexpr)
        end
    end,
    function (exc, cont)
        exc = exception(exc)
        if not exc.translate then
            local w = string_writer()
            local _, anchor = unsyntax(sxexpr)
            anchor:stream_message_with_source(w, 'while translating expression')
            local fmt = StreamValueFormat()
            fmt.naked = true
            stream_expr(w, sxexpr, fmt)
            exc.translate = w()
        end
        error(exc)
    end,
    function (...)
        return destcont(...)
    end)
end

--------------------------------------------------------------------------------

-- path must be resident
translate_root = function(expr, name, cont)
    assert_string(name)

    local anchor
    expr, anchor = unsyntax(expr)
    expr = unwrap(Type.List, expr)

    local mainfunc = Label.create_function(Any(Syntax(Any(Symbol(name)), anchor)))
    local ret = mainfunc.parameters[PARAM_Cont]

    return translate_expr_list(mainfunc, expr,
        make_callable_dest(Any(ret), function()
            return cont(Any(mainfunc))
        end),
        anchor)
end

-- special forms
--------------------------------------------------------------------------------

builtins.call = Form(translate_call, Symbol("call"))
builtins["cc/call"] = Form(translate_contcall, Symbol("cc/call"))
builtins[Symbol.FnCCForm] = Form(translate_fn_body, Symbol("fn-body"))
builtins[Symbol.QuoteForm] = Form(translate_quote, Symbol("quote"))

end -- do

function Anchor.extract(value)
    if value.type == Type.Syntax then
        return value.value.anchor
    elseif value.type == Type.Parameter then
        local anchor = value.value.anchor
        if anchor then
            return anchor
        end
    elseif value.type == Type.List then
        if value.value ~= EOL then
            local head = value.value.at
            -- try to extract head
            return Anchor.extract(head)
        end
    end
end

--------------------------------------------------------------------------------
-- SPECIALIZER
--------------------------------------------------------------------------------

local specialize
do

local function build_scc(top)
    local S = {}
    local P = {}
    local C = 0
    local Cmap = {}
    local SCCmap = {}
    local function walk(obj)
        Cmap[obj] = C
        C = C + 1
        table.insert(S, obj)
        table.insert(P, obj)
        local function walk_arg(arg)
            arg = maybe_unsyntax(arg)
            if arg.type == Type.Label then
                arg = arg.value
                local Cw = Cmap[arg]
                if Cw == nil then
                    walk(arg)
                elseif not SCCmap[arg] then
                    assert(#P >= 1)
                    while Cmap[P[#P]] > Cw do
                        table.remove(P)
                        assert(#P >= 1)
                    end
                end
            end
        end
        walk_arg(obj.enter)
        for i,arg in ipairs(obj.arguments) do
            walk_arg(arg)
        end
        assert(#P >= 1)
        if P[#P] == obj then
            assert(#S >= 1)
            local scc = {}
            while true do
                local q = S[#S]
                table.insert(scc, q)
                SCCmap[q] = scc
                print(q)
                table.remove(S)
                if q == obj then
                    break
                end
            end
            print()
            table.remove(P)
        end
    end
    walk(top)
    return SCCmap
end

--[[
REMOVING UNUSED PARAMETERS AND ARGUMENTS
----------------------------------------

- a parameter that has no users is unused
- unused parameters can be typed to Nothing, or removed if at tail position
- conclusively, any argument passed to a non-existing or Nothing-typed parameter
  can be set to none or removed if at tail position
- if the argument was a parameter reference in itself, then that parameter
  may now be unused too - repeat.
--]]
local function remove_unused_parameters_and_arguments(func)
    local pg
    local users

    local function update_users()
        pg = program(func)
        users = build_users(pg)
    end

    local function remove_unused_args(label)
        local label_users = users[label]
        if not label_users then
            return
        end
        local params = label.parameters
        local pcount = #params
        -- todo: we made some changes - truncate or set-none arguments of callers
        for user,_ in pairs(label_users) do
            --local cont = maybe_unsyntax(user.arguments[1])
            local callee = maybe_unsyntax(user.enter)
            if callee == label then
                local args = user.arguments
                local acount = #args
                for i=acount,1,-1 do
                    local arg = args[i]
                    local narg,anchor = maybe_unsyntax(arg)
                    local mparam = params[i]
                    if not mparam or mparam.type == Type.Nothing then
                        args[i] = none
                    end
                end
            end
        end

    end

    local function remove_unused_params(label)
        local params = label.parameters
        local pcount = #params
        local changed = false
        for i=pcount,1,-1 do
            local param = params[i]
            if not users[param] then
                if i == #params then
                    changed = true
                    table.remove(params)
                else
                    param.type = Type.Nothing
                end
            end
        end
        return changed
    end

    local loops = 0
    while true do
        loops = loops + 1
        local changed = false
        update_users()
        for label,_ in pairs(pg) do
            remove_unused_args(label)
        end
        update_users()
        for label,_ in pairs(pg) do
            if remove_unused_params(label) then
                changed = true
            end
        end
        if not changed then
            break
        end
    end
    print("loops:",loops)

end

local function intersect(a, b)
    local result = {}
    for k,_ in pairs(a) do
        if b[k] then
            result[k] = true
        end
    end
    return result
end

--[[
things we must solve in the specializer:

* ensure there are no vararg parameters anymore
* ensure all parameters are typed, and continuation parameters know their argument types
* ensure no arguments or parameters of type Type exist in the code
* ensure all nodes are in CFF form, that is, we only have basic-block like
  or returning top level functions, and there are no free variables.

TODO: extension to scope: parent scope also covers all subscopes
    that is, if a label is live in indirect live label, it is also live in parent

general process:
begin by mangling entry label (e.g. specialize return continuation to call exit(0))

after mangling an entry point:
    NOTE: mangling an entry point with exactly the same arguments yields the
          same mangled label.
          make sure the entry point is cached at this point.

    obtain scope of label (a scope is a complete function)
    - get rid of all unused parameters in scope
    - fold all constant expressions in scope!
        - generally, it's useful to compact the content as far as possible
          before mangling additional functions, so that optimal forms are copied
    - walk scope of label in order, starting at entry point
    if body is constant expression:
        - fold
        if constant expression can't be folded:
            - mangle continuation for specialized return type
    if body references label (any position, except continuation?):
        - mangle!
            - always inline arguments
        special case recursive functions?

]]
local function dump_program(func)
    local pg = program(func)
    local users = build_users(pg)
    local scopes = build_scopes(pg, users)
    local opts = {}
    --opts.users = users
    opts.scopes = scopes
    stream_il(stdout_writer, Any(func), opts)
end

local function dump_label(func)
    local opts = {}
    opts.follow_labels = false
    stream_il(stdout_writer, Any(func), opts)
end

specialize = function(func, args, cont)
    local opts = {}

    func = unwrap(Type.Label, func)

    dump_program(func)
    print()

    local function verify_argtype(ptype, value)
        if ptype ~= Type.Any then
            local nvalue = maybe_unsyntax(value)
            if nvalue.type == Type.Parameter then
                if nvalue.value.type ~= ptype then
                    location_error("type "
                        .. tostring(ptype)
                        .. " expected, got "
                        .. tostring(nvalue.value.type))
                end
            else
                unwrap(ptype, nvalue)
            end
        end
    end

    do
        local function is_label_type(_type)
            return _type == Type.Label or _type:super() == Type.Label
        end

        local function map_arguments(map, params, args)
            local pcount = #params
            local rcount = #args
            local srci = 1
            for i=1,pcount do
                local param = params[i]
                local arg
                if param.vararg then
                    if i == 1 then
                        location_error("continuation parameter can't be vararg")
                    end
                    if param.type ~= Type.Any then
                        location_error("vararg parameter can't be typed")
                    end
                    -- how many parameters after this one
                    local remparams = pcount - i
                    -- how many varargs to capture
                    local vargsize = max(0, rcount - srci - remparams + 1)
                    local argvalues = {}
                    for k=srci,(srci + vargsize - 1) do
                        assert(args[k])
                        table.insert(argvalues, args[k])
                    end
                    arg = Any(Type.VarArgs, argvalues)
                    srci = srci + vargsize
                elseif srci <= rcount then
                    local value = args[srci]
                    if is_label_type(param.type) and value.type == Type.Label then
                    else
                        verify_argtype(param.type, value)
                    end
                    arg = value
                    srci = srci + 1
                else
                    verify_argtype(param.type, none)
                    arg = none
                end
                map[param] = arg
            end

        end

        local function get_argtype(arg)
            arg = maybe_unsyntax(arg)
            if arg.type == Type.Parameter then
                return arg.value.type
            else
                return arg.type
            end
        end

        local function build_argtypes(body, start)
            local types = {}
            for i=start,#body do
                table.insert(types, get_argtype(body[i]))
            end
            return types
        end

        local refine_body

        local EOM = {}
        local function load_val(map, keys)
            for i=1,#keys-1 do
                local key = keys[i]
                map = map[key]
                if map == nil then
                    return
                end
            end
            return map[EOM]
        end
        local function store_val(map, keys, val)
            for i=1,#keys do
                local key = keys[i]
                local submap = map[key]
                if submap == nil then
                    submap = {}
                    map[key] = submap
                end
                map = submap
            end
            map[EOM] = val
        end

        local drop_type_map = {}
        local function drop_type(func, argtypes)
            print("drop_type", func, unpack(argtypes))
            assert_label(func)
            local keys = { func, unpack(argtypes) }
            local rfunc = load_val(drop_type_map, keys)
            if rfunc then
                return rfunc
            end

            local params = func.parameters
            local same_signature = true
            if #params ~= #argtypes then
                same_signature = false
            else
                for i,argtype in ipairs(argtypes) do
                    local param = params[i]
                    if param.type ~= argtype then
                        same_signature = false
                        break
                    end
                end
            end

            if same_signature then
                rfunc = func
            else
                local map = {}
                local args = {}
                local anchor = func.body_anchor or func.anchor
                local sxname = Any(Syntax(Any(Symbol.Unnamed), anchor))
                local newparams = {}
                for _,argtype in ipairs(argtypes) do
                    local param = Parameter(sxname, argtype)
                    table.insert(args, Any(param))
                    table.insert(newparams, param)
                end
                map_arguments(map, params, args)

                local pg = program(func)
                local users = build_users(pg)
                local scopes = build_scopes(pg, users)

                rfunc = mangle(func, newparams, map, scopes)
            end
            store_val(drop_type_map, keys, rfunc)
            return refine_body(rfunc)
        end

        local drop_map = {}
        local function drop(enter, args)
            print("drop", enter, unpack(args))
            local func = unwrap(Type.Label, enter)
            assert_label(func)
            local keys = { func }
            for _,arg in ipairs(args) do
                arg = maybe_unsyntax(arg)
                if arg.type == Type.Parameter then
                    if not arg.value.label then
                        arg = Parameter
                    end
                elseif arg.type == Type.Label then
                else
                    arg = tostring(arg)
                end
                table.insert(keys, arg)
            end
            local rfunc = load_val(drop_map, keys)
            print(unpack(keys))
            if rfunc then
                error("CACHED!")
                return rfunc
            end

            local pg = program(func)
            local users = build_users(pg)
            local scopes = build_scopes(pg, users)

            local anchor = func.body_anchor or func.anchor

            local map = {}
            local params = func.parameters
            map_arguments(map, params, args)
            local destparams = {}
            for i,arg in ipairs(args) do
                arg = maybe_unsyntax(arg)
                if arg.type == Type.Parameter and arg.value.label == nil then
                    table.insert(destparams, arg.value)
                elseif i == 1 then
                    local atype = get_argtype(arg)
                    if atype ~= Type.Nothing then
                        atype = Type.Any
                    end
                    table.insert(destparams,
                        Parameter(Any(Syntax(Any(Symbol.Unnamed),anchor)), atype))
                end
            end

            rfunc = mangle(func, destparams, map, scopes)

            store_val(drop_map, keys, rfunc)
            return refine_body(rfunc)
        end

        local function has_constants(body)
            for i=2,#body do
                local arg = body[i]
                local narg = maybe_unsyntax(arg)
                if narg.type == Type.Label then
                    return true
                elseif narg.type == Type.Parameter then
                else
                    return true
                end
            end
            return false
        end

        local function partially_untyped(label)
            assert_label(label)
            for _,param in ipairs(label.parameters) do
                if param.type == Type.Any then
                    return true
                end
            end
            return false
        end

        local function contains_label_argument(body)
            for i=2,#body do
                local arg = body[i]
                local narg = maybe_unsyntax(arg)
                if narg.type == Type.Label then
                    return true
                end
            end
            return false
        end

        local function is_simple_forward(body)
            return (#body <= 1) and is_null_or_none(maybe_unsyntax(body[1]))
        end

        local function can_be_partially_inlined(body)
            return has_constants(body)
        end

        local function has_typed_arguments(body)

            return has_constants(body)
        end

        local function apply_continuation_type(body, rettypes)
            local cont = maybe_unsyntax(body[1])
            if cont.type == Type.Label then
                cont = drop_type(cont.value, rettypes)
                body[1] = Any(cont)
            elseif cont.type == Type.Parameter then
                cont = cont.value
                local newtype = Type.TypedLabel(unpack(rettypes))
                if cont.type == Type.Any then
                    cont.type = newtype
                elseif cont.type == newtype then
                    -- all is fine
                else
                    error("can't retype continuation parameter "
                        .. tostring(cont)
                        .. " as "
                        .. tostring(newtype))
                end
            end
        end

        local function is_label_factory(func)
            assert_label(func)
            local p_cont = func.parameters[1]
            if p_cont.type:super() == Type.Label then
                for _,arg in ipairs(p_cont.type.types) do
                    if arg == Type.Label or arg:super() == Type.Label then
                        return true
                    end
                end
            end
            return false
        end

        local refined = {}
        refine_body = function(rfunc)
            if refined[rfunc] then
                return rfunc
            end
            refined[rfunc] = true
            local func = rfunc
            while true do
                refined[func] = true
                ----[[
                print("----")
                dump_program(func)
                print("----")
                --]]
                local body = func.arguments
                local f = maybe_unsyntax(func.enter)
                if f.type == Type.Label then
                    if is_simple_forward(body) then
                        f = f.value
                        func.enter = f.enter
                        func.arguments = f.arguments
                    elseif contains_label_argument(body) then
                        --error("droppin")
                        -- labels passed as argument must be dropped
                        local newf = drop(f, body)
                        func.enter = Any(newf)
                        func.arguments = { none }
                    else
                        local newf
                        -- drop types
                        local argtypes = build_argtypes(body, 1)
                        if argtypes[1] ~= Type.Nothing then
                            argtypes[1] = Type.Any
                        end
                        newf = drop_type(f.value, argtypes)
                        if is_label_factory(newf) then
                            -- label factories must be dropped
                            newf = drop(Any(newf), body)
                            func.enter = Any(newf)
                            func.arguments = { none }
                        end
                        local p_cont = newf.parameters[1]
                        func.enter = Any(newf)
                        if p_cont.type ~= Type.Nothing then
                            if p_cont.type:super() == Type.Label then
                                local rettypes = p_cont.type.types
                                assert(rettypes)
                                apply_continuation_type(func.arguments, rettypes)
                            else
                                error("parameter of label type expected, got "
                                    .. tostring(p_cont))
                            end
                        else
                            func.arguments[1] = none
                        end
                        break
                    end
                elseif f.type == Type.Builtin then
                    local ff = f.value
                    if not fold_constants(func) then
                        --print("could not fold",f,unpack(body))
                        --[[
                        for i=2,#body do
                            local arg = body[i]
                            arg = maybe_unsyntax(arg)
                            if arg.type == Type.Label then
                                print("branching to",arg,f,unpack(body))
                                refine_body(arg.value)
                            end
                        end
                        --]]
                        if f == globals:lookup(Symbol("branch")) then
                            -- always drop both branches
                            local label1 = maybe_unsyntax(body[3])
                            local label2 = maybe_unsyntax(body[4])
                            local args = {body[1]}
                            label1 = drop(label1, args)
                            label2 = drop(label2, args)
                            body[3] = Any(label1)
                            body[4] = Any(label2)
                        elseif ff.type_func then
                            local argtypes = build_argtypes(body, 2)
                            local rettypes = { Type.Nothing,
                                ff.type_func(unpack(argtypes)) }
                            apply_continuation_type(body, rettypes)
                        end
                        break
                    end
                elseif f.type == Type.Parameter then
                    local argtypes = build_argtypes(body, 1)
                    apply_continuation_type({f}, argtypes)
                    break
                else
                    print(f, unpack(body))
                    error("unable to handle")
                    break
                end
            end
            return rfunc
        end

        func = drop(Any(func), args)
    end

    --build_scc(func)
    --optimize(func)
    --remove_unused_parameters_and_arguments(func)

    print("after:")
    dump_program(func)

    func = Any(func)

    return cont(func)
end
end

--------------------------------------------------------------------------------
-- BUILTINS
--------------------------------------------------------------------------------

do -- reduce number of locals

local function checkargs(mincount, maxcount, ...)
    if ((mincount <= 0) and (maxcount == -1)) then
        return true
    end

    local count = 0
    for i=1,select('#', ...) do
        local arg = select(i, ...)
        if arg ~= null then
            assert_any(arg)
            count = count + 1
        else
            break
        end
    end

    if ((maxcount >= 0) and (count > maxcount)) then
        location_error(
            format("excess argument. At most %i arguments expected", maxcount))
    end
    if ((mincount >= 0) and (count < mincount)) then
        location_error(
            format("at least %i arguments expected", mincount))
    end
    return count
end

local function wrap_simple_builtin(f)
    return function(self, cont, ...)
        if is_none(cont) then
            location_error("missing return")
        end
        return retcall(cont, f(...))
    end
end

local function foldable_builtin(f, tf)
    local b = Builtin(f)
    b.foldable = true
    b.type_func = tf
    return b
end

local function typed_builtin(f, tf)
    local b = Builtin(f)
    b.type_func = tf
    return b
end

local function builtin_macro(value)
    return macro(Any(Builtin(wrap_expand_builtin(value))))
end

local function unwrap_integer(value)
    local super = value.type:super()
    if super == Type.Integer then
        return int64_t(value.value)
    else
        location_error("integer expected, not " .. repr(value))
    end
end

local any_cast

-- constants
--------------------------------------------------------------------------------

builtins["true"] = bool(true)
builtins["false"] = bool(false)
builtins["none"] = none

builtins["scope-list-wildcard-symbol"] = Symbol.ListWildcard
builtins["scope-symbol-wildcard-symbol"] = Symbol.SymbolWildcard
builtins["scope-this-fn/cc-symbol"] = Symbol.ThisFnCC

builtins["interpreter-dir"] = cstr(C.bangra_interpreter_dir)
builtins["interpreter-path"] = cstr(C.bangra_interpreter_path)
builtins["interpreter-timestamp"] = cstr(C.bangra_compile_time_date())

do
    local style = Scope()
    for k,v in pairs(Style) do
        style:bind(Any(Symbol(k)), Any(v))
    end
    builtins["Style"] = style
end

-- types
--------------------------------------------------------------------------------

define_types(function(enum_name, lang_name)
    builtins[lang_name] = Type[enum_name]
end)

builtins.size_t = Type.U64

builtins["debug-build?"] = Any(bool(global_opts.debug))

-- builtin macros
--------------------------------------------------------------------------------

builtins["fn/cc"] = builtin_macro(expand_fn_cc)
builtins["syntax-extend"] = builtin_macro(expand_syntax_extend)

-- flow control
--------------------------------------------------------------------------------

local b_true = bool(true)
local function branch(self, cont, cond, then_cont, else_cont)
    checkargs(3,3,cond,then_cont,else_cont)
    if unwrap(Type.Bool, cond) == b_true then
        return call(then_cont, cont)
    else
        return call(else_cont, cont)
    end
end
branch = Builtin(branch)
branch.fold_func = function(enter, body)
    local cond = body[2]
    if cond.type ~= Type.Parameter then
        cond = unwrap(Type.Bool, cond)
        if cond == b_true then
            return body[3], { body[1] }
        else
            return body[4], { body[1] }
        end
    end
    return enter, body
end
builtins.branch = branch

--[[
local function ordered_branch(self, cont, a, b,
    equal_cont, unordered_cont, less_cont, greater_cont)
    checkargs(6,6,a,b,equal_cont,unordered_cont,less_cont,greater_cont)
    local function compare_error()
        location_error("types "
            .. tostring(a.type)
            .. " and "
            .. tostring(b.type)
            .. " are incomparable")
    end
    local function unordered()
        local rcmp = b.type:lookup(Symbol.Compare)
        if rcmp then
            return call(rcmp, cont, b, a,
                equal_cont, unordered_cont, greater_cont, less_cont)
        else
            compare_error()
        end
    end
    local cmp = a.type:lookup(Symbol.Compare)
    if cmp then
        return call(cmp, cont, a, b,
            equal_cont, Any(Builtin(unordered, Symbol.RCompare)),
            less_cont, greater_cont)
    else
        return unordered()
    end
end
-- ordered-branch(a, b, equal, unordered [, less [, greater])
builtins['ordered-branch'] = ordered_branch
--]]

builtins.error = function(cont, self, msg)
    checkargs(1,1, msg)
    return location_error(unwrap(Type.String, msg))
end

builtins["syntax-error"] = function(cont, self, sxobj, msg)
    checkargs(2,2, sxobj, msg)
    local _,anchor = unsyntax(sxobj)
    set_active_anchor(anchor)
    return location_error(unwrap(Type.String, msg))
end

builtins.xpcall = function(self, cont, func, xfunc)
    checkargs(2,2, func, xfunc)
    return xpcallcc(function(cont)
            return call(func,
                Any(Builtin(function(_self, _cont, ...)
                    return cont(...)
                end, Symbol.XPCallReturn)))
        end,
        function(exc, cont)
            if getmetatable(exc) ~= Any then
                if type(exc) == "table" then
                    exc = Any(exc.msg)
                else
                    exc = Any(exc)
                end
            end
            return call(xfunc,
                Any(Builtin(function(_self, _cont, ...)
                    return cont(...)
                end, Symbol.XPCallReturn)), exc)
        end,
        function(...)
            return retcall(cont, ...)
        end)
end

builtins["set-exception-handler!"] = wrap_simple_builtin(function(handler)
    _xpcallcc_handler = handler
end)
builtins["get-exception-handler"] = wrap_simple_builtin(function(handler)
    return _xpcallcc_handler
end)

-- constructors
--------------------------------------------------------------------------------

builtins["alloc"] = wrap_simple_builtin(function(_type)
    checkargs(1,1, _type)
    _type = unwrap(Type.Type, _type)
    local size = _type:lookup(Symbol.Size)
    if size then
        size = tonumber(unwrap(Type.SizeT, size))
    else
        size = 0
    end
    local buf = new(typeof('$[$]', uint8_t, size))
    return Any(_type, buf)
end)

builtins["list-load"] = wrap_simple_builtin(function(path)
    checkargs(1,1, path)
    path = unwrap(Type.String, path)
    local src = SourceFile.open(path)
    local ptr = src:strptr()
    local lexer = Lexer.init(ptr, ptr + src.length, path)
    return parse(lexer)
end)

builtins["list-parse"] = wrap_simple_builtin(function(s, path)
    checkargs(1,2, s, path)
    path = path and unwrap(Type.String, path) or "<string>"
    s = unwrap(Type.String, s)
    local ptr = rawstring(s)
    local lexer = Lexer.init(ptr, ptr + #s, path)
    return parse(lexer)
end)

builtins.expand = function(self, cont, expr, scope)
    checkargs(2,2, expr, scope)
    local _scope = unwrap(Type.Scope, scope)
    return expand_root(expr, _scope, function(expexpr)
        if expexpr then
            return retcall(cont, expexpr, scope)
        else
            error(expexpr)
        end
    end)
end

builtins.eval = function(self, cont, expr, scope, path)
    checkargs(1,3, expr, scope, path)
    if scope then
        scope = unwrap(Type.Scope, scope)
    else
        scope = globals
    end
    if path then
        path = unwrap(Type.String, path)
    else
        path = "<eval>"
    end
    return expand_root(expr, scope, function(expexpr)
        return translate_root(expexpr, path, function(result)
                return retcall(cont, result)
            end)
    end)
end

-- pass arguments to specialize; when the argument is an unbound parameter,
-- then this is the new parameter the function will be linked to.
builtins.mangle = function(self, cont, func, ...)
    checkargs(1,-1, func, ...)
    return specialize(func, { ... }, function(func)
        return retcall(cont, func)
    end)
end

builtins["syntax-quote"] = wrap_simple_builtin(function(value)
    checkargs(1,1, value)
    value = unwrap(Type.Syntax, value)
    return Any(Syntax(value.datum, value.anchor, true))
end)

builtins["syntax-unquote"] = wrap_simple_builtin(function(value)
    checkargs(1,1, value)
    value = unwrap(Type.Syntax, value)
    return Any(Syntax(value.datum, value.anchor, false))
end)

builtins["syntax-quoted?"] = wrap_simple_builtin(function(value)
    checkargs(1,1, value)
    value = unwrap(Type.Syntax, value)
    return Any(bool(value.quoted))
end)

builtins["block-scope-macro"] = wrap_simple_builtin(function(func)
    checkargs(1,1, func)
    unwrap(Type.Label, func)
    return macro(func)
end)

builtins.cons = wrap_simple_builtin(function(at, next)
    checkargs(2,2, at, next)
    next = unwrap(Type.List, next)
    return Any(List(at, next))
end)

builtins["syntax-cons"] = wrap_simple_builtin(function(at, next)
    checkargs(2,2, at, next)
    unwrap(Type.Syntax, at)
    local next, next_anchor = unsyntax(next)
    next = unwrap(Type.List, next)
    return Any(Syntax(Any(List(at, next)), next_anchor))
end)

builtins["syntax->datum"] = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    return (maybe_unsyntax(value))
end)

builtins["syntax->anchor"] = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    local _,anchor = unsyntax(value)
    return Any(anchor)
end)

builtins["active-anchor"] = wrap_simple_builtin(function()
    return Any(get_active_anchor())
end)

builtins["datum->syntax"] = wrap_simple_builtin(function(value, anchor)
    checkargs(1,2,value,anchor)

    if anchor == null then
        anchor = Anchor.extract(value)
        if anchor == null then
            location_error("argument of type "
                .. repr(value.type)
                .. " does not embed anchor, but no anchor was provided")
        end
    elseif anchor.type == Type.Syntax then
        anchor = anchor.value.anchor
    else
        anchor = unwrap(Type.Anchor, anchor)
    end
    return Any(Syntax(value,anchor))
end)

builtins["ref-new"] = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    return Any(Type.Ref, { value })
end)

builtins["string-new"] = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    value = maybe_unsyntax(value)
    local ty = value.type
    -- todo: types should do that conversion
    if ty == Type.String then
        return value
    elseif ty == Type.Symbol then
        return Any(value.value.name)
    else
        return Any(value.type:format_value(value.value, plain_styler))
    end
end)

builtins["symbol-new"] = wrap_simple_builtin(function(name)
    checkargs(1,1,name)
    return Any(Symbol(unwrap(Type.String, name)))
end)

builtins["list-new"] = wrap_simple_builtin(function(...)
    checkargs(0,-1,...)
    return Any(List.from_args(...))
end)

builtins["type-new"] = wrap_simple_builtin(function(name)
    checkargs(1,1,name)
    return Any(Type(unwrap(Type.Symbol, name)))
end)

builtins["label-new"] = wrap_simple_builtin(function(name)
    checkargs(1,1,name)
    return Any(Label(name))
end)

builtins["syntax-list"] = wrap_simple_builtin(function(...)
    checkargs(0,-1,...)
    local vacount = select('#', ...)
    for i=1,vacount do
        unwrap(Type.Syntax, select(i, ...))
    end
    local anchor
    if vacount > 0 then
        local _
        _,anchor = maybe_unsyntax(select(1, ...))
    else
        anchor = get_active_anchor()
    end
    return Any(Syntax(Any(List.from_args(...)), anchor))
end)

builtins["parameter-new"] = wrap_simple_builtin(function(name,_type)
    checkargs(1,2,name,_type)
    if _type then
        _type = unwrap(Type.Type, _type)
    end
    return Any(Parameter(name, _type))
end)

builtins["scope-new"] = wrap_simple_builtin(function(parent)
    checkargs(0,1,parent)
    if parent then
        return Any(Scope(unwrap(Type.Scope, parent)))
    else
        return Any(Scope())
    end
end)

each_numerical_type(function(T)
    builtins[T.name:lower() .. "-new"] = wrap_simple_builtin(function(x)
        checkargs(1,1,x)
        local xs = x.type:super()
        if xs ~= Type.Integer and xs ~= Type.Real then
            error("Unable to apply type "
                .. tostring(T) .. " to value of type "
                .. tostring(x.type))
        end
        return Any(T.ctype(x.value))
    end)
end)

-- comparisons
--------------------------------------------------------------------------------

local any_true = Any(bool(true))
local any_false = Any(bool(false))

local function compare_func(T, ordered)
    local prefix = T.name
    builtins[prefix .. "=="] = foldable_builtin(wrap_simple_builtin(function(a, b)
        a = unwrap(T, a)
        b = unwrap(T, b)
        if (a == b) then
            return any_true
        else
            return any_false
        end
    end))
    builtins[prefix .. "!="] = foldable_builtin(wrap_simple_builtin(function(a, b)
        a = unwrap(T, a)
        b = unwrap(T, b)
        if (a ~= b) then
            return any_true
        else
            return any_false
        end
    end))
    if ordered then
        builtins[prefix .. "<"] = foldable_builtin(wrap_simple_builtin(function(a, b)
            a = unwrap(T, a)
            b = unwrap(T, b)
            if (a < b) then
                return any_true
            else
                return any_false
            end
        end))
        builtins[prefix .. "<="] = foldable_builtin(wrap_simple_builtin(function(a, b)
            a = unwrap(T, a)
            b = unwrap(T, b)
            if (a <= b) then
                return any_true
            else
                return any_false
            end
        end))
        builtins[prefix .. ">"] = foldable_builtin(wrap_simple_builtin(function(a, b)
            a = unwrap(T, a)
            b = unwrap(T, b)
            if (a > b) then
                return any_true
            else
                return any_false
            end
        end))
        builtins[prefix .. ">="] = foldable_builtin(wrap_simple_builtin(function(a, b)
            a = unwrap(T, a)
            b = unwrap(T, b)
            if (a >= b) then
                return any_true
            else
                return any_false
            end
        end))
    end
end

compare_func(Type.Bool)
compare_func(Type.Symbol)
compare_func(Type.Parameter)
compare_func(Type.Label)
compare_func(Type.Scope)
compare_func(Type.Type)

compare_func(Type.String, true)

builtins["type<"] = foldable_builtin(wrap_simple_builtin(function(a, b)
    a = unwrap(Type.Type, a)
    b = unwrap(Type.Type, b)
    while true do
        a = a:super()
        if not a then
            return any_false
        elseif a == b then
            return any_true
        end
    end
end))

-- TODO:
-- comparisons for none, list, type, syntax should be done in boot script

--[[
builtins["list-compare"] = function(self, cont, a, b,
    equal_cont, unordered_cont, less_cont, greater_cont)
    if a.type == Type.List and b.type == Type.List then
        local x = unwrap(Type.List, a)
        local y = unwrap(Type.List, b)
        local function loop()
            if (x == y) then
                return call(equal_cont, cont)
            elseif (x == EOL) then
                return call(less_cont, cont)
            elseif (y == EOL) then
                return call(greater_cont, cont)
            end
            return ordered_branch(none, cont, x.at, y.at,
                Any(Builtin(function()
                    x = x.next
                    y = y.next
                    return loop()
                end, Symbol.CompareListNext)),
                unordered_cont, less_cont, greater_cont)
        end
        return loop()
    else
        return call(unordered_cont, cont)
    end
end

builtins["type-compare"] = function(self, cont, a, b,
    equal_cont, unordered_cont, less_cont, greater_cont)
    if a.type == Type.Type and b.type == Type.Type then
        local x = unwrap(Type.Type, a)
        local y = unwrap(Type.Type, b)
        if x == y then
            return call(equal_cont, cont)
        else
            local xs = x:super()
            local ys = y:super()
            if xs == y then
                return call(less_cont, cont)
            elseif ys == x then
                return call(greater_cont, cont)
            end
        end
    end
    return call(unordered_cont, cont)
end

builtins["syntax-compare"] = function(self, cont, a, b,
    equal_cont, unordered_cont, less_cont, greater_cont)
    local x = maybe_unsyntax(a)
    local y = maybe_unsyntax(b)
    return ordered_branch(none, cont,
        x, y, equal_cont, unordered_cont, less_cont, greater_cont)
end
--]]

-- cast
--------------------------------------------------------------------------------

any_cast = function(self, cont, totype, value)
    checkargs(2,2, totype, value)
    local fromtype = Any(value.type)
    local func = value.type:lookup(Symbol.Cast)
    local function fallback_call(err)
        local desttype = unwrap(Type.Type, totype)
        local function errmsg()
            location_error("can not cast from type "
                .. tostring(value.type)
                .. " to "
                .. tostring(desttype))
        end
        func = desttype:lookup(Symbol.Cast)
        if func ~= null then
            return call(func,
                Any(Builtin(function(_dest, _cont, result)
                    if result == null then
                        errmsg()
                    else
                        return retcall(cont, result)
                    end
                end, Symbol.RCast)), fromtype, totype, value)
        else
            errmsg()
        end
    end
    if func ~= null then
        return call(func,
            Any(Builtin(function(_cont, _dest, result)
                if result == null then
                    return fallback_call()
                else
                    return retcall(cont, result)
                end
            end, Symbol.RCast)), fromtype, totype, value)
    end
    return fallback_call()
end
builtins.cast = any_cast

builtins.bitcast = wrap_simple_builtin(function(totype, value)
    checkargs(2,2, totype, value)
    local fromtype = value.type
    totype = unwrap(Type.Type, totype)
    local fromsize = fromtype:size()
    local tosize = totype:size()
    if fromsize ~= tosize then
        location_error("cannot bitcast: size mismatch ("
            .. tostring(fromsize)
            .. " != "
            .. tostring(tosize)
            .. ")")
    end
    return Any(totype, value.value)
end)

local default_casts = wrap_simple_builtin(function(fromtype, totype, value)
    fromtype = unwrap(Type.Type, fromtype)
    totype = unwrap(Type.Type, totype)
    local fromsuper = fromtype:lookup(Symbol.Super)
    local tosuper = totype:lookup(Symbol.Super)
    fromsuper = fromsuper and unwrap(Type.Type, fromsuper)
    tosuper = tosuper and unwrap(Type.Type, tosuper)
    -- extend integer types of same signed type, but no truncation
    if fromsuper == Type.Integer and tosuper == Type.Integer then
        local from_unsigned = unwrap(Type.Bool, fromtype:lookup(Symbol.Unsigned))
        local to_unsigned = unwrap(Type.Bool, totype:lookup(Symbol.Unsigned))
        if (from_unsigned == to_unsigned) then
            local from_size = unwrap(Type.SizeT, fromtype:lookup(Symbol.Size))
            local to_size = unwrap(Type.SizeT, totype:lookup(Symbol.Size))
            if from_size <= to_size then
                return Any(totype.ctype(value.value))
            end
        end
    end
end)

--[[
each_numerical_type(function(T)
    builtin_op(T, Symbol.Cast, default_casts)
end)
]]

-- join
--------------------------------------------------------------------------------

builtins["string-join"] = wrap_simple_builtin(function(a, b)
    checkargs(2,2,a,b)
    a = unwrap(Type.String, a)
    b = unwrap(Type.String, b)
    return Any(a .. b)
end)

builtins["list-join"] = wrap_simple_builtin(function(a, b)
    checkargs(2,2,a,b)
    local la = unwrap(Type.List, a)
    local lb = unwrap(Type.List, b)
    local l = lb
    while (la ~= EOL) do
        l = List(la.at, l)
        la = la.next
    end
    return Any(reverse_list_inplace(l, lb, lb))
end)

-- arithmetic
--------------------------------------------------------------------------------

local function opmaker(T, ctype)
    local atype = typeof('$[$]', ctype, 1)
    local rtype = typeof('$&', ctype)
    local function arithmetic_op1(sym, opname)
        local op = C["bangra_" .. T.name .. "_" .. opname]
        assert(op)

        local function f(x)
            checkargs(1,1,x)
            x = unwrap(T, x)
            local srcval = new(atype)
            op(srcval, x)
            return Any(cast(ctype, cast(rtype, srcval)))
        end
        f = Builtin(wrap_simple_builtin(f))
        f.foldable = true
        function f.type_func(x)
            return T
        end
        builtins[T.name .. sym.name] = f
    end
    local function arithmetic_op2(sym, opname)
        local op = C["bangra_" .. T.name .. "_" .. opname]
        assert(op)

        local function f(a,b)
            checkargs(2,2,a,b)
            a = unwrap(T, a)
            b = unwrap(T, b)
            local srcval = new(atype)
            op(srcval, a, b)
            return Any(cast(ctype, cast(rtype, srcval)))
        end

        f = Builtin(wrap_simple_builtin(f))
        f.foldable = true
        function f.type_func(a,b)
            return T
        end
        builtins[T.name .. sym.name] = f
    end
    local function bool_op2(sym, opname)
        local op = C["bangra_" .. T.name .. "_" .. opname]
        assert(op)

        local function f(a,b)
            checkargs(2,2,a,b)
            a = unwrap(T, a)
            b = unwrap(T, b)
            return Any(bool(op(a, b)))
        end

        f = Builtin(wrap_simple_builtin(f))
        f.foldable = true
        function f.type_func(a,b)
            return Type.Bool
        end
        builtins[T.name .. sym.name] = f
    end
    local function arithmetic_shiftop(sym, opname)
        local op = C["bangra_" .. T.name .. "_" .. opname]
        assert(op)

        local function f(a,b)
            checkargs(2,2,a,b)
            a = unwrap(T, a)
            b = unwrap(Type.I32, b)
            local srcval = new(atype)
            op(srcval, a, b)
            return Any(cast(ctype, cast(rtype, srcval)))
        end

        f = Builtin(wrap_simple_builtin(f))
        f.foldable = true
        function f.type_func(a,b)
            return T
        end
        builtins[T.name .. sym.name] = f
    end
    return {
        op1 = arithmetic_op1,
        op2 = arithmetic_op2,
        bop2 = bool_op2,
        shiftop = arithmetic_shiftop
    }
end

each_numerical_type(function(T, ctype)
    local make = opmaker(T, ctype)

    make.op2(Symbol.Add, "add")
    make.op2(Symbol.Sub, "sub")
    make.op2(Symbol.Mul, "mul")
    make.op2(Symbol.Div, "div")
    make.op2(Symbol.Mod, "mod")
    make.op2(Symbol.Pow, "pow")

    make.bop2(Symbol.Equal, "eq")
    make.bop2(Symbol.NotEqual, "ne")
    make.bop2(Symbol.Greater, "gt")
    make.bop2(Symbol.GreaterEqual, "ge")
    make.bop2(Symbol.Less, "lt")
    make.bop2(Symbol.LessEqual, "le")
end)

each_numerical_type(function(T, ctype)
    local make = opmaker(T, ctype)

    make.op2(Symbol.BitAnd, "band")
    make.op2(Symbol.BitOr, "bor")
    make.op2(Symbol.BitXor, "bxor")
    make.op1(Symbol.BitNot, "bnot")
    make.shiftop(Symbol.LShift, "shl")
    make.shiftop(Symbol.RShift, "shr")
end, {ints = true})

builtins["not"] = wrap_simple_builtin(function(value)
    checkargs(1,1, value)
    value = unwrap(Type.Bool, value)
    if tonumber(value) == 0 then
        return any_true
    else
        return any_false
    end
end)

-- interrogation
--------------------------------------------------------------------------------

builtins["type-index"] = wrap_simple_builtin(function(_type)
    checkargs(1,1, _type)
    return Any(size_t(unwrap(Type.Type, _type).index))
end)

builtins.typeof = foldable_builtin(wrap_simple_builtin(function(value)
    checkargs(1,1, value)
    return Any(value.type)
end))

builtins["element-type"] = wrap_simple_builtin(function(_type)
    checkargs(1,1, _type)
    _type = unwrap(Type.Type, _type)
    return Any(_type:element_type())
end)

local function countof_func(T)
    builtins[T.name:lower() .. "-countof"] = function(self, cont, value)
        value = unwrap(T, value)
        return retcall(cont, Any(size_t(#value)))
    end
end

countof_func(Type.String)
countof_func(Type.List)

builtins["scope-at"] = wrap_simple_builtin(function(x, name)
    checkargs(2,2,x,name)
    x = unwrap(Type.Scope, x)
    name = unwrap(Type.Symbol, maybe_unsyntax(name))
    return x:lookup(name)
end)
builtins["type-at"] = wrap_simple_builtin(function(x, name)
    checkargs(2,2,x,name)
    x = unwrap(Type.Type, x)
    name = unwrap(Type.Symbol, maybe_unsyntax(name))
    return x:lookup(name)
end)
builtins["string-at"] = wrap_simple_builtin(function(x, i)
    checkargs(2,2,x,i)
    x = unwrap(Type.String, x)
    i = tonumber(unwrap_integer(i)) + 1
    return Any(x:sub(i,i))
end)
builtins["ref-at"] = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    value = unwrap(Type.Ref, value)
    return value[1]
end)

builtins["list@"] = wrap_simple_builtin(function(x, i)
    checkargs(2,2,x,i)
    x = unwrap(Type.List, x)
    i = unwrap_integer(i)
    for k=1,tonumber(i) do
        x = x.next
    end
    return x.at
end)

builtins["list-at"] = foldable_builtin(wrap_simple_builtin(function(x)
    checkargs(1,1,x)
    x = unwrap(Type.List, x)
    return x.at
end))

builtins["list-next"] = foldable_builtin(wrap_simple_builtin(function(x)
    checkargs(1,1,x)
    x = unwrap(Type.List, x)
    return Any(x.next)
end))

builtins["list-slice"] = wrap_simple_builtin(function(value, i0, i1)
    checkargs(3,3,value,i0,i1)
    local list = unwrap(Type.List, value)
    i0 = unwrap_integer(i0)
    i1 = unwrap_integer(i1)
    local i = int64_t(0)
    while (i < i0) do
        assert(list ~= EOL)
        list = list.next
        i = i + 1
    end
    local count = int64_t(0)
    if list ~= EOL then
        count = list.count
    end
    if (count ~= (i1 - i0)) then
        -- need to chop off tail, which requires creating a new list
        assert(list ~= EOL)
        local outlist = EOL
        while (i < i1) do
            assert(list ~= EOL)
            outlist = List(list.at, outlist)
            list = list.next
            i = i + 1
        end
        list = reverse_list_inplace(outlist)
    end
    return Any(list)
end)

builtins["string-slice"] = wrap_simple_builtin(function(value, i0, i1)
    checkargs(3,3,value,i0,i1)
    value = unwrap(Type.String, value)
    i0 = tonumber(unwrap_integer(i0)) + 1
    i1 = tonumber(unwrap_integer(i1))
    return Any(value:sub(i0, i1))
end)

builtins["get-scope-symbol"] = wrap_simple_builtin(function(scope, key, defvalue)
    checkargs(2, 3, scope, key, defvalue)

    scope = unwrap(Type.Scope, scope)
    key = unwrap(Type.Symbol, maybe_unsyntax(key))

    return scope:lookup(key) or defvalue or none
end)

builtins["next-scope-symbol"] = wrap_simple_builtin(function(scope, key)
    checkargs(1, 2, scope, key)
    scope = unwrap(Type.Scope, scope)
    if is_null_or_none(key) then
        key = null
    else
        key = unwrap(Type.Symbol, maybe_unsyntax(key))
    end
    local _,value = next(scope.symbols, key)
    if value == null then
        return
    else
        return value[1], value[2]
    end
end)

builtins["label-parameters"] = wrap_simple_builtin(function(label)
    checkargs(1,1, label)
    label = unwrap(Type.Label, label)
    local params = {}
    local plist = label.parameters
    for i=1,#plist do
        table.insert(params, Any(plist[i]))
    end
    return unpack(params)
end)

builtins["label-arguments"] = wrap_simple_builtin(function(label)
    checkargs(1,1, label)
    label = unwrap(Type.Label, label)
    return unpack(label.arguments)
end)

builtins["label-target"] = wrap_simple_builtin(function(label)
    checkargs(1,1, label)
    label = unwrap(Type.Label, label)
    return label.enter
end)

builtins["label-anchor"] = wrap_simple_builtin(function(label)
    checkargs(1,1, label)
    label = unwrap(Type.Label, label)
    if label.anchor then
        return Any(label.anchor)
    end
end)

builtins["label-body-anchor"] = wrap_simple_builtin(function(label)
    checkargs(1,1, label)
    label = unwrap(Type.Label, label)
    if label.body_anchor then
        return Any(label.body_anchor)
    end
end)

builtins["label-name"] = wrap_simple_builtin(function(label)
    checkargs(1,1, label)
    label = unwrap(Type.Label, label)
    return Any(label.name)
end)

builtins["label-uid"] = wrap_simple_builtin(function(label)
    checkargs(1,1, label)
    label = unwrap(Type.Label, label)
    return Any(size_t(label.uid))
end)

builtins["parameter-name"] = wrap_simple_builtin(function(param)
    checkargs(1,1, param)
    param = unwrap(Type.Parameter, param)
    return Any(param.name)
end)

builtins["parameter-anchor"] = wrap_simple_builtin(function(param)
    checkargs(1,1, param)
    param = unwrap(Type.Parameter, param)
    if param.anchor then
        return Any(param.anchor)
    end
end)

builtins["parameter-label"] = wrap_simple_builtin(function(param)
    checkargs(1,1, param)
    param = unwrap(Type.Parameter, param)
    return Any(param.label)
end)

builtins["parameter-index"] = wrap_simple_builtin(function(param)
    checkargs(1,1, param)
    param = unwrap(Type.Parameter, param)
    return Any(int32_t(param.index))
end)

builtins["parameter-type"] = wrap_simple_builtin(function(param)
    checkargs(1,1, param)
    param = unwrap(Type.Parameter, param)
    return Any(param.type)
end)

local valtoptr
do
    local supported = set({'int','float','struct'})
    local passthru = set({'array', 'ptr'})
    valtoptr = function(x)
        local xtype = reflect.typeof(x).what
        if passthru[xtype] then
            return x
        elseif not supported[xtype] then
            error("C type category " .. xtype .. " not supported")
        end
        local T1 = typeof('$[$]', ffi.typeof(x), 1)
        return(T1(x))
    end
end

local function ptrtoval(_type, ptr)
    local ctype = rawget(_type, 'ctype')
    if ctype then
        ptr = cast(typeof('$*', ctype), ptr)
        ptr = cast(typeof('$&', ctype), ptr)
        ptr = cast(ctype, ptr)
        return Any(_type, ptr)
    else
        local dstsz = _type:size()
        local buf = new(typeof('$[$]', uint8_t, tonumber(dstsz)))
        ffi.copy(buf, ptr, dstsz)
        return Any(_type, buf)
    end
end

-- we just keep all boxed values alive forever for now
local keepalive = {}
builtins["box"] = wrap_simple_builtin(function(value)
    checkargs(1,1, value)
    if value.type == Type.Boxed then
        value = value.value
        print(value)
        value = typeof('$*[$]',int8_t,1)(value)
        print(value)
    else
        value = value.value
        if type(value) ~= "cdata" then
            location_error("source is opaque")
        end
    end
    value = valtoptr(value)
    table.insert(keepalive, value)
    value = cast(typeof('$*',int8_t), value)
    return Any(Type.Boxed, value)
end)

builtins["unbox"] = wrap_simple_builtin(function(_type, value)
    checkargs(2,2, _type, value)
    _type = unwrap(Type.Type, _type)
    value = unwrap(Type.Boxed, value)
    return ptrtoval(_type, value)
end)

builtins["extract-memory"] = wrap_simple_builtin(function(_type, value, offset)
    checkargs(3,3, _type, value, offset)
    _type = unwrap(Type.Type, _type)
    offset = unwrap(Type.SizeT, offset)
    local dstsz = _type:size()
    local srcsz = value.type:size()
    value = value.value
    if type(value) ~= "cdata" then
        location_error("source is opaque")
    end
    value = valtoptr(value)
    if (offset + dstsz) > srcsz then
        location_error("source offset out of bounds")
    end
    local ctype = rawget(_type, 'ctype')
    return ptrtoval(_type, value + offset)
end)

-- data manipulation
--------------------------------------------------------------------------------

builtins["set-scope-symbol!"] = wrap_simple_builtin(function(dest, key, value)
    checkargs(3,3, dest, key, value)
    local atable = unwrap(Type.Scope, dest)
    atable:bind(key, value)
end)

builtins["del-scope-symbol!"] = wrap_simple_builtin(function(dest, key)
    checkargs(2,2, dest, key)
    local atable = unwrap(Type.Scope, dest)
    atable:del(key)
end)

builtins["set-type-symbol!"] = wrap_simple_builtin(function(dest, key, value)
    checkargs(3,3, dest, key, value)
    local atable = unwrap(Type.Type, dest)
    atable:bind(key, value)
end)

builtins["ref-set!"] = wrap_simple_builtin(function(ref, value)
    checkargs(2,2, ref, value)
    ref = unwrap(Type.Ref, ref)
    ref[1] = value
end)

builtins["label-append-parameter!"] = wrap_simple_builtin(function(label, param)
    checkargs(2,2, label, param)
    label = unwrap(Type.Label, label)
    param = unwrap(Type.Parameter, param)
    label:append_parameter(param)
end)

builtins["copy-memory!"] = wrap_simple_builtin(
    function(dst, dstofs, src, srcofs, srcsz)
        checkargs(5,5, dst, dstofs, src, srcofs, srcsz)
        dstofs = unwrap(Type.SizeT, dstofs)
        srcofs = unwrap(Type.SizeT, srcofs)
        srcsz = unwrap(Type.SizeT, srcsz)
        dst = dst.value
        src = src.value
        if type(dst) ~= "cdata" then
            location_error("destination is opaque")
        end
        if type(src) ~= "cdata" then
            location_error("source is opaque")
        end
        src = valtoptr(src)
        if (dstofs + srcsz) > ffi.sizeof(dst) then
            location_error("destination offset out of bounds")
        end
        if (srcofs + srcsz) > ffi.sizeof(src) then
            location_error("source offset out of bounds")
        end
        ffi.copy(dst + dstofs, src + srcofs, srcsz)
    end)

--copy-memory! self offset arg (size_t 0) sz

-- varargs
--------------------------------------------------------------------------------

builtins["va@"] = wrap_simple_builtin(function(index, ...)
    return select(tonumber(unwrap_integer(index)) + 1, ...)
end)

builtins["va-countof"] = wrap_simple_builtin(function(...)
    return Any(size_t(select('#', ...)))
end)

-- auxiliary utilities
--------------------------------------------------------------------------------

builtins.dump = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    local fmt = StreamValueFormat()
    fmt.naked = true
    fmt.anchors = "all"
    stream_expr(
        stdout_writer,
        value, fmt)
    return value
end)

builtins["dump-label"] = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    stream_il(stdout_writer, value)
end)

builtins.repr = wrap_simple_builtin(function(value, styler)
    checkargs(1,2,value, styler)
    if styler == null then
        return Any(value.type:format_value(value.value, plain_styler))
    else
        return Any(value.type:format_value(value.value, function(style, text)
            local result
            execute(styler, function(ret)
                result = unwrap(Type.String, ret)
            end, Any(style), Any(text))
            return result
        end))
    end
end)

builtins.print = typed_builtin(wrap_simple_builtin(function(...)
    local writer = stdout_writer
    for i=1,select('#', ...) do
        if i > 1 then
            writer(' ')
        end
        local arg = select(i, ...)
        if arg.type == Type.String then
            writer(arg.value)
        else
            writer(arg:repr(default_styler))
        end
    end
    writer('\n')
end), function(...)
    return
end)

builtins.prompt = wrap_simple_builtin(function(s, pre)
    checkargs(1,2,s,pre)
    s = unwrap(Type.String, s)
    if pre then
        pre = unwrap(Type.String, pre)
        C.linenoisePreloadBuffer(pre)
    end
    local r = C.linenoise(s)
    if r == NULL then
        return none
    end
    C.linenoiseHistoryAdd(r)
    return Any(cstr(r))
end)

builtins["globals"] = wrap_simple_builtin(function()
    return Any(globals)
end)

builtins["set-globals!"] = wrap_simple_builtin(function(value)
    checkargs(1,1,value)
    globals = unwrap(Type.Scope, value)
end)

builtins["interpreter-version"] = function(dest, cont)
    return retcall(cont,
        Any(int(global_opts.version_major)),
        Any(int(global_opts.version_minor)),
        Any(int(global_opts.version_patch)))
end

builtins['stack-level'] = wrap_simple_builtin(function()
    return Any(int32_t(debugger.stack_level()))
end)

builtins['clear-traceback'] = wrap_simple_builtin(function()
    debugger.clear_traceback()
end)

builtins.traceback = wrap_simple_builtin(function(limit, trunc)
    checkargs(0, 2, limit, trunc)
    local opts = {}
    if limit then
        opts.stack_start = tonumber(unwrap_integer(limit))
    end
    if trunc then
        opts.stack_end = tonumber(unwrap_integer(trunc))
    end
    local w = string_writer()
    debugger.stream_traceback(w, opts)
    return Any(w())
end)

builtins.args = wrap_simple_builtin(function()
    local result = {}
    local count = tonumber(C.bangra_argc)
    for i=0,count-1 do
        table.insert(result, Any(cstr(C.bangra_argv[i])))
    end
    return unpack(result)
end)

builtins.exit = function(cont, self, code)
    checkargs(0, 1, code)
    if code then
        code = tonumber(unwrap_integer(code))
    else
        code = 0
    end
    os.exit(code)
end

builtins['set-debug-trace!'] = wrap_simple_builtin(function(value)
    checkargs(1, 1, value)
    global_opts.trace_execution = unwrap(Type.Bool, value) == bool(true)
end)

builtins['default-styler'] = wrap_simple_builtin(function(style, text)
    checkargs(2, 2, style, text)

    return Any(default_styler(
        unwrap(Type.String, style),
        unwrap(Type.String, text)))
end)

--------------------------------------------------------------------------------
-- GLOBALS
--------------------------------------------------------------------------------

local function prepare_builtin_value(name, value, _type)
    local ty = type(value)
    if ty == "function" then
        value = Builtin(value)
    end
    if getmetatable(value) ~= Any then
        value = Any(value)
    end
    if type(name) == "string" then
        name = Symbol(name)
    end
    local displayname = name
    if _type then
        displayname = Symbol(_type.name .. "." .. name.name)
    end
    if ((value.type == Type.Builtin)
        or (value.type == Type.Form))
        and value.value.name == Symbol.Unnamed then
        value.value.name = displayname
    elseif is_macro_type(value.type)
        and value.value.name == Symbol.Unnamed then
        value.value.name = displayname
    end
    return Any(name), value
end

local function decl_builtin(name, value)
    globals:bind(prepare_builtin_value(name, value))
end

function Type.Nothing:format_value(x, styler)
    return styler(Style.Keyword, "none")
end
function Type.Symbol:format_value(x, styler)
    return styler(Style.Symbol,
        escape_string(x.name, SYMBOL_ESCAPE_CHARS))
end
function Type.String:format_value(x, styler)
    return styler(Style.String,
        '"' .. escape_string(x, "\"") .. '"')
end
function Type.Builtin:format_value(x)
    return tostring(x)
end

local function init_globals()
    Type.Builtin:bind(Any(Symbol.Super), Any(Type.Callable))
    Type.Label:bind(Any(Symbol.Super), Any(Type.Callable))

    do
        local ctype = typeof('void *')
        local refct = reflect.typeof(ctype)
        local _type = Type.Boxed
        _type.ctype = ctype
        _type:bind(Any(Symbol.Size), Any(size_t(refct.size)))
        _type:bind(Any(Symbol.Alignment), Any(size_t(refct.alignment)))
    end

    local function configure_int_type(_type, ctype, fmt)
        local refct = reflect.typeof(ctype)
        _type:bind(Any(Symbol.Size), Any(size_t(refct.size)))
        _type:bind(Any(Symbol.Alignment), Any(size_t(refct.alignment)))
        _type:bind(Any(Symbol.Bitwidth), Any(int(refct.size * 8)))
        _type:bind(Any(Symbol.Unsigned), Any(bool(refct.unsigned or false)))
        _type:bind(Any(Symbol.Super), Any(Type.Integer))
        _type.ctype = ctype
        if _type == Type.Bool then
            function _type:format_value(x, styler)
                if x == bool(true) then
                    return styler(Style.Keyword, "true")
                else
                    return styler(Style.Keyword, "false")
                end
            end
        else
            function _type:format_value(x, styler)
                return styler(Style.Number, cformat(fmt, x))
            end
        end
    end
    local function configure_real_type(_type, ctype)
        local refct = reflect.typeof(ctype)
        _type:bind(Any(Symbol.Size), Any(size_t(refct.size)))
        _type:bind(Any(Symbol.Alignment), Any(size_t(refct.alignment)))
        _type:bind(Any(Symbol.Bitwidth), Any(int(refct.size * 8)))
        _type:bind(Any(Symbol.Super), Any(Type.Real))
        _type.ctype = ctype
        function _type:format_value(x, styler)
            return styler(Style.Number, cformat("%g", x))
        end
    end
    configure_int_type(Type.Bool, bool)
    configure_int_type(Type.U8, uint8_t, "%u")
    configure_int_type(Type.U16, uint16_t, "%u")
    configure_int_type(Type.U32, uint32_t, "%u")
    configure_int_type(Type.U64, uint64_t, "%llu")
    configure_int_type(Type.I8, int8_t, "%d")
    configure_int_type(Type.I16, int16_t, "%d")
    configure_int_type(Type.I32, int32_t, "%d")
    configure_int_type(Type.I64, int64_t, "%lld")

    configure_real_type(Type.R32, float)
    configure_real_type(Type.R64, double)

    globals = Scope()
    for name,value in pairs(builtins) do
        decl_builtin(name, value)
    end
end

init_globals()
end -- do

--------------------------------------------------------------------------------
-- MAIN
--------------------------------------------------------------------------------

xpcallcc(
    function(cont)
        local basedir = cstr(C.bangra_interpreter_dir)
        local srcpath = basedir .. "/bangra.b"
        local src
        if global_opts.debug then
            src = SourceFile.open(srcpath)
        else
            src = SourceFile.open(srcpath, cstr(C.bangra_b, C.bangra_b_len))
        end
        local ptr = src:strptr()
        local lexer = Lexer.init(ptr, ptr + src.length, src.path)
        local expr = parse(lexer)

        --[[
        do
            local fmt = StreamValueFormat(true)
            fmt.anchors = "none"
            stream_expr(stdout_writer, expr, fmt)
        end
        --]]

        return expand_root(expr, null, function(expexpr)
            return translate_root(expexpr, "main", function(func)
                return call(func, globals:lookup(Symbol("exit")))
            end)
        end)
    end,
    function (err, cont)
        local is_complex_msg = type(err) == "table" and err.msg
        local w = string_writer()
        if is_complex_msg then
            if err.macros then
                w(err.macros)
            end
            if err.translate then
                w(err.translate)
            end
        end
        if global_opts.print_lua_traceback
            or not is_complex_msg
            or not err.interpreter_error then
            w(traceback("",3))
            w('\n\n')
        end
        debugger.stream_traceback(w)
        if is_complex_msg then
            if err.quoted then
                -- return as-is
                return err.msg
            end
            if err.anchor then
                err.anchor:stream_message_with_source(w, err.msg)
            else
                w(err.msg)
                w('\n')
            end
        else
            w(tostring(err))
        end
        print(w())
        return cont()
    end,
    function ()
        os.exit(1)
    end)
