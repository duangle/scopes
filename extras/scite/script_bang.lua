print("Bang Lexer 0.1")

-- see http://www.scintilla.org/ScriptLexer.html for more info

local keyword_str = props["keywords.bang"]
local KEYWORDS = {}
for s in string.gmatch(keyword_str, "%S+") do
  KEYWORDS[s] = true
end

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
    S_MATCH_OP_NO = 34
    S_MATCH_OP_YES = 35
    NUMERIC = "0123456789"
    ALPHA = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    IDENTIFIER = NUMERIC .. ALPHA .. "!$%&*/:<=>?^_~+-.@"

    if (styler.initStyle == S_WHITESPACE) then
        styler.initStyle = S_WHITESPACE
    end
    styler:StartStyling(styler.startPos, styler.lengthDoc, styler.initStyle)
    while styler:More() do
        if styler:State() == S_IDENTIFIER then
            if not IDENTIFIER:find(styler:Current(), 1, true) then
                identifier = styler:Token()
                if KEYWORDS[identifier] then
                    styler:ChangeState(S_KEYWORD)
                end
                styler:SetState(S_WHITESPACE)
            end
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
        end
        if styler:State() == S_WHITESPACE then
            if styler:Match("#") then
                styler:SetState(S_LINECOMMENT)
            elseif styler:Match("\"") then
                styler:SetState(S_STRING)
            elseif IDENTIFIER:find(styler:Current(), 1, true) then
                styler:SetState(S_IDENTIFIER)
            end
        end
        styler:Forward()
    end
    styler:EndStyling()
end


