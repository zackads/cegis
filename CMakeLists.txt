cmake_minimum_required(VERSION 4.2)
project(emofom)

# --- Option 1: try a system / module-provided z3 first ---
find_package(Z3 CONFIG QUIET)

if(Z3_FOUND)
    message(STATUS "Using system Z3 ${Z3_VERSION_STRING} from ${Z3_DIR}")
    set(Z3_TARGET z3::libz3)
else()
    message(STATUS "System Z3 not found.  Building vendored copy from source.")

    include(FetchContent)

    # --- Option 2: vendored local source for offline / locked-down HPC ---
    FetchContent_Declare(
            z3
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/z3
    )

    FetchContent_MakeAvailable(z3)

    set(Z3_TARGET libz3)
endif()

set(CMAKE_CXX_STANDARD 20)

add_executable(emofom
        main.cpp
        synthesizer.cpp
        problems.cpp
        program_printer.cpp
)
target_link_libraries(emofom PRIVATE ${Z3_TARGET})

if(NOT Z3_FOUND)
    target_include_directories(emofom PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/z3/src/api
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/z3/src/api/c++)
endif()