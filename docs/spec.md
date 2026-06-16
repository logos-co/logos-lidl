# LIDL — Logos Interface Definition Language

## Overall Description

LIDL is a small, language-neutral interface definition language for describing the
public contract of a Logos module: its identity and metadata, the named record
types it exchanges, the methods it can be called with, and the events it emits.

A module's contract is published as a `.lidl` text artifact. That artifact is the
**cross-language interchange** of the Logos module system: each SDK ships a codegen
backend that reads `.lidl` and produces native bindings — typed callers, event
subscribers, and provider dispatch — for its language (C++, Rust, …). A consumer can
generate everything it needs to call a module from that module's `.lidl` alone,
without building the module or sharing any source.

This component — `logos-lidl` — is the **frontend** for that interchange: it defines
the grammar and the in-memory model (the AST/IR), and provides the four operations
every backend builds on:

- **Tokenize** `.lidl` source into a token stream.
- **Parse** a token stream into a `ModuleDecl` AST.
- **Serialize** a `ModuleDecl` back to canonical `.lidl` text.
- **Validate** a `ModuleDecl` for semantic consistency.

It deliberately contains **no code generation** and **no type mapping** to any target
language. Those are the responsibility of each SDK's backend (e.g. C++ glue/clients in
`logos-cpp-generator`, Rust bindings in `logos-rust-sdk`). The frontend's only job is
to turn text into a validated, structured model — and back.

## Why LIDL exists

Logos modules are written in different languages and run in separate processes, yet any
module must be callable from any other without the caller building, linking, or sharing
source with the callee. That requires a single, neutral description of a module's
**callable surface** — the methods it answers, the events it emits, the record types it
exchanges, and its identity and dependencies — that every SDK can read and turn into
native bindings.

LIDL is that description. Its design constraints follow directly:

- **It models an *interface*, not just data.** A contract is methods + events + module
  metadata + dependencies, not a single data shape.
- **It is tiny and dependency-free.** The whole grammar is a closed, fixed set of
  constructs that any SDK can re-implement in a few hundred lines of its own standard
  library — so the frontend can live natively inside the C++ SDK, the Rust SDK, and any
  future one, with no shared runtime.
- **Its type vocabulary is deliberately conventional** (see below), so the types are
  familiar and map cleanly onto a compact binary encoding later.

### Why not just use CDDL?

