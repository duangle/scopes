print("Bang Lexer 0.1")

-- see http://www.scintilla.org/ScriptLexer.html for more info
-- lua scripting intro is here http://www.scintilla.org/SciTELua.html
-- API is here http://www.scintilla.org/PaneAPI.html

function getprop(name)
    local prop = props[name]
    if (#prop == 0) then
        return
    end
    return prop
end

function splitstr(str)
    local result = {}
    for s in string.gmatch(str, "%S+") do
      result[s] = true
    end
    return result
end

keyword_str = getprop("keywords.bang") or
    "bang import-c dump-module function"
        .. " call int real defvalue deftype label phi br ret cond-br bitcast"
        .. " inttoptr ptrtoint getelementptr define declare type packed run module vector array struct"
        .. " do do-splice null global quote typeof dump extractelement extractvalue load store ..."

operator_str = getprop("operators.bang") or
    "+ - ++ -- * / % == != > >= < <= not and or = := @ ** ^ & | ~ ."

type_str = getprop("types.bang") or
    "i1 i8 i16 i32 i64 half float double"

KEYWORDS = splitstr(keyword_str)
OPERATORS = splitstr(operator_str)
TYPES = splitstr(type_str)

function OnStyle(styler)
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
    S_MATCH_OP_NO = 34
    S_MATCH_OP_YES = 35
    WHITESPACE = " \t\n"
    CONTROLCHARS = ".,:;"
    END_IDENTIFIER = "()[]{}\"';#:,.\t\n "
    BRACES = "([{}])"
    INT_MATCH = "^-?%d+$"
    FLOAT_MATCH1 = "^-?%d*[.]%d+$"
    FLOAT_MATCH2 = "^-?%d+[.]%d*$"

    styler:StartStyling(styler.startPos, styler.lengthDoc, styler.initStyle)

    while styler:More() do
        if styler:State() == S_IDENTIFIER then
            if styler:Match("\\") then
                styler:Forward()
            else
                if END_IDENTIFIER:find(styler:Current(), 1, true) then
                    identifier = styler:Token()
                    if identifier:match(INT_MATCH) then
                        if styler:Current() ~= "." then
                            styler:ChangeState(S_NUMBER)
                            styler:SetState(S_WHITESPACE)
                        else
                            -- keep going
                        end
                    else
                        if KEYWORDS[identifier] then
                            styler:ChangeState(S_KEYWORD)
                        elseif OPERATORS[identifier] then
                            styler:ChangeState(S_OPERATOR)
                        elseif TYPES[identifier] then
                            styler:ChangeState(S_TYPE)
                        elseif identifier:match(FLOAT_MATCH1)
                            or identifier:match(FLOAT_MATCH2) then
                            styler:ChangeState(S_NUMBER)
                        end
                        styler:SetState(S_WHITESPACE)
                    end
                end
            end
        elseif styler:State() == S_OPERATOR then
            styler:SetState(S_WHITESPACE)
        elseif styler:State() == S_BRACE then
            styler:SetState(S_WHITESPACE)
        elseif styler:State() == S_LINECOMMENT then
            if styler:Match("\n") then
                styler:SetState(S_WHITESPACE)
            end
        elseif styler:State() == S_STRING then
            if styler:Match("\\") then
                styler:Forward()
            elseif styler:Match("\"") then
                styler:ForwardSetState(S_WHITESPACE)
            end
        elseif styler:State() == S_SQ_STRING then
            if styler:Match("\\") then
                styler:Forward()
            elseif styler:Match("'") then
                styler:ForwardSetState(S_WHITESPACE)
            end
        end
        if styler:State() == S_WHITESPACE then
            if WHITESPACE:find(styler:Current(), 1, true) then
            elseif styler:Match("#") then
                styler:SetState(S_LINECOMMENT)
            elseif styler:Match("\"") then
                styler:SetState(S_STRING)
            elseif styler:Match("'") then
                styler:SetState(S_SQ_STRING)
            elseif BRACES:find(styler:Current(), 1, true) then
                styler:SetState(S_BRACE)
            elseif CONTROLCHARS:find(styler:Current(), 1, true) then
                styler:SetState(S_WHITESPACE)
            else
                styler:SetState(S_IDENTIFIER)
            end
        end
        styler:Forward()
    end
    styler:EndStyling()
end


