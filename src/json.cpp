#include "lidl/json.hpp"

#include <nlohmann/json.hpp>

namespace lidl {

namespace {

using nlohmann::json;

const char* kindToStr(TypeExpr::Kind k)
{
    switch (k) {
    case TypeExpr::Primitive: return "primitive";
    case TypeExpr::Array:     return "array";
    case TypeExpr::Map:       return "map";
    case TypeExpr::Optional:  return "optional";
    case TypeExpr::Named:     return "named";
    }
    return "primitive";
}

TypeExpr::Kind kindFromStr(const std::string& s)
{
    if (s == "array")    return TypeExpr::Array;
    if (s == "map")      return TypeExpr::Map;
    if (s == "optional") return TypeExpr::Optional;
    if (s == "named")    return TypeExpr::Named;
    return TypeExpr::Primitive;
}

json typeToJson(const TypeExpr& te)
{
    json elems = json::array();
    for (const TypeExpr& e : te.elements) elems.push_back(typeToJson(e));
    return json{
        {"kind", kindToStr(te.kind)},
        {"name", te.name},
        {"elements", elems},
    };
}

TypeExpr typeFromJson(const json& j)
{
    TypeExpr te;
    te.kind = kindFromStr(j.value("kind", "primitive"));
    te.name = j.value("name", "");
    if (j.contains("elements"))
        for (const json& e : j.at("elements")) te.elements.push_back(typeFromJson(e));
    return te;
}

json paramToJson(const ParamDecl& p)
{
    return json{{"name", p.name}, {"type", typeToJson(p.type)}};
}

ParamDecl paramFromJson(const json& j)
{
    ParamDecl p;
    p.name = j.value("name", "");
    p.type = typeFromJson(j.at("type"));
    return p;
}

json paramsToJson(const std::vector<ParamDecl>& params)
{
    json arr = json::array();
    for (const ParamDecl& p : params) arr.push_back(paramToJson(p));
    return arr;
}

std::vector<ParamDecl> paramsFromJson(const json& j)
{
    std::vector<ParamDecl> params;
    if (j.contains("params"))
        for (const json& p : j.at("params")) params.push_back(paramFromJson(p));
    return params;
}

json fieldToJson(const FieldDecl& f)
{
    return json{{"name", f.name}, {"type", typeToJson(f.type)}, {"optional", f.optional}};
}

FieldDecl fieldFromJson(const json& j)
{
    FieldDecl f;
    f.name = j.value("name", "");
    f.type = typeFromJson(j.at("type"));
    f.optional = j.value("optional", false);
    return f;
}

json typeDeclToJson(const TypeDecl& t)
{
    json fields = json::array();
    for (const FieldDecl& f : t.fields) fields.push_back(fieldToJson(f));
    return json{{"name", t.name}, {"fields", fields}};
}

TypeDecl typeDeclFromJson(const json& j)
{
    TypeDecl t;
    t.name = j.value("name", "");
    if (j.contains("fields"))
        for (const json& f : j.at("fields")) t.fields.push_back(fieldFromJson(f));
    return t;
}

json methodToJson(const MethodDecl& m)
{
    return json{
        {"name", m.name},
        {"params", paramsToJson(m.params)},
        {"returnType", typeToJson(m.returnType)},
        {"description", m.description},
        {"jsonReturn", m.jsonReturn},
        {"resultReturn", m.resultReturn},
    };
}

MethodDecl methodFromJson(const json& j)
{
    MethodDecl m;
    m.name = j.value("name", "");
    m.params = paramsFromJson(j);
    m.returnType = typeFromJson(j.at("returnType"));
    m.description = j.value("description", "");
    m.jsonReturn = j.value("jsonReturn", false);
    m.resultReturn = j.value("resultReturn", false);
    return m;
}

json eventToJson(const EventDecl& e)
{
    return json{
        {"name", e.name},
        {"params", paramsToJson(e.params)},
        {"description", e.description},
    };
}

EventDecl eventFromJson(const json& j)
{
    EventDecl e;
    e.name = j.value("name", "");
    e.params = paramsFromJson(j);
    e.description = j.value("description", "");
    return e;
}

} // namespace

std::string toJson(const ModuleDecl& module, bool pretty)
{
    json types = json::array();
    for (const TypeDecl& t : module.types) types.push_back(typeDeclToJson(t));
    json methods = json::array();
    for (const MethodDecl& m : module.methods) methods.push_back(methodToJson(m));
    json events = json::array();
    for (const EventDecl& e : module.events) events.push_back(eventToJson(e));

    json j{
        {"name", module.name},
        {"version", module.version},
        {"description", module.description},
        {"category", module.category},
        {"depends", module.depends},
        {"types", types},
        {"methods", methods},
        {"events", events},
    };
    return pretty ? j.dump(2) : j.dump();
}

ModuleDecl moduleFromJson(const std::string& jsonText)
{
    json j = json::parse(jsonText);
    ModuleDecl m;
    m.name = j.value("name", "");
    m.version = j.value("version", "");
    m.description = j.value("description", "");
    m.category = j.value("category", "");
    if (j.contains("depends"))
        m.depends = j.at("depends").get<std::vector<std::string>>();
    if (j.contains("types"))
        for (const json& t : j.at("types")) m.types.push_back(typeDeclFromJson(t));
    if (j.contains("methods"))
        for (const json& md : j.at("methods")) m.methods.push_back(methodFromJson(md));
    if (j.contains("events"))
        for (const json& ed : j.at("events")) m.events.push_back(eventFromJson(ed));
    return m;
}

} // namespace lidl
