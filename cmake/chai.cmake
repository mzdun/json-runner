set(CHAISCRIPT_VERSION 6.1.0)
find_package(chaiscript ${CHAISCRIPT_VERSION} QUIET)

if (NOT chaiscript_FOUND)
  include(FetchContent)

  FetchContent_Declare(
    chaiscript
    GIT_REPOSITORY https://github.com/ChaiScript/ChaiScript.git
    GIT_TAG v${CHAISCRIPT_VERSION}
  )

  FetchContent_GetProperties(chaiscript)
  if (NOT chaiscript_POPULATED)
    set(FETCHCONTENT_QUIET NO)
    FetchContent_Populate(chaiscript)

    set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_MODULES ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBFUZZ_TESTER OFF CACHE BOOL "" FORCE)
    set(BUILD_IN_CPP17_MODE ON CACHE BOOL "" FORCE)
  endif()

  add_library(chaiscript INTERFACE IMPORTED GLOBAL)
  target_include_directories(chaiscript INTERFACE "${chaiscript_SOURCE_DIR}/include")
endif()

message(STATUS "chaiscript_SOURCE_DIR ${chaiscript_SOURCE_DIR}")

get_target_property(includes chaiscript INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "chaiscript ${includes}")
