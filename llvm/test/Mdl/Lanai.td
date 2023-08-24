// Check that we can convert the Lanai tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/Lanai/Lanai.td -I %p/../../lib/Target/Lanai -I %p/../../include -print-records -o /tmp/Lanai.txt
// RUN: llvm-tdscan /tmp/Lanai.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/Lanai.mdl --output_dir /tmp/



