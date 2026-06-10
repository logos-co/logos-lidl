#ifndef LIDL_PARSER_HPP
#define LIDL_PARSER_HPP

#include "lidl/ast.hpp"
#include <string>

namespace lidl {

struct ParseResult {
    ModuleDecl module;
    std::string error;
    int errorLine = 0;
    int errorColumn = 0;
    bool hasError() const { return !error.empty(); }
};

ParseResult parse(const std::string& source);

} // namespace lidl

#endif // LIDL_PARSER_HPP
