#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "lidl/identity.hpp"
#include "lidl/json.hpp"
#include "lidl/lexer.hpp"
#include "lidl/parser.hpp"
#include "lidl/serializer.hpp"
#include "lidl/validator.hpp"
#include "lidl/lidl_c.h"

using namespace lidl;

namespace {

// Method/event descriptions + return-shape flags. The escaped quote in the
// module description and the embedded quote/newline in greet's doc exercise
// the serializer's escaping against the lexer's decoding.
const char* kDocumented =
    "module doc_module {\n"
    "  version \"1.0.0\"\n"
    "  description \"A \\\"documented\\\" module\"\n"
    "  depends []\n"
    "\n"
    "  method greet(name: tstr) -> tstr description \"Greets \\\"name\\\".\\nReturns a salutation.\"\n"
    "  method fetch() -> result description \"May fail\"\n"
    "  method dump() -> any\n"
    "  method many() -> [any]\n"
    "\n"
    "  event tick(n: int) description \"Fires each tick\"\n"
    "}\n";

const char* kCanonical =
    "module chat_module {\n"
    "  version \"1.2.0\"\n"
    "  description \"Chat module\"\n"
    "  category \"messaging\"\n"
    "  depends [waku_module, storage_module]\n"
    "\n"
    "  type Message {\n"
    "    id: tstr\n"
    "    ? payload: bstr\n"
    "    tags: [tstr]\n"
    "    meta: {tstr: any}\n"
    "  }\n"
    "\n"
    "  method send(channel: tstr, message: Message) -> result\n"
    "  method count() -> int\n"
    "\n"
    "  event messageReceived(channel: tstr, message: Message)\n"
    "}\n";

} // namespace

TEST(Lexer, TokenizesCanonicalDocument)
{
    auto lr = tokenize(kCanonical);
    ASSERT_FALSE(lr.hasError()) << lr.error;
    ASSERT_FALSE(lr.tokens.empty());
    EXPECT_EQ(lr.tokens.front().type, Token::Module);
    EXPECT_EQ(lr.tokens.back().type, Token::Eof);
}

TEST(Lexer, CommentsAndEscapes)
{
    auto lr = tokenize("; a comment line\nmodule m { description \"a \\\"quoted\\\" word\" depends [] }");
    ASSERT_FALSE(lr.hasError());
    bool found = false;
    for (const auto& t : lr.tokens)
        if (t.type == Token::StringLit) { EXPECT_EQ(t.text, "a \"quoted\" word"); found = true; }
    EXPECT_TRUE(found);
}

TEST(Lexer, ErrorsOnUnterminatedString)
{
    auto lr = tokenize("module m { description \"oops }");
    EXPECT_TRUE(lr.hasError());
}

TEST(Parser, ParsesCanonicalDocument)
{
    auto pr = parse(kCanonical);
    ASSERT_FALSE(pr.hasError()) << pr.error;
    const ModuleDecl& m = pr.module;
    EXPECT_EQ(m.name, "chat_module");
    EXPECT_EQ(m.version, "1.2.0");
    EXPECT_EQ(m.depends, (std::vector<std::string>{"waku_module", "storage_module"}));
    ASSERT_EQ(m.types.size(), 1u);
    EXPECT_EQ(m.types[0].fields.size(), 4u);
    EXPECT_TRUE(m.types[0].fields[1].optional);
    EXPECT_EQ(m.types[0].fields[2].type.kind, TypeExpr::Array);
    EXPECT_EQ(m.types[0].fields[3].type.kind, TypeExpr::Map);
    ASSERT_EQ(m.methods.size(), 2u);
    EXPECT_EQ(m.methods[0].params.size(), 2u);
    EXPECT_EQ(m.methods[0].params[1].type.kind, TypeExpr::Named);
    EXPECT_EQ(m.methods[1].returnType.name, "int");
    ASSERT_EQ(m.events.size(), 1u);
    EXPECT_EQ(m.events[0].name, "messageReceived");
}

