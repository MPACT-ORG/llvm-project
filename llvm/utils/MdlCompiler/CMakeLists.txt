cmake_minimum_required(VERSION 3.13.4)
project(mdl)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# required if linking to static library
add_definitions(-DANTLR4CPP_STATIC)

# using /MD flag for antlr4_runtime (for Visual C++ compilers only)
set(ANTLR4_WITH_STATIC_CRT OFF)
# add external build for antlrcpp
include(ExternalAntlr4Cpp)
# add antrl4cpp artifacts to project environment
include_directories(${ANTLR4_INCLUDE_DIRS})

# We look in /usr/local/lib by default.  Add more places to look for Antlr.
# set(ANTLR_PATHS ...)

# Antlr versions prior to 4.10 work, but we prefer newer versions.
find_package(ANTLR 4.9 REQUIRED)
message(STATUS "Antlr version: " ${ANTLR_VERSION})

# Call macro to add grammar to your build dependencies.
antlr_target(MdlGrammarParser mdl.g4 PACKAGE  mpact::mdl::generated
	OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/libs/)

# include generated files in project environment
include_directories(${ANTLR_MdlGrammarParser_OUTPUT_DIR})

## Add LLVM
#### find_package(LLVM REQUIRED CONFIG)
#### message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
#### message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

#### include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
#### separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
#### add_definitions(${LLVM_DEFINITIONS_LIST})

# Find the libraries that correspond to the LLVM components
llvm_map_components_to_libnames(llvm_libs support)

# We're going to compile a few components from LLVMSupport with RTTI. So find
# the source directory.
get_target_property(support_sources LLVMSupport SOURCES)
get_target_property(support_dir LLVMSupport SOURCE_DIR)

# add generated grammar to mdl binary target
add_executable(llvm-mdl mdl_main.cpp mdl.cpp mdl.h mdl_dump.cpp
               mdl_generate.cpp mdl_generate.h mdl_output.cpp mdl_output.h
               mdl_predicate.cpp mdl_util.cpp mdl_visitor.cpp mdl_visitor.h
	       ${ANTLR_MdlGrammarParser_CXX_OUTPUTS}
               ${support_dir}/CommandLine.cpp
	       ${support_dir}/FormatVariadic.cpp
               ${support_dir}/Error.cpp)

# we must compile Antlr and the mdl compiler with RTTI (because of Antlr)
set_property(TARGET llvm-mdl PROPERTY CXX_STANDARD 17)
set_target_properties(llvm-mdl PROPERTIES COMPILE_FLAGS "-frtti")
set_target_properties(antlr4_static PROPERTIES COMPILE_FLAGS "-frtti")

# Link against LLVMSupport (compiled without RTTI) and Antlr libraries
target_link_libraries(llvm-mdl ${llvm_libs} antlr4_static)

