/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 SunnySlopes
 */

/*
 * NoSlimeFinderBase — public C API
 * ============================================================================
 *
 * Stable contract between the NoSlimeFinderBase library and its consumers
 * (the NoSlimeFinder GUI JNI shim and the standalone CLI).
 *
 * The implementation lives in src/noslime_search.cpp (+ cubiomes and
 * SlimeOptimizer submodules). Consumers must only include this header; the
 * internal search file is not part of the contract.
 *
 * Threading:
 *   - noslime_run() is synchronous and must run on a worker thread.
 *   - noslime_get_progress(), noslime_pause(), noslime_resume(),
 *     noslime_cancel(), noslime_result_count() and noslime_get_results() may
 *     be called from other threads while noslime_run() is executing.
 *   - Only one search may run at a time; a second noslime_run() call blocks
 *     on the internal API mutex until the first one finishes.
 */

#ifndef NOSLIMEFINDER_BASE_H
#define NOSLIMEFINDER_BASE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOSLIME_BASE_VERSION_MAJOR 1
#define NOSLIME_BASE_VERSION_MINOR 0
#define NOSLIME_BASE_VERSION_PATCH 0
#define NOSLIME_BASE_VERSION_STRING "1.0.0"

/* Fixed geometry: 24 < distance <= 128 blocks = 49640 ring blocks. */
#define NOSLIME_RING_INNER 24
#define NOSLIME_RING_OUTER 128
#define NOSLIME_RING_TOTAL 49640
#define NOSLIME_DONUT_CHUNKS 192

enum
{
    NOSLIME_STAGE_IDLE = 0,
    NOSLIME_STAGE_PREFILTER = 1,
    NOSLIME_STAGE_SCAN = 2,
    NOSLIME_STAGE_COARSE = 3,
    NOSLIME_STAGE_EXACT = 4,
    NOSLIME_STAGE_DONE = 5,
    NOSLIME_STAGE_CANCELLED = 6,
    NOSLIME_STAGE_ERROR = 7
};

typedef struct NoSlimeParams
{
    int64_t seed;
    int32_t afk_x0, afk_x1, afk_z0, afk_z1; /* inclusive block coordinates */
    int32_t threads;
    int32_t biome_y;
    int32_t max_results;
    int32_t min_area;
    /* 0 => max(1000, 10 * max_results), capped at 1000. */
    int32_t candidate_limit;
    int32_t mc; /* cubiomes MCVersion */
    const int32_t *avoid_ids;
    int32_t avoid_count;
} NoSlimeParams;

typedef struct NoSlimeResult
{
    int32_t x, z;
    int32_t area;    /* NOSLIME_RING_TOTAL - slime - avoid_x */
    int32_t slime;   /* ring blocks in slime chunks */
    int32_t chunks;  /* slime chunks in the 192-chunk donut */
    int32_t avoid_x; /* avoided ring blocks that are not slime */
    int32_t avoid_y; /* avoided ring blocks that are also slime */
} NoSlimeResult;

/* One Overworld biome entry for the GUI biome picker. */
typedef struct NoSlimeBiome
{
    int32_t id;
    char name[64];
} NoSlimeBiome;

/*
 * Runs one search synchronously on the calling thread.
 * Returns 0 on success, 1 when cancelled, < 0 on error.
 * Results and progress are read through the accessors below.
 */
int noslime_run(const NoSlimeParams *params);

int noslime_pause(void);
int noslime_resume(void);
int noslime_cancel(void);

/*
 * out must have 8 elements:
 * [0]=stage, [1]=done, [2]=paused, [3]=error,
 * [4]=stage_current, [5]=stage_total, [6]=elapsed_ms, [7]=result_count
 */
void noslime_get_progress(int64_t out[8]);

/* Last error message; empty when no error occurred. */
const char *noslime_last_error(void);

int32_t noslime_result_count(void);
/* Returns the total result count; copies at most `capacity` records to `out`. */
int32_t noslime_get_results(NoSlimeResult *out, int32_t capacity);

/* ---- cubiomes-backed helpers (biome picker / version resolution) ---- */

/*
 * Resolves a Minecraft version name ("26.3", "1.21.4", "latest", ...) or a raw
 * cubiomes MCVersion number. Returns the MCVersion, or -1 when unsupported.
 */
int noslime_parse_mc(const char *text);

/* Canonical version name for an MCVersion, e.g. "26.3"; "unknown" if invalid. */
const char *noslime_mc_name(int mc);

/* Newest supported MCVersion (MC_NEWEST). */
int noslime_newest_mc(void);

/*
 * Lists Overworld biomes that exist in `mc`, ordered by biome id descending.
 * Fills at most `capacity` entries of `out` and returns the total count.
 * `out` may be NULL when `capacity` is 0 (use this to query the count).
 */
int32_t noslime_list_biomes(int mc, NoSlimeBiome *out, int32_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* NOSLIMEFINDER_BASE_H */
