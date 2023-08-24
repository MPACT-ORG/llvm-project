// Check that we can convert the R600 tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/AMDGPU/R600.td -I %p/../../lib/Target/AMDGPU -I %p/../../include -print-records -o /tmp/R600.txt
// RUN: llvm-tdscan /tmp/R600.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/R600.mdl --output_dir /tmp/



