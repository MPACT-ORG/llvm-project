// Check that we can convert the PowerPC tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/PowerPC/PPC.td -I %p/../../lib/Target/PowerPC -I %p/../../include -print-records -o /tmp/PPC.txt
// RUN: llvm-tdscan /tmp/PPC.txt --nowarnings --gen-arch-spec --output-dir /tmp/
// RUN: llvm-mdl /tmp/PPC.mdl --output-dir /tmp/



