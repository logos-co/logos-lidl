#include "lidl/cli.hpp"

#include "lidl/identity.hpp"
#include "lidl/json.hpp"
#include "lidl/parser.hpp"
#include "lidl/serializer.hpp"
#include "lidl/validator.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#ifndef LOGOS_LIDL_VERSION
#define LOGOS_LIDL_VERSION "unknown"
#endif
#ifndef LOGOS_LIDL_GIT_REV
#define LOGOS_LIDL_GIT_REV "unknown"
#endif

namespace lidl {

namespace {

enum ExitCode {
    kOk = 0,
    kUsage = 1,
    kUnreadable = 2,
    kParseError = 3,
    kIdentityError = 4,
    kInvalid = 5,
};

const char* const kUsageText =
    "usage: lidl <command> [options] <file|->\n"
    "       lidl --version | --help\n"
    "\n"
    "commands:\n"
    "  json [--identity] [--pretty] <file|->   print the JSON AST\n"
    "  check [--identity] [--json] <file|->    validate the contract\n"
    "  fmt <file|->                            print the canonical .lidl text\n"
    "\n"
    "options:\n"
    "  --identity   add the derived built-ins name(), version() and lidl()\n"
    "  --pretty     indent the JSON AST\n"
    "  --json       print {\"errors\":[...],\"warnings\":[...]} on stdout\n"
    "\n"
    "'-' reads standard input.\n"
    "exit status: 0 ok, 1 usage, 2 unreadable input, 3 parse error,\n"
    "             4 identity error, 5 validation errors\n";

struct Options {
    std::string command;
    std::string input;
    bool identity = false;
    bool pretty = false;
    bool json = false;
};

int usageError(std::ostream& err, const std::string& message)
{
    err << "lidl: " << message << "\n"
        << "Try 'lidl --help' for more information.\n";
    return kUsage;
}

bool readStream(std::istream& in, std::string& data)
{
    char buf[4096];
    while (in.read(buf, sizeof buf) || in.gcount() > 0)
        data.append(buf, static_cast<size_t>(in.gcount()));
    return !in.bad();
}

// stdio rather than ifstream: fread reports a directory or I/O error that iostreams would hide.
bool readFile(const std::string& path, std::string& data, std::string& error)
{
    errno = 0;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = errno ? std::strerror(errno) : "cannot open";
        return false;
    }
    errno = 0;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
    const bool failed = std::ferror(f) != 0;
    const int readErrno = errno;
    std::fclose(f);
    if (failed) error = readErrno ? std::strerror(readErrno) : "read error";
    return !failed;
}

// Returns -1 when the arguments name a command to run, else the exit code.
int parseArgs(const std::vector<std::string>& args, Options& opts, std::ostream& out, std::ostream& err)
{
    if (args.empty()) return usageError(err, "missing command");

    const std::string& first = args[0];
    if (first == "--help" || first == "-h") {
        out << kUsageText;
        return kOk;
    }
    if (first == "--version") {
        if (args.size() > 1) return usageError(err, "unexpected argument '" + args[1] + "'");
        out << "lidl " LOGOS_LIDL_VERSION " (" LOGOS_LIDL_GIT_REV ")\n";
        return kOk;
    }
    if (first != "json" && first != "check" && first != "fmt") {
        if (first.size() > 1 && first[0] == '-') return usageError(err, "unknown option '" + first + "'");
        return usageError(err, "unknown command '" + first + "'");
    }
    opts.command = first;

    bool haveInput = false;
    bool endOfOptions = false;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (!endOfOptions && a.size() > 1 && a[0] == '-') {
            if (a == "--") { endOfOptions = true; continue; }
            if (a == "--help" || a == "-h") { out << kUsageText; return kOk; }
            if (a == "--identity" && opts.command != "fmt") { opts.identity = true; continue; }
            if (a == "--pretty" && opts.command == "json") { opts.pretty = true; continue; }
            if (a == "--json" && opts.command == "check") { opts.json = true; continue; }
            return usageError(err, "unknown option '" + a + "' for '" + opts.command + "'");
        }
        if (haveInput) return usageError(err, "unexpected argument '" + a + "'");
        opts.input = a;
        haveInput = true;
    }
    if (!haveInput)
        return usageError(err, "'" + opts.command + "' needs an input file, or '-' for stdin");
    return -1;
}

} // namespace

int runCli(int argc, const char* const* argv, std::istream& in, std::ostream& out, std::ostream& err)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i] ? argv[i] : "");

    Options opts;
    const int parsed = parseArgs(args, opts, out, err);
    if (parsed >= 0) return parsed;

    const bool fromStdin = opts.input == "-";
    const std::string displayName = fromStdin ? "<stdin>" : opts.input;
    std::string source;
    if (fromStdin) {
        if (!readStream(in, source)) {
            err << "lidl: cannot read <stdin>\n";
            return kUnreadable;
        }
    } else {
        std::string why;
        if (!readFile(opts.input, source, why)) {
            err << "lidl: cannot read '" << opts.input << "': " << why << "\n";
            return kUnreadable;
        }
    }

    ParseResult pr = parse(source);
    if (pr.hasError()) {
        err << displayName << ":" << pr.errorLine << ":" << pr.errorColumn << ": " << pr.error << "\n";
        return kParseError;
    }

    if (opts.identity) {
        const IdentityInjection injection = injectIdentityMethods(pr.module);
        if (injection.hasError()) {
            err << displayName << ": " << injection.error << "\n";
            return kIdentityError;
        }
    }

    if (opts.command == "fmt") {
        out << serialize(pr.module);
        return kOk;
    }

    ValidationResult vr;
    if (opts.command == "check") vr = validate(pr.module);

    if (opts.command == "json" || opts.json) {
        std::string text;
        // nlohmann refuses invalid UTF-8, which the byte-transparent lexer lets into strings.
        try {
            if (opts.command == "json") {
                text = toJson(pr.module, opts.pretty);
            } else {
                nlohmann::json report;
                report["errors"] = vr.errors;
                report["warnings"] = vr.warnings;
                text = report.dump();
            }
        } catch (const std::exception& e) {
            err << displayName << ": cannot encode as JSON: " << e.what() << "\n";
            return kUnreadable;
        }
        out << text << "\n";
    } else {
        for (const std::string& e : vr.errors) err << displayName << ": error: " << e << "\n";
        for (const std::string& w : vr.warnings) err << displayName << ": warning: " << w << "\n";
    }
    return vr.hasErrors() ? kInvalid : kOk;
}

} // namespace lidl
