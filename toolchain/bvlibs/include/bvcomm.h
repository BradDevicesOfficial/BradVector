#ifndef BVLIB_BVCOMM_H
#define BVLIB_BVCOMM_H

#include <stddef.h>
#include <stdint.h>

/* ─── BVComm — BradVector Communication Library (reference) ───
 *
 * Collectives over the BradFusion fabric reference model.  Each rank owns
 * a buffer bound into a fabric node address window; data moves through
 * fabric_dma, so bytes traverse the same node graph (and accrue the same
 * link latency) that device communication would.
 *
 * The transport is a host-simulated fabric: there is no multi-warp BVRT
 * device model yet, so collectives are correctness references, not a
 * performance claim.
 *
 * Phase 0: all-reduce (sum), broadcast, all-gather for f32.
 * The fabric is a process-global reference singleton, so one world per
 * process.
 */

struct bvcomm_world;

/* Create a world of `nranks` ranks.  Each rank gets `buf_bytes` of user
 * buffer plus `scratch_bytes` of staging (used as the ring inbox); both
 * are bound into the rank's fabric window.  NULL on failure. */
struct bvcomm_world *bvcomm_init(int nranks, size_t buf_bytes,
                                 size_t scratch_bytes);

/* Tear down a world and release its buffers and channels. */
void bvcomm_destroy(struct bvcomm_world *w);

/* Library version string. */
const char *bvcomm_version(void);

/* Number of ranks in the world. */
int bvcomm_nranks(const struct bvcomm_world *w);

/* Rank-local user buffer (buf_bytes usable).  NULL on bad rank. */
void *bvcomm_buf(struct bvcomm_world *w, int rank);

/* Collectives.  `off`/`count` are f32 elements within the user buffer.
 * All-reduce leaves the elementwise sum in every rank.  Broadcast copies
 * `root`'s elements to every rank.  All-gather places rank r's `count`
 * inputs into every rank's out region at element offset out_off+r*count.
 * Return 0 on success, -1 bad args, -2 buffer/scratch too small. */
int bvcomm_allreduce_sum_f32(struct bvcomm_world *w, size_t off, size_t count);
int bvcomm_broadcast_f32(struct bvcomm_world *w, int root,
                         size_t off, size_t count);
int bvcomm_allgather_f32(struct bvcomm_world *w, size_t in_off,
                         size_t out_off, size_t count);

#endif /* BVLIB_BVCOMM_H */
