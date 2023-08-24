// Check that we can convert the Hexagon tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/Hexagon/Hexagon.td -I %p/../../lib/Target/Hexagon -I %p/../../include -print-records -o /tmp/Hexagon.txt
// RUN: llvm-tdscan /tmp/Hexagon.txt --nowarnings --gen_arch_spec --output_dir /tmp/
// RUN: llvm-mdl /tmp/Hexagon.mdl --output_dir /tmp/



