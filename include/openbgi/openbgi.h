/* libobgi: the OpenBGI engine as a library, for native launchers (SwiftUI on iOS/macOS, Compose on Android).
 * Strings are UTF-8. Call obgi_run from the main thread. */
#ifndef OPENBGI_H
#define OPENBGI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define OBGI_API __declspec(dllexport)
#else
#define OBGI_API __attribute__((visibility("default")))
#endif

/* Runs a game session and returns when it exits. `argv` follows obgi_run's CLI options
 * (argv[0] is ignored): <game_dir>, --window, --launcher, --data-dir, --driver,
 * --resources-dir, --shader-dir, --font-dir, --log-file, --frames, --render-headless, ...
 * Safe to call repeatedly from the main thread (iOS in-process re-entrancy).
 * Returns 0 on success; otherwise non-zero and, when `error` is not NULL, writes a message
 * truncated to `error_size`. */
OBGI_API int obgi_run(int argc, char** argv, char* error, size_t error_size);

typedef struct obgi_game_info {
    char id[64];    /* normalized short name, e.g. from system.arc debug.inf or folder name */
    char name[128]; /* display name */
} obgi_game_info;

/* Cheaply checks whether `dir` holds a BGI game (looks for autoload.arc / system.arc).
 * Returns 1 and fills *info when detected, 0 otherwise. */
OBGI_API int obgi_detect(const char* dir, obgi_game_info* info);

typedef struct obgi_probe_result {
    int ok;           /* 1 when bootable (autoload.arc & system.arc exist and open cleanly) */
    int complete;     /* 1 when ok and every .arc in `game_dir` opens without error */
    int arcs_found;   /* total .arc files found in `game_dir` */
    int has_sc_patch; /* 1 when a simplified Chinese story patch is detected */
    char game_id[64];
    char game_name[128];
    char missing[512]; /* comma-separated missing or broken archives, or "" */
    char message[512]; /* reason when !ok, or "" */
} obgi_probe_result;

/* Probes a game folder without opening a window. Returns result->ok. */
OBGI_API int obgi_probe(const char* game_dir, obgi_probe_result* result);

OBGI_API const char* obgi_version(void);

#ifdef __cplusplus
}
#endif

#endif
