# logos-lidl — Project Description

The language-neutral LIDL frontend: lexer, parser, AST/IR, serializer, and validator
for the Logos Interface Definition Language, plus the `lidl` command-line tool. The core
library is pure standard C++17 with **zero dependencies**. The C ABI (`logos_lidl_c`) and
the CLI add nlohmann/json, and the test suite uses GoogleTest.

Extracted from `logos-cpp-sdk`'s `cpp-generator/experimental/lidl_*` sources and
de-Qt'd — `QString`/`QVector` were replaced with `std::string`/`std::vector` — so the
frontend can be embedded by any SDK backend, not just the C++ one. The grammar is
unchanged from that origin, including structurally-reserved keywords being valid in name
positions.

## Project Structure

```
logos-lidl/
├── include/lidl/                 # Public headers (the entire API surface)
│   ├── ast.hpp                   # AST/IR: TypeExpr, FieldDecl, ParamDecl, MethodDecl,
│   │                             #         EventDecl, TypeDecl, ModuleDecl
│   ├── cli.hpp                   # runCli() (not installed)
│   ├── identity.hpp              # injectIdentityMethods(): name(), version(), lidl()
│   ├── json.hpp                  # toJson() / moduleFromJson(): the JSON AST
│   ├── lexer.hpp                 # Token, LexResult, tokenize()
│   ├── lidl_c.h                  # C ABI over the JSON AST
│   ├── parser.hpp                # ParseResult, parse()
│   ├── serializer.hpp            # serialize()
│   └── validator.hpp             # ValidationResult, validate()
├── src/                          # Implementations
│   ├── cli.cpp                   # The lidl commands: json, check, fmt
│   ├── identity.cpp              # Derived built-in methods
│   ├── json.cpp                  # AST <-> JSON (nlohmann)
│   ├── lexer.cpp                 # Tokenizer
│   ├── lidl_c.cpp                # C ABI
│   ├── parser.cpp                # Recursive-descent parser (parse() runs tokenize())
│   ├── serializer.cpp            # ModuleDecl → canonical .lidl text
│   └── validator.cpp             # Semantic checks over a ModuleDecl
├── tools/
│   └── lidl_main.cpp             # main() for bin/lidl
├── tests/
│   ├── test_cli.cpp              # CLI tests, run in-process on string streams
│   └── test_lidl.cpp             # GoogleTest suite (lexer, parser, roundtrip, validator)
├── CMakeLists.txt                # Static libs, bin/lidl, install/export, GTest-discovered tests
├── flake.nix                     # Nix build; `nix build` builds everything and runs tests
├── flake.lock
└── README.md
```

Everything in `include/lidl/` is the public API; `src/` holds the implementations, with
parser/validator internals (the `Parser`/`Validator` classes, the builtin-type set) kept
in anonymous namespaces in the `.cpp` files.

## Technology Stack

- **Language:** C++17. The core library uses the standard library only (`<string>`,
  `<vector>`, `<sstream>`, `<unordered_map>`, `<unordered_set>`, `<cctype>`).
- **Build:** CMake ≥ 3.14, Ninja. Produces the static libraries `logos_lidl` and
  `logos_lidl_c`, both position-independent so they can be linked into shared objects,
  and the `lidl` executable.
- **Tests:** GoogleTest, discovered via `gtest_discover_tests` and run under CTest.
- **Packaging:** Nix flake. Input `logos-nix` (nixpkgs follows `logos-nix/nixpkgs`).
  - Outputs: `packages.{logos-lidl,default,tests}`, a matching `checks.tests` and a
    `devShells.default`, on the four standard systems (`{aarch64,x86_64}-{darwin,linux}`).
  - On those systems, `packages.lidl-cli` holds only `bin/lidl`, and `apps.{lidl,default}`
    run it.
  - `packages.x86_64-windows.*` is a cross build of the libraries only, without the CLI
    or the tests.
- **CI:** GitHub Actions (`.github/workflows/ci.yml`) on Ubuntu and macOS, using the
  DeterminateSystems Nix installer and the `logos-co` Cachix cache. The job runs
  `nix build .#checks.<system>.tests`, then `nix build .#lidl-cli` and
  `nix run .#lidl -- --version`.