TEST(Parser, KeywordsAreValidNames)
{
    // Keywords are reserved only structurally; in name positions they are
    // ordinary identifiers (regression for the same-line/reserved-word fix).
    auto pr = parse(
        "module module {\n"
        "  depends []\n"
        "  method method(version: tstr, depends: int) -> bool\n"
        "  event event(category: tstr)\n"
        "}\n");
    ASSERT_FALSE(pr.hasError()) << pr.error;
    EXPECT_EQ(pr.module.name, "module");
    ASSERT_EQ(pr.module.methods.size(), 1u);
    EXPECT_EQ(pr.module.methods[0].name, "method");
    EXPECT_EQ(pr.module.methods[0].params[0].name, "version");
    ASSERT_EQ(pr.module.events.size(), 1u);
    EXPECT_EQ(pr.module.events[0].params[0].name, "category");
}

TEST(Parser, ReportsErrorsWithLocation)
{
    auto pr = parse("module m {\n  method broken( -> int\n}\n");
    ASSERT_TRUE(pr.hasError());
    EXPECT_GT(pr.errorLine, 1);
}

TEST(RoundTrip, SerializeParseStable)
{
    auto pr = parse(kCanonical);
    ASSERT_FALSE(pr.hasError()) << pr.error;

    const std::string text1 = serialize(pr.module);
    auto pr2 = parse(text1);
    ASSERT_FALSE(pr2.hasError()) << pr2.error << "\n--- serialized:\n" << text1;

    // AST-stable…
    EXPECT_EQ(pr.module, pr2.module);
    // …and byte-stable from the first serialization onward.
    EXPECT_EQ(text1, serialize(pr2.module));
}

TEST(RoundTrip, OptionalAndNestedTypes)
{
    ModuleDecl m;
    m.name = "m";
    MethodDecl md;
    md.name = "f";
    ParamDecl p;
    p.name = "x";
    p.type.kind = TypeExpr::Optional;
    p.type.elements.resize(1);
    p.type.elements[0].kind = TypeExpr::Array;
    p.type.elements[0].elements.resize(1);
    p.type.elements[0].elements[0].kind = TypeExpr::Map;
    p.type.elements[0].elements[0].elements.resize(2);
    p.type.elements[0].elements[0].elements[0] = TypeExpr{TypeExpr::Primitive, "tstr", {}};
    p.type.elements[0].elements[0].elements[1] = TypeExpr{TypeExpr::Primitive, "any", {}};
    md.params.push_back(p);
    md.returnType = TypeExpr{TypeExpr::Primitive, "bool", {}};
    m.methods.push_back(md);

    auto pr = parse(serialize(m));
    ASSERT_FALSE(pr.hasError()) << pr.error;
    EXPECT_EQ(pr.module.methods[0].params[0].type, m.methods[0].params[0].type);
}

TEST(Validator, CatchesDuplicatesAndUnknownTypes)
{
    auto pr = parse(
        "module m {\n"
        "  depends []\n"
        "  method f(a: Unknown) -> int\n"
        "  method f() -> int\n"
        "}\n");
    ASSERT_FALSE(pr.hasError()) << pr.error;
    auto vr = validate(pr.module);
    ASSERT_TRUE(vr.hasErrors());
    bool unknown = false, dup = false;
    for (const auto& e : vr.errors) {
        if (e.find("Unknown type") != std::string::npos) unknown = true;
        if (e.find("Duplicate method") != std::string::npos) dup = true;
    }
    EXPECT_TRUE(unknown);
    EXPECT_TRUE(dup);
}

TEST(Validator, AcceptsCanonicalDocument)
{
    auto pr = parse(kCanonical);
    ASSERT_FALSE(pr.hasError());
    auto vr = validate(pr.module);
    EXPECT_FALSE(vr.hasErrors());
}

