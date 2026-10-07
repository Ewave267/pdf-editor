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
if(BUILD_TESTING)
    add_test(NAME foundation-smoke COMMAND pdf-form-editor)
    set_tests_properties(foundation-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "PDF Form Editor 0.1.0")
endif()
