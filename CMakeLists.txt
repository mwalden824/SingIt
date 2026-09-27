cmake_minimum_required(VERSION 3.16)

project(SingIt VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Multimedia)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        musicdatabase.h
        musicdatabase.cpp
        third_party/sqlite/sqlite3.c
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SingIt
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        musicdatabase.h musicdatabase.cpp
        musicimporter.h musicimporter.cpp
        simsearchmodel.h simsearchmodel.cpp
    )
else()
    if(ANDROID)
        add_library(SingIt SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(SingIt
            ${PROJECT_SOURCES}
        )
    endif()
endif()


# ============================================================
# ONNX Runtime
# ============================================================

target_include_directories(SingIt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tokenizers-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)

target_link_directories(SingIt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tokenizers-cpp/build-mingw
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tokenizers-cpp/build-mingw/x86_64-pc-windows-gnu/release
)

target_link_libraries(SingIt PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    onnxruntime
    tokenizers_cpp
    tokenizers_c
    ws2_32
    ntdll
)

target_link_options(SingIt PRIVATE
    -Wno-drectve
)

# Copy ONNX Runtime DLL next to the executable after building
if(WIN32)
    add_custom_command(TARGET SingIt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:SingIt>"
    )
endif()


# ============================================================
# Application properties
# ============================================================

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.SingIt)
endif()

set_target_properties(SingIt PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)

install(TARGETS SingIt
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SingIt)
endif()