TEST(Descriptions, ParsedFromMethodsAndEvents)
{
    auto pr = parse(kDocumented);
    ASSERT_FALSE(pr.hasError()) << pr.error;
    const ModuleDecl& m = pr.module;

    EXPECT_EQ(m.description, "A \"documented\" module");
    ASSERT_EQ(m.methods.size(), 4u);
    EXPECT_EQ(m.methods[0].description, "Greets \"name\".\nReturns a salutation.");
    EXPECT_EQ(m.methods[1].description, "May fail");
    EXPECT_EQ(m.methods[2].description, ""); // no doc
    ASSERT_EQ(m.events.size(), 1u);
    EXPECT_EQ(m.events[0].description, "Fires each tick");
}

TEST(Descriptions, ReturnShapeFlagsRestoredOnParse)
{
    auto pr = parse(kDocumented);
    ASSERT_FALSE(pr.hasError()) << pr.error;
    const ModuleDecl& m = pr.module;

    EXPECT_FALSE(m.methods[0].resultReturn); // tstr
    EXPECT_FALSE(m.methods[0].jsonReturn);
    EXPECT_TRUE(m.methods[1].resultReturn);  // result
    EXPECT_TRUE(m.methods[2].jsonReturn);    // any
    EXPECT_TRUE(m.methods[3].jsonReturn);    // [any]
}

TEST(Descriptions, RoundTripStable)
{
    auto pr = parse(kDocumented);
    ASSERT_FALSE(pr.hasError()) << pr.error;

    const std::string text1 = serialize(pr.module);
    // Docs survive serialization as a trailing `description "..."` clause.
    EXPECT_NE(text1.find("description \"Greets"), std::string::npos);
    EXPECT_NE(text1.find("description \"Fires each tick\""), std::string::npos);

    auto pr2 = parse(text1);
    ASSERT_FALSE(pr2.hasError()) << pr2.error << "\n--- serialized:\n" << text1;
    EXPECT_EQ(pr.module, pr2.module);
    EXPECT_EQ(text1, serialize(pr2.module));
}

// --- C ABI / JSON bridge ----------------------------------------------------

TEST(CAbi, ParseToJsonCarriesDescriptionsAndFlags)
{
    char* err = nullptr;
    char* json = lidl_parse_to_json(kDocumented, &err);
    ASSERT_NE(json, nullptr) << (err ? err : "(no error)");
    EXPECT_EQ(err, nullptr);

    const std::string j(json);
    EXPECT_NE(j.find("\"description\":\"May fail\""), std::string::npos);
    EXPECT_NE(j.find("\"resultReturn\":true"), std::string::npos);
    EXPECT_NE(j.find("\"jsonReturn\":true"), std::string::npos);

    lidl_free_string(json);
}

TEST(CAbi, JsonRoundTripStable)
{
    char* err = nullptr;
    char* json = lidl_parse_to_json(kDocumented, &err);
    ASSERT_NE(json, nullptr) << (err ? err : "(no error)");

    char* lidlText = lidl_serialize_from_json(json, &err);
    ASSERT_NE(lidlText, nullptr) << (err ? err : "(no error)");
    EXPECT_EQ(err, nullptr);

    // .lidl -> json -> .lidl -> json is json-stable.
    char* json2 = lidl_parse_to_json(lidlText, &err);
    ASSERT_NE(json2, nullptr) << (err ? err : "(no error)");
    EXPECT_STREQ(json, json2);

    lidl_free_string(json);
    lidl_free_string(lidlText);
    lidl_free_string(json2);
}

TEST(CAbi, ValidateJsonReportsErrors)
{
    char* err = nullptr;
    char* badJson = lidl_parse_to_json(
        "module m {\n  depends []\n  method f(a: Unknown) -> int\n}\n", &err);
    ASSERT_NE(badJson, nullptr) << (err ? err : "(no error)");

    char* report = lidl_validate_json(badJson);
    ASSERT_NE(report, nullptr);
    EXPECT_NE(std::string(report).find("Unknown type"), std::string::npos);

    lidl_free_string(badJson);
    lidl_free_string(report);
}

TEST(CAbi, ParseErrorIsReported)
{
    char* err = nullptr;
    char* json = lidl_parse_to_json("not a module", &err);
    EXPECT_EQ(json, nullptr);
    ASSERT_NE(err, nullptr);
    EXPECT_FALSE(std::string(err).empty());
    lidl_free_string(err);
}

