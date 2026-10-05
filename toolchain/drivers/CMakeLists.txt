add_library(brad-drivers STATIC
    bradcore.c
    hypercore_atlas.c
    bradnpu.c
    eroe.c
    bradlink.c
    fabric.c
    spmp.c
    bradcvt_stub.c
    braddrive.c
    bradcompress.c
    bradsense.c
)

target_link_libraries(brad-drivers PUBLIC brad-hw)
target_include_directories(brad-drivers PUBLIC ../include)
