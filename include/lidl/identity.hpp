#ifndef LIDL_IDENTITY_HPP
#define LIDL_IDENTITY_HPP

// Module identity methods — `name()` and `version()`.
//
// Every Logos module answers these, and no author writes them: they are
// derived from the module declaration (which the builder derives in turn from
// metadata.json), so the value a module reports cannot drift from the value it
// was built with. Hand-written literals did drift, which is why this exists.
//
// Every backend calls injectIdentityMethods() on each ModuleDecl it obtains —
// parsed from a .lidl, or derived from an impl header — on BOTH the provider
// and the consumer side. Same function, same result, so the two sides cannot
// disagree about whether a module has them.
//
// The injected methods are marked MethodDecl::derived, which has two
// consequences worth knowing: serialize() omits them, so a published .lidl
// stays exactly what its author wrote no matter when injection ran; and a
// backend must emit a BODY for them rather than delegating to the module's
// impl class, which has no such member.

#include "lidl/ast.hpp"

#include <string>

namespace lidl {

// The identity method names. `name` is the module's identity; `version` is
// release metadata the module reports. They are not the same kind of thing,
// and only `name` is load-bearing (the host refuses a module whose reported
// name disagrees with the package name it was loaded as).
extern const char* const kIdentityName;
extern const char* const kIdentityVersion;

// True for a name reserved by the identity surface. Backends use this to keep
// an author's own declaration from being treated as ordinary API.
bool isIdentityMethod(const std::string& methodName);

// The signature every identity method has: no parameters, returns tstr.
MethodDecl identityMethodDecl(const std::string& methodName);

struct IdentityInjection {
    bool addedName = false;
    bool addedVersion = false;
    // Non-empty when the module already declares an identity method with an
    // incompatible signature. The caller MUST surface this rather than
    // silently keeping the author's version: a `name` that takes an argument
    // or returns something other than tstr is a different method wearing a
    // reserved name, and every consumer would generate a wrapper for it.
    std::string error;

    bool hasError() const { return !error.empty(); }
};

// Append `name()` and `version()` to `module` unless it already declares them.
//
// Appends rather than inserts so existing method positions are unchanged, for
// any backend that keys on declaration order.
//
// An existing declaration with the exact identity signature is left alone and
// reported as not-added — this is not an error. A module MAY implement `name()`
// itself (some do); it then simply owns the method, keeps `derived == false`,
// and backends delegate to it as they would to any other author method.
//
// Idempotent: running it twice over the same AST adds nothing the second time.
IdentityInjection injectIdentityMethods(ModuleDecl& module);

} // namespace lidl

#endif // LIDL_IDENTITY_HPP
