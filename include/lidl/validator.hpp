#ifndef LIDL_VALIDATOR_HPP
#define LIDL_VALIDATOR_HPP

#include "lidl/ast.hpp"
#include <string>
#include <vector>

namespace lidl {

struct ValidationResult {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool hasErrors() const { return !errors.empty(); }
};

ValidationResult validate(const ModuleDecl& module);

} // namespace lidl

#endif // LIDL_VALIDATOR_HPP
