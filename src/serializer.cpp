#include "lidl/serializer.hpp"

#include <sstream>

namespace lidl {

namespace {

std::string serializeTypeExpr(const TypeExpr& te)
{
    switch (te.kind) {
    case TypeExpr::Primitive: case TypeExpr::Named: return te.name;
    case TypeExpr::Array: return "[" + serializeTypeExpr(te.elements[0]) + "]";
    case TypeExpr::Map: return "{" + serializeTypeExpr(te.elements[0]) + ": " + serializeTypeExpr(te.elements[1]) + "}";
    case TypeExpr::Optional: return "? " + serializeTypeExpr(te.elements[0]);
    }
    return std::string();
}

void serializeParams(std::ostringstream& s, const std::vector<ParamDecl>& params)
{
    for (size_t i = 0; i < params.size(); ++i) {
        s << params[i].name << ": " << serializeTypeExpr(params[i].type);
        if (i + 1 < params.size()) s << ", ";
    }
}

} // namespace

std::string serialize(const ModuleDecl& module)
{
    std::ostringstream s;
    s << "module " << module.name << " {\n";
    if (!module.version.empty()) s << "  version \"" << module.version << "\"\n";
    if (!module.description.empty()) s << "  description \"" << module.description << "\"\n";
    if (!module.category.empty()) s << "  category \"" << module.category << "\"\n";
    s << "  depends [";
    for (size_t i = 0; i < module.depends.size(); ++i) { s << module.depends[i]; if (i + 1 < module.depends.size()) s << ", "; }
    s << "]\n";
    for (const TypeDecl& td : module.types) {
        s << "\n  type " << td.name << " {\n";
        for (const FieldDecl& fd : td.fields) {
            s << "    ";
            if (fd.optional) s << "? ";
            s << fd.name << ": " << serializeTypeExpr(fd.type) << "\n";
        }
        s << "  }\n";
    }
    if (!module.methods.empty()) s << "\n";
    for (const MethodDecl& md : module.methods) {
        s << "  method " << md.name << "(";
        serializeParams(s, md.params);
        s << ") -> " << serializeTypeExpr(md.returnType) << "\n";
    }
    if (!module.events.empty()) s << "\n";
    for (const EventDecl& ed : module.events) {
        s << "  event " << ed.name << "(";
        serializeParams(s, ed.params);
        s << ")\n";
    }
    s << "}\n";
    return s.str();
}

} // namespace lidl
