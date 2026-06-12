#ifndef LIDL_SERIALIZER_HPP
#define LIDL_SERIALIZER_HPP

#include "lidl/ast.hpp"
#include <string>

namespace lidl {

std::string serialize(const ModuleDecl& module);

} // namespace lidl

#endif // LIDL_SERIALIZER_HPP
