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
    // The type exactly as written. `? name: T` leaves this as T and sets
    // `optional`; `name: ?T` leaves `optional` false and makes this an
    // Optional wrapping T. Both are preserved verbatim so serialization is
    // spelling-stable — ask fieldIsOptional()/fieldValueType() for meaning.
    TypeExpr type;
    // The leading-`?`-before-the-name spelling. NOT the answer to "is this
    // field optional" — that is fieldIsOptional(), which also honors an
    // Optional type. Never read this flag on its own.
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

// --- Optionality (see docs/spec.md, "Optionality") --------------------------
//
// `?T` is TWO-state: a value of T, or empty. Never three-state. Every target
// language has exactly one empty inhabitant, so a backend maps `?T` onto its
// single optional-of-T type.
//
// Optionality has two equivalent spellings — the field flag (`? name: T`) and
// the type kind (`name: ?T`) — and they MUST produce identical bindings. These
// helpers are the one place that reconciliation happens: every backend reads
// them instead of re-deriving optionality, so the two spellings cannot drift.

// True when this type expression is itself an optional.
inline bool typeIsOptional(const TypeExpr& t)
{
    return t.kind == TypeExpr::Optional;
}

// The value type carried by an optional — i.e. `?T` -> `T`, `T` -> `T`.
// Strips *every* leading Optional layer: optionality is idempotent under the
// two-state rule, so `??T` denotes the same two states as `?T` and must not be
// allowed to become a third one. A degenerate Optional with no element (only
// reachable by hand-building an AST or via the JSON bridge) is returned as-is
// rather than dereferenced.
inline const TypeExpr& optionalValueType(const TypeExpr& t)
{
    const TypeExpr* p = &t;
    while (p->kind == TypeExpr::Optional && !p->elements.empty()) p = &p->elements[0];
    return *p;
}

// Whether a record field may be empty. Reconciles the two spellings: true for
// `? name: T` and for `name: ?T` alike.
inline bool fieldIsOptional(const FieldDecl& f)
{
    return f.optional || typeIsOptional(f.type);
}

// The type of a record field's value, with optionality stripped. Returns `T`
// for `? name: T`, for `name: ?T`, and for the redundant `? name: ?T`.
inline const TypeExpr& fieldValueType(const FieldDecl& f)
{
    return optionalValueType(f.type);
}

// Parameters (method params, event params) have only the type-kind spelling —
// a positional slot has no name-flag form. Provided so a backend can walk
// fields and params through the same accessors.
inline bool paramIsOptional(const ParamDecl& p)
{
    return typeIsOptional(p.type);
}

inline const TypeExpr& paramValueType(const ParamDecl& p)
{
    return optionalValueType(p.type);
}

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
