#ifndef BRAD_COMPRESS_H
#define BRAD_COMPRESS_H

#include <stdint.h>
#include <stddef.h>

#define HDB_MAX_PROFILES  7

enum hdb_profile {
    HDB_PROFILE_GEOMETRY   = 0,
    HDB_PROFILE_TEXTURE    = 1,
    HDB_PROFILE_AUDIO      = 2,
    HDB_PROFILE_MESH       = 3,
    HDB_PROFILE_VIDEO      = 4,
    HDB_PROFILE_BINARY     = 5,
    HDB_PROFILE_GENERAL    = 6,
};

int  hdb_init(void);
int  hdb_decompress(const void *src, size_t src_len,
                    void *dst, size_t *dst_len,
                    enum hdb_profile profile);
int  hdb_compress(const void *src, size_t src_len,
                  void *dst, size_t *dst_len,
                  enum hdb_profile profile);
int  hdb_stream_to_spmp(const void *src, size_t src_len,
                        uint64_t spmp_addr,
                        enum hdb_profile profile);
int  hdb_query_ratio(enum hdb_profile profile,
                     unsigned *compression_ratio_x100);
int  hdb_query_speed(enum hdb_profile profile,
                      unsigned *decomp_gbs,
                      unsigned *compress_gbs);
int  hdb_query_stats(uint64_t *compressed,
                      uint64_t *decompressed,
                      uint64_t *bytes_in,
                      uint64_t *bytes_out,
                      unsigned *errors);

#endif
