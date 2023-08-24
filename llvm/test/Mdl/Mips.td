// Check that we can convert the Mips tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/Mips/Mips.td -I %p/../../lib/Target/Mips -I %p/../../include -print-records -o /tmp/Mips.txt
// RUN: llvm-tdscan /tmp/Mips.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/Mips.mdl --output_dir /tmp/



