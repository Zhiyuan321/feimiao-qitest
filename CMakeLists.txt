cmake_minimum_required(VERSION 3.24)

project(QITestWorkstation LANGUAGES CXX)

# Keep the application target identifier and engineering paths in English.
# MSVC emits QITestWorkstation.exe to avoid linker code-page failures; MinGW
# and macOS retain the Chinese customer-facing product name below. Plugins and
# packaging scripts depend on these stable identifiers.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(QITEST_BUILD_TESTS "Build QITest automated tests" ON)
# Enable this option for the Windows 7 build. Other platforms use the Qt 6 branch.
option(QITEST_WIN7 "Build the separate Windows 7 SP1 x64 compatibility target" OFF)
set(QITEST_QT_COMPONENTS Core Gui Widgets Network Sql Svg)
if(QITEST_BUILD_TESTS)
    list(APPEND QITEST_QT_COMPONENTS Test)
endif()
if(QITEST_WIN7)
    if(NOT WIN32)
        message(FATAL_ERROR "QITEST_WIN7 is only supported for Windows targets")
    endif()
    find_package(Qt5 5.12.12 EXACT REQUIRED COMPONENTS ${QITEST_QT_COMPONENTS})
    set(QT_VERSION_MAJOR 5)
    if(CMAKE_CROSSCOMPILING)
        foreach(tool moc rcc uic)
            if(NOT EXISTS "${QITEST_QT5_HOST_ROOT}/bin/${tool}")
                message(FATAL_ERROR "Set QITEST_QT5_HOST_ROOT to Qt 5.12.12 macOS host tools")
            endif()
            set_target_properties(Qt5::${tool} PROPERTIES
                IMPORTED_LOCATION "${QITEST_QT5_HOST_ROOT}/bin/${tool}")
        endforeach()
    endif()
    add_compile_definitions(QITEST_WIN7=1 WINVER=0x0601 _WIN32_WINNT=0x0601)
else()
    find_package(Qt6 6.9 REQUIRED COMPONENTS ${QITEST_QT_COMPONENTS})
    set(QT_VERSION_MAJOR 6)
    qt_standard_project_setup()
endif()

