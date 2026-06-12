#ifndef LIDL_AST_HPP
#define LIDL_AST_HPP

// Language-neutral LIDL AST — the interchange IR every Logos SDK backend
// (C++, Rust, ...) generates from. Pure standard C++; the .lidl text form
// produced by serialize() is the cross-language artifact modules publish.

#include <string>
#include <vector>

namespace lidl {

struct TypeExpr {
    enum Kind { Primitive, Array, Map, Optional, Named };
    Kind kind = Primitive;
    std::string name; // primitive name ("tstr","int",...) or custom type name
    std::vector<TypeExpr> elements; // Array: [0]=elem, Map: [0]=key [1]=val, Optional: [0]=inner

    bool operator==(const TypeExpr& o) const {
        return kind == o.kind && name == o.name && elements == o.elements;
    }
    bool operator!=(const TypeExpr& o) const { return !(*this == o); }
};

struct FieldDecl {
    std::string name;
    TypeExpr type;
    bool optional = false;

    bool operator==(const FieldDecl& o) const {
        return name == o.name && type == o.type && optional == o.optional;
    }
};

struct ParamDecl {
    std::string name;
    TypeExpr type;

    bool operator==(const ParamDecl& o) const {
        return name == o.name && type == o.type;
    }
};

struct MethodDecl {
    std::string name;
    std::vector<ParamDecl> params;
    TypeExpr returnType;
    // Doc comment adjacent to the method declaration (becomes "description").
    std::string description;
    // True when the impl returns LogosMap or LogosList (nlohmann::json).
    bool jsonReturn = false;
    // True when the impl returns StdLogosResult.
    bool resultReturn = false;

    bool operator==(const MethodDecl& o) const {
        return name == o.name && params == o.params && returnType == o.returnType
            && description == o.description
            && jsonReturn == o.jsonReturn && resultReturn == o.resultReturn;
    }
};

struct EventDecl {
    std::string name;
    std::vector<ParamDecl> params;
    std::string description;

    bool operator==(const EventDecl& o) const {
        return name == o.name && params == o.params && description == o.description;
    }
};

struct TypeDecl {
    std::string name;
    std::vector<FieldDecl> fields;

    bool operator==(const TypeDecl& o) const {
        return name == o.name && fields == o.fields;
    }
};

struct ModuleDecl {
    std::string name;
    std::string version;
    std::string description;
    std::string category;
    std::vector<std::string> depends;
    std::vector<TypeDecl> types;
    std::vector<MethodDecl> methods;
    std::vector<EventDecl> events;

    bool operator==(const ModuleDecl& o) const {
        return name == o.name && version == o.version
            && description == o.description && category == o.category
            && depends == o.depends && types == o.types
            && methods == o.methods && events == o.events;
    }
};

} // namespace lidl

#endif // LIDL_AST_HPP
