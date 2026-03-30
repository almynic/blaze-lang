cmake_minimum_required(VERSION 3.20)
project(blaze_lang C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Source files
set(BLAZE_SOURCES
    main.c
    src/memory.c
    src/value.c
    src/types.c
    src/chunk.c
    src/debug.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/typechecker.c
    src/generic.c
    src/compiler.c
    src/vm.c
    src/object.c
    src/module.c
    src/colors.c
)

# Header files (for IDE support)
set(BLAZE_HEADERS
    src/common.h
    src/memory.h
    src/value.h
    src/types.h
    src/chunk.h
    src/debug.h
    src/scanner.h
    src/ast.h
    src/parser.h
    src/typechecker.h
    src/generic.h
    src/compiler.h
    src/vm.h
    src/object.h
    src/module.h
    src/colors.h
)

add_executable(blaze ${BLAZE_SOURCES} ${BLAZE_HEADERS})

# Include directories
target_include_directories(blaze PRIVATE ${CMAKE_SOURCE_DIR}/src)

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
