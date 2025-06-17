// Check that we can convert the ARM tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/ARM/ARM.td -I %p/../../lib/Target/ARM -I %p/../../include -print-records -o /tmp/ARM.txt
// RUN: llvm-tdscan /tmp/ARM.txt --nowarnings --gen-arch-spec --output-dir /tmp/
// RUN: llvm-mdl /tmp/ARM.mdl --output-dir /tmp/



