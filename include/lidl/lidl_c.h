#ifndef LIDL_C_H
#define LIDL_C_H

/* C ABI over the logos-lidl frontend — the JSON bridge that lets non-C++
 * SDKs (Rust, ...) reach the canonical parser / serializer / validator
 * without reimplementing the grammar. AST is exchanged in the JSON wire form
 * produced by lidl/json.hpp.
 *
 * String ownership: every char* returned by a function below is malloc'd and
 * must be released with lidl_free_string (mirrors the liblogos_core idiom).
 *
 * Optionality across this boundary: a field can be written `? name: T` or
 * `name: ?T` and the two mean the same thing, so the JSON carries the
 * frontend's own reconciliation and a backend must not compute its own.
 * Read `isOptional` + `valueType` on every field and parameter object, and
 * `returnIsOptional` + `returnValueType` on every method object. The raw
 * `optional` flag and `type` are the verbatim spelling, kept only so the wire
 * form round-trips; do not decide optionality from them. The derived keys are
 * output-only and are ignored on the way back in. */

#if defined(_WIN32)
#  define LIDL_C_EXPORT __declspec(dllexport)
#else
#  define LIDL_C_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Parse .lidl text into the JSON AST wire form.
 * Success: returns a malloc'd JSON string; if err is non-null, *err is set to
 *          NULL.
 * Failure: returns NULL; if err is non-null, *err is set to a malloc'd error
 *          message. */
LIDL_C_EXPORT char* lidl_parse_to_json(const char* lidl, char** err);

/* Serialize a JSON AST wire form back into .lidl text. Same ownership/error
 * contract as lidl_parse_to_json; the error path also covers malformed JSON. */
LIDL_C_EXPORT char* lidl_serialize_from_json(const char* json, char** err);

/* Validate a JSON AST. Returns a malloc'd JSON object
 * {"errors":[...],"warnings":[...]}, or NULL if the input JSON is malformed. */
LIDL_C_EXPORT char* lidl_validate_json(const char* json);

/* Append the derived module identity methods -- name() and version() -- to a
 * JSON AST, returning the augmented JSON. See lidl/identity.hpp for why this
 * is a separate pass rather than part of parsing.
 *
 * Every backend runs this on each module it handles, on both the provider and
 * the consumer side. It is exposed here so a non-C++ SDK runs the SAME pass
 * rather than a reimplementation that could disagree about, say, whether an
 * author's own name() counts.
 *
 * Success: returns malloc'd JSON; *err set to NULL.
 * Failure: returns NULL and sets *err -- malformed input JSON, or the module
 *          declares a reserved identity name with an incompatible signature. */
LIDL_C_EXPORT char* lidl_inject_identity_json(const char* json, char** err);

/* Release a string returned by any function above. NULL is a no-op. */
LIDL_C_EXPORT void lidl_free_string(char* s);

#ifdef __cplusplus
}
#endif

#endif /* LIDL_C_H */
