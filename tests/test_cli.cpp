#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "lidl/cli.hpp"
#include "lidl/identity.hpp"
#include "lidl/lidl_c.h"
#include "lidl/parser.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Already canonical: `lidl fmt` must reproduce it byte for byte.
const char* kCliDoc =
    "module cli_module {\n"
    "  version \"2.0.0\"\n"
    "  description \"A \\\"quoted\\\" module\\nover two lines\"\n"
    "  category \"testing\"\n"
    "  depends [storage_module]\n"
    "  optional_depends [waku_module]\n"
    "\n"
    "  type Entry {\n"
    "    id: tstr\n"
    "    ? note: tstr\n"
    "    tags: ? [tstr]\n"
    "    meta: {tstr: any}\n"
    "  }\n"
    "\n"
    "  method put(entry: Entry, data: bstr) -> result description \"Stores an entry.\"\n"
    "  method list(prefix: ? tstr) -> [Entry]\n"
    "  method notify()\n"
    "\n"
    "  event stored(id: tstr, size: uint) description \"Fires after put.\"\n"
    "}\n";

const char* kBroken = "module m {\n  method broken( -> int\n}\n";

struct CliResult {
    int code = -1;
    std::string out;
    std::string err;
};

CliResult runLidl(const std::vector<std::string>& args, const std::string& stdinText = std::string())
{
    std::vector<const char*> argv{"lidl"};
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    std::istringstream in(stdinText);
    std::ostringstream out, err;
    CliResult r;
    r.code = lidl::runCli(static_cast<int>(argv.size()) - 1, argv.data(), in, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

std::string joined(const std::vector<std::string>& args)
{
    std::string s = "lidl";
    for (const std::string& a : args) s += " " + a;
    return s;
}

std::string writeTemp(const std::string& name, const std::string& text)
{
    const std::string path = ::testing::TempDir() + "lidl_cli_" + name;
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

// Takes ownership of a lidl_c result and its error string.
std::string adopt(char* result, char* error)
{
    EXPECT_EQ(error, nullptr) << (error ? error : "");
    std::string s = result ? result : "";
    lidl_free_string(result);
    lidl_free_string(error);
    return s;
}

std::string cParseToJson(const std::string& source)
{
    char* error = nullptr;
    char* json = lidl_parse_to_json(source.c_str(), &error);
    return adopt(json, error);
}

std::string cInjectIdentity(const std::string& json)
{
    char* error = nullptr;
    char* injected = lidl_inject_identity_json(json.c_str(), &error);
    return adopt(injected, error);
}

std::string injectionError(const std::string& source)
{
    lidl::ParseResult pr = lidl::parse(source);
    EXPECT_FALSE(pr.hasError()) << pr.error;
    return lidl::injectIdentityMethods(pr.module).error;
}

const nlohmann::json* findMethod(const nlohmann::json& doc, const std::string& name)
{
    for (const nlohmann::json& m : doc.at("methods"))
        if (m.at("name") == name) return &m;
    return nullptr;
}

} // namespace

TEST(Cli, JsonMatchesTheCAbi)
{
    const std::string path = writeTemp("json.lidl", kCliDoc);
    const CliResult r = runLidl({"json", path});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "");
    EXPECT_EQ(r.out, cParseToJson(kCliDoc) + "\n");
    EXPECT_EQ(findMethod(nlohmann::json::parse(r.out), "lidl"), nullptr);
}

TEST(Cli, JsonIdentityMatchesTheCAbiAndAddsLidl)
{
    const std::string path = writeTemp("identity.lidl", kCliDoc);
    const CliResult r = runLidl({"json", "--identity", path});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "");
    EXPECT_EQ(r.out, cInjectIdentity(cParseToJson(kCliDoc)) + "\n");

    const nlohmann::json doc = nlohmann::json::parse(r.out);
    for (const char* name : {"name", "version", "lidl"}) {
        const nlohmann::json* m = findMethod(doc, name);
        ASSERT_NE(m, nullptr) << name;
        EXPECT_EQ(m->at("derived"), true) << name;
        EXPECT_TRUE(m->at("params").empty()) << name;
        EXPECT_EQ(m->at("returnType").at("name"), "tstr") << name;
    }
    // Appended after the authored methods, which keep their order.
    EXPECT_EQ(doc.at("methods").back().at("name"), "lidl");
    EXPECT_EQ(doc.at("methods").at(0).at("name"), "put");
}