> Note: `logos-lidl` is a standalone repo with its own flake and is **not currently
> registered in the workspace `ws` CLI / `flake.nix` / `dep-graph.nix`**. Build and test
> it directly with `nix` (see below) rather than via `ws build`/`ws test`.

## Components

### AST / IR (`ast.hpp`)

The shared data model. Every struct provides `operator==` (and `TypeExpr` also `!=`) so
backends and tests can compare contracts structurally.

- **`TypeExpr`** — a type expression. `kind ∈ {Primitive, Array, Map, Optional, Named}`;
  `name` holds the primitive or custom type name; `elements` holds the children — for
  `Array`: `[0]` = element type; for `Map`: `[0]` = key, `[1]` = value; for `Optional`:
  `[0]` = inner type. (`Primitive`/`Named` have no elements.)
- **`FieldDecl`** — a record field: `name`, `type`, `optional` flag. Both members hold
  the spelling *as written* — `? name: T` sets the flag, `name: ?T` makes the type an
  `Optional` — so never read either one alone; use the accessors below.
- **`ParamDecl`** — a parameter: `name`, `type`.
- **`MethodDecl`** — a method: `name`, `params`, optional `returnType` (absent means no
  `->` clause and no returned value), plus:
  - `description` — the method's doc text, written as a trailing `description "..."`
    clause (surfaced as the method's description by backends).
  - `jsonReturn` — the method returns a JSON-shaped value (`any`, `[any]` or a map; in C++,
    `LogosMap`/`LogosList`).
  - `resultReturn` — the method returns `result` (`StdLogosResult`).
  - `derived` — added by `injectIdentityMethods` rather than written by the author.
    `serialize()` omits these methods.
- **`EventDecl`** — an event: `name`, `params`, and a `description` (same role as on
  methods).
- **`TypeDecl`** — a named record type: `name`, `fields`.
- **`ModuleDecl`** — the whole contract: `name`, `version`, `description`, `category`,
  `depends[]`, `optional_depends[]`, `types[]`, `methods[]`, `events[]`.

#### Optionality accessors (`ast.hpp`)

`?T` is **two-state** — a value of `T`, or empty — and has two equivalent spellings for a
record field. These free functions are the one place they are reconciled, so no backend
re-derives optionality and the spellings cannot drift apart (see `docs/spec.md`,
*Optionality*):

```cpp
bool             lidl::typeIsOptional(const TypeExpr&);      // kind == Optional
const TypeExpr&  lidl::optionalValueType(const TypeExpr&);   // ?T -> T, ??T -> T, T -> T
bool             lidl::fieldIsOptional(const FieldDecl&);    // flag OR optional type
const TypeExpr&  lidl::fieldValueType(const FieldDecl&);     // value type, ? stripped
bool             lidl::paramIsOptional(const ParamDecl&);    // positional: type kind only
const TypeExpr&  lidl::paramValueType(const ParamDecl&);
```

`optionalValueType` strips *every* leading `Optional` layer — optionality is idempotent
under the two-state rule, so `??T` must not become a third state — and returns a
degenerate element-less `Optional` as-is rather than dereferencing it.

> Descriptions are part of the text grammar: a trailing `description "..."` on the
> module, on methods and on events. They survive a text round trip. `jsonReturn` and
> `resultReturn` are never written: `parse()` recomputes them from the return type, while a
> C++ impl-header parser in a backend sets them from the C++ return types.

### Lexer (`lexer.hpp` / `lexer.cpp`)

`LexResult tokenize(const std::string& source)`.

Produces a `std::vector<Token>` terminated by an `Eof` token. `Token::Type` covers:

- **Keywords:** `Module`, `TypeKw`, `Method`, `Event`, `Version`, `Description`,
  `Category`, `Depends`.
- **Literals:** `Ident`, `StringLit`.
- **Symbols:** `LBrace` `}` `RBrace`, `LParen` `RParen`, `LBracket` `RBracket`, `Colon`,
  `Comma`, `Arrow` (`->`), `Question` (`?`).
- **Special:** `Eof`, `Error`.

Details:

- Whitespace (space, tab, `\r`, `\n`) is skipped; `\n` advances the line counter.
- `;` starts a comment that runs to end of line.
- String literals are double-quoted and support the escapes `\n`, `\t`, `\\`, `\"`
  (any other escaped char passes through as itself). A newline inside a string, or EOF
  before the closing quote, is an "Unterminated string literal" error.
- Identifiers match `[A-Za-z_][A-Za-z0-9_]*`; a lookup against the keyword table
  reclassifies matching words. Non-ASCII bytes are only valid inside string literals.
- Every token carries 1-based `line`/`column`. On error, `LexResult.error` is set with
  `errorLine`/`errorColumn`; `hasError()` reports it.

### Parser (`parser.hpp` / `parser.cpp`)

`ParseResult parse(const std::string& source)` — runs `tokenize()` first; a lex error is
surfaced as a parse error with the same location. On success, `ParseResult.module` is the
`ModuleDecl`; on failure `error`/`errorLine`/`errorColumn` describe the first problem.

The parser is a hand-written recursive-descent `Parser` class (anonymous namespace). The
grammar:

```
module     = "module" NAME "{" body "}" EOF
body       = (metadata | type_def | method_def | event_def)*
metadata   = "version" STRING | "description" STRING | "category" STRING
           | "depends" "[" (NAME ("," NAME)*)? "]"
           | "optional_depends" "[" (NAME ("," NAME)*)? "]"
type_def   = "type" NAME "{" field* "}"
field      = "?"? NAME ":" type_expr
method_def = "method" NAME "(" params ")" ("->" type_expr)? ("description" STRING)?
event_def  = "event" NAME "(" params ")" ("description" STRING)?
params     = (NAME ":" type_expr ("," NAME ":" type_expr)*)?
type_expr  = "?" type_expr
           | "[" type_expr "]"
           | "{" type_expr ":" type_expr "}"
           | NAME                       ; builtin → Primitive, else → Named
NAME       = IDENT | any keyword token  ; keywords are reserved only structurally
```

The `atName()` helper implements the structurally-reserved-keyword rule: in any name
position, a keyword token is accepted as an identifier. In `type_expr`, a bare name is
classified `Primitive` if it is one of the eight builtins
(`tstr bstr int uint float64 bool result any`) and `Named` otherwise — unknown named
types are not rejected here but later by the validator.

The historical direct spelling `-> void` is accepted as migration input and stored as
an absent `returnType`; `void` is not part of `type_expr`, and serialization therefore
emits the canonical no-return form with no arrow.

### Validator (`validator.hpp` / `validator.cpp`)

`ValidationResult validate(const ModuleDecl& module)` — never throws; collects all
problems into `errors` and `warnings`. Errors: empty module name,
duplicate/builtin-shadowing type names, duplicate method names, duplicate event names,
duplicate parameter names within a method, unknown named-type references resolved
recursively through arrays, maps, and optionals (being inside an optional does not exempt
a type from resolution), and an optional in a **map key** position — a key has no empty
inhabitant. Warnings, for documents that are valid but risk reading as a third state: a
field marked optional twice (`? name: ?T`), a redundant nested optional (`??T`), and an
optional `any` (`any` already admits the empty value).

### Serializer (`serializer.hpp` / `serializer.cpp`)

`std::string serialize(const ModuleDecl& module)` — renders a `ModuleDecl` to canonical
`.lidl` text: two-space indentation, metadata emitted only when non-empty, a `depends
[...]` line always present, then types, then methods, then events. A method with an
absent `returnType` is emitted without an arrow. Type expressions
render recursively (`[T]`, `{K: V}`, `? T`). Output re-parses to an equal AST and is
byte-stable on the next serialization (see roundtrip tests). It emits descriptions,
escaping `\`, `"`, newline and tab. It does **not** emit comments or the
`jsonReturn`/`resultReturn` flags, and it skips `derived` methods, so injecting the
built-ins never changes the published text. `lidl fmt` prints exactly this output.

It also does **not** canonicalise between the two spellings of an optional field: `?
label: tstr` serializes back as `? label: tstr` and `note: ?tstr` as `note: ? tstr`, so a
contract survives a normalization pass as its author wrote it. (The *wire* encoder
described in `docs/spec.md` does canonicalise — that is a different layer.)

### CLI (`cli.hpp` / `cli.cpp`, `tools/lidl_main.cpp`)

`lidl::runCli(argc, argv, in, out, err)` implements `lidl json`, `lidl check` and
`lidl fmt` on top of the functions above. It takes its streams as parameters, so
`tests/test_cli.cpp` runs it in-process, and `tools/lidl_main.cpp` passes it the standard
streams. It is built as the static library `logos_lidl_cli`. Neither that library nor
`cli.hpp` is installed. The README lists the commands and exit codes.

## API

All functions live in `namespace lidl`. The full surface:

```cpp
#include "lidl/lexer.hpp"
#include "lidl/parser.hpp"
#include "lidl/serializer.hpp"
#include "lidl/validator.hpp"

lidl::LexResult        lidl::tokenize(const std::string& source);
lidl::ParseResult      lidl::parse(const std::string& source);   // tokenizes internally
std::string            lidl::serialize(const lidl::ModuleDecl& module);
lidl::ValidationResult lidl::validate(const lidl::ModuleDecl& module);

// ast.hpp — optionality, reconciled once for every backend (see above)
bool                   lidl::typeIsOptional(const lidl::TypeExpr&);
const lidl::TypeExpr&  lidl::optionalValueType(const lidl::TypeExpr&);
bool                   lidl::fieldIsOptional(const lidl::FieldDecl&);
const lidl::TypeExpr&  lidl::fieldValueType(const lidl::FieldDecl&);
bool                   lidl::paramIsOptional(const lidl::ParamDecl&);
const lidl::TypeExpr&  lidl::paramValueType(const lidl::ParamDecl&);
```

Result types:

- `LexResult { std::vector<Token> tokens; std::string error; int errorLine, errorColumn; bool hasError(); }`
- `ParseResult { ModuleDecl module; std::string error; int errorLine, errorColumn; bool hasError(); }`
- `ValidationResult { std::vector<std::string> errors, warnings; bool hasErrors(); }`

### Example: parse, validate, roundtrip

```cpp
#include "lidl/parser.hpp"
#include "lidl/validator.hpp"
#include "lidl/serializer.hpp"
#include <iostream>

int main() {
    const std::string src = R"(
        module wallet_module {
            version "1.0.0"
            depends [crypto_module]

            type Account { address: tstr  balance: uint  ? label: tstr }

            method getBalance(address: tstr) -> uint
            event onTransfer(from: tstr, to: tstr, amount: uint)
        }
    )";

    lidl::ParseResult pr = lidl::parse(src);
    if (pr.hasError()) {
        std::cerr << "parse error " << pr.errorLine << ":" << pr.errorColumn
                  << " — " << pr.error << "\n";
        return 1;
    }

    lidl::ValidationResult vr = lidl::validate(pr.module);
    for (const std::string& e : vr.errors) std::cerr << "invalid: " << e << "\n";
    if (vr.hasErrors()) return 1;

    // Canonical, byte-stable .lidl text.
    std::cout << lidl::serialize(pr.module);
    return 0;
}
```

### Example: build a contract in code

```cpp
lidl::ModuleDecl m;
m.name = "counter_module";

lidl::MethodDecl inc;
inc.name = "increment";
inc.returnType = lidl::TypeExpr{ lidl::TypeExpr::Primitive, "int", {} };
m.methods.push_back(inc);

std::string text = lidl::serialize(m);   // emit .lidl from an AST built by hand
```

## Building and Testing

`logos-lidl` has its own flake and is built directly with `nix` (it is not wired into the
workspace `ws` CLI):

```bash
cd repos/logos-lidl

# Build the static libraries, headers and bin/lidl, and run the test suite (doCheck = true).
nix build

# Run exactly the test check (what CI runs).
nix build ".#checks.$(nix eval --impure --raw --expr 'builtins.currentSystem').tests" -L

# Run the CLI.
nix run .#lidl -- --help

# Enter a dev shell with cmake, ninja, and gtest available, then build by hand.
nix develop
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake options:
- `LOGOS_LIDL_BUILD_TESTS` (default `ON`) builds the GoogleTest suite, which requires
  `find_package(GTest)`.
- `LOGOS_LIDL_BUILD_CLI` (default `ON`) builds `bin/lidl` and adds the CLI tests to the
  suite.
- `LOGOS_LIDL_GIT_REV` (default `unknown`) is the revision `lidl --version` prints. The
  flake sets it from `self.shortRev`, or from `self.dirtyShortRev` for a dirty tree.

### Test coverage (`tests/test_lidl.cpp`, `tests/test_cli.cpp`)

The suite parses, validates, and roundtrips a single canonical document
(`chat_module`, exercising metadata, an optional field, array and map field types, a
named-type method parameter, and an event):

| Test | What it checks |
| ---- | -------------- |
| `Lexer.TokenizesCanonicalDocument` | First token is `Module`, last is `Eof` |
| `Lexer.CommentsAndEscapes` | `;` comments skipped; `\"` escape unescaped in the literal |
| `Lexer.ErrorsOnUnterminatedString` | Unterminated string sets the error flag |
| `Parser.ParsesCanonicalDocument` | Metadata, types/fields (optional, array, map), methods, params, events parsed correctly |
| `Parser.KeywordsAreValidNames` | Keywords usable as module/method/param/event names (structural-reservation regression) |
| `Parser.ReportsErrorsWithLocation` | A malformed method reports an error with a line > 1 |
| `RoundTrip.SerializeParseStable` | parse → serialize → parse is AST-stable and byte-stable |
| `RoundTrip.OptionalAndNestedTypes` | A hand-built `? [ {tstr: any} ]` type survives serialize → parse |
| `Validator.CatchesDuplicatesAndUnknownTypes` | Unknown type reference and duplicate method both reported |
| `Validator.AcceptsCanonicalDocument` | A well-formed document produces no errors |

Optionality (`docs/spec.md`, *Optionality*) is covered by a second group, built around a
document that says the same field two ways (`? flagged: tstr` and `typed: ?tstr`) plus a
non-optional control:

| Test | What it checks |
| ---- | -------------- |
| `Optional.BothSpellingsParse` | Each spelling lands in the AST as written — neither is normalized into the other |
| `Optional.AccessorsAgreeAcrossSpellings` | `fieldIsOptional`/`fieldValueType` give one answer for both spellings |
| `Optional.PositionalSlotsUseTheTypeSpelling` | `paramIsOptional`/`paramValueType` and an optional return type |
| `Optional.IsIdempotent` | `? x: ?T` and `??T` collapse to one optional layer over `T` (two-state, never three) |
| `Optional.DegenerateOptionalIsNotDereferenced` | An `Optional` with no element (JSON-bridge reachable) does not walk off the end |
| `Optional.SerializerPreservesEachSpelling` | Both spellings survive serialize → parse verbatim, AST- and byte-stable |
| `Validator.AcceptsOptionalsAndKeepsCheckingThem` | Optionals are clean; an unknown type inside one is still an error (×3 slots) |
| `Validator.RejectsOptionalMapKey` | `{?tstr: int}` is one located error; an optional map *value* is fine |
| `Validator.WarnsOnRedundantOptionality` | Double-marked field, `??T`, and `?any` (both spellings) warn without erroring |
| `CAbi.JsonCarriesDerivedOptionality` | `isOptional`/`valueType` on fields and params, `returnIsOptional`/`returnValueType` on methods |
| `CAbi.DerivedOptionalityIsOutputOnly` | Derived keys are ignored on input, so each spelling survives .lidl → json → .lidl |
| `CAbi.ValidateJsonReportsOptionalWarnings` | The new errors and warnings cross the C ABI |

The CLI tests (`tests/test_cli.cpp`, built when `LOGOS_LIDL_BUILD_CLI` is on) call `runCli`
on string streams:

| Test | What it checks |
| ---- | -------------- |
| `Cli.JsonMatchesTheCAbi` / `Cli.JsonIdentityMatchesTheCAbiAndAddsLidl` | `json` prints exactly `lidl_parse_to_json`; with `--identity` it prints `lidl_inject_identity_json`, including the derived `lidl` |
| `Cli.JsonPrettyIsTheSameDocumentIndented` / `Cli.JsonStdoutIsExactlyOneDocumentAndANewline` | stdout holds one JSON document and a newline |
| `Cli.ParseErrorIsFileLineColumnMessage` | `<file>:<line>:<col>: <message>` (with `<stdin>` for `-`) and exit 3, for parse and lex errors |
| `Cli.IdentityRejectsAnAuthoredLidlMethod` / `Cli.IdentityRejectsAReservedSignature` | Exit 4 with the injection's own message |
| `Cli.NoReturnMethodOmitsTheReturnKeys` | No return keys for `method f()` or legacy `-> void` |
| `Cli.CheckAcceptsNoReturnAndLegacyVoid` / `Cli.CheckFailsAVoidParameter` | `check` passes both no-return spellings and fails `x: void` with exit 5 |
| `Cli.CheckReportsWarningsWithoutFailing` / `Cli.CheckJsonMatchesTheCAbi` | Warnings alone exit 0; `--json` prints exactly `lidl_validate_json` |
| `Cli.FmtReproducesCanonicalTextByteForByte` / `Cli.FmtCanonicalizesAndIsIdempotent` | Canonical text comes back unchanged; other text is canonicalized once |
| `Cli.DashReadsStdin` | `-` gives the same results as the file |
| `Cli.UsageErrorsExitOne` / `Cli.UnreadableInputExitsTwo` / `Cli.InvalidUtf8CannotBecomeJson` | Exit codes 1 and 2 |
| `Cli.VersionAndHelp` | `lidl <version> (<git rev>)` and the usage text |

## Relationship to the Logos SDKs

`logos-lidl` is the producer/consumer-neutral core. The full module-binding story lives
in the SDK backends that depend on it:

- A **producer** emits a module's `.lidl` (often generated from its implementation by an
  SDK tool — e.g. the C++ impl-header parser in `logos-cpp-generator`).
