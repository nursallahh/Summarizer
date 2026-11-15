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
    SMZ_ERR_INTERNAL = 3,         /* Unexpected internal error */
    SMZ_ERR_NOT_FOUND = 4         /* The referenced object (e.g. a job id) does not exist */
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

/* ------------------------------------------------------------------------------------------
 * Context, background jobs and events
 *
 * A context owns the worker threads and the event queue. Long-running work never blocks the
 * caller: functions that start work return a job id immediately, and the work reports back
 * through events. The UI drains events once per frame with smz_poll_events.
 *
 * Threading:
 *   - smz_create and smz_destroy must not run concurrently with any other call on the same context.
 *   - All other context functions may be called from any thread.
 *   - smz_destroy cancels all jobs and waits for them to stop; it returns quickly because jobs
 *     check for cancellation regularly. Always call it (Unity: in OnDestroy), or worker threads
 *     keep running inside the editor process.
 *
 * Events (JSON objects, in the order they happened):
 *   {"type":"job_started","job":1,"kind":"demo"}
 *   {"type":"job_progress","job":1,"done":40,"total":100}
 *   {"type":"job_finished","job":1,"outcome":"succeeded|cancelled|failed","message":""}
 * Every job emits exactly one job_started and one job_finished. job_progress events may be
 * dropped if the queue is full (the UI stopped polling); dropped events are counted.
 * ------------------------------------------------------------------------------------------ */

typedef struct smz_context smz_context;

/* Creates a context with its worker threads. Free it with smz_destroy. */
SMZ_API smz_status smz_create(smz_context** out_ctx);

/* Cancels all jobs, waits for workers to stop and frees the context. Safe to call with NULL. */
SMZ_API void smz_destroy(smz_context* ctx);

/*
 * Removes up to max_events events from the queue and returns them as:
 *   {"events":[...],"dropped":0,"pending":0}
 * "dropped" counts progress events discarded since the previous call; "pending" is how many
 * events are still queued. *out_json is newly allocated; free it with smz_free_string.
 */
SMZ_API smz_status smz_poll_events(smz_context* ctx, int32_t max_events, char** out_json);

/*
 * Starts a demo job that reports total_steps progress events at about steps_per_second.
 * Used to test the job and event pipeline end to end (e.g. 10000 steps at 1000/s).
 */
SMZ_API smz_status smz_start_demo_job(smz_context* ctx, int32_t total_steps, int32_t steps_per_second,
                                      int64_t* out_job_id);

/* Requests cancellation of a job. Returns SMZ_ERR_NOT_FOUND if it already finished or never existed. */
SMZ_API smz_status smz_cancel_job(smz_context* ctx, int64_t job_id);

/*
 * Starts a job that inspects files: content hash (SHA-256), type detected from the content,
 * size, slide count for presentations and capture time for photos. Files are only read,
 * never changed, moved or copied.
 *
 * paths:     UTF-8 file or folder paths separated by '\n'.
 * recursive: non-zero to include sub-folders of the given folders.
 *
 * For every file the job emits, in a stable order (sorted by path):
 *   {"type":"file_imported","job":1,"path":"C:\\...\\a.jpg","name":"a.jpg",
 *    "sha256":"<64 hex>","file_type":"jpeg","size":12345,"pages":-1,
 *    "captured_at":"2026-10-14T10:32:05","error":""}
 * file_type is one of: jpeg, png, heic, webp, pdf, pptx, docx, text, audio, unsupported.
 * pages is the slide count for pptx and -1 when not known (PDF pages come with PDF support).
 * captured_at is the photo's EXIF capture time (camera local time) or "".
 * The job_finished message lists paths that could not be found, if any.
 */
SMZ_API smz_status smz_import_paths(smz_context* ctx, const char* paths, int32_t recursive, int64_t* out_job_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SMZ_API_H */
