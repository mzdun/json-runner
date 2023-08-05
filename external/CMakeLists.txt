set(JSON_TESTING OFF CACHE BOOL "Compile and/or run self-tests")
set(JSON_INSTALL OFF CACHE BOOL "Install the library")

add_library(arch::deps INTERFACE IMPORTED)
target_link_libraries(arch::deps INTERFACE
    libzip::zip
    ZLIB::ZLIB
    BZip2::BZip2
    LibLZMA::LibLZMA
)

add_subdirectory(json)
add_subdirectory(libarch)

set_target_properties(json arch PROPERTIES FOLDER libs)

fix_vs_modules(json)
fix_vs_modules(arch)
fix_vs_modules(arch::deps)

set_parent_scope()
