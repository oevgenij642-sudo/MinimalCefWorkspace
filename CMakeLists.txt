cmake_minimum_required(VERSION 3.18)
project(MinimalCefWorkspace LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenGL REQUIRED)

set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef" CACHE PATH "Path to CEF")
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")

if(WIN32)
    set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "Use dynamic runtime" FORCE)
endif()

find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

if(UNIX)
    target_compile_options(libcef_dll_wrapper PRIVATE -Wno-error -Wno-deprecated-declarations)
endif()

if(WIN32)
    set(SDL2_WIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2")
    set(SDL2_INCLUDE_DIRS "${SDL2_WIN_DIR}/include")
    set(SDL2_LIBRARIES
        "${SDL2_WIN_DIR}/lib/x64/SDL2.lib"
        "${SDL2_WIN_DIR}/lib/x64/SDL2main.lib"
    )
    set(SDL2_DLL "${SDL2_WIN_DIR}/lib/x64/SDL2.dll")
else()
    find_package(SDL2 REQUIRED)
    string(STRIP "${SDL2_INCLUDE_DIRS}" SDL2_INCLUDE_DIRS)
    string(STRIP "${SDL2_LIBRARIES}" SDL2_LIBRARIES)
endif()

add_executable(MinimalCefWorkspace main.cpp)

target_include_directories(MinimalCefWorkspace PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CEF_ROOT}"
    "${SDL2_INCLUDE_DIRS}"
)

if(WIN32)
    target_compile_options(MinimalCefWorkspace PRIVATE /utf-8 /wd4996)

    target_link_libraries(MinimalCefWorkspace PRIVATE
        libcef_dll_wrapper
        "${CEF_BINARY_DIR}/libcef.lib"
        ${SDL2_LIBRARIES}
        OpenGL::GL
    )

    COPY_FILES(MinimalCefWorkspace "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:MinimalCefWorkspace>")
    COPY_FILES(MinimalCefWorkspace "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:MinimalCefWorkspace>")
    add_custom_command(TARGET MinimalCefWorkspace POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL}" "$<TARGET_FILE_DIR:MinimalCefWorkspace>"
    )
else()
    target_compile_options(MinimalCefWorkspace PRIVATE -Wno-error -Wno-deprecated-declarations)

    target_link_libraries(MinimalCefWorkspace PRIVATE
        libcef_dll_wrapper
        "${CEF_BINARY_DIR}/libcef.so"
        ${SDL2_LIBRARIES}
        OpenGL::GL
        pthread
        dl
    )

    set_target_properties(MinimalCefWorkspace PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )

    COPY_FILES(MinimalCefWorkspace "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    COPY_FILES(MinimalCefWorkspace "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
endif()

foreach(BG_FILE "background.png" "background.jpg" "background.bmp")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${BG_FILE}")
        add_custom_command(TARGET MinimalCefWorkspace POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${BG_FILE}"
            "$<TARGET_FILE_DIR:MinimalCefWorkspace>/${BG_FILE}"
        )
    endif()
endforeach()