// --- Optionality ------------------------------------------------------------
//
// `?T` is two-state, and its two spellings — the field flag (`? name: T`) and
// the type kind (`name: ?T`) — mean exactly the same thing. See docs/spec.md,
// "Optionality".

namespace {

// `flagged` and `typed` are the same field said two ways; `plain` is the
// control. Serialization must keep them distinct on the page while every
// accessor collapses them to one answer.
const char* kOptional =
    "module opt_module {\n"
    "  depends []\n"
    "\n"
    "  type Account {\n"
    "    plain: tstr\n"
    "    ? flagged: tstr\n"
    "    typed: ?tstr\n"
    "  }\n"
    "\n"
    "  method find(id: ?tstr) -> ?Account\n"
    "}\n";

} // namespace

TEST(Optional, BothSpellingsParse)
{
    auto pr = parse(kOptional);
    ASSERT_FALSE(pr.hasError()) << pr.error;
    const std::vector<FieldDecl>& f = pr.module.types.at(0).fields;
    ASSERT_EQ(f.size(), 3u);

    // The AST keeps the spelling: the flag on one, the Optional kind on the
    // other. Neither is normalized into the other.
    EXPECT_FALSE(f[0].optional);
    EXPECT_TRUE(f[1].optional);
    EXPECT_EQ(f[1].type.kind, TypeExpr::Primitive);
    EXPECT_FALSE(f[2].optional);
    EXPECT_EQ(f[2].type.kind, TypeExpr::Optional);
}

TEST(Optional, AccessorsAgreeAcrossSpellings)
{
    auto pr = parse(kOptional);
    ASSERT_FALSE(pr.hasError()) << pr.error;
    const std::vector<FieldDecl>& f = pr.module.types.at(0).fields;

    EXPECT_FALSE(fieldIsOptional(f[0]));
    EXPECT_TRUE(fieldIsOptional(f[1]));
    EXPECT_TRUE(fieldIsOptional(f[2]));

    // …and one value type, whichever way it was written.
    const TypeExpr tstr{TypeExpr::Primitive, "tstr", {}};
    EXPECT_EQ(fieldValueType(f[0]), tstr);
    EXPECT_EQ(fieldValueType(f[1]), tstr);
    EXPECT_EQ(fieldValueType(f[2]), tstr);
}

TEST(Optional, PositionalSlotsUseTheTypeSpelling)
{
    auto pr = parse(kOptional);
    ASSERT_FALSE(pr.hasError()) << pr.error;
    const MethodDecl& m = pr.module.methods.at(0);

    EXPECT_TRUE(paramIsOptional(m.params.at(0)));
    EXPECT_EQ(paramValueType(m.params.at(0)), (TypeExpr{TypeExpr::Primitive, "tstr", {}}));
    EXPECT_TRUE(typeIsOptional(m.returnType));
    EXPECT_EQ(optionalValueType(m.returnType), (TypeExpr{TypeExpr::Named, "Account", {}}));
}

TEST(Optional, IsIdempotent)
{
    // Two-state, never three: `? x: ?T` and `??T` both collapse to one
    // optional layer over T.
    auto pr = parse(
        "module m {\n"
        "  depends []\n"
        "  type T { ? both: ?tstr }\n"
        "  method f(x: ??int) -> bool\n"
        "}\n");
    ASSERT_FALSE(pr.hasError()) << pr.error;

    const FieldDecl& both = pr.module.types.at(0).fields.at(0);
    EXPECT_TRUE(fieldIsOptional(both));
    EXPECT_EQ(fieldValueType(both), (TypeExpr{TypeExpr::Primitive, "tstr", {}}));
    EXPECT_EQ(paramValueType(pr.module.methods.at(0).params.at(0)),
              (TypeExpr{TypeExpr::Primitive, "int", {}}));
}