TEST(Cli, JsonPrettyIsTheSameDocumentIndented)
{
    const CliResult compact = runLidl({"json", "-"}, kCliDoc);
    const CliResult pretty = runLidl({"json", "--pretty", "-"}, kCliDoc);
    ASSERT_EQ(compact.code, 0);
    ASSERT_EQ(pretty.code, 0);
    EXPECT_EQ(pretty.out, nlohmann::json::parse(compact.out).dump(2) + "\n");
    EXPECT_GT(std::count(pretty.out.begin(), pretty.out.end(), '\n'), 1);
}

TEST(Cli, JsonStdoutIsExactlyOneDocumentAndANewline)
{
    for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
             {"json", "-"}, {"json", "--identity", "-"}, {"json", "--pretty", "--identity", "-"}}) {
        const CliResult r = runLidl(args, kCliDoc);
        ASSERT_EQ(r.code, 0) << joined(args);
        EXPECT_EQ(r.err, "") << joined(args);
        ASSERT_GE(r.out.size(), 2u);
        EXPECT_EQ(r.out.back(), '\n');
        EXPECT_NE(r.out[r.out.size() - 2], '\n');
        // nlohmann's strict parse rejects anything after the first value.
        EXPECT_TRUE(nlohmann::json::parse(r.out).is_object()) << joined(args);
    }
    const CliResult compact = runLidl({"json", "-"}, kCliDoc);
    EXPECT_EQ(std::count(compact.out.begin(), compact.out.end(), '\n'), 1);
}

TEST(Cli, ParseErrorIsFileLineColumnMessage)
{
    const std::string path = writeTemp("broken.lidl", kBroken);
    const CliResult r = runLidl({"json", path});
    EXPECT_EQ(r.code, 3);
    EXPECT_EQ(r.out, "");
    EXPECT_EQ(r.err, path + ":2:18: Expected parameter name\n");

    for (const char* cmd : {"json", "check", "fmt"}) {
        const CliResult s = runLidl({cmd, "-"}, kBroken);
        EXPECT_EQ(s.code, 3) << cmd;
        EXPECT_EQ(s.out, "") << cmd;
        EXPECT_EQ(s.err, "<stdin>:2:18: Expected parameter name\n") << cmd;
    }

    // Lexer errors take the same path.
    const CliResult lex = runLidl({"fmt", "-"}, "module m {\n  @\n}\n");
    EXPECT_EQ(lex.code, 3);
    EXPECT_EQ(lex.err, "<stdin>:2:3: Unexpected character '@'\n");
}

TEST(Cli, IdentityRejectsAnAuthoredLidlMethod)
{
    const std::string src = "module m {\n  depends []\n  method lidl() -> tstr\n}\n";
    const std::string message = injectionError(src);
    ASSERT_NE(message.find("generator-owned"), std::string::npos) << message;

    for (const char* cmd : {"json", "check"}) {
        const CliResult r = runLidl({cmd, "--identity", "-"}, src);
        EXPECT_EQ(r.code, 4) << cmd;
        EXPECT_EQ(r.out, "") << cmd;
        EXPECT_EQ(r.err, "<stdin>: " + message + "\n") << cmd;
    }
    // Without --identity nothing applies the built-in rules.
    EXPECT_EQ(runLidl({"json", "-"}, src).code, 0);
}

TEST(Cli, IdentityRejectsAReservedSignature)
{
    const std::string src = "module m {\n  depends []\n  method version() -> result\n}\n";
    const std::string message = injectionError(src);
    ASSERT_NE(message.find("reserved for module identity"), std::string::npos) << message;

    const CliResult r = runLidl({"json", "--identity", "-"}, src);
    EXPECT_EQ(r.code, 4);
    EXPECT_EQ(r.out, "");
    EXPECT_EQ(r.err, "<stdin>: " + message + "\n");
}

