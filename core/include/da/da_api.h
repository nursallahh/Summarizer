/*
 * Course Archive — public C interface (C ABI) of the C++ core.
 *
 * This header is the only contract between the core and everything that uses it:
 * Unity (C#, P/Invoke), the command-line tool and the tests call only these functions.
 *
 * CONTRACT RULES
 * 1. Text: all strings are UTF-8, including file paths.
 * 2. Errors: every function that performs work returns da_status. Anything other than DA_OK is an error.
 * 3. Memory ownership:
 *    - Functions returning "const char*" (e.g. da_version) return a static string owned by the core.
 *      The caller must NOT free it.
 *    - Functions returning text through a "char** out_..." parameter allocate new memory.
 *      The caller MUST free it with da_free_string when done.
 * 4. Compatibility: any backward-incompatible change to this file increments DA_ABI_VERSION.
 *    The Unity side compares da_abi_version() with the value it expects at startup.
 */
#ifndef DA_API_H
#define DA_API_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(DA_CORE_BUILDING)
#    define DA_API __declspec(dllexport)
#  else
#    define DA_API __declspec(dllimport)
#  endif
#else
#  define DA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the C interface. Incremented only on incompatible changes. */
#define DA_ABI_VERSION 1

typedef enum da_status {
    DA_OK = 0,
    DA_ERR_INVALID_ARGUMENT = 1, /* Invalid argument (e.g. NULL pointer) */
    DA_ERR_OUT_OF_MEMORY = 2,    /* Memory allocation failed */
    DA_ERR_INTERNAL = 3          /* Unexpected internal error */
} da_status;

/* Core version, e.g. "0.0.1". Owned by the core; do not free. */
DA_API const char* da_version(void);

/* The C interface version the core was built with (DA_ABI_VERSION). */
DA_API int32_t da_abi_version(void);

/* Human-readable description of a status code. Owned by the core; do not free. */
DA_API const char* da_status_message(da_status status);

/*
 * Returns build information as JSON:
 *   {"version":"0.0.1","abi":1,"compiler":"...","build_type":"..."}
 * *out_json is newly allocated; the caller frees it with da_free_string.
 */
DA_API da_status da_build_info(char** out_json);

/* Frees a string allocated by the core. Safe to call with NULL. */
DA_API void da_free_string(char* str);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DA_API_H */