TEST(Optional, DegenerateOptionalIsNotDereferenced)
{
    // Reachable only by hand-building an AST or via the JSON bridge, but the
    // accessors must not walk off an Optional with no element.
    TypeExpr empty;
    empty.kind = TypeExpr::Optional;
    EXPECT_EQ(&optionalValueType(empty), &empty);

    FieldDecl f;
    f.name = "x";
    f.type = empty;
    EXPECT_TRUE(fieldIsOptional(f));
    EXPECT_EQ(fieldValueType(f).kind, TypeExpr::Optional);
}

TEST(Optional, SerializerPreservesEachSpelling)
{
    auto pr = parse(kOptional);
    ASSERT_FALSE(pr.hasError()) << pr.error;

    const std::string text1 = serialize(pr.module);
    // The serializer normalizes whitespace, not meaning: neither spelling is
    // rewritten into the other.
    EXPECT_NE(text1.find("? flagged: tstr"), std::string::npos) << text1;
    EXPECT_NE(text1.find("typed: ? tstr"), std::string::npos) << text1;
    EXPECT_EQ(text1.find("? typed:"), std::string::npos) << text1;

    auto pr2 = parse(text1);
    ASSERT_FALSE(pr2.hasError()) << pr2.error << "\n--- serialized:\n" << text1;
    EXPECT_EQ(pr.module, pr2.module);
    EXPECT_EQ(text1, serialize(pr2.module));
}

TEST(Validator, AcceptsOptionalsAndKeepsCheckingThem)
{
    auto pr = parse(kOptional);
    ASSERT_FALSE(pr.hasError()) << pr.error;
    auto vr = validate(pr.module);
    EXPECT_FALSE(vr.hasErrors());
    EXPECT_TRUE(vr.warnings.empty());

    // Optional widens the domain by exactly one inhabitant; it does not turn
    // type checking off.
    auto bad = parse(
        "module m {\n"
        "  depends []\n"
        "  type T { ? a: Unknown }\n"
        "  method f(b: ?AlsoUnknown) -> ?StillUnknown\n"
        "}\n");
    ASSERT_FALSE(bad.hasError()) << bad.error;
    auto badVr = validate(bad.module);
    EXPECT_EQ(badVr.errors.size(), 3u);
    for (const auto& e : badVr.errors)
        EXPECT_NE(e.find("Unknown type"), std::string::npos) << e;
}

TEST(Validator, RejectsOptionalMapKey)
{
    auto pr = parse(
        "module m {\n"
        "  depends []\n"
        "  method f(m: {?tstr: int}) -> bool\n"
        "}\n");
    ASSERT_FALSE(pr.hasError()) << pr.error;
    auto vr = validate(pr.module);
    ASSERT_TRUE(vr.hasErrors());
    // Reported once, at the key, and naming the slot.
    ASSERT_EQ(vr.errors.size(), 1u);
    EXPECT_NE(vr.errors[0].find("map key"), std::string::npos) << vr.errors[0];
    EXPECT_NE(vr.errors[0].find("parameter 'm' of method 'f'"), std::string::npos) << vr.errors[0];

    // An optional map *value* is fine — a named slot can simply be left out.
    auto ok = parse("module m {\n  depends []\n  method f(m: {tstr: ?int}) -> bool\n}\n");
    ASSERT_FALSE(ok.hasError()) << ok.error;
    EXPECT_FALSE(validate(ok.module).hasErrors());
}

TEST(Validator, WarnsOnRedundantOptionality)
{
    auto pr = parse(
        "module m {\n"
        "  depends []\n"
        "  type T {\n"
        "    ? both: ?tstr\n"
        "    ? loose: any\n"
        "  }\n"
        "  method f(x: ??int, y: ?any) -> bool\n"
        "}\n");
    ASSERT_FALSE(pr.hasError()) << pr.error;
    auto vr = validate(pr.module);
    EXPECT_FALSE(vr.hasErrors()); // warnings only — the document is valid

    int twice = 0, nested = 0, anyOpt = 0;
    for (const auto& w : vr.warnings) {
        if (w.find("marked optional twice") != std::string::npos) ++twice;
        if (w.find("Redundant nested optional") != std::string::npos) ++nested;
        if (w.find("Optional 'any'") != std::string::npos) ++anyOpt;
    }
    EXPECT_EQ(twice, 1);
    EXPECT_EQ(nested, 1);
    // Both `? loose: any` (field flag) and `y: ?any` (type kind) are optional
    // `any` — the warning must not be blind to the flag spelling.
    EXPECT_EQ(anyOpt, 2);
}

