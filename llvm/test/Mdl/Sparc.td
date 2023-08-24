// Check that we can convert the Sparc tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/Sparc/Sparc.td -I %p/../../lib/Target/Sparc -I %p/../../include -print-records -o /tmp/SP.txt
// RUN: llvm-tdscan /tmp/SP.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/SP.mdl --output_dir /tmp/



