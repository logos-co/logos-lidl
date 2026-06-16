#include "lidl/lidl_c.h"

#include "lidl/json.hpp"
#include "lidl/parser.hpp"
#include "lidl/serializer.hpp"
#include "lidl/validator.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace {

char* dupString(const std::string& s)
{
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

void setErr(char** err, const std::string& msg)
{
    if (err) *err = dupString(msg);
}

} // namespace

extern "C" {

char* lidl_parse_to_json(const char* lidl, char** err)
{
    if (err) *err = nullptr;
    if (!lidl) { setErr(err, "null input"); return nullptr; }
    lidl::ParseResult pr = lidl::parse(lidl);
    if (pr.hasError()) {
        setErr(err, "parse error at line " + std::to_string(pr.errorLine)
                    + " column " + std::to_string(pr.errorColumn) + ": " + pr.error);
        return nullptr;
    }
    try {
        return dupString(lidl::toJson(pr.module));
    } catch (const std::exception& e) {
        setErr(err, std::string("json serialization error: ") + e.what());
        return nullptr;
    }
}

char* lidl_serialize_from_json(const char* json, char** err)
{
    if (err) *err = nullptr;
    if (!json) { setErr(err, "null input"); return nullptr; }
    try {
        lidl::ModuleDecl module = lidl::moduleFromJson(json);
        return dupString(lidl::serialize(module));
    } catch (const std::exception& e) {
        setErr(err, std::string("invalid JSON AST: ") + e.what());
        return nullptr;
    }
}

char* lidl_validate_json(const char* json)
{
    if (!json) return nullptr;
    try {
        lidl::ModuleDecl module = lidl::moduleFromJson(json);
        lidl::ValidationResult vr = lidl::validate(module);
        nlohmann::json out;
        out["errors"] = vr.errors;
        out["warnings"] = vr.warnings;
        return dupString(out.dump());
    } catch (const std::exception&) {
        return nullptr;
    }
}

void lidl_free_string(char* s)
{
    std::free(s);
}

} // extern "C"
