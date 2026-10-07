add_executable(test_eroe test_eroe.c)
target_link_libraries(test_eroe brad-drivers)
add_test(NAME eroe COMMAND test_eroe)

add_executable(test_hypercore test_hypercore.c)
target_link_libraries(test_hypercore brad-drivers)
add_test(NAME hypercore COMMAND test_hypercore)

add_executable(test_npu test_npu.c)
target_link_libraries(test_npu brad-drivers)
add_test(NAME npu COMMAND test_npu)

add_executable(test_fabric test_fabric.c)
target_link_libraries(test_fabric brad-drivers)
add_test(NAME fabric COMMAND test_fabric)

add_executable(test_spmp test_spmp.c)
target_link_libraries(test_spmp brad-drivers)
add_test(NAME spmp COMMAND test_spmp)

add_executable(test_drive test_drive.c)
target_link_libraries(test_drive brad-drivers)
add_test(NAME drive COMMAND test_drive)

add_executable(test_compress test_compress.c)
target_link_libraries(test_compress brad-drivers)
add_test(NAME compress COMMAND test_compress)

add_executable(test_bradsense test_bradsense.c)
target_link_libraries(test_bradsense brad-drivers)
add_test(NAME bradsense COMMAND test_bradsense)

add_executable(test_bradsense_pixels test_bradsense_pixels.c)
target_link_libraries(test_bradsense_pixels brad-drivers m)
add_test(NAME bradsense_pixels COMMAND test_bradsense_pixels)

add_executable(test_bradcore test_bradcore.c)
target_link_libraries(test_bradcore brad-drivers)
add_test(NAME bradcore COMMAND test_bradcore)

if(BRAD_BUILD_EMU)
    add_executable(test_core_emu test_core_emu.c)
    target_link_libraries(test_core_emu brad-emu)
    add_test(NAME core_emu COMMAND test_core_emu)
endif()

if(BRAD_BUILD_BVECTOR)
    add_executable(test_bradvector test_bradvector.c)
    target_link_libraries(test_bradvector brad-vector)
    target_link_libraries(test_bradvector m)
    add_test(NAME bradvector COMMAND test_bradvector)

    add_executable(test_bradlib test_bradlib.c)
    target_link_libraries(test_bradlib brad-vector)
    target_link_libraries(test_bradlib m)
    add_test(NAME bradlib COMMAND test_bradlib)
endif()


# M2 -- BradSense metrics. Golden path is injected because this same harness is
# built from two layouts (src/tests internally, toolchain/tests here).
add_executable(test_bradsense_metrics test_bradsense_metrics.c)
target_link_libraries(test_bradsense_metrics brad-drivers m)
target_compile_definitions(test_bradsense_metrics PRIVATE
    BRADSENSE_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/golden/bradsense_metrics.json")
add_test(NAME bradsense_metrics COMMAND test_bradsense_metrics)
add_test(NAME bradsense_metrics_emit COMMAND test_bradsense_metrics --emit)
set_tests_properties(bradsense_metrics_emit PROPERTIES
                     PASS_REGULAR_EXPRESSION "\"psnr_db\"")
