// Check that we can convert the AMDGPU tablegen files to MDL and compile them.
// This is really just a sanity check.

// RUN: llvm-tblgen %p/../../lib/Target/AMDGPU/AMDGPU.td -I %p/../../lib/Target/AMDGPU -I %p/../../include -print-records -o /tmp/AMDGPU.txt
// RUN: llvm-tdscan /tmp/AMDGPU.txt --nowarnings --gen-arch-spec --output-dir /tmp/
// RUN: llvm-mdl /tmp/AMDGPU.mdl --output-dir /tmp/

