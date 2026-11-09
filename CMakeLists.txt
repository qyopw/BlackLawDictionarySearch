cmake_minimum_required(VERSION 3.10)
project(BlackLawDictionarySearch VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Исполняемый файл кладём в корень проекта (рядом с input.txt / output.txt)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(dictionary_search main.cpp)

target_compile_features(dictionary_search PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(dictionary_search PRIVATE /W4 /EHsc)
else()
    target_compile_options(dictionary_search PRIVATE -Wall -Wextra -Wpedantic)
endif()