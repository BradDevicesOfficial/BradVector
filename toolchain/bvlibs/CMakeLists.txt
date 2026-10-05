# Embed a .bvbs kernel as a C string and track it for reconfiguration.
function(bvlib_kernel FILE_NAME VAR_OUT)
    set(_path "${CMAKE_CURRENT_SOURCE_DIR}/kernels/${FILE_NAME}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_path}")
    file(READ "${_path}" _content)
    string(REPLACE "\\" "\\\\" _content "${_content}")
    string(REPLACE "\"" "\\\"" _content "${_content}")
    string(REPLACE "\n" "\\n" _content "${_content}")
    set(${VAR_OUT} "${_content}" PARENT_SCOPE)
endfunction()

# ── BVML — math ──
bvlib_kernel(bvml_saxpy.bvbs BVML_K_SAXPY)
bvlib_kernel(bvml_dot.bvbs BVML_K_DOT)
bvlib_kernel(bvml_gemm.bvbs BVML_K_GEMM)
set(K_SAXPY "${BVML_K_SAXPY}")
set(K_DOT "${BVML_K_DOT}")
set(K_GEMM "${BVML_K_GEMM}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kernels/bvml_kernels.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/bvml_kernels.h" @ONLY)

add_library(bvml STATIC src/bvml.c)
target_include_directories(bvml PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(bvml PUBLIC brad-vector)

add_executable(test_bvml tests/test_bvml.c)
target_link_libraries(test_bvml PRIVATE bvml)
add_test(NAME bvml COMMAND test_bvml)

# ── BVN — neural ──
bvlib_kernel(bvn_relu.bvbs BVN_K_RELU)
bvlib_kernel(bvn_affine.bvbs BVN_K_AFFINE)
bvlib_kernel(bvn_softmax.bvbs BVN_K_SOFTMAX)
bvlib_kernel(bvn_sigmul.bvbs BVN_K_SIGMUL)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kernels/bvn_kernels.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/bvn_kernels.h" @ONLY)

add_library(bvn STATIC src/bvn.c)
target_include_directories(bvn PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(bvn PUBLIC brad-vector)

add_executable(test_bvn tests/test_bvn.c)
target_link_libraries(test_bvn PRIVATE bvn)
add_test(NAME bvn COMMAND test_bvn)

# ── BVComm — communication (over the BradFusion fabric reference) ──
if(TARGET brad-drivers)
    add_library(bvcomm STATIC src/bvcomm.c)
    target_include_directories(bvcomm PUBLIC include)
    target_link_libraries(bvcomm PUBLIC brad-drivers)

    add_executable(test_bvcomm tests/test_bvcomm.c)
    target_link_libraries(test_bvcomm PRIVATE bvcomm)
    add_test(NAME bvcomm COMMAND test_bvcomm)
endif()
