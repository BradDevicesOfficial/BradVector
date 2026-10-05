#include "brad/bradcvt.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int brad_cvt_compile(struct brad_cvt_config *cfg,
                      struct brad_cvt_kernel_info *info)
{
    if (!cfg || !info)
        return BRAD_CVT_ERR_PARSE;
    (void)cfg;
    memset(info, 0, sizeof(*info));
    return BRAD_CVT_OK;
}

int brad_cvt_translate_ptx(const char *ptx_src,
                            size_t ptx_len,
                            void **brad_vec_bin,
                            size_t *bin_len)
{
    if (!ptx_src || !brad_vec_bin || !bin_len)
        return BRAD_CVT_ERR_PARSE;
    (void)ptx_len;
    *brad_vec_bin = NULL;
    *bin_len = 0;
    return BRAD_CVT_OK;
}

int brad_cvt_translate_cubin(const void *cubin,
                              size_t cubin_len,
                              void **brad_vec_bin,
                              size_t *bin_len)
{
    if (!cubin || !brad_vec_bin || !bin_len)
        return BRAD_CVT_ERR_PARSE;
    (void)cubin_len;
    *brad_vec_bin = NULL;
    *bin_len = 0;
    return BRAD_CVT_OK;
}

void brad_cvt_free(void *bin)
{
    free(bin);
}

const char *brad_cvt_status_str(int status)
{
    switch (status) {
    case BRAD_CVT_OK:              return "OK";
    case BRAD_CVT_ERR_PARSE:       return "Parse error";
    case BRAD_CVT_ERR_TRANSLATION: return "Translation error";
    case BRAD_CVT_ERR_UNSUPPORTED: return "Unsupported feature";
    case BRAD_CVT_ERR_MEMORY:      return "Out of memory";
    default:                       return "Unknown error";
    }
}
