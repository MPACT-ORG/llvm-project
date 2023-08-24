# Directions for including the MDL in a compiler for a CPU.
# This takes 3 parameters: (which for most CPUs are all the same value)
# - The CPU name (AArch64, ARM, X86, etc)
# - The tablegen dependence name prefix (different for some CPUs)
# - The CPU Family name (different for some CPUs)
macro(buildmdl)
  if(LLVM_ENABLE_MDL)
    add_custom_target(TdScan${ARGV0}
	    COMMAND llvm-tdscan -gen_arch_spec --family_name=${ARGV2}
	            --nowarnings ${ARGV0}.txt
	    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	    COMMENT "Generating ${ARGV0}.mdl..."
	    DEPENDS ${ARGV1}CommonTableGen
	    VERBATIM)

    set(MdlDatabase${ARGV0}
      "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlInfo.inc"
      "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlInfo.h"
      "${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}GenMdlTarget.inc"
    )

    add_custom_command(OUTPUT ${MdlDatabase${ARGV0}}
      COMMAND llvm-mdl ${ARGV0}.mdl
	      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	      COMMENT "Generating ${ARGV0}GenMdlInfo.inc..."
	      DEPENDS TdScan${ARGV0}
	      VERBATIM)

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

