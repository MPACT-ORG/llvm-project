// Check that we can convert the SystemZ tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/SystemZ/SystemZ.td -I %p/../../lib/Target/SystemZ -I %p/../../include -print-records -o /tmp/SystemZ.txt
// RUN: llvm-tdscan /tmp/SystemZ.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/SystemZ.mdl --output_dir /tmp/



