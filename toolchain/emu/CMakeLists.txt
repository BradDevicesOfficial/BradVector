add_library(brad-emu STATIC
    brad_core_emu.c
    brad_cache_emu.c
)

target_link_libraries(brad-emu PUBLIC brad-hw)
target_include_directories(brad-emu PUBLIC ../include)
