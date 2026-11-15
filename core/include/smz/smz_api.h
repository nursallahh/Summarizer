/*
 * Summarizer — public C interface (C ABI) of the C++ core.
 *
 * This header is the only contract between the core and everything that uses it:
 * Unity (C#, P/Invoke), the command-line tool and the tests call only these functions.
 *
 * CONTRACT RULES
 * 1. Text: all strings are UTF-8, including file paths.
 * 2. Errors: every function that performs work returns smz_status. Anything other than SMZ_OK is an error.
 * 3. Memory ownership:
 *    - Functions returning "const char*" (e.g. smz_version) return a static string owned by the core.
 *      The caller must NOT free it.
 *    - Functions returning text through a "char** out_..." parameter allocate new memory.
 *      The caller MUST free it with smz_free_string when done.
 * 4. Compatibility: any backward-incompatible change to this file increments SMZ_ABI_VERSION.
 *    The Unity side compares smz_abi_version() with the value it expects at startup.
 */
#ifndef SMZ_API_H
#define SMZ_API_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(SMZ_CORE_BUILDING)
#    define SMZ_API __declspec(dllexport)
#  else
#    define SMZ_API __declspec(dllimport)
#  endif
#else
#  define SMZ_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the C interface. Incremented only on incompatible changes. */
#define SMZ_ABI_VERSION 1

typedef enum smz_status {
    SMZ_OK = 0,
    SMZ_ERR_INVALID_ARGUMENT = 1, /* Invalid argument (e.g. NULL pointer) */
    SMZ_ERR_OUT_OF_MEMORY = 2,    /* Memory allocation failed */
    SMZ_ERR_INTERNAL = 3          /* Unexpected internal error */
} smz_status;

/* Core version, e.g. "0.0.1". Owned by the core; do not free. */
SMZ_API const char* smz_version(void);

/* The C interface version the core was built with (SMZ_ABI_VERSION). */
SMZ_API int32_t smz_abi_version(void);

/* Human-readable description of a status code. Owned by the core; do not free. */
SMZ_API const char* smz_status_message(smz_status status);

/*
 * Returns build information as JSON:
 *   {"version":"0.0.1","abi":1,"compiler":"...","build_type":"..."}
 * *out_json is newly allocated; the caller frees it with smz_free_string.
 */
SMZ_API smz_status smz_build_info(char** out_json);

/* Frees a string allocated by the core. Safe to call with NULL. */
SMZ_API void smz_free_string(char* str);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SMZ_API_H */
