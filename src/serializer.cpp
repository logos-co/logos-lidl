#include "lidl/serializer.hpp"

#include <sstream>

namespace lidl {

namespace {

// Escape a description for a "..."-delimited LIDL string literal (the lexer
// decodes \\ \" \n \t). Keeps method/event docs intact across a .lidl
// round-trip so introspection (lm / getMethods) still shows them. Backslash
// must be replaced first so the escapes introduced below aren't re-escaped.
std::string lidlEscapeStr(std::string in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    return out;
}

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
    if (!module.description.empty()) s << "  description \"" << lidlEscapeStr(module.description) << "\"\n";
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
        s << ") -> " << serializeTypeExpr(md.returnType);
        if (!md.description.empty()) s << " description \"" << lidlEscapeStr(md.description) << "\"";
        s << "\n";
    }
    if (!module.events.empty()) s << "\n";
    for (const EventDecl& ed : module.events) {
        s << "  event " << ed.name << "(";
        serializeParams(s, ed.params);
        s << ")";
        if (!ed.description.empty()) s << " description \"" << lidlEscapeStr(ed.description) << "\"";
        s << "\n";
    }
    s << "}\n";
    return s.str();
}

} // namespace lidl
