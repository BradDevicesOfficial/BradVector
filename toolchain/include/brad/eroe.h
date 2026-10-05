#ifndef BRAD_EROE_H
#define BRAD_EROE_H

#include <stdint.h>

#define EROE_MAX_PROFILES 16
#define EROE_MAX_WORKLOADS 32

enum eroe_profile {
    EROE_PROFILE_COMPUTE    = 0,
    EROE_PROFILE_GAMING     = 1,
    EROE_PROFILE_AI_INF     = 2,
    EROE_PROFILE_VIDEO      = 3,
    EROE_PROFILE_IDLE       = 4,
    EROE_PROFILE_BATTERY    = 5,
    EROE_PROFILE_CUSTOM     = 6,
};

enum eroe_unit {
    EROE_UNIT_CPU  = 0,
    EROE_UNIT_GPU  = 1,
    EROE_UNIT_NPU  = 2,
    EROE_UNIT_MEM  = 3,
};

enum eroe_fan_curve {
    EROE_FAN_SILENT      = 0,
    EROE_FAN_BALANCED    = 1,
    EROE_FAN_PERFORMANCE = 2,
    EROE_FAN_AGGRESSIVE  = 3,
};

struct eroe_workload_info {
    char              name[32];
    enum eroe_unit    unit;
    float             load_factor;
};

struct eroe_power_budget {
    unsigned cpu_pct;   
    unsigned gpu_pct;
    unsigned npu_pct;
    unsigned mem_pct;   
};

struct eroe_thermal_state {
    unsigned junction_c;     
    unsigned hotspot_c;
    unsigned board_c;
    unsigned battery_pct;
};

struct eroe_profile_config {
    enum eroe_profile             profile;
    struct eroe_power_budget      budget;
    unsigned                      freq_limit_mhz;
    unsigned                      fan_curve;    
    int                           throttle_on_temp;
    int                           battery_saver;
};

int  eroe_init(void);
int  eroe_set_profile(enum eroe_profile profile);
int  eroe_set_custom_profile(struct eroe_profile_config *cfg);
int  eroe_declare_workload(const char *name,
                            enum eroe_unit unit,
                            float load_factor);
int  eroe_get_workloads(struct eroe_workload_info *out,
                        unsigned *count);
struct eroe_thermal_state eroe_get_thermal(void);
struct eroe_power_budget eroe_get_budget(void);
int  eroe_set_fan_curve(unsigned curve_id);
int  eroe_set_power_cap(unsigned watts);
int  eroe_set_temp_limit(unsigned celsius);

#endif
