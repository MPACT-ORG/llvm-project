// Check that we can convert the RISCV tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/RISCV/RISCV.td -I %p/../../lib/Target/RISCV -I %p/../../include -print-records -o /tmp/RISCV.txt
// RUN: llvm-tdscan /tmp/RISCV.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/RISCV.mdl --output_dir /tmp/



