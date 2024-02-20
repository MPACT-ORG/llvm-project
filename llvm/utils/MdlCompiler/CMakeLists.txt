# Cmake for building as part of LLVM
set(LLVM_LINK_COMPONENTS Support)
add_llvm_utility(llvm-mdl
	mdl.cpp mdl_dump.cpp mdl_generate.cpp mdl_main.cpp mdl_output.cpp
	mdl_predicate.cpp mdl_util.cpp mdl_parser.cpp mdl_lexer.cpp
	mdl.h mdl_parser.h mdl_generate.h mdl_output.h)

