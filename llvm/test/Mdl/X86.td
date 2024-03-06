// Check that we can convert the X86 tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/X86/X86.td -I %p/../../lib/Target/X86 -I %p/../../include -print-records -o /tmp/X86.txt
// RUN: llvm-tdscan /tmp/X86.txt --nowarnings --gen-arch-spec --output-dir /tmp/
// RUN: llvm-mdl /tmp/X86.mdl --output-dir /tmp/



