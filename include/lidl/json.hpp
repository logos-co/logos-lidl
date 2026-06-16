#ifndef LIDL_JSON_HPP
#define LIDL_JSON_HPP

// AST <-> JSON (text) bridge. The JSON form is the cross-language wire format
// the C ABI (lidl_c.h) exposes to non-C++ SDKs (Rust, ...): they reach the
// canonical parser/serializer/validator without reimplementing the grammar.
//
// The public API takes/returns std::string so this header stays
// dependency-free; nlohmann/json is an implementation detail of json.cpp.

#include "lidl/ast.hpp"
#include <string>

namespace lidl {

// Serialize an AST to its JSON wire form. Covers every field, including
// `description` and the `jsonReturn`/`resultReturn` return-shape flags.
std::string toJson(const ModuleDecl& module, bool pretty = false);

// Parse the JSON wire form back into an AST. Throws nlohmann::json::exception
// (a std::exception) on malformed JSON.
ModuleDecl moduleFromJson(const std::string& json);

} // namespace lidl

#endif // LIDL_JSON_HPP
