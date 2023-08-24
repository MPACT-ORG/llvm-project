# Cmake for building as part of LLVM
set(LLVM_LINK_COMPONENTS Support)
add_llvm_utility(llvm-tdscan scan.cpp arch.cpp debug.cpp register.cpp output.cpp)

