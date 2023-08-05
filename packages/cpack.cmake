if (CPACK_GENERATOR STREQUAL "WIX")
    include(${CMAKE_CURRENT_LIST_DIR}/wix/cpack.cmake)
endif()