TEST(Cli, NoReturnMethodOmitsTheReturnKeys)
{
    const CliResult r = runLidl({"json", "-"},
        "module m {\n  depends []\n\n  method notify()\n  method legacy() -> void\n}\n");
    ASSERT_EQ(r.code, 0) << r.err;
    const nlohmann::json doc = nlohmann::json::parse(r.out);
    ASSERT_EQ(doc.at("methods").size(), 2u);
    for (const nlohmann::json& m : doc.at("methods")) {
        EXPECT_FALSE(m.contains("returnType")) << m.dump();
        EXPECT_FALSE(m.contains("returnIsOptional")) << m.dump();
        EXPECT_FALSE(m.contains("returnValueType")) << m.dump();
    }
}

TEST(Cli, CheckAcceptsNoReturnAndLegacyVoid)
{
    for (const char* src : {"module m {\n  depends []\n  method f()\n}\n",
                            "module m {\n  depends []\n  method f() -> void\n}\n"}) {
        for (const char* flag : {"--json", "--identity"}) {
            const CliResult r = runLidl({"check", flag, "-"}, src);
            EXPECT_EQ(r.code, 0) << flag << "\n" << src << r.err;
            EXPECT_EQ(r.err, "") << flag << "\n" << src;
        }
        const CliResult plain = runLidl({"check", "-"}, src);
        EXPECT_EQ(plain.code, 0) << src << plain.err;
        EXPECT_EQ(plain.out, "");
        EXPECT_EQ(plain.err, "");
    }
}

TEST(Cli, CheckFailsAVoidParameter)
{
    const CliResult r = runLidl({"check", "-"},
        "module m {\n  depends []\n  method f(x: void) -> bool\n}\n");
    EXPECT_EQ(r.code, 5);
    EXPECT_EQ(r.out, "");
    EXPECT_EQ(r.err, "<stdin>: error: Unknown type 'void'\n");
}

TEST(Cli, CheckReportsWarningsWithoutFailing)
{
    const CliResult r = runLidl({"check", "-"},
        "module m {\n  depends []\n  type T { ? both: ?tstr }\n  method f(t: T) -> bool\n}\n");
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.out, "");
    EXPECT_EQ(r.err.rfind("<stdin>: warning: Field 'both' of type 'T' is marked optional twice", 0), 0u)
        << r.err;
    EXPECT_EQ(std::count(r.err.begin(), r.err.end(), '\n'), 1);
}

TEST(Cli, CheckJsonMatchesTheCAbi)
{
    const std::string src =
        "module m {\n  depends []\n  type T { ? both: ?tstr }\n  method f(x: void, t: T) -> bool\n}\n";
    const CliResult r = runLidl({"check", "--json", "-"}, src);
    EXPECT_EQ(r.code, 5);
    EXPECT_EQ(r.err, "");

    char* report = lidl_validate_json(cParseToJson(src).c_str());
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(r.out, std::string(report) + "\n");
    lidl_free_string(report);

    const nlohmann::json doc = nlohmann::json::parse(r.out);
    EXPECT_EQ(doc.at("errors"), nlohmann::json::array({"Unknown type 'void'"}));
    EXPECT_EQ(doc.at("warnings").size(), 1u);

    const CliResult clean = runLidl({"check", "--json", "--identity", "-"}, kCliDoc);
    EXPECT_EQ(clean.code, 0);
    EXPECT_EQ(clean.out, "{\"errors\":[],\"warnings\":[]}\n");
    EXPECT_EQ(clean.err, "");
}

TEST(Cli, FmtReproducesCanonicalTextByteForByte)
{
    const std::string path = writeTemp("canonical.lidl", kCliDoc);
    const CliResult r = runLidl({"fmt", path});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "");
    EXPECT_EQ(r.out, kCliDoc);
}

