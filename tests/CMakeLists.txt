find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

qt_add_executable(rf_tests RfTests.cpp)
target_link_libraries(rf_tests PRIVATE rfui Qt6::Test)
target_compile_definitions(rf_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME rf_calculations_and_documents COMMAND rf_tests)

qt_add_executable(ui_tests UiTests.cpp)
target_link_libraries(ui_tests PRIVATE rfui Qt6::Test)
target_compile_definitions(ui_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
set(ui_interaction_cases
    aboutCreditsAndReferences
    touchstoneImportAndCombinedPng
    touchstoneErrorsAndUndo
    touchstoneSingleFrequency
    frequencyResponseControlsAndMarker
    frequencyResponseErrorsAndEmpty
    newElementEditors
    componentConnectionIsKeyboardReachable
    schematicReportLayout
    schematicTopologies
    schematicSelectionAndEditing
    schematicEmptyAndLongNetworks
    synthesisWizards
    synthesisInvalidPreview
    exportLtspiceFromMenu
    acceptComponentWithKeyboard
    componentKeyboardContract
    componentInvalidKeyboardEdit
    componentTabCycle
    openExamples
    exportNetworkImage
    addTuneSelectDeleteUndo
    saveLoadAndSelection
    wheelAndChartSelection
    setupAndPresets
    failedIoPreservesNetwork
    cancelEditorAndClose
    renderChart
)
set(ui_interaction_tests)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-output")
foreach(case IN LISTS ui_interaction_cases)
    set(test_name "qt_interactions_${case}")
    add_test(NAME "${test_name}" COMMAND ui_tests "${case}" -o
        "${CMAKE_BINARY_DIR}/test-output/${case}.txt,txt"
    )
    list(APPEND ui_interaction_tests "${test_name}")
    set_tests_properties("${test_name}" PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()
qt_add_executable(synthesis_tests SynthesisTests.cpp)
target_link_libraries(synthesis_tests PRIVATE rfui Qt6::Test)
add_test(NAME network_synthesis COMMAND synthesis_tests)
qt_add_executable(response_tests FrequencyResponseTests.cpp)
target_link_libraries(response_tests PRIVATE rfcore Qt6::Test)
add_test(NAME frequency_response COMMAND response_tests)
qt_add_executable(touchstone_tests TouchstoneTests.cpp)
target_link_libraries(touchstone_tests PRIVATE rfui Qt6::Test)
add_test(NAME touchstone_import COMMAND touchstone_tests)
qt_add_executable(ltspice_tests LtspiceTests.cpp)
target_link_libraries(ltspice_tests PRIVATE rfui Qt6::Test)
add_test(NAME ltspice_export COMMAND ltspice_tests)
set_tests_properties(ltspice_export PROPERTIES TIMEOUT 300)
if(WIN32)
    set_tests_properties(rf_calculations_and_documents ${ui_interaction_tests} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows"
        TIMEOUT 180
    )
else()
    set_tests_properties(rf_calculations_and_documents ${ui_interaction_tests} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180
    )
endif()
