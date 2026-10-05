#ifndef BRAD_RAM_H
#define BRAD_RAM_H

#include <stdint.h>
#include <stddef.h>

#define BRAD_RAM_DIES_PER_STACK  12
#define BRAD_RAM_MAX_STACKS      4

struct brad_ram_die_info {
    unsigned  speed_gbps;
    size_t    capacity_gb;
    unsigned  bus_width;
    unsigned  voltage_mv;
    unsigned  t_rcd_ns;
    unsigned  t_cl_ns;
    unsigned  temp_c;
};

struct brad_ram_stack_info {
    unsigned  num_dies;
    size_t    total_capacity_gb;
    unsigned  bandwidth_gbs;
    unsigned  l4_cache_kb;
    int       dds_enabled;
};

int  brad_ram_init(void);
int  brad_ram_die_info(unsigned stack, unsigned die,
                       struct brad_ram_die_info *info);
int  brad_ram_stack_info(unsigned stack,
                         struct brad_ram_stack_info *info);
int  brad_ram_dds_configure(unsigned stack,
                            int enable,
                            unsigned prefetch_depth);
int  brad_ram_set_voltage(unsigned stack,
                          unsigned voltage_mv);
int  brad_ram_set_freq(unsigned stack,
                       unsigned freq_mhz);
int  brad_ram_read_temp(unsigned stack, unsigned *temp_c);
int  brad_ram_sec_enable(unsigned stack);

#endif
