cmake_minimum_required(VERSION 3.16)
project(NDLang VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NDLANG_WITH_LLVM "Build the LLVM backend (object code emission)" ON)
option(NDLANG_ALL_TARGETS "Link every LLVM target so --target can cross-compile (e.g. aarch64)" OFF)
option(NDLANG_BUILD_TESTS "Build and register the test suite" ON)

if(MSVC)
  add_compile_options(/W4)
  set(NDL_NO_WARNINGS "/w")
else()
  add_compile_options(-Wall -Wextra)
  set(NDL_NO_WARNINGS "-w")
endif()

# ---------------------------------------------------------------- Flex / Bison
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

set(NDL_FLEX_FLAGS "")
if(FLEX_EXECUTABLE MATCHES "win_flex")
  set(NDL_FLEX_FLAGS "--wincompat")
endif()

# Scanner for the NDLang front end (the hand-written parser consumes its tokens).
FLEX_TARGET(NDLangScanner src/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp
            COMPILE_FLAGS "${NDL_FLEX_FLAGS}")

# ------------------------------------------------------------------ front end
add_library(ndlang_front STATIC
  src/Lexer.cpp
  src/Parser.cpp
  src/Sema.cpp
  src/ASTPrinter.cpp
  src/Interpreter.cpp
  ${FLEX_NDLangScanner_OUTPUTS})
target_include_directories(ndlang_front PUBLIC include)
# Flex-generated code is not warning-clean.
set_source_files_properties(${FLEX_NDLangScanner_OUTPUTS} PROPERTIES COMPILE_FLAGS
  "${NDL_NO_WARNINGS}")

# ------------------------------------------------------------------- compiler
add_executable(ndlangc src/main.cpp)
target_link_libraries(ndlangc PRIVATE ndlang_front)

if(NDLANG_WITH_LLVM)
  find_package(LLVM CONFIG)
  if(LLVM_FOUND)
    message(STATUS "NDLang: using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
    separate_arguments(NDL_LLVM_DEFS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
    if(NDLANG_ALL_TARGETS)
      set(NDL_LLVM_TARGET_COMPONENTS all-targets)
      target_compile_definitions(ndlangc PRIVATE NDLANG_ALL_TARGETS)
    else()
      set(NDL_LLVM_TARGET_COMPONENTS native)
    endif()
    llvm_map_components_to_libnames(NDL_LLVM_LIBS core support passes ${NDL_LLVM_TARGET_COMPONENTS})
    target_sources(ndlangc PRIVATE src/CodeGen.cpp)
    target_include_directories(ndlangc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(ndlangc PRIVATE NDLANG_HAVE_LLVM ${NDL_LLVM_DEFS})
    target_link_libraries(ndlangc PRIVATE ${NDL_LLVM_LIBS})
  else()
    message(WARNING "NDLang: LLVM not found - building without the backend "
                    "(--emit-tokens, --emit-ast and --interpret still work). "
                    "Set LLVM_DIR to enable it.")
  endif()
endif()

# --------------------------------------- Bison/LR comparison (expression only)
BISON_TARGET(ExprParser tools/bison_expr/expr.y ${CMAKE_CURRENT_BINARY_DIR}/expr.tab.cpp
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/expr.tab.h
             REPORT_FILE ${CMAKE_CURRENT_BINARY_DIR}/expr.output
             COMPILE_FLAGS "-Wall")
FLEX_TARGET(ExprScanner tools/bison_expr/expr.l ${CMAKE_CURRENT_BINARY_DIR}/expr_lex.cpp
            COMPILE_FLAGS "${NDL_FLEX_FLAGS}")
ADD_FLEX_BISON_DEPENDENCY(ExprScanner ExprParser)
add_executable(ndlang-bison-expr ${BISON_ExprParser_OUTPUTS} ${FLEX_ExprScanner_OUTPUTS})
target_include_directories(ndlang-bison-expr PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(ndlang-bison-expr PROPERTIES COMPILE_FLAGS "${NDL_NO_WARNINGS}")

# ---------------------------------------------------------------------- tests
if(NDLANG_BUILD_TESTS)
  enable_testing()
  add_executable(unit_tests tests/unit_tests.cpp)
  target_link_libraries(unit_tests PRIVATE ndlang_front)
  add_test(NAME unit COMMAND unit_tests)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME examples_interpreted
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/run_tests.py
                     $<TARGET_FILE:ndlangc> ${CMAKE_SOURCE_DIR} --mode interpret)
    if(LLVM_FOUND)
      add_test(NAME examples_compiled
               COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/run_tests.py
                       $<TARGET_FILE:ndlangc> ${CMAKE_SOURCE_DIR} --mode compile)
    endif()
    add_test(NAME bison_expr
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/run_bison_tests.py
                     $<TARGET_FILE:ndlang-bison-expr>)
  endif()
endif()
