cmake_minimum_required(VERSION 3.20)
project(blaze_lang C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Source files (grouped under src/<module>/ — see src/README.md)
set(BLAZE_SOURCES
    main.c
    src/base/memory.c
    src/base/value.c
    src/base/chunk.c
    src/semantics/types.c
    src/runtime/debug.c
    src/syntax/scanner.c
    src/syntax/ast.c
    src/syntax/parser.c
    src/semantics/typechecker.c
    src/semantics/generic.c
    src/codegen/compiler.c
    src/runtime/vm.c
    src/runtime/object.c
    src/module/module.c
    src/support/colors.c
)

# Header files (for IDE support)
set(BLAZE_HEADERS
    src/base/common.h
    src/base/memory.h
    src/base/value.h
    src/base/chunk.h
    src/semantics/types.h
    src/runtime/debug.h
    src/syntax/scanner.h
    src/syntax/ast.h
    src/syntax/parser.h
    src/semantics/typechecker.h
    src/semantics/generic.h
    src/codegen/compiler.h
    src/runtime/vm.h
    src/runtime/object.h
    src/module/module.h
    src/support/colors.h
)

add_executable(blaze ${BLAZE_SOURCES} ${BLAZE_HEADERS})

# Include roots: flat #include "vm.h" etc. (no path prefix in sources)
target_include_directories(blaze PRIVATE
    ${CMAKE_SOURCE_DIR}/src/base
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/syntax
    ${CMAKE_SOURCE_DIR}/src/semantics
    ${CMAKE_SOURCE_DIR}/src/codegen
    ${CMAKE_SOURCE_DIR}/src/module
    ${CMAKE_SOURCE_DIR}/src/support
)

# Find and link readline
find_library(READLINE_LIBRARY readline PATHS /opt/homebrew/lib /usr/local/lib)
find_path(READLINE_INCLUDE_DIR readline/readline.h PATHS /opt/homebrew/include /usr/local/include)

if(READLINE_LIBRARY AND READLINE_INCLUDE_DIR)
    message(STATUS "Found readline: ${READLINE_LIBRARY}")
    target_link_libraries(blaze ${READLINE_LIBRARY})
    target_include_directories(blaze PRIVATE ${READLINE_INCLUDE_DIR})
    target_compile_definitions(blaze PRIVATE HAVE_READLINE=1)
else()
    message(STATUS "Readline not found, using built-in line editing")
endif()
