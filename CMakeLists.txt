cmake_minimum_required(VERSION 3.21)

project(RFdudeSmithChartV2
    VERSION 0.1.0
    DESCRIPTION "Cross-platform RF Smith chart application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(RFDUDE_BUNDLE_QT "Deploy Qt runtime dependencies with the installed application" OFF)
if(RFDUDE_BUNDLE_QT AND UNIX AND NOT APPLE)
    find_program(QT_DEPLOY_PATCHELF_EXECUTABLE patchelf REQUIRED)
    set(QT_DEPLOY_USE_PATCHELF ON)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

file(GLOB example_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*")
foreach(example IN LISTS example_files)
    get_filename_component(example_name "${example}" NAME)
    configure_file("${example}" "${CMAKE_CURRENT_BINARY_DIR}/examples/${example_name}" COPYONLY)
endforeach()

add_library(rfcore
    src/RfModel.cpp
    src/RfModel.h
    src/FrequencyResponse.cpp
    src/FrequencyResponse.h
    src/Touchstone.cpp
    src/Touchstone.h
    src/Synthesis.cpp
    src/Synthesis.h
)
target_include_directories(rfcore PUBLIC src)
set_target_properties(rfcore PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(rfui
    src/DocumentIO.cpp
    src/DocumentIO.h
    src/Editors.cpp
    src/Editors.h
    src/FrequencyResponseDialog.cpp
    src/FrequencyResponseDialog.h
    src/ResponsePlotWidget.cpp
    src/ResponsePlotWidget.h
    src/LtspiceExport.cpp
    src/LtspiceExport.h
    src/SynthesisDialogs.cpp
    src/SynthesisDialogs.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/SmithChartWidget.cpp
    src/SmithChartWidget.h
    src/SchematicWidget.cpp
    src/SchematicWidget.h
)
target_link_libraries(rfui PUBLIC rfcore Qt6::Widgets)

qt_add_executable(RFdudeSmithChartV2 src/main.cpp)
target_link_libraries(RFdudeSmithChartV2 PRIVATE rfui)
set_target_properties(RFdudeSmithChartV2 PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.rfdude.smithchartv2
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

foreach(target rfcore rfui RFdudeSmithChartV2)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
set(rfdude_runtime_dir "${CMAKE_INSTALL_BINDIR}")
if(RFDUDE_BUNDLE_QT AND UNIX AND NOT APPLE)
    set(rfdude_private_dir "lib/rfdudesmithchartv2")
    set(rfdude_runtime_dir "${rfdude_private_dir}/bin")
    set_target_properties(RFdudeSmithChartV2 PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    file(RELATIVE_PATH RFDUDE_LAUNCHER_TARGET
        "/${CMAKE_INSTALL_BINDIR}" "/${rfdude_runtime_dir}/RFdudeSmithChartV2")
    configure_file(packaging/linux/RFdudeSmithChartV2.in
        "${CMAKE_CURRENT_BINARY_DIR}/launcher/RFdudeSmithChartV2" @ONLY)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/launcher/RFdudeSmithChartV2"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS RFdudeSmithChartV2
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${rfdude_runtime_dir}
)
install(FILES README.md LICENSE NOTICE.txt DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/examples
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/rfdude-smith-chart-v2.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/rfdude-smith-chart-v2.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

if(RFDUDE_BUNDLE_QT)
    if(UNIX AND NOT APPLE)
        qt6_generate_deploy_script(
            TARGET RFdudeSmithChartV2
            OUTPUT_SCRIPT deploy_script
            CONTENT "
function(rfdude_deploy_private_runtime)
    # Qt uses the install prefix for copying and the deploy prefix for runtime paths.
    string(APPEND CMAKE_INSTALL_PREFIX \"/${rfdude_private_dir}\")
    string(APPEND QT_DEPLOY_PREFIX \"/${rfdude_private_dir}\")
    set(QT_DEPLOY_BIN_DIR bin)
    set(QT_DEPLOY_LIB_DIR lib)
    set(QT_DEPLOY_LIBEXEC_DIR libexec)
    set(QT_DEPLOY_PLUGINS_DIR plugins)
    set(QT_DEPLOY_QML_DIR qml)
    set(QT_DEPLOY_TRANSLATIONS_DIR translations)
    qt_deploy_runtime_dependencies(
        EXECUTABLE \"$<TARGET_FILE:RFdudeSmithChartV2>\"
        GENERATE_QT_CONF
    )
endfunction()
rfdude_deploy_private_runtime()
")
    else()
        qt_generate_deploy_app_script(
            TARGET RFdudeSmithChartV2
            OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    endif()
    install(SCRIPT ${deploy_script})
endif()

set(CPACK_PACKAGE_VENDOR "RFdude")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Graphical Smith chart utility - free and unsupported")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
elseif(APPLE)
    set(CPACK_GENERATOR DragNDrop)
else()
    set(CPACK_GENERATOR DEB)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "RFdude Project")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libgcc-s1, libgl1, libegl1, libx11-6, libxkbcommon0, libxkbcommon-x11-0, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-render-util0, libxcb-xinerama0, libxcb-xkb1"
    )
endif()
include(CPack)
