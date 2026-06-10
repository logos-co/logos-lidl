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
            for (const FieldDecl& fd : td.fields) validateTypeExpr(fd.type, result);
        }

        std::unordered_set<std::string> seenMethods;
        for (const MethodDecl& md : m_mod.methods) {
            if (seenMethods.count(md.name)) result.errors.push_back("Duplicate method definition '" + md.name + "'");
            seenMethods.insert(md.name);
            validateTypeExpr(md.returnType, result);
            std::unordered_set<std::string> seenParams;
            for (const ParamDecl& pd : md.params) {
                validateTypeExpr(pd.type, result);
                if (seenParams.count(pd.name)) result.errors.push_back("Duplicate parameter '" + pd.name + "' in method '" + md.name + "'");
                seenParams.insert(pd.name);
            }
        }

        std::unordered_set<std::string> seenEvents;
        for (const EventDecl& ed : m_mod.events) {
            if (seenEvents.count(ed.name)) result.errors.push_back("Duplicate event definition '" + ed.name + "'");
            seenEvents.insert(ed.name);
            for (const ParamDecl& pd : ed.params) validateTypeExpr(pd.type, result);
        }
        return result;
    }

private:
    const ModuleDecl& m_mod;
    std::unordered_set<std::string> m_declaredTypes;

    void validateTypeExpr(const TypeExpr& te, ValidationResult& result) {
        switch (te.kind) {
        case TypeExpr::Primitive: break;
        case TypeExpr::Named:
            if (!m_declaredTypes.count(te.name)) result.errors.push_back("Unknown type '" + te.name + "'");
            break;
        case TypeExpr::Array: validateTypeExpr(te.elements[0], result); break;
        case TypeExpr::Map: validateTypeExpr(te.elements[0], result); validateTypeExpr(te.elements[1], result); break;
        case TypeExpr::Optional: validateTypeExpr(te.elements[0], result); break;
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
