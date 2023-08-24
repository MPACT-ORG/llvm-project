// Check that we can convert the AArch64 tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/AArch64/AArch64.td -I %p/../../lib/Target/AArch64 -I %p/../../include -print-records -o /tmp/AArch64.txt
// RUN: llvm-tdscan /tmp/AArch64.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/AArch64.mdl --output_dir /tmp/



