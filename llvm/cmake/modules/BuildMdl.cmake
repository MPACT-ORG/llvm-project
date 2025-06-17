# This file contains macros that include the MDL language in the compiler for a CPU.
# To use MDL for a target, simply include a <CPU>.mdl file in the same directory as the
# <CPU> tablegen files. We look for that file here, and enable MDL use for that CPU.
# Note that its not necessary to write descriptions for all subtargets, just the ones you
# want to support with MDL.  The undescribe subtargets will revert to using schedules or
# intineraries.
#
# The "LLVM_ENABLE_MDL" Cmake configuration parameter enables MDL for ALL targets, and will
# generate MDL descriptions for every target and subtarget that has schedules or itineraries.
# This is for testing - to make sure the MDL language supports all the architecture features
# necessary to support existing targets.

# For targets that don't use MDL, we write a few "dummy" header and inc files.
#    ARGV0 - The CPU name (AArch64, ARM, X86, etc)
#    ARGV1 - The tablegen dependence name prefix (different for some CPUs)
#    ARGV2 - The CPU Family name (different for some CPUs)
macro(makeMdlFiles)
  set(MDL_FAMILY ${ARGV2})
  set(MDL_CPU ${ARGV0})
  configure_file(${CMAKE_HOME_DIRECTORY}/cmake/modules/GenMdlInfo.inc.in
	         ${ARGV0}GenMdlInfo.inc @ONLY)
  configure_file(${CMAKE_HOME_DIRECTORY}/cmake/modules/GenMdlInfo.h.in
	         ${ARGV0}GenMdlInfo.h @ONLY)
  configure_file(${CMAKE_HOME_DIRECTORY}/cmake/modules/GenMdlTarget.inc.in
	         ${ARGV0}GenMdlTarget.inc @ONLY)
endmacro()

# Directions for including the MDL in a compiler for a CPU.
# This takes 3 parameters: (which for most CPUs are all the same value)
#    ARGV0 - The CPU name (AArch64, ARM, X86, etc)
#    ARGV1 - The tablegen dependence name prefix (different for some CPUs)
#    ARGV2 - The CPU Family name (different for some CPUs)
macro(buildmdl)
  # First check to see if we have an mdl file to compile.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ARGV0}.mdl")
    set(MDL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${ARGV0}.mdl")
    set(ENABLE_MDL_USE 1)
    add_custom_target(TdScan${ARGV0}
         COMMAND llvm-tdscan --family-name=${ARGV2} --nowarnings ${ARGV0}.txt
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
         COMMENT "Generating ${ARGV0}_instructions.mdl..."
         DEPENDS ${ARGV1}CommonTableGen VERBATIM)

  # If there isn't an mdl file, but the LLVM_ENABLE_MDL flag was specified,
  # then generate an mdl file in the binary directory. This is primarily for
  # testing for targets that don't have an MDL file.
  elseif(LLVM_ENABLE_MDL)
    set(MDL_FILE "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}.mdl")
    set(ENABLE_MDL_USE 1)
    add_custom_target(TdScan${ARGV0}
           COMMAND llvm-tdscan --family-name=${ARGV2} --gen-arch-spec
                               --nowarnings ${ARGV0}.txt
           WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
           COMMENT "Generating ${ARGV0}.mdl..."
           DEPENDS ${ARGV1}CommonTableGen VERBATIM)

  else()
    makeMdlFiles(${ARGV0} ${ARGV1} ${ARGV2})
    set(ENABLE_MDL_USE 0) 
  endif()

  # if we are using MDL, then compile the mdl file.
  if (${ENABLE_MDL_USE})
    set(MdlDatabase${ARGV0}
      "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlInfo.inc"
      "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlInfo.h"
      "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlTarget.inc")

    add_custom_command(OUTPUT ${MdlDatabase${ARGV0}}
         COMMAND llvm-mdl ${MDL_FILE}
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
         COMMENT "Generating ${ARGV0}GenMdlInfo.inc..."
         DEPENDS TdScan${ARGV0} VERBATIM)

    # tablegen macro
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
            ${MdlDatabase${ARGV0}})
    set(TABLEGEN_OUTPUT ${TABLEGEN_OUTPUT}
            "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlInfo.inc")
    set(TABLEGEN_OUTPUT ${TABLEGEN_OUTPUT}
            "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlInfo.h")
    set(TABLEGEN_OUTPUT ${TABLEGEN_OUTPUT}
            "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlTarget.inc")
    set_source_files_properties(
            ${CMAKE_CURRENT_BINARY_DIR}/${MdlDatabase${ARGV0}}
            PROPERTIES GENERATED 1)

    # add_public_tablegen_target macro
    add_custom_target(Mdl${ARGV0} DEPENDS ${TABLEGEN_OUTPUT})

    if(LLVM_COMMON_DEPENDS)
      add_dependencies(Mdl${ARGV0} ${LLVM_COMMON_DEPENDS})
    endif()

    set_target_properties(Mdl${ARGV0} PROPERTIES FOLDER "MdlGeneration")
    set(LLVM_COMMON_DEPENDS ${LLVM_COMMON_DEPENDS} Mdl${ARGV0})
  endif()
endmacro()

