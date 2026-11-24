cmake_minimum_required(VERSION 3.16)

project(OctQuill VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UTF-8（仅 MSVC）
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# 产物输出路径（用构建目录，避免污染源码）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_OBJECT_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/obj)

# 自动扫描源文件
file(GLOB_RECURSE ALL_SRCS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
set(MAIN_SRCS "${CMAKE_SOURCE_DIR}/src/main.cpp")
set(LIB_SRCS ${ALL_SRCS})
list(REMOVE_ITEM LIB_SRCS ${MAIN_SRCS})

set(LIB_NAME "${PROJECT_NAME}Lib")

if(LIB_SRCS)
    add_library(${LIB_NAME} STATIC ${LIB_SRCS})
    target_include_directories(${LIB_NAME} 
		PUBLIC	${CMAKE_SOURCE_DIR}/include
        PRIVATE	${CMAKE_SOURCE_DIR}/src
    )
    target_compile_options(${LIB_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
else()
    add_library(${LIB_NAME} INTERFACE)
    target_include_directories(${LIB_NAME} INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

if(EXISTS ${MAIN_SRCS})
    add_executable(${PROJECT_NAME} ${MAIN_SRCS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIB_NAME})   # 无论 STATIC 还是 INTERFACE 都链
else()
    message(FATAL_ERROR "缺少主程序入口文件: ${MAIN_SRCS}")
endif()