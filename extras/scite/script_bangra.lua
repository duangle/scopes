print("Bangra Lexer loaded.")

-- see http://www.scintilla.org/ScriptLexer.html for more info
-- lua scripting intro is here http://www.scintilla.org/SciTELua.html
-- API is here http://www.scintilla.org/PaneAPI.html

local function getprop(name)
    local prop = props[name]
    if (#prop == 0) then
        return
    end
    return prop
end

local function splitstr(str)
    local result = {}
    for s in string.gmatch(str, "%S+") do
      result[s] = true
    end
    return result
end

local function bangra_symbols()
    return {
    KEYWORDS = splitstr(getprop("keywords.bangra_lang") or
        "bangra let external cfunction branch function null true false"
            .. " print repr tupleof import-c quote eval structof"
            .. " typeof letrec with ::* ::@ syntax-macro syntax-scope cons"
            .. " string expand call escape do empty? dump-syntax"
            .. " syntax-extend dump list-join list-head? if else elseif loop"
            .. " repeat syntax-single-macro length foreach kindof"
            .. " table none slice null? assert qquote unquote unquote-splice"
            .. " list-atom? list-load load globals table-join require"
            .. " set-key! cstr pointer return continuation"
        ),

    OPERATORS = splitstr(getprop("operators.bangra_lang") or
        "+ - ++ -- * / % == != > >= < <= not and or = @ ** ^ & | ~ , . .. : += -="
            .. " *= /= %= ^= &= |= ~= <- ? :="
        ),


    TYPES = splitstr(getprop("types.bangra_lang") or
        "int int8 int16 int32 int64 uint8 uint16 uint32 uint64 void"
            .. " rawstring opaque half float double symbol list parameter"
            .. " frame closure flow"
        )
    }
end

local function unknown_symbols()
    return {
    KEYWORDS = {},

    OPERATORS = {},

    TYPES = {}
    }
end

local dsl_table = {}

REALCONST = splitstr("inf +inf -inf nan +nan -nan")

local symbol_terminators = "()[]{}\"';#"
local integer_terminators = "()[]{}\"';#"
local real_terminators = "()[]{}\"';#."

local token_eof = 0
local token_open = '('
local token_close = ')'
local token_square_open = '['
local token_square_close = ']'
local token_curly_open = '{'
local token_curly_close = '}'
local token_string = '"'
local token_sqstring = "'"
local token_symbol = 'S'
local token_escape = '\\'
local token_statement = ';'
local token_number = 'N'
local token_comment = '#'

local function strchr(str, c)
    return str:find(c, 1, true)
end

local function isspace(c)
    return strchr(" \t\n", c)
end

local chr = string.char
local function deref(ptr)
    local ch = editor.CharAt[ptr]
    if ch >= 0 and ch <= 255 then
        return chr(editor.CharAt[ptr])
    else
        return "?"
    end
end

local function Lexer()
    local start = 0
    local eof = 0
    local cursor = 0
    local next_cursor = 0
    local line = 0
    local next_line = 0

    local lineno = 0
    local next_lineno = 0

    local token = 0

    local lexer = {}
    function lexer.init (start_, eof_)
        start = start_
        eof = eof_

        next_cursor = start
        next_lineno = 1
        next_line = start
    end

    function lexer.range()
        return cursor, next_cursor - cursor
    end

    function lexer.string()
        local s = editor:GetText()
        return s:sub(cursor + 1,next_cursor)
    end

    local function readChar ()
        local c = deref(next_cursor)
        next_cursor = next_cursor + 1
        return c
    end

    local function readSymbol ()
        local escape = false
        while (next_cursor ~= eof) do
            local c = readChar()
            if (escape) then
                if (c == '\n') then
                    next_lineno = next_lineno + 1
                    next_line = next_cursor
                end
                -- ignore character
                escape = false
            elseif (c == '\\') then
                -- escape
                escape = true
            elseif isspace(c) or strchr(symbol_terminators, c) then
                next_cursor = next_cursor - 1
                break
            end
        end
    end

    local function readSingleSymbol ()
    end

    local function readString (terminator)
        local escape = false
        while (next_cursor ~= eof) do
            local c = readChar()
            if (c == '\n') then
                next_lineno = next_lineno + 1
                next_line = next_cursor
            end
            if (escape) then
                -- ignore character
                escape = false
            elseif (c == '\\') then
                -- escape
                escape = true
            elseif (c == terminator) then
                break
            end
        end
    end

    local function skipCharSet(p, chars)
        while (p < eof) do
            local c = deref(p)
            if not strchr(chars, c) then
                return p
            end
            p = p + 1
        end
        return p
    end

    local function skipNumber(p)
        local np
        if strchr("+-", deref(p)) then
            p = p + 1
        end
        local numset = "0123456789"
        if deref(p) == "0" and deref(p + 1) == "x" then
            -- 0x hexadecimal
            p = p + 2
            numset = "0123456789abcdefABCDEF"
        end
        local digits = 0
        np = skipCharSet(p, numset)
        digits = digits + (np - p)
        p = np
        if deref(p) == "." then
            p = p + 1
            np = skipCharSet(p, numset)
            digits = digits + (np - p)
            p = np
        end
        if (digits == 0) then return end
        if deref(p) == "e" then
            p = p + 1
            if strchr("+-", deref(p)) then
                p = p + 1
                np = skipCharSet(p, "0123456789")
                if (p == np) then return end
                digits = digits + (np - p)
                return np
            else
                return
            end
        end
        return p
    end

    local function readNumber()
        local p = skipNumber(cursor)
        if (not p) then return end
        local c = deref(p)
        if ((p == cursor)
            or (p >= eof)
            or ((not isspace(c)) and (not strchr(real_terminators, c)))) then
            return false
        end
        next_cursor = p
        return true
    end

    function lexer.readToken ()
        lineno = next_lineno
        line = next_line
        cursor = next_cursor
        while (true) do
            if (next_cursor == eof) then
                token = token_eof
                break
            end
            local c = readChar()
            if (c == '\n') then
                next_lineno = next_lineno + 1
                next_line = next_cursor
            end
            if (isspace(c)) then
                lineno = next_lineno
                line = next_line
                cursor = next_cursor
            elseif (c == '#') then
                token = token_comment
                readString('\n')
                break
            elseif (c == '(') then
                token = token_open
                break
            elseif (c == ')') then
                token = token_close
                break
            elseif (c == '[') then
                token = token_square_open
                break
            elseif (c == ']') then
                token = token_square_close
                break
            elseif (c == '{') then
                token = token_curly_open
                break
            elseif (c == '}') then
                token = token_curly_close
                break
            elseif (c == '\\') then
                token = token_escape
                break
            elseif (c == '"') then
                token = token_string
                readString(c)
                break
            elseif (c == '\'') then
                token = token_sqstring
                readString(c)
                break
            elseif (c == ';') then
                token = token_statement
                break
            elseif readNumber() then
                token = token_number
                --print("<" .. lexer.string() .. ">")
                break
            else
                token = token_symbol
                readSymbol()
                break
            end
        end
        return token
    end

    return lexer
end


local function makeset(...)
    local set = {}
    for i=1,select("#",...) do
        set[select(i,...)] = true
    end
    return set
end

local brace_tokens = makeset(token_open, token_close, token_square_open,
    token_square_close, token_curly_open, token_curly_close, token_escape,
    token_statement)

local function getHeader()
    local lexer = Lexer()
    local s = editor:GetText()
    lexer.init(0, #s, 0)

    while true do
        local token = lexer.readToken()
        if (token == token_eof) then return end
        if (token == token_symbol) then return lexer.string() end
        if (token ~= token_comment) then return end
    end
end

function OnStyle(styler)
    local header = getHeader()
    local symbols = (dsl_table[header] or bangra_symbols)()

    S_DEFAULT = 32
    S_WHITESPACE = 0
    S_LINECOMMENT = 1
    S_NUMBER = 2
    S_KEYWORD = 3
    S_STRING = 6
    S_BLOCKCOMMENT = 7
    S_UNCLOSEDLINE = 8
    S_IDENTIFIER = 9
    S_OPERATOR = 10
    S_SQ_STRING = 11
    S_BRACE = 12
    S_TYPE = 13
    S_DOTSEQ = 14
    S_MATCH_OP_NO = 34
    S_MATCH_OP_YES = 35

    local incomplete_styles = makeset(S_STRING, S_SQ_STRING, S_LINECOMMENT)

    local lexer = Lexer()
    local lineno = editor:LineFromPosition(styler.startPos);
    local lineStart = editor:PositionFromLine(lineno)
    -- if string, backtrack to first line with different style
    while (incomplete_styles[editor.StyleAt[lineStart]]
            or incomplete_styles[editor.StyleAt[lineStart+1]]) and lineno >= 0 do
        lineno = lineno - 1
        lineStart = editor:PositionFromLine(lineno)
    end
    local lineEnd = editor:PositionFromLine(
        editor:LineFromPosition(styler.startPos + styler.lengthDoc) + 1)

    lexer.init(lineStart, lineEnd, styler.initStyle)

    while true do
        local token = lexer.readToken()
        if (token == token_eof) then break end
        local offset,length = lexer.range()
        editor:StartStyling(offset, 0)
        if token == token_symbol then
            local sym = lexer.string()
            if (symbols.KEYWORDS[sym]) then
                editor:SetStyling(length, S_KEYWORD)
            elseif (symbols.OPERATORS[sym]) then
                editor:SetStyling(length, S_OPERATOR)
            elseif (symbols.TYPES[sym]) then
                editor:SetStyling(length, S_TYPE)
            elseif (REALCONST[sym]) then
                editor:SetStyling(length, S_NUMBER)
            else
                editor:SetStyling(length, S_IDENTIFIER)
            end
        elseif token == token_comment then
            editor:SetStyling(length, S_LINECOMMENT)
        elseif token == token_number then
            editor:SetStyling(length, S_NUMBER)
        elseif token == token_string then
            editor:SetStyling(length, S_STRING)
        elseif token == token_sqstring then
            editor:SetStyling(length, S_SQ_STRING)
        elseif brace_tokens[token] then
            editor:SetStyling(length, S_BRACE)
        end
        -- editor:SetStyling(lengthLine, style)
    end
end