add_library(qitest_core STATIC
# Scientific computation and simulated-instrument foundation; no UI sources.
    src/device/IInstrumentAdapter.h
    src/core/AnalysisEngine.cpp
    src/core/QuantitationEngine.cpp
    src/core/CalibrationModel.cpp
    src/core/ChromatogramEngine.cpp
    src/core/SpectralComparison.cpp
    src/device/SimulatedInstrument.cpp
)
target_include_directories(qitest_core PUBLIC src)
target_link_libraries(qitest_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_library(qitest_library STATIC
    src/library/SpectralLibraryRepository.cpp
    src/library/UserStandardRepository.cpp
)
target_include_directories(qitest_library PUBLIC src)
target_link_libraries(qitest_library PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

add_library(qitest_workspace STATIC
# Persistence, archives, and reports; kept separate from model inference.
    src/storage/WorkspaceRepository.cpp
    src/storage/RunArchiveCodec.cpp
    src/storage/ScanSeriesCodec.cpp
    src/storage/CalibrationDocument.cpp
    src/storage/IntegrationDocument.cpp
    src/storage/ArchiveImportWorker.cpp
    src/report/ReportGenerator.cpp
    src/support/DiagnosticBundle.cpp
)
target_include_directories(qitest_workspace PUBLIC src)
target_link_libraries(qitest_workspace PUBLIC qitest_core Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Sql)

add_library(qitest_security STATIC src/security/AuthorizationPolicy.cpp)
target_include_directories(qitest_security PUBLIC src)
target_link_libraries(qitest_security PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_library(qitest_ai_evidence STATIC
    src/ai/AiEvidenceBuilder.cpp
    src/ai/AiCommandRouter.cpp
    src/ai/AiToolProtocol.cpp
    src/ai/LocalKnowledgeStore.cpp
    src/ai/AiSafetyGuard.cpp
)
target_include_directories(qitest_ai_evidence PUBLIC src)
target_link_libraries(qitest_ai_evidence PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)

add_executable(qitest_library_import tools/LibraryImportMain.cpp)
target_link_libraries(qitest_library_import PRIVATE qitest_library Qt${QT_VERSION_MAJOR}::Core)

# JOM and NMake parse generated Makefile paths through the active Windows code
# page. Invoke rcc directly for Makefile generators so resource compilation is
# independent of that parser. Ninja and Xcode continue to use AUTORCC.
set(QITEST_QRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/qitest.qrc")
function(qitest_add_app_qrc target)
    if(WIN32 AND CMAKE_GENERATOR MATCHES "Makefiles")
        set(out "${CMAKE_CURRENT_BINARY_DIR}/${target}_qrc_qitest.cpp")
        set(rcc_args -name qitest)
        if(QT_VERSION_MAJOR EQUAL 6)
            list(PREPEND rcc_args --no-zstd)
        endif()
        add_custom_command(
            OUTPUT "${out}"
            COMMAND "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::rcc>" ${rcc_args} -o "${out}" "${QITEST_QRC}"
            DEPENDS "${QITEST_QRC}"
            VERBATIM
            COMMENT "RCC qitest.qrc for ${target}"
        )
        target_sources(${target} PRIVATE "${out}")
    else()
        target_sources(${target} PRIVATE "${QITEST_QRC}")
    endif()
endfunction()

set(QITEST_APP_SOURCES
# Sources for the production desktop application, not the automated test binaries.
    src/ui/InstrumentWorkbench.cpp
    src/ui/MethodEditorDialog.cpp
    src/main.cpp
    src/app/AppController.cpp
    src/ai/LocalAiBridge.cpp
    src/ui/MainWindow.cpp
    src/ui/ChatTranscript.cpp
    src/ui/SpectrumPlot.cpp
    src/ui/ChromatogramDialog.cpp
    src/ui/CalibrationPage.cpp
    src/ui/UserStandardsPage.cpp
    src/ui/StandardComparisonDialog.cpp
    src/ui/scientz/theme/ScientzTheme.cpp
    src/ui/scientz/models/ScientzActionRegistry.cpp
)
if(APPLE)
    list(APPEND QITEST_APP_SOURCES resources/brand/qitest-app.icns)
    set_source_files_properties(resources/brand/qitest-app.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()
add_executable(QITestWorkstation ${QITEST_APP_SOURCES})
qitest_add_app_qrc(QITestWorkstation)
if(WIN32)
    enable_language(RC)
    configure_file(resources/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/qitest-windows.rc" @ONLY)
    target_sources(QITestWorkstation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/qitest-windows.rc")
endif()
target_include_directories(QITestWorkstation PRIVATE src)
target_link_libraries(QITestWorkstation PRIVATE qitest_core qitest_library qitest_workspace qitest_security qitest_ai_evidence Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Svg)
target_compile_definitions(QITestWorkstation PRIVATE
    QITEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    set_target_properties(QITestWorkstation PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "QITestWorkstation"
    )
else()
    set_target_properties(QITestWorkstation PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "飞秒质谱工作站"
    )
endif()
if(APPLE)
    set_target_properties(QITestWorkstation PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "飞秒质谱工作站"
        MACOSX_BUNDLE_GUI_IDENTIFIER "cn.feimiao.massspectrometry.workstation.qt"
        MACOSX_BUNDLE_ICON_FILE "qitest-app.icns"
    )
endif()

if(QITEST_BUILD_TESTS)
enable_testing()
add_executable(qitest_device_tests tests/InstrumentControlTests.cpp src/app/AppController.cpp src/ai/LocalAiBridge.cpp)
target_include_directories(qitest_device_tests PRIVATE src)
target_link_libraries(qitest_device_tests PRIVATE qitest_core qitest_library qitest_workspace
    qitest_security qitest_ai_evidence Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
target_compile_definitions(qitest_device_tests PRIVATE QITEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME qitest_device_tests COMMAND qitest_device_tests)
add_executable(qitest_ai_lifecycle_tests tests/LocalAiBridgeTests.cpp src/ai/LocalAiBridge.cpp)
target_include_directories(qitest_ai_lifecycle_tests PRIVATE src)
target_link_libraries(qitest_ai_lifecycle_tests PRIVATE qitest_ai_evidence
    Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME qitest_ai_lifecycle_tests COMMAND qitest_ai_lifecycle_tests)

add_executable(qitest_core_tests tests/CoreTests.cpp)
target_link_libraries(qitest_core_tests PRIVATE qitest_core Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME qitest_core_tests COMMAND qitest_core_tests)

add_executable(qitest_ai_tests
    tests/AiEvidenceTests.cpp
)
target_include_directories(qitest_ai_tests PRIVATE src)
target_link_libraries(qitest_ai_tests PRIVATE qitest_ai_evidence qitest_workspace qitest_core Qt${QT_VERSION_MAJOR}::Test)
target_compile_definitions(qitest_ai_tests PRIVATE QITEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME qitest_ai_tests COMMAND qitest_ai_tests)

add_executable(qitest_library_tests tests/LibraryTests.cpp)
target_link_libraries(qitest_library_tests PRIVATE qitest_library Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME qitest_library_tests COMMAND qitest_library_tests)

add_executable(qitest_workspace_tests tests/WorkspaceTests.cpp tests/fixtures/public_ms/fixtures.qrc)
target_link_libraries(qitest_workspace_tests PRIVATE qitest_workspace qitest_core Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME qitest_workspace_tests COMMAND qitest_workspace_tests)

add_executable(qitest_security_tests tests/SecurityTests.cpp)
target_link_libraries(qitest_security_tests PRIVATE qitest_security Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME qitest_security_tests COMMAND qitest_security_tests)

add_executable(qitest_ui_tests
    src/ui/InstrumentWorkbench.cpp
    src/ui/MethodEditorDialog.cpp
    tests/UiSmokeTests.cpp
    src/ui/UserStandardsPage.cpp
    src/ui/StandardComparisonDialog.cpp
    tests/fixtures/public_ms/fixtures.qrc
    src/ui/CalibrationPage.cpp
    src/app/AppController.cpp
    src/ai/LocalAiBridge.cpp
    src/ui/MainWindow.cpp
    src/ui/ChatTranscript.cpp
    src/ui/SpectrumPlot.cpp
    src/ui/ChromatogramDialog.cpp
    src/ui/scientz/theme/ScientzTheme.cpp
    src/ui/scientz/models/ScientzActionRegistry.cpp
)
qitest_add_app_qrc(qitest_ui_tests)
target_include_directories(qitest_ui_tests PRIVATE src)
target_link_libraries(qitest_ui_tests PRIVATE qitest_core qitest_library qitest_workspace qitest_security
    qitest_ai_evidence Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Svg Qt${QT_VERSION_MAJOR}::Test)
target_compile_definitions(qitest_ui_tests PRIVATE QITEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME qitest_ui_tests COMMAND qitest_ui_tests)
set_tests_properties(qitest_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
