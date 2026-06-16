#ifndef LIDL_C_H
#define LIDL_C_H

/* C ABI over the logos-lidl frontend — the JSON bridge that lets non-C++
 * SDKs (Rust, ...) reach the canonical parser / serializer / validator
 * without reimplementing the grammar. AST is exchanged in the JSON wire form
 * produced by lidl/json.hpp.
 *
 * String ownership: every char* returned by a function below is malloc'd and
 * must be released with lidl_free_string (mirrors the liblogos_core idiom). */

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

/* Release a string returned by any function above. NULL is a no-op. */
LIDL_C_EXPORT void lidl_free_string(char* s);

#ifdef __cplusplus
}
#endif

#endif /* LIDL_C_H */
