add_executable(braddev braddev.c)
target_link_libraries(braddev PRIVATE brad-vector)
if(TARGET brad-drivers)
    target_link_libraries(braddev PRIVATE brad-drivers)
endif()
if(TARGET bvml AND TARGET bvn)
    target_link_libraries(braddev PRIVATE bvml bvn)
    target_compile_definitions(braddev PRIVATE BRADDEV_HAVE_BVLIB=1)
endif()
target_include_directories(braddev PRIVATE ../bradvector)
  # The Kit's reported version comes from project(... VERSION ...) so the binary
  # cannot claim a version the build system does not agree with. A release that
  # says one thing and measures another is exactly the receipt failure this
  # project exists to avoid.
  target_compile_definitions(braddev PRIVATE BRADDEV_VERSION="${PROJECT_VERSION}")