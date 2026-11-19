cmake_minimum_required(VERSION 3.18)
project(MinimalCefWorkspace LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenGL REQUIRED)


set(STB_DIR "${CMAKE_CURRENT_BINARY_DIR}/stb")
file(MAKE_DIRECTORY "${STB_DIR}")
if(NOT EXISTS "${STB_DIR}/stb_image.h")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        "${STB_DIR}/stb_image.h"
        TLS_VERIFY ON
    )
endif()


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
    include(FetchContent)
    FetchContent_Declare(
        SDL2_win
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.8/SDL2-devel-2.30.8-VC.zip
    )
    FetchContent_MakeAvailable(SDL2_win)
    set(SDL2_INCLUDE_DIRS "${sdl2_win_SOURCE_DIR}/include")
    set(SDL2_LIBRARIES
        "${sdl2_win_SOURCE_DIR}/lib/x64/SDL2.lib"
        "${sdl2_win_SOURCE_DIR}/lib/x64/SDL2main.lib"
    )
    set(SDL2_DLL "${sdl2_win_SOURCE_DIR}/lib/x64/SDL2.dll")
else()
    find_package(SDL2 REQUIRED)
endif()

add_executable(MinimalCefWorkspace main.cpp)

target_include_directories(MinimalCefWorkspace PRIVATE
    ${CEF_ROOT}
    ${SDL2_INCLUDE_DIRS}${STB_DIR}
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