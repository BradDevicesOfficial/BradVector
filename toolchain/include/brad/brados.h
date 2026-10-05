#ifndef BRAD_OS_H
#define BRAD_OS_H

#include <stdint.h>
#include <stddef.h>

#define BRAD_OS_MAX_NAME        64
#define BRAD_OS_MAX_PROCESSES   4096
#define BRAD_OS_MAX_THREADS     16384

enum brad_os_sku {
    BRAD_OS_MOBILE   = 0,
    BRAD_OS_SLATE    = 1,
    BRAD_OS_COMPUTE  = 2,
    BRAD_OS_NANO     = 3,
};

enum brad_os_tier {
    BRAD_OS_TIER_NANO      = 0,
    BRAD_OS_TIER_STANDARD  = 1,
    BRAD_OS_TIER_PRO       = 2,
};

enum brad_os_edition {
    BRAD_OS_ED_STANDARD  = 0,
    BRAD_OS_ED_DEV       = 1,
    BRAD_OS_ED_XR        = 2,
    BRAD_OS_ED_GAMING    = 3,
    BRAD_OS_ED_EDUCATION = 4,
};

struct brad_os_process_info {
    unsigned                pid;
    char                    name[BRAD_OS_MAX_NAME];
    uint64_t                mem_used;
    uint64_t                mem_virtual;
    unsigned                cpu_usage_pct;
    unsigned                num_threads;
    enum brad_os_edition    edition;
};

int  brados_init(enum brad_os_sku sku,
                 enum brad_os_tier tier,
                 enum brad_os_edition edition);
int  brados_process_create(const char *name,
                           unsigned *pid);
int  brados_process_destroy(unsigned pid);
int  brados_process_info(unsigned pid,
                         struct brad_os_process_info *info);
int  brados_thread_create(unsigned pid,
                          void (*entry)(void*),
                          void *arg,
                          unsigned *tid);
int  brados_thread_join(unsigned tid);
int  brados_set_process_priority(unsigned pid,
                                 unsigned priority);
int  brados_set_process_affinity(unsigned pid,
                                 unsigned cpu_mask);
int  brados_suspend_process(unsigned pid);
int  brados_resume_process(unsigned pid);

#endif
