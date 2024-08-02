cmake_minimum_required(VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -Wall -Wextra -Wpedantic -O0 -g2 -ggdb -Wno-psabi")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -Wall -Wextra -Wpedantic -O3 -Wno-psabi")

project(cpp_base CXX C)

# set(LIBS_DIR "lib/")
set(LIBS_INCLUDE)
set(LIBS_LINK)

## For CMake dependencies
find_package(fmt REQUIRED)
list(APPEND LIBS_INCLUDE ${FMT_INCLUDE_DIRS})
list(APPEND LIBS_LINK fmt)

## For PkgConfig
# find_package(PkgConfig REQUIRED)
# pkg_search_module(GLFW REQUIRED glfw3)
# list(APPEND SHOGLE_LIB_INCLUDE ${GLFW_INCLUDE_DIRS})
# list(APPEND SHOGLE_LIB_LINK glfw)

file(GLOB_RECURSE SOURCE_FILES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC src ${LIBS_INCLUDE})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
target_link_libraries(${PROJECT_NAME} ${LIBS_LINK})
