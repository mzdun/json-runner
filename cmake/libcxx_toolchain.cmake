include("${CMAKE_CURRENT_LIST_DIR}/../build/conan/conan_toolchain.cmake")

string(REPLACE " -stdlib=libstdc++" " -stdlib=libc++" CMAKE_CXX_FLAGS_INIT_LOCAL "${CMAKE_CXX_FLAGS_INIT}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT_LOCAL}")
