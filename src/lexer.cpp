#include "lidl/lexer.hpp"

#include <cctype>
#include <unordered_map>

namespace lidl {

namespace {

Token makeToken(Token::Type type, std::string text, int line, int col)
{
    Token t;
    t.type = type;
    t.text = std::move(text);
    t.line = line;
    t.column = col;
    return t;
}

const std::unordered_map<std::string, Token::Type>& keywords()
{
    static const std::unordered_map<std::string, Token::Type> kw = {
        {"module",      Token::Module},
        {"type",        Token::TypeKw},
        {"method",      Token::Method},
        {"event",       Token::Event},
        {"version",     Token::Version},
        {"description", Token::Description},
        {"category",    Token::Category},
        {"depends",     Token::Depends},
    };
    return kw;
}

// Identifiers are ASCII (module/method/type names); non-ASCII bytes pass
// through string literals untouched (UTF-8 transparent).
bool isIdentStart(unsigned char c) { return std::isalpha(c) || c == '_'; }
bool isIdentChar(unsigned char c) { return std::isalnum(c) || c == '_'; }

} // namespace

LexResult tokenize(const std::string& source)
{
    LexResult result;
    size_t pos = 0;
    int line = 1, col = 1;
    const size_t len = source.size();

    auto makeError = [&](const std::string& msg) {
        result.error = msg;
        result.errorLine = line;
        result.errorColumn = col;
    };

    while (pos < len) {
        const char c = source[pos];

        if (c == ' ' || c == '\t' || c == '\r') { ++pos; ++col; continue; }
        if (c == '\n') { ++pos; ++line; col = 1; continue; }
        if (c == ';') { while (pos < len && source[pos] != '\n') ++pos; continue; }

        const int startLine = line, startCol = col;

        if (c == '-' && pos + 1 < len && source[pos + 1] == '>') {
            result.tokens.push_back(makeToken(Token::Arrow, "->", startLine, startCol));
            pos += 2; col += 2; continue;
        }

        Token::Type symType = Token::Error;
        switch (c) {
        case '{': symType = Token::LBrace; break;
        case '}': symType = Token::RBrace; break;
        case '(': symType = Token::LParen; break;
        case ')': symType = Token::RParen; break;
        case '[': symType = Token::LBracket; break;
        case ']': symType = Token::RBracket; break;
        case ':': symType = Token::Colon; break;
        case ',': symType = Token::Comma; break;
        case '?': symType = Token::Question; break;
        default: break;
        }
        if (symType != Token::Error) {
            result.tokens.push_back(makeToken(symType, std::string(1, c), startLine, startCol));
            ++pos; ++col; continue;
        }

        if (c == '"') {
            ++pos; ++col;
            std::string value;
            while (pos < len && source[pos] != '"') {
                if (source[pos] == '\n') { makeError("Unterminated string literal"); return result; }
                if (source[pos] == '\\' && pos + 1 < len) {
                    ++pos; ++col;
                    const char esc = source[pos];
                    if (esc == 'n') value += '\n';
                    else if (esc == 't') value += '\t';
                    else if (esc == '\\') value += '\\';
                    else if (esc == '"') value += '"';
                    else value += esc;
                } else { value += source[pos]; }
                ++pos; ++col;
            }
            if (pos >= len) { makeError("Unterminated string literal"); return result; }
            ++pos; ++col;
            result.tokens.push_back(makeToken(Token::StringLit, value, startLine, startCol));
            continue;
        }

        if (isIdentStart(static_cast<unsigned char>(c))) {
            const size_t start = pos;
            while (pos < len && isIdentChar(static_cast<unsigned char>(source[pos]))) { ++pos; ++col; }
            std::string word = source.substr(start, pos - start);
            const auto& kw = keywords();
            auto it = kw.find(word);
            result.tokens.push_back(makeToken(it != kw.end() ? it->second : Token::Ident,
                                              std::move(word), startLine, startCol));
            continue;
        }

        makeError(std::string("Unexpected character '") + c + "'");
        return result;
    }

    result.tokens.push_back(makeToken(Token::Eof, std::string(), line, col));
    return result;
}

} // namespace lidl
