cmake_minimum_required(VERSION 3.28)
project(RedisWatcher)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)

# 查找 GLib
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})

# 查找 Libevent
pkg_check_modules(LIBEVENT REQUIRED libevent)
include_directories(${LIBEVENT_INCLUDE_DIRS})

# 查找 Hiredis
find_library(HIREDIS_LIB hiredis)
include_directories(/usr/include/hiredis)

# 查找 Jansson
pkg_check_modules(JANSSON REQUIRED jansson)
include_directories(${JANSSON_INCLUDE_DIRS})
link_directories(${JANSSON_LIBRARY_DIRS})

# 查找 Curl
find_package(CURL REQUIRED)

# 查找 OpenSSL 库
find_package(OpenSSL REQUIRED)

# 查找源文件
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

# 生成可执行文件
add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${CMAKE_PROJECT_NAME}
        ${GLIB_LIBRARIES}
        ${LIBEVENT_LIBRARIES}
        ${HIREDIS_LIB}
        ${JANSSON_LIBRARIES}
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
)

# 安装
install(TARGETS ${CMAKE_PROJECT_NAME} DESTINATION /opt/redis-watcher/bin)