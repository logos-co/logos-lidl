# logos-lidl

The **language-neutral LIDL frontend**: lexer, parser, AST/IR, serializer and
validator for the Logos Interface Definition Language. Pure standard C++ with
zero dependencies.

`.lidl` text is the cross-language interchange of the Logos module system:
modules publish their contract as a `.lidl` artifact, and each SDK provides a
codegen backend over this frontend — C++ (`logos-cpp-generator`), Rust
(`logos-rust-sdk`), etc. — to produce typed callers, event subscribers and
provider dispatch without building the dependency module.

Extracted from `logos-cpp-sdk`'s `cpp-generator/experimental/lidl_*` and
de-Qt'd (`QString`/`QVector` → `std::string`/`std::vector`); same grammar,
including structurally-reserved keywords being valid in name positions.

## Building

```bash
nix build          # libraries, headers and bin/lidl (runs tests)
```

API lives in `namespace lidl`: `tokenize`, `parse`, `serialize`, `validate`
over `lidl::ModuleDecl` (see `include/lidl/ast.hpp`).

## The `lidl` CLI

```bash
nix run .#lidl -- json --identity my_module.lidl   # JSON AST, built-ins included
nix run .#lidl -- fmt my_module.lidl               # canonical .lidl text
nix build .#lidl-cli                               # just bin/lidl
```

| Command | Prints |
|---|---|
| `lidl json [--identity] [--pretty] <file\|->` | The JSON AST (the `lidl_parse_to_json` form) and one newline, and nothing else on stdout. `--identity` appends the derived `name()`, `version()` and `lidl()` built-ins. |
| `lidl check [--identity] [--json] <file\|->` | Each validation error and warning on its own stderr line. With `--json`, `{"errors":[...],"warnings":[...]}` goes to stdout instead. |
| `lidl fmt <file\|->` | The canonical text that every published contract goes through. Canonical input comes back byte for byte, so `lidl fmt X \| cmp - X` checks it. Legacy `-> void` becomes a method with no return clause. |
| `lidl --version` | `lidl <version> (<git revision>)` |
| `lidl --help` | Usage |

`-` reads stdin. Diagnostics go to stderr. A parse error is printed as
`<file>:<line>:<col>: <message>`, with `<stdin>` as the file name for `-`.

| Exit code | Meaning |
|---|---|
| 0 | OK. Warnings alone do not fail `check`. |
| 1 | Usage error |
| 2 | Unreadable input, including text `json` cannot encode (invalid UTF-8) |
| 3 | Parse error |
| 4 | Identity error with `--identity`: an authored `lidl()`, or `name()`/`version()` with a different signature |
| 5 | `check` found validation errors |

The package is called `lidl-cli` rather than `lidl` because module flakes use
`packages.<system>.lidl` for a module's contract.