TEST(CAbi, JsonCarriesDerivedOptionality)
{
    char* err = nullptr;
    char* raw = lidl_parse_to_json(kOptional, &err);
    ASSERT_NE(raw, nullptr) << (err ? err : "(no error)");
    const nlohmann::json j = nlohmann::json::parse(raw);
    lidl_free_string(raw);

    const nlohmann::json tstr{{"kind", "primitive"}, {"name", "tstr"}, {"elements", nlohmann::json::array()}};
    const nlohmann::json fields = j.at("types").at(0).at("fields");
    ASSERT_EQ(fields.size(), 3u);

    // The raw spelling is still there, verbatim and different per field…
    EXPECT_EQ(fields[1].at("optional"), true);            // ? flagged: tstr
    EXPECT_EQ(fields[2].at("optional"), false);           // typed: ?tstr
    EXPECT_EQ(fields[2].at("type").at("kind"), "optional");

    // …but the derived pair — the only thing a backend is allowed to read — is
    // identical for the two spellings.
    EXPECT_EQ(fields[0].at("isOptional"), false);
    EXPECT_EQ(fields[0].at("valueType"), tstr);
    EXPECT_EQ(fields[1].at("isOptional"), true);
    EXPECT_EQ(fields[1].at("valueType"), tstr);
    EXPECT_EQ(fields[2].at("isOptional"), true);
    EXPECT_EQ(fields[2].at("valueType"), tstr);
    EXPECT_EQ(fields[1].at("valueType"), fields[2].at("valueType"));

    // Positional slots carry it too: the parameter object, and the return on
    // the method (a return type has no wrapper object of its own).
    const nlohmann::json method = j.at("methods").at(0);
    EXPECT_EQ(method.at("params").at(0).at("isOptional"), true);
    EXPECT_EQ(method.at("params").at(0).at("valueType"), tstr);
    EXPECT_EQ(method.at("returnIsOptional"), true);
    EXPECT_EQ(method.at("returnValueType").at("kind"), "named");
    EXPECT_EQ(method.at("returnValueType").at("name"), "Account");
}

TEST(CAbi, DerivedOptionalityIsOutputOnly)
{
    // The derived keys are ignored on the way back in — `type`/`optional` are
    // what rebuild the AST — so each spelling survives .lidl -> json -> .lidl
    // as written.
    char* err = nullptr;
    char* json = lidl_parse_to_json(kOptional, &err);
    ASSERT_NE(json, nullptr) << (err ? err : "(no error)");

    char* lidlText = lidl_serialize_from_json(json, &err);
    ASSERT_NE(lidlText, nullptr) << (err ? err : "(no error)");
    const std::string text(lidlText);
    EXPECT_NE(text.find("? flagged: tstr"), std::string::npos) << text;
    EXPECT_NE(text.find("typed: ? tstr"), std::string::npos) << text;

    char* json2 = lidl_parse_to_json(lidlText, &err);
    ASSERT_NE(json2, nullptr) << (err ? err : "(no error)");
    EXPECT_STREQ(json, json2);

    lidl_free_string(json);
    lidl_free_string(lidlText);
    lidl_free_string(json2);
}

TEST(CAbi, ValidateJsonReportsOptionalWarnings)
{
    char* err = nullptr;
    char* json = lidl_parse_to_json(
        "module m {\n  depends []\n  method f(m: {?tstr: int}, y: ?any) -> bool\n}\n", &err);
    ASSERT_NE(json, nullptr) << (err ? err : "(no error)");

    char* report = lidl_validate_json(json);
    ASSERT_NE(report, nullptr);
    const std::string r(report);
    EXPECT_NE(r.find("map key"), std::string::npos) << r;
    EXPECT_NE(r.find("Optional 'any'"), std::string::npos) << r;

    lidl_free_string(json);
    lidl_free_string(report);
}

