set(JSON_TESTING OFF CACHE BOOL "Compile and/or run self-tests")
set(JSON_INSTALL OFF CACHE BOOL "Install the library")

add_library(arch::deps INTERFACE IMPORTED)

macro(select_arch_import tag win_name linux_name)
    if (TARGET ${win_name})
        target_link_libraries(arch::deps INTERFACE ${win_name})
    elseif(TARGET ${linux_name})
        target_link_libraries(arch::deps INTERFACE ${linux_name})
    else()
        message(FATAL_ERROR "Export from ${tag} conan module unknown")
    endif()
endmacro()

select_arch_import(libzip libzip::zip libzip::libzip)
select_arch_import(zlib ZLIB::ZLIB zlib::zlib)
select_arch_import(bzip2 BZip2::BZip2 bzip2::bzip2)
if (TARGET LibLZMA::LibLZMA)
    target_link_libraries(arch::deps INTERFACE LibLZMA::LibLZMA)
endif()


add_subdirectory(json)
add_subdirectory(libarch)

set_target_properties(json arch PROPERTIES FOLDER libs)

fix_vs_modules(json)
fix_vs_modules(arch)
fix_vs_modules(arch::deps)

set_parent_scope()
