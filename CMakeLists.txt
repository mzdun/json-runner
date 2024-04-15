cmake_minimum_required (VERSION 3.25)
project (json-runner
  DESCRIPTION "JSON test orchestration"
  VERSION 0.2.2
  LANGUAGES CXX
)

set(PROJECT_VERSION_SHORT "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
set(PROJECT_VERSION_STABILITY "" # or "-alpha", or "-beta", or "-rc.1", or "-rc.2"
    CACHE STRING "" FORCE)

if (${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_BINARY_DIR})
  message(FATAL_ERROR "Building in source firectory is not supported")
endif()

set(RUNNER_TESTING ON CACHE BOOL "Compile and/or run self-tests")
set(RUNNER_SANITIZE OFF CACHE BOOL "Compile with sanitizers enabled")
set(RUNNER_CUTDOWN_OS OFF CACHE BOOL "Run tests on cutdown OS (e.g. GitHub docker)")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_BINARY_DIR}/conan" "${PROJECT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_PREFIX_PATH "${PROJECT_BINARY_DIR}/conan")

set(CONAN_CMAKE_SILENT_OUTPUT ON)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(ctre REQUIRED)
find_package(fmt REQUIRED)
find_package(mbits-args REQUIRED)
find_package(libzip REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(lzma_sdk REQUIRED)

include(output_dirs_setup)
old_conan_output_dirs_setup()

if (MSVC)
  set(ADDITIONAL_WALL_FLAGS
    /D_CRT_SECURE_NO_WARNINGS
    /D_CRT_NONSTDC_NO_WARNINGS
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /W4
    /w14242
    /w14254
    /w14263
    /w14265
    /w14287
    /we4289
    /w14296
    /w14311
    /w14545
    /w14546
    /w14547
    /w14549
    /w14555
    /w14619
    /w14640
    /w14826
    /w14905
    /w14906
    /w14928
    /w14946)
  if (COV_SANITIZE)
    list(APPEND ADDITIONAL_WALL_FLAGS
      /fsanitize=address
      /D_DISABLE_VECTOR_ANNOTATION
      /D_DISABLE_STRING_ANNOTATION
    )
  endif()
else()
  set(ADDITIONAL_WALL_FLAGS
      -Wall -Wextra
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Woverloaded-virtual
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wnull-dereference
      -Wdouble-promotion
      -Wformat=2
  )
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    list(APPEND ADDITIONAL_WALL_FLAGS
      -fcolor-diagnostics
      -Wno-braced-scalar-init
    # -Wlifetime
    )
  else()
    list(APPEND ADDITIONAL_WALL_FLAGS
      -fdiagnostics-color
      -Wmisleading-indentation
      -Wduplicated-cond
      -Wduplicated-branches
      -Wlogical-op
      -Wuseless-cast
      )
  endif()
  if (COV_SANITIZE)
    list(APPEND ADDITIONAL_WALL_FLAGS
      -fsanitize=address
      -fsanitize=undefined
      
    )
    list(APPEND ADDITIONAL_LINK_FLAGS
      -fsanitize=address
      -fsanitize=undefined
    )
  endif()
endif()

if (COV_SANITIZE)
  message(STATUS "This build is prepared for sanitization")
endif()

include(${PROJECT_SOURCE_DIR}/packages/config.cmake)
include(CPack)

set(BINARY_DIR bin)
set(ROOT_DIR "..")

message(STATUS "Sanitizer: ${COV_SANITIZE}")

include(runner)
include(chai)

add_subdirectory(external)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
endif()

set(SOURCES
    src/base/cmake.cc
    src/base/cmake.hh
    src/base/diff.cc
    src/base/diff.hh
    src/base/seed_sequence.hh
    src/base/shell.cc
    src/base/shell.hh
    src/base/str.cc
    src/base/str.hh
    src/bindings/filesystem.hh
    src/bindings/runner.hh
    src/bindings/span.hh
    src/bindings/string.hh
    src/chai.cc
    src/chai.hh
    src/entry_point.cc
    src/io/file.cc
    src/io/file.hh
    src/io/path_env.hh
    src/io/presets.cc
    src/io/presets.hh
    src/io/run.hh
    src/main.cc
    src/mt/queue.hh
    src/mt/thread_pool.cc
    src/mt/thread_pool.hh
    src/testbed/commands.cc
    src/testbed/commands.hh
    src/testbed/runtime.cc
    src/testbed/runtime.hh
    src/testbed/test.cc
    src/testbed/test.hh
)

if (UNIX)
	list(APPEND SOURCES
    src/posix/run.cc
  )
elseif(WIN32)
	list(APPEND SOURCES
    src/win32/run.cc
  )
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

configure_file(src/version.hh.in ${CMAKE_CURRENT_BINARY_DIR}/src/version.hh @ONLY)

add_executable(json-runner ${SOURCES})
target_include_directories(json-runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/src)

target_link_libraries(json-runner PRIVATE
    ctre::ctre
    fmt::fmt
    mbits::args
    json
    arch
    chaiscript
)

if (WIN32)
    target_compile_options(json-runner PUBLIC /D_UNICODE /DUNICODE)
    target_link_options(json-runner PRIVATE /ENTRY:wmainCRTStartup)
    fix_vs_modules(json-runner)
endif()

install(TARGETS json-runner
    RUNTIME DESTINATION ${BINARY_DIR}
    COMPONENT main_exec
)

install(
    FILES ${PROJECT_SOURCE_DIR}/schema.json
    DESTINATION share/json-runner-${PROJECT_VERSION_SHORT}
    COMPONENT main_exec
)

cpack_add_component(main_exec
    DISPLAY_NAME "Main executable"
    GROUP apps
)

cpack_add_component(tools
    DISPLAY_NAME "Core tools"
    GROUP apps
)

cpack_add_component_group(apps
    DISPLAY_NAME "Executables"
    EXPANDED
)
