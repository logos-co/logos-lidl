#include "lidl/identity.hpp"

#include <algorithm>

namespace lidl {

const char* const kIdentityName = "name";
const char* const kIdentityVersion = "version";

bool isIdentityMethod(const std::string& methodName)
{
    return methodName == kIdentityName || methodName == kIdentityVersion;
}

namespace {

TypeExpr tstrType()
{
    TypeExpr t;
    t.kind = TypeExpr::Primitive;
    t.name = "tstr";
    return t;
}

const char* identityDescription(const std::string& methodName)
{
    return methodName == kIdentityName
        ? "The module's name, as declared in its metadata."
        : "The module's version, as declared in its metadata.";
}

// An author-declared method may stand in for an injected one only when it is
// the same method: no parameters, returning tstr. Description is ignored --
// the author's own wording is theirs to choose.
bool matchesIdentitySignature(const MethodDecl& md)
{
    return md.params.empty()
        && md.returnType.kind == TypeExpr::Primitive
        && md.returnType.name == "tstr";
}

std::string describeSignature(const MethodDecl& md)
{
    std::string s = md.name + "(";
    for (size_t i = 0; i < md.params.size(); ++i) {
        if (i) s += ", ";
        s += md.params[i].name;
    }
    s += ") -> ";
    s += md.returnType.name.empty() ? "void" : md.returnType.name;
    return s;
}

} // namespace

MethodDecl identityMethodDecl(const std::string& methodName)
{
    MethodDecl md;
    md.name = methodName;
    md.returnType = tstrType();
    md.description = identityDescription(methodName);
    md.derived = true;
    return md;
}

IdentityInjection injectIdentityMethods(ModuleDecl& module)
{
    IdentityInjection result;

    const std::string wanted[] = { kIdentityName, kIdentityVersion };
    for (const std::string& methodName : wanted) {
        auto it = std::find_if(module.methods.begin(), module.methods.end(),
                               [&](const MethodDecl& m) { return m.name == methodName; });

        if (it != module.methods.end()) {
            // Already derived: a second pass over the same AST. Idempotent, so
            // a backend never has to track whether it injected yet.
            if (it->derived) continue;
            if (!matchesIdentitySignature(*it)) {
                result.error = "module '" + module.name + "' declares '"
                    + describeSignature(*it) + "', but '" + methodName
                    + "' is reserved for module identity and must be '"
                    + methodName + "() -> tstr'";
                return result;
            }
            continue; // the author implements it; leave it alone
        }

        module.methods.push_back(identityMethodDecl(methodName));
        if (methodName == kIdentityName) result.addedName = true;
        else result.addedVersion = true;
    }

    return result;
}

} // namespace lidl