TEST(Cli, FmtCanonicalizesAndIsIdempotent)
{
    const CliResult first = runLidl({"fmt", "-"},
        "; not kept\nmodule m{version \"1\" method f()->void method g(x:?int)->bool depends[]}");
    ASSERT_EQ(first.code, 0) << first.err;
    EXPECT_EQ(first.out,
              "module m {\n  version \"1\"\n  depends []\n\n"
              "  method f()\n  method g(x: ? int) -> bool\n}\n");

    const CliResult second = runLidl({"fmt", "-"}, first.out);
    EXPECT_EQ(second.code, 0);
    EXPECT_EQ(second.out, first.out);
}

TEST(Cli, DashReadsStdin)
{
    const std::string path = writeTemp("stdin.lidl", kCliDoc);
    for (const char* cmd : {"json", "check", "fmt"}) {
        const CliResult fromFile = runLidl({cmd, path});
        const CliResult fromStdin = runLidl({cmd, "-"}, kCliDoc);
        EXPECT_EQ(fromStdin.code, 0) << cmd;
        EXPECT_EQ(fromStdin.code, fromFile.code) << cmd;
        EXPECT_EQ(fromStdin.out, fromFile.out) << cmd;
    }
}

TEST(Cli, UsageErrorsExitOne)
{
    const std::vector<std::vector<std::string>> cases = {
        {},
        {"frobnicate", "-"},
        {"--frobnicate"},
        {"json", "--frobnicate", "-"},
        {"json", "--json", "-"},
        {"check", "--pretty", "-"},
        {"fmt", "--identity", "-"},
        {"json"},
        {"check", "--json"},
        {"json", "a.lidl", "b.lidl"},
        {"--version", "extra"},
    };
    for (const std::vector<std::string>& args : cases) {
        const CliResult r = runLidl(args, kCliDoc);
        EXPECT_EQ(r.code, 1) << joined(args);
        EXPECT_EQ(r.out, "") << joined(args);
        EXPECT_NE(r.err.find("lidl --help"), std::string::npos) << joined(args) << "\n" << r.err;
    }
}

TEST(Cli, UnreadableInputExitsTwo)
{
    const std::string missing = ::testing::TempDir() + "lidl_cli_does_not_exist.lidl";
    const CliResult r = runLidl({"json", missing});
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.out, "");
    EXPECT_NE(r.err.find("'" + missing + "'"), std::string::npos) << r.err;

    // A directory opens, but reading it fails.
    const CliResult dir = runLidl({"fmt", ::testing::TempDir()});
    EXPECT_EQ(dir.code, 2) << dir.err;
    EXPECT_EQ(dir.out, "");

    // After "--" a leading dash is a file name, not an option.
    EXPECT_EQ(runLidl({"json", "--", "-missing.lidl"}).code, 2);
}

TEST(Cli, InvalidUtf8CannotBecomeJson)
{
    const std::string src = "module m {\n  description \"\xff\"\n  depends []\n}\n";
    const CliResult r = runLidl({"json", "-"}, src);
    EXPECT_EQ(r.code, 2);
    EXPECT_EQ(r.out, "");
    EXPECT_EQ(r.err.rfind("<stdin>: cannot encode as JSON: ", 0), 0u) << r.err;

    // fmt stays byte-transparent, like serialize().
    const CliResult fmt = runLidl({"fmt", "-"}, src);
    EXPECT_EQ(fmt.code, 0);
    EXPECT_EQ(fmt.out, src);
}

TEST(Cli, VersionAndHelp)
{
    const CliResult v = runLidl({"--version"});
    EXPECT_EQ(v.code, 0);
    EXPECT_EQ(v.out, "lidl " LOGOS_LIDL_VERSION " (" LOGOS_LIDL_GIT_REV ")\n");
    EXPECT_EQ(v.err, "");

    for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
             {"--help"}, {"-h"}, {"json", "--help"}}) {
        const CliResult h = runLidl(args);
        EXPECT_EQ(h.code, 0) << joined(args);
        EXPECT_EQ(h.out.rfind("usage: lidl ", 0), 0u) << joined(args);
        EXPECT_EQ(h.err, "") << joined(args);
    }
}
