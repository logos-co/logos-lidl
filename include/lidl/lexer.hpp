#ifndef LIDL_LEXER_HPP
#define LIDL_LEXER_HPP

#include <string>
#include <vector>

namespace lidl {

struct Token {
    enum Type {
        // Keywords
        Module, TypeKw, Method, Event,
        Version, Description, Category, Depends,
        // Literals
        Ident, StringLit,
        // Symbols
        LBrace, RBrace, LParen, RParen, LBracket, RBracket,
        Colon, Comma, Arrow, Question,
        // Special
        Eof, Error
    };

    Type type = Eof;
    std::string text;
    int line = 0;
    int column = 0;
};

struct LexResult {
    std::vector<Token> tokens;
    std::string error;
    int errorLine = 0;
    int errorColumn = 0;
    bool hasError() const { return !error.empty(); }
};

LexResult tokenize(const std::string& source);

} // namespace lidl

#endif // LIDL_LEXER_HPP
