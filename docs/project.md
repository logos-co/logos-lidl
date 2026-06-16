# logos-lidl — Project Description

The language-neutral LIDL frontend: lexer, parser, AST/IR, serializer, and validator
for the Logos Interface Definition Language. Pure standard C++17 with **zero
dependencies** (the test suite uses GoogleTest, the library itself uses nothing beyond
the standard library).

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
│   ├── lexer.hpp                 # Token, LexResult, tokenize()
│   ├── parser.hpp                # ParseResult, parse()
│   ├── serializer.hpp            # serialize()
│   └── validator.hpp             # ValidationResult, validate()
├── src/                          # Implementations
│   ├── lexer.cpp                 # Tokenizer
│   ├── parser.cpp                # Recursive-descent parser (parse() runs tokenize())
│   ├── serializer.cpp            # ModuleDecl → canonical .lidl text
│   └── validator.cpp             # Semantic checks over a ModuleDecl
├── tests/
│   └── test_lidl.cpp             # GoogleTest suite (lexer, parser, roundtrip, validator)
├── CMakeLists.txt                # Static lib + install/export + GTest-discovered tests
├── flake.nix                     # Nix build; `nix build` builds the lib and runs tests
├── flake.lock
└── README.md
```

Everything in `include/lidl/` is the public API; `src/` holds the implementations, with
parser/validator internals (the `Parser`/`Validator` classes, the builtin-type set) kept
in anonymous namespaces in the `.cpp` files.

## Technology Stack

- **Language:** C++17, standard library only (`<string>`, `<vector>`, `<sstream>`,
  `<unordered_map>`, `<unordered_set>`, `<cctype>`).
- **Build:** CMake ≥ 3.14, Ninja. Produces a single static library `logos_lidl`.
- **Tests:** GoogleTest, discovered via `gtest_discover_tests` and run under CTest.
- **Packaging:** Nix flake. Input `logos-nix` (nixpkgs follows `logos-nix/nixpkgs`).
  Outputs `packages.{logos-lidl,default,tests}`, a matching `checks.tests`, and a
  `devShells.default`. Builds on the four standard systems
  (`{aarch64,x86_64}-{darwin,linux}`).
- **CI:** GitHub Actions (`.github/workflows/ci.yml`) on Ubuntu and macOS, using the
  DeterminateSystems Nix installer and the `logos-co` Cachix cache; the job runs
  `nix build .#checks.<system>.tests`.

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
- **`FieldDecl`** — a record field: `name`, `type`, `optional` flag.
- **`ParamDecl`** — a parameter: `name`, `type`.
- **`MethodDecl`** — a method: `name`, `params`, `returnType`, plus three fields that the
  text grammar does **not** populate but backends and richer producers do:
  - `description` — a doc comment associated with the method (surfaced as the method's
    description by backends).
  - `jsonReturn` — set by a backend when the implementation returns a JSON-shaped value
    (`LogosMap`/`LogosList`).
  - `resultReturn` — set when the implementation returns `StdLogosResult`.
- **`EventDecl`** — an event: `name`, `params`, and a `description` (same role as on
  methods).
- **`TypeDecl`** — a named record type: `name`, `fields`.
- **`ModuleDecl`** — the whole contract: `name`, `version`, `description`, `category`,
  `depends[]`, `types[]`, `methods[]`, `events[]`.

> The `description` / `jsonReturn` / `resultReturn` fields exist on the AST so a single
> IR can carry information richer than the surface syntax (e.g. a C++ impl-header parser
> in a backend can fill them). The LIDL **text** grammar in this repo neither emits nor
> parses them — `serialize()` does not render them and `parse()` leaves them at their
> defaults.

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
type_def   = "type" NAME "{" field* "}"
field      = "?"? NAME ":" type_expr
method_def = "method" NAME "(" params ")" "->" type_expr
event_def  = "event" NAME "(" params ")"
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

### Validator (`validator.hpp` / `validator.cpp`)

`ValidationResult validate(const ModuleDecl& module)` — never throws; collects all
problems into `errors` (with a reserved `warnings` channel). Checks: empty module name,
duplicate/builtin-shadowing type names, duplicate method names, duplicate event names,
duplicate parameter names within a method, and unknown named-type references resolved
recursively through arrays, maps, and optionals.

### Serializer (`serializer.hpp` / `serializer.cpp`)

`std::string serialize(const ModuleDecl& module)` — renders a `ModuleDecl` to canonical
`.lidl` text: two-space indentation, metadata emitted only when non-empty, a `depends
[...]` line always present, then types, then methods, then events. Type expressions
render recursively (`[T]`, `{K: V}`, `? T`). Output re-parses to an equal AST and is
byte-stable on the next serialization (see roundtrip tests). It does **not** emit
comments, `description`s, or the `jsonReturn`/`resultReturn` flags.

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

# Build the static library + headers and run the test suite (doCheck = true).
nix build

# Run exactly the test check (what CI runs).
nix build ".#checks.$(nix eval --impure --raw --expr 'builtins.currentSystem').tests" -L

# Enter a dev shell with cmake, ninja, and gtest available, then build by hand.
nix develop
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake options: `LOGOS_LIDL_BUILD_TESTS` (default `ON`) toggles building the GoogleTest
suite (which requires `find_package(GTest)`).

### Test coverage (`tests/test_lidl.cpp`)

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
- **Comments and formatting are not preserved.** `serialize()` produces a canonical
  layout; `;` comments and original whitespace from hand-written input are lost on
  roundtrip (the AST is preserved, the exact bytes are not until after the first
  serialization).
- **`description` / `jsonReturn` / `resultReturn` are not part of the text grammar.** They
  exist on the AST for backends to populate, but `parse()` never sets them and
  `serialize()` never emits them — so they do not survive a text roundtrip.
- **No generics, inheritance, or default parameter values.** The language has none.
- **One module per document.** A `.lidl` file declares exactly one module; content after
  the closing `}` is a parse error.
- **Validation is structural, not cross-module.** It resolves named types within the same
  module and catches duplicates; it does not verify that names in `depends` correspond to
  real modules or that referenced dependency contracts exist.
- **Parsing reports one error at a time.** The parser stops at and reports the first
  error; the validator, by contrast, accumulates all semantic errors.
- **Not registered in the workspace tooling.** Use `nix` directly, not `ws`.