- A **consumer** parses a dependency's `.lidl` and runs its own SDK backend over the
  resulting `ModuleDecl` to generate typed callers, event subscribers, and provider
  dispatch — without building the dependency.

The mapping from LIDL primitives to concrete language types (`tstr` → `QString` /
`std::string` / Rust `String`, etc.), all code emission, and the runtime transport are
**not** part of this repo. See `logos-cpp-sdk`'s `cpp-generator/docs` for the C++
backend's end-to-end pipeline.

## Known Limitations

- **No code generation or type mapping.** By design — those live in the backends. This
  repo only goes text ↔ AST and validates the AST.
- **Comments and formatting are not preserved.** `serialize()` (and so `lidl fmt`)
  produces a canonical layout; `;` comments and original whitespace from hand-written
  input are lost on roundtrip (the AST is preserved, the exact bytes are not until after
  the first serialization).
- **Optionality is modelled and published, not yet consumed.** The frontend parses `?T`
  in either spelling, validates it, exposes the reconciled answer through the accessors
  and the JSON wire form, and `docs/spec.md` makes the wire semantics normative — but as
  of this change no SDK backend reads any of it. The C++ cdylib generator still rejects a
  `?T` outright as not cdylib-eligible, the Rust generator emits records with no
  optionality, and the Qt path flattens every type expression to a type-name string
  before optionality could be seen. Making a backend honor `?T` (Rust `Option<T>`, C++
  `std::optional<T>`) is separate, per-SDK work.
- **No generics, inheritance, or default parameter values.** The language has none.
- **One module per document.** A `.lidl` file declares exactly one module; content after
  the closing `}` is a parse error.
- **Validation is structural, not cross-module.** It resolves named types within the same
  module and catches duplicates; it does not verify that names in `depends` correspond to
  real modules or that referenced dependency contracts exist.
- **Parsing reports one error at a time.** The parser stops at and reports the first
  error; the validator, by contrast, accumulates all semantic errors.
- **Not registered in the workspace tooling.** Use `nix` directly, not `ws`.