// --- Identity methods -------------------------------------------------------
//
// `name()` and `version()` are derived from the module declaration, never
// hand-written. The pass runs on BOTH the provider and the consumer side, so
// these tests pin the properties that make the two sides agree: it is
// idempotent, it appends (never reorders), it defers to an author who
// implements the method, and it refuses a reserved name used for anything else.

namespace {

ModuleDecl parseOk(const std::string& src)
{
    ParseResult r = parse(src);
    EXPECT_FALSE(r.hasError()) << r.error;
    return r.module;
}

const MethodDecl* findMethod(const ModuleDecl& m, const std::string& name)
{
    for (const MethodDecl& md : m.methods)
        if (md.name == name) return &md;
    return nullptr;
}

} // namespace

TEST(Identity, InjectsBothMethodsWithTheIdentitySignature)
{
    ModuleDecl m = parseOk("module m {\n  depends []\n  method work() -> bool\n}\n");
    ASSERT_EQ(m.methods.size(), 1u);

    const IdentityInjection r = injectIdentityMethods(m);
    EXPECT_FALSE(r.hasError()) << r.error;
    EXPECT_TRUE(r.addedName);
    EXPECT_TRUE(r.addedVersion);

    ASSERT_EQ(m.methods.size(), 3u);
    // Appended, so an existing method keeps its position.
    EXPECT_EQ(m.methods[0].name, "work");

    for (const char* n : { "name", "version" }) {
        const MethodDecl* md = findMethod(m, n);
        ASSERT_NE(md, nullptr) << n;
        EXPECT_TRUE(md->params.empty());
        EXPECT_EQ(md->returnType.kind, TypeExpr::Primitive);
        EXPECT_EQ(md->returnType.name, "tstr");
        EXPECT_FALSE(md->description.empty()) << n;
        EXPECT_TRUE(md->derived) << n;
    }
}

TEST(Identity, IsIdempotent)
{
    // A backend must never have to track whether it injected yet.
    ModuleDecl m = parseOk("module m {\n  depends []\n  method work() -> bool\n}\n");
    injectIdentityMethods(m);
    const ModuleDecl once = m;

    const IdentityInjection second = injectIdentityMethods(m);
    EXPECT_FALSE(second.hasError()) << second.error;
    EXPECT_FALSE(second.addedName);
    EXPECT_FALSE(second.addedVersion);
    EXPECT_EQ(m.methods.size(), once.methods.size());
    EXPECT_TRUE(m == once);
}

TEST(Identity, DerivedMethodsNeverReachTheArtifact)
{
    // The published .lidl is the author's contract verbatim. Serializing an
    // injected AST must produce the same text as serializing the raw one, so
    // no call site can leak the identity methods by injecting too early.
    const std::string src = "module m {\n  version \"1.0.0\"\n  depends []\n\n"
                            "  method work() -> bool\n}\n";
    ModuleDecl m = parseOk(src);
    const std::string before = serialize(m);
    injectIdentityMethods(m);

    EXPECT_EQ(serialize(m), before);
    EXPECT_EQ(serialize(m), src);

    // ...and a module whose only methods are derived emits no method block at
    // all, rather than a stray blank line.
    ModuleDecl empty = parseOk("module e {\n  depends []\n}\n");
    injectIdentityMethods(empty);
    EXPECT_EQ(serialize(empty).find("method "), std::string::npos) << serialize(empty);
}

