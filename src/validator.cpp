#include "lidl/validator.hpp"

#include <unordered_set>

namespace lidl {

namespace {

const std::unordered_set<std::string>& builtinTypes()
{
    static const std::unordered_set<std::string> bt = {
        "tstr", "bstr", "int", "uint", "float64", "bool", "result", "any"
    };
    return bt;
}

class Validator {
public:
    explicit Validator(const ModuleDecl& mod) : m_mod(mod) {
        for (const TypeDecl& td : mod.types) m_declaredTypes.insert(td.name);
    }

    ValidationResult validate() {
        ValidationResult result;
        if (m_mod.name.empty()) result.errors.push_back("Module name is empty");

        std::unordered_set<std::string> seenTypes;
        for (const TypeDecl& td : m_mod.types) {
            if (builtinTypes().count(td.name)) result.errors.push_back("Type '" + td.name + "' shadows a builtin type");
            if (seenTypes.count(td.name)) result.errors.push_back("Duplicate type definition '" + td.name + "'");
            seenTypes.insert(td.name);
            for (const FieldDecl& fd : td.fields) {
                const std::string where = "field '" + fd.name + "' of type '" + td.name + "'";
                // The two spellings of optionality mean the same thing, so
                // writing both is redundant, not an error — but it is worth
                // saying, because a backend that (wrongly) re-derives
                // optionality from one spelling alone will disagree here.
                if (fd.optional && typeIsOptional(fd.type))
                    result.warnings.push_back("Field '" + fd.name + "' of type '" + td.name
                        + "' is marked optional twice ('? " + fd.name + ": ?T'); the field flag and the "
                          "optional type are equivalent, one suffices");
                // `? name: T` is optionality just as much as `name: ?T` is, so
                // the `any` rule below must see it too.
                if (fd.optional && !typeIsOptional(fd.type))
                    warnOptionalAny(fd.type, where, result);
                validateTypeExpr(fd.type, result, where, /*inMapKey=*/false, /*underOptional=*/false);
            }
        }

        std::unordered_set<std::string> seenMethods;
        for (const MethodDecl& md : m_mod.methods) {
            if (seenMethods.count(md.name)) result.errors.push_back("Duplicate method definition '" + md.name + "'");
            seenMethods.insert(md.name);
            validateTypeExpr(md.returnType, result, "return type of method '" + md.name + "'", false, false);
            std::unordered_set<std::string> seenParams;
            for (const ParamDecl& pd : md.params) {
                validateTypeExpr(pd.type, result, "parameter '" + pd.name + "' of method '" + md.name + "'", false, false);
                if (seenParams.count(pd.name)) result.errors.push_back("Duplicate parameter '" + pd.name + "' in method '" + md.name + "'");
                seenParams.insert(pd.name);
            }
        }

        std::unordered_set<std::string> seenEvents;
        for (const EventDecl& ed : m_mod.events) {
            if (seenEvents.count(ed.name)) result.errors.push_back("Duplicate event definition '" + ed.name + "'");
            seenEvents.insert(ed.name);
            for (const ParamDecl& pd : ed.params)
                validateTypeExpr(pd.type, result, "parameter '" + pd.name + "' of event '" + ed.name + "'", false, false);
        }
        return result;
    }

private:
    const ModuleDecl& m_mod;
    std::unordered_set<std::string> m_declaredTypes;

    // `any` already admits the empty value, so wrapping it adds no state — it
    // only invites a backend to spell a three-state Option<Value>, which the
    // two-state rule forbids.
    void warnOptionalAny(const TypeExpr& inner, const std::string& where, ValidationResult& result) {
        if (inner.kind == TypeExpr::Primitive && inner.name == "any")
            result.warnings.push_back("Optional 'any' in " + where
                + " is redundant: 'any' already admits the empty value; use 'any'");
    }

    // `inMapKey` marks the key half of a `{K: V}`; `underOptional` marks a
    // type expression that is already the payload of an enclosing optional.
    void validateTypeExpr(const TypeExpr& te, ValidationResult& result,
                          const std::string& where, bool inMapKey, bool underOptional) {
        switch (te.kind) {
        case TypeExpr::Primitive: break;
        case TypeExpr::Named:
            if (!m_declaredTypes.count(te.name)) result.errors.push_back("Unknown type '" + te.name + "'");
            break;
        case TypeExpr::Array:
            if (!te.elements.empty()) validateTypeExpr(te.elements[0], result, where, false, false);
            break;
        case TypeExpr::Map:
            if (te.elements.size() >= 2) {
                validateTypeExpr(te.elements[0], result, where, /*inMapKey=*/true, false);
                validateTypeExpr(te.elements[1], result, where, false, false);
            }
            break;
        case TypeExpr::Optional:
            // A map key has no empty inhabitant: "absent key" is not a key
            // whose value is empty, it is the absence of the entry. There is
            // nothing for `?K` to denote, so it is rejected outright.
            if (inMapKey)
                result.errors.push_back("Optional is not allowed in a map key position (in " + where + ")");
            // Optionality is two-state and therefore idempotent — `??T`
            // denotes exactly what `?T` does. Accepted and collapsed rather
            // than rejected, but flagged so it does not read as a third state.
            if (underOptional)
                result.warnings.push_back("Redundant nested optional in " + where
                    + ": '??T' denotes the same two states as '?T'");
            if (!te.elements.empty()) {
                warnOptionalAny(te.elements[0], where, result);
                // Optional widens the domain by exactly one inhabitant; it does
                // not switch off type checking, so the payload is validated as
                // usual (an unknown named type stays an error).
                // inMapKey is not propagated: the key position is reported
                // once, at the outermost optional.
                validateTypeExpr(te.elements[0], result, where, /*inMapKey=*/false, /*underOptional=*/true);
            }
            break;
        }
    }
};

} // namespace

ValidationResult validate(const ModuleDecl& module)
{
    Validator v(module);
    return v.validate();
}

} // namespace lidl
