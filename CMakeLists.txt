cmake_minimum_required(VERSION 3.20)

project(PdfFormEditor VERSION 0.1.0 LANGUAGES CXX)

add_executable(pdf-form-editor src/app/main.cpp)
target_compile_features(pdf-form-editor PRIVATE cxx_std_20)
set_target_properties(pdf-form-editor PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(pdf-form-editor PRIVATE /W4)
else()
    target_compile_options(pdf-form-editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

option(PDF_EDITOR_BUILD_XFA_PROBE "Build the isolated Linux PDFium XFA integration probe" OFF)
if(PDF_EDITOR_BUILD_XFA_PROBE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The XFA probe currently requires the Linux Bubblewrap sandbox")
    endif()
    find_package(PDFium CONFIG REQUIRED)
    if(NOT PDFium_VERSION VERSION_EQUAL "157.0.8086.0")
        message(FATAL_ERROR "The probe requires pinned PDFium 157.0.8086.0")
    endif()
    if(NOT PDF_EDITOR_PDFIUM_PATCHSET STREQUAL "1")
        message(FATAL_ERROR "The probe requires the persistence fix: run python3 tools/build_pdfium.py and use .deps/pdfium-patched")
    endif()
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    find_program(NODE_EXECUTABLE node REQUIRED)
    execute_process(COMMAND "${NODE_EXECUTABLE}" --version OUTPUT_VARIABLE node_version
        OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE node_status)
    string(REGEX REPLACE "^v" "" node_version "${node_version}")
    if(NOT node_status EQUAL 0 OR node_version VERSION_LESS "24.0.0")
        message(FATAL_ERROR "The pinned independent reader requires Node.js 24 or newer")
    endif()
    set(PDF_EDITOR_PDFJS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/reader/node_modules/pdfjs-dist"
        CACHE PATH "Pinned PDF.js package for independent XFA validation")
    if(NOT EXISTS "${PDF_EDITOR_PDFJS_DIR}/legacy/build/pdf.mjs")
        message(FATAL_ERROR "Independent reader is missing: run npm ci --prefix tests/reader --ignore-scripts")
    endif()
    find_program(BWRAP_EXECUTABLE bwrap REQUIRED)
    file(READ "${PDFium_DIR}/args.gn" pdfium_args)
    if(NOT pdfium_args MATCHES "pdf_enable_xfa = true" OR
       NOT pdfium_args MATCHES "pdf_enable_v8 = true")
        message(FATAL_ERROR "PDFium must be built with both XFA and V8 enabled")
    endif()
    add_library(pdf-xfa-probe STATIC src/pdf/XfaProbe.cpp)
    target_compile_features(pdf-xfa-probe PUBLIC cxx_std_20)
    target_include_directories(pdf-xfa-probe PUBLIC src)
    target_compile_definitions(pdf-xfa-probe PRIVATE PDF_ENABLE_XFA PDF_ENABLE_V8)
    target_link_libraries(pdf-xfa-probe PRIVATE pdfium)
    target_compile_options(pdf-xfa-probe PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(pdf-xfa-probe PROPERTIES CXX_EXTENSIONS OFF)
    add_executable(xfa-probe-worker src/app/xfa_probe_main.cpp)
    target_link_libraries(xfa-probe-worker PRIVATE pdf-xfa-probe)
endif()

if(BUILD_TESTING)
    add_test(NAME foundation-smoke COMMAND pdf-form-editor)
    set_tests_properties(foundation-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "PDF Form Editor 0.1.0")
    if(PDF_EDITOR_BUILD_XFA_PROBE)
        add_test(NAME xfa-regressions
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xfa_probe.py"
                "$<TARGET_FILE:xfa-probe-worker>" "${PDFium_DIR}")
        set_tests_properties(xfa-regressions PROPERTIES TIMEOUT 90 LABELS "regression;security")
        add_test(NAME xfa-roundtrip
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_xfa_roundtrip.py"
                "$<TARGET_FILE:xfa-probe-worker>" "${PDFium_DIR}" "${NODE_EXECUTABLE}" "${PDF_EDITOR_PDFJS_DIR}")
        set_tests_properties(xfa-roundtrip PROPERTIES TIMEOUT 90 LABELS "milestone;independent-reader")
    endif()
endif()