TEST(Identity, DefersToAnAuthorWhoImplementsIt)
{
    // Real case: delivery_module declares `std::string name() const`, which
    // derives to exactly the identity signature. That is not a conflict --
    // the module simply owns the method.
    ModuleDecl m = parseOk(
        "module m {\n  depends []\n"
        "  method name() -> tstr description \"mine\"\n}\n");

    const IdentityInjection r = injectIdentityMethods(m);
    EXPECT_FALSE(r.hasError()) << r.error;
    EXPECT_FALSE(r.addedName);
    EXPECT_TRUE(r.addedVersion);

    EXPECT_EQ(findMethod(m, "name")->description, "mine");
    EXPECT_EQ(m.methods.size(), 2u);
    // The author owns it, so it is NOT derived: it stays in the published
    // contract and backends delegate to the impl for it.
    EXPECT_FALSE(findMethod(m, "name")->derived);
    EXPECT_TRUE(findMethod(m, "version")->derived);
    EXPECT_NE(serialize(m).find("method name()"), std::string::npos);
}

TEST(Identity, RejectsAReservedNameUsedForSomethingElse)
{
    // A `name` that takes an argument is a different method wearing a reserved
    // name. Silently keeping it would make every consumer generate a wrapper
    // whose shape contradicts the identity surface.
    ModuleDecl withParam = parseOk(
        "module m {\n  depends []\n  method name(which: tstr) -> tstr\n}\n");
    const IdentityInjection a = injectIdentityMethods(withParam);
    EXPECT_TRUE(a.hasError());
    EXPECT_NE(a.error.find("reserved"), std::string::npos) << a.error;

    ModuleDecl wrongReturn = parseOk(
        "module m {\n  depends []\n  method version() -> int\n}\n");
    const IdentityInjection b = injectIdentityMethods(wrongReturn);
    EXPECT_TRUE(b.hasError());
    EXPECT_NE(b.error.find("version"), std::string::npos) << b.error;
}

TEST(Identity, ParseAndSerializeStayFreeOfIt)
{
    // The pass is explicit, never folded into parse(): a published .lidl must
    // round-trip byte-identically, and must not carry two methods the author
    // did not write.
    const std::string src = "module m {\n  version \"1.0.0\"\n  depends []\n\n"
                            "  method work() -> bool\n}\n";
    const ModuleDecl m = parseOk(src);
    EXPECT_EQ(m.methods.size(), 1u);
    EXPECT_EQ(serialize(m), src);
    EXPECT_EQ(findMethod(m, "name"), nullptr);
}

TEST(Identity, NamesTheReservedSet)
{
    EXPECT_TRUE(isIdentityMethod("name"));
    EXPECT_TRUE(isIdentityMethod("version"));
    EXPECT_FALSE(isIdentityMethod("moduleVersion"));
    EXPECT_FALSE(isIdentityMethod(""));
}

TEST(CAbi, InjectIdentityMatchesTheCppPass)
{
    // The C ABI is what non-C++ SDKs call. It must produce exactly what the
    // C++ pass produces -- that identity is the whole reason it is exposed
    // rather than reimplemented per SDK.
    const char* src = "module m {\n  depends []\n  method work() -> bool\n}\n";

    ModuleDecl viaCpp = parseOk(src);
    injectIdentityMethods(viaCpp);

    char* err = nullptr;
    char* json = lidl_parse_to_json(src, &err);
    ASSERT_NE(json, nullptr) << (err ? err : "(no error)");
    char* injected = lidl_inject_identity_json(json, &err);
    ASSERT_NE(injected, nullptr) << (err ? err : "(no error)");

    EXPECT_EQ(nlohmann::json::parse(injected), nlohmann::json::parse(toJson(viaCpp)));

    lidl_free_string(json);
    lidl_free_string(injected);
}

TEST(CAbi, InjectIdentityReportsAReservedNameConflict)
{
    char* err = nullptr;
    char* json = lidl_parse_to_json(
        "module m {\n  depends []\n  method name(which: tstr) -> tstr\n}\n", &err);
    ASSERT_NE(json, nullptr) << (err ? err : "(no error)");

    char* injected = lidl_inject_identity_json(json, &err);
    EXPECT_EQ(injected, nullptr);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(std::string(err).find("reserved"), std::string::npos) << err;

    lidl_free_string(json);
    lidl_free_string(err);
}
