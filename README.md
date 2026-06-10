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
nix build          # library + headers (runs tests)
```

API lives in `namespace lidl`: `tokenize`, `parse`, `serialize`, `validate`
over `lidl::ModuleDecl` (see `include/lidl/ast.hpp`).