LIDL's type vocabulary is borrowed straight from **CDDL** (Concise Data Definition
Language, [RFC 8610](https://www.rfc-editor.org/rfc/rfc8610)): the primitive names
`tstr`, `bstr`, `int`, `uint`, `float64`, `bool`, and `any` are CDDL prelude types, the
`;` line-comment syntax is CDDL's, and the `?` optional marker, `[…]` arrays, and
`{K: V}` maps are CDDL-shaped. LIDL is intentionally **CDDL-flavored** — the one addition
to the primitive set is `result` (a structured success/value/error), which CDDL has no
equivalent of. So the relationship is reuse, not avoidance.

What LIDL does **not** adopt is CDDL *as its grammar*. The decisive reason is a single
one:

**CDDL describes data, not interfaces.** CDDL is a schema language for the shape of a
single CBOR/JSON data item. It has no notion of a *method* (a named operation with a
parameter list and a return type), an *event* (a fire-and-forget signal), a *module* (a
named collection of operations), or *module metadata and dependencies* (`version`,
`category`, `depends`). Those — the interface layer — are exactly what a module contract
is *about*, and they are absent from CDDL. Expressing them in pure CDDL would mean
inventing ad-hoc conventions on top of it (encoding "this group is the request, that
group is the response"), i.e. reinventing an IDL inside a data-schema language.

## Definitions & Acronyms

| Term | Definition |
| ---- | ---------- |
| **LIDL** | Logos Interface Definition Language — the lightweight DSL specified here |
| **Module contract** | The full public surface of one module: metadata, types, methods, events |
| **`.lidl` artifact** | The canonical text form of a module contract; the cross-language interchange unit |
| **Frontend** | This component — the lexer, parser, AST, serializer, and validator. No codegen |
| **Backend** | A per-SDK code generator that consumes the AST and emits native bindings (lives outside this repo) |
| **AST / IR** | The in-memory model (`ModuleDecl` and its children) produced by parsing and consumed by backends |
| **`ModuleDecl`** | The AST node representing a complete module contract |
| **`TypeExpr`** | The AST node representing a type — a primitive, named record, array, map, or optional |
| **Roundtrip** | parse → serialize → parse, expected to be AST-stable and (from the first serialization) byte-stable |

## Domain Model

### The interchange pipeline

```
.lidl text
    │
    ▼
 tokenize()  ──hasError?──► LexResult.error (line, column)
    │
    ▼
 parse()     ──hasError?──► ParseResult.error (line, column)
    │
    ▼
 ModuleDecl (AST / IR)  ◄──────────────┐
    │                                  │
    ├──► validate() ──► ValidationResult { errors[], warnings[] }
    │                                  │
    └──► serialize() ──► canonical .lidl text ──(re-parse)──┘
                                       │
                                       ▼
                          consumed by per-SDK backends
                          (C++, Rust, …) — out of scope here
```

`parse()` runs `tokenize()` internally, so the common entry point is text → `ModuleDecl`
in one call. A lexer error short-circuits before parsing; both stages report a
human-readable message plus the 1-based line and column where the problem was found.

### A module contract

A LIDL document declares exactly one module. The contract has four parts:

1. **Metadata** — `version`, `description`, `category`, and a `depends` list of other
   module names. All optional; order-independent; each may appear in the body.
2. **Types** — named record types (`type Name { … }`) with typed, optionally-marked
   fields. These are the structured payloads methods and events exchange.
3. **Methods** — the call-half of the API: a name, a parameter list, and a return type.
4. **Events** — the subscribe-half of the API: a name and a parameter list (events are
   fire-and-forget and have no return type).

```
module wallet_module {
    version "1.0.0"
    description "Wallet operations"
    category "finance"
    depends [crypto_module]

    type Account {
        address: tstr
        balance: uint
        ? label: tstr          ; optional field
    }

    method createAccount(passphrase: tstr) -> tstr
    method getBalance(address: tstr) -> uint
    method listAccounts() -> [tstr]
    method transfer(from: tstr, to: tstr, amount: uint) -> result

    event onTransfer(from: tstr, to: tstr, amount: uint)
}
```

Comments start with `;` and run to the end of the line.

### Type system

Built-in primitive types — the leaves every composite is built from:

| LIDL type | Meaning |
| --------- | ------- |
| `tstr`    | Text string |
| `bstr`    | Binary data (byte string) |
| `int`     | Signed integer |
| `uint`    | Unsigned integer |
| `float64` | Double-precision floating point |
| `bool`    | Boolean |
| `result`  | Structured result (success / value / error) |
| `any`     | Untyped / dynamic value |

The primitive names (and the `;`, `?`, `[…]`, `{K: V}` syntax) are taken from CDDL
([RFC 8610](https://www.rfc-editor.org/rfc/rfc8610)); `result` is the one Logos-specific
addition. See [Why not just use CDDL?](#why-not-just-use-cddl) for why LIDL borrows
CDDL's type layer but is not CDDL.

How each primitive maps onto a concrete language type (`tstr` → `QString` vs.
`std::string` vs. a Rust `String`, etc.) is a **backend** concern and is intentionally
not part of this specification.

Composite type expressions, which nest arbitrarily:

- `[T]` — **array** of `T` (e.g. `[tstr]`, `[[int]]`).
- `{K: V}` — **map** from key type `K` to value type `V` (e.g. `{tstr: any}`).
- `? T` — **optional** `T` — the value may be absent.
- A bare identifier that names a `type` declared in the same module is a **named** type
  reference; any other identifier is treated as a primitive name by the parser and
  rejected later by the validator if it is not a known type.

A field may also be marked optional with a leading `?` before its name
(`? label: tstr`), which is equivalent in meaning to giving it an optional type.

### Reserved words are only structurally reserved

The keywords `module`, `type`, `method`, `event`, `version`, `description`,
`category`, and `depends` are reserved **only at the start of a declaration**. In a
*name position* — a module name, type name, method name, parameter name, event name, or
an entry in a `depends` list — they are ordinary identifiers. This is a deliberate
property of the grammar: a module may legitimately be named `module`, a method named
`method`, or a parameter named `version`, and such a document must parse and round-trip
unchanged. The grammar stays unambiguous because every name position is only reached
after a structural keyword has already been consumed.

### Semantic rules (validation)

Parsing accepts any grammatically well-formed document; **validation** enforces the
contract-level rules a backend can rely on. A module is valid when:

- The module name is non-empty.
- No two `type` definitions share a name, and no type name shadows a built-in
  primitive.
- No two `method` definitions share a name.
- No two `event` definitions share a name.
- No two parameters within the same method share a name.
- Every named type referenced anywhere (a field, parameter, or return type, at any
  nesting depth inside arrays/maps/optionals) resolves to a `type` declared in the
  module.

Validation returns a list of `errors` (and a reserved channel for `warnings`); it never
throws. A document can parse cleanly yet fail validation — e.g. referencing an undefined
type, or declaring a method twice.

## Workflows

### Publish a contract (producer side)

A module author writes (or generates from their implementation) a `.lidl` describing the
module's public surface, then ships it as an artifact alongside the built module. Other
languages and other teams consume that one file.

### Consume a contract (consumer side)

A consumer takes a dependency's `.lidl`, runs it through `parse()` to get a `ModuleDecl`,
optionally `validate()`s it, and hands the AST to its own SDK's backend to emit typed
callers and event subscribers — all without building the dependency.

### Roundtrip / normalization

`serialize(parse(text))` produces a **canonical** rendering of the contract. The model
guarantees:

- **AST-stable** — re-parsing serialized output yields an equal `ModuleDecl`.
- **Byte-stable from the first serialization onward** — serializing that re-parsed model
  produces identical text. (The very first input need not already be canonical — e.g.
  whitespace and comments are not preserved — but everything downstream is fixed.)

This makes serialization usable for normalizing hand-written `.lidl`, for diffing
contracts, and as the reference oracle in roundtrip tests.

## Functional Requirements

- **FR-1 — Lexing.** Convert `.lidl` source into a token stream covering the eight
  keywords, identifiers, double-quoted string literals (with `\n`, `\t`, `\\`, `\"`
  escapes), the structural symbols, and end-of-input. `;` begins a line comment.
  Non-ASCII bytes are transparent inside string literals. On a lexical error (e.g. an
  unterminated string, an unexpected character) report a message with 1-based line and
  column.
- **FR-2 — Parsing.** Recursive-descent parse the token stream into a single
  `ModuleDecl`, supporting metadata, named record types with optional fields, methods
  with parameter lists and return types, events with parameter lists, and arbitrarily
  nested array / map / optional / named / primitive type expressions. Reject trailing
  content after the module's closing brace. Honor the structurally-reserved-keyword rule.
  Report the first error with line and column.
- **FR-3 — Validation.** Given a `ModuleDecl`, return all semantic errors per the rules
  in *Semantic rules* above, without throwing.
- **FR-4 — Serialization.** Render a `ModuleDecl` to canonical `.lidl` text that
  re-parses to an equal model and is byte-stable on subsequent serialization.
- **FR-5 — Language neutrality.** Contain no target-language type mapping and no code
  generation. The frontend's outputs (token stream, AST, serialized text, validation
  result) are the entire public surface; all binding generation lives in per-SDK
  backends.
- **FR-6 — No dependencies.** The contract model and all four operations are expressible
  in the standard library of the host language with no third-party or framework
  dependencies (so the same frontend can be embedded anywhere a backend runs).

## Out of Scope

- Code generation, and any mapping from LIDL types to concrete language types — these
  belong to each SDK's backend.
- Transport, serialization-on-the-wire, and the runtime RPC mechanism used to actually
  invoke a module.
- Versioning / compatibility policy between two revisions of a contract.
- Generics or parameterized types, type inheritance/extension, and default parameter
  values — the language has none of these by design.
