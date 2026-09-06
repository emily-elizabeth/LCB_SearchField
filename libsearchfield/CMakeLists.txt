cmake_minimum_required(VERSION 3.15)
if(APPLE)
    project(libsearchfield VERSION 1.0.0 LANGUAGES C CXX OBJCXX)
else()
    project(libsearchfield VERSION 1.0.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(searchfield SHARED)

# Force the output filename to "libsearchfield" on all platforms.
# On macOS/Linux CMake already adds the "lib" prefix, so PREFIX="" plus
# OUTPUT_NAME "libsearchfield" gives libsearchfield.dylib / libsearchfield.so.
# On Windows there is no automatic prefix, so this gives libsearchfield.dll.
set_target_properties(searchfield PROPERTIES
    OUTPUT_NAME "libsearchfield"
    PREFIX      ""
)

target_include_directories(searchfield
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(APPLE)
    target_sources(searchfield PRIVATE src/libsearchfield_osx.mm)
    target_link_libraries(searchfield PRIVATE
        "-framework Cocoa"
        "-framework AppKit"
    )

elseif(WIN32)
    target_sources(searchfield PRIVATE src/libsearchfield_win.cpp)
    target_link_libraries(searchfield PRIVATE comctl32)
    target_compile_definitions(searchfield PRIVATE UNICODE _UNICODE)

elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    target_sources(searchfield PRIVATE src/libsearchfield_lnx.cpp)
    target_include_directories(searchfield PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(searchfield PRIVATE ${GTK3_LIBRARIES})
    target_compile_options(searchfield PRIVATE ${GTK3_CFLAGS_OTHER})
endif()

# Determine the arch-platform output folder used by HyperXTalk
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CODE_SUBDIR "arm64-mac")
    else()
        set(CODE_SUBDIR "x86_64-mac")
    endif()
elseif(WIN32)
    set(CODE_SUBDIR "x86_64-win")
elseif(UNIX)
    set(CODE_SUBDIR "x86_64-lnx")
endif()

set(WIDGET_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../widget/code/${CODE_SUBDIR}")

install(TARGETS searchfield
    LIBRARY  DESTINATION "${WIDGET_CODE_DIR}"
    RUNTIME  DESTINATION "${WIDGET_CODE_DIR}"
)
install(FILES include/libsearchfield.h DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/include")
