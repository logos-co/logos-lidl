#include <gtest/gtest.h>

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
