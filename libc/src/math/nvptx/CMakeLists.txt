# Math functions not yet available in the libc project, or those not yet tuned
# for GPU workloads are provided as wrappers over vendor libraries. If we find
# them ahead of time we will import them statically. Otherwise, we will keep
# them as external references and expect them to be resolved by the user when
# they compile. In the future,we will use implementations from the 'libc'
# project and not provide these wrappers.
if(CUDAToolkit_FOUND)
  set(libdevice_path ${CUDAToolkit_BIN_DIR}/../nvvm/libdevice/libdevice.10.bc)
  if (EXISTS ${libdevice_path})
    message(STATUS "Found the CUDA device library. Implementations falling back "
                   "to the vendor libraries will be resolved statically.")
    set(bitcode_link_flags
        "SHELL:-Xclang -mlink-builtin-bitcode -Xclang ${libdevice_path}")
  endif()
else()
  message(STATUS "Could not find the CUDA device library. Unimplemented "
                 "functions will be an external reference to the vendor libraries.")
endif()

add_entrypoint_object(
  ceil
  SRCS
    ceil.cpp
  HDRS
    ../ceil.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  ceilf
  SRCS
    ceilf.cpp
  HDRS
    ../ceilf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  copysign
  SRCS
    copysign.cpp
  HDRS
    ../copysign.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  copysignf
  SRCS
    copysignf.cpp
  HDRS
    ../copysignf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fabs
  SRCS
    fabs.cpp
  HDRS
    ../fabs.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fabsf
  SRCS
    fabsf.cpp
  HDRS
    ../fabsf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  floor
  SRCS
    floor.cpp
  HDRS
    ../floor.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  floorf
  SRCS
    floorf.cpp
  HDRS
    ../floorf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fma
  SRCS
    fma.cpp
  HDRS
    ../fma.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fmaf
  SRCS
    fmaf.cpp
  HDRS
    ../fmaf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fmax
  SRCS
    fmax.cpp
  HDRS
    ../fmax.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fmaxf
  SRCS
    fmaxf.cpp
  HDRS
    ../fmaxf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fmin
  SRCS
    fmin.cpp
  HDRS
    ../fmin.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fminf
  SRCS
    fminf.cpp
  HDRS
    ../fminf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fmod
  SRCS
    fmod.cpp
  HDRS
    ../fmod.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  fmodf
  SRCS
    fmodf.cpp
  HDRS
    ../fmodf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  nearbyint
  SRCS
    nearbyint.cpp
  HDRS
    ../nearbyint.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  nearbyintf
  SRCS
    nearbyintf.cpp
  HDRS
    ../nearbyintf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  remainder
  SRCS
    remainder.cpp
  HDRS
    ../remainder.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  remainderf
  SRCS
    remainderf.cpp
  HDRS
    ../remainderf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  rint
  SRCS
    rint.cpp
  HDRS
    ../rint.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  rintf
  SRCS
    rintf.cpp
  HDRS
    ../rintf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  round
  SRCS
    round.cpp
  HDRS
    ../round.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  sqrt
  SRCS
    sqrt.cpp
  HDRS
    ../sqrt.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  sqrtf
  SRCS
    sqrtf.cpp
  HDRS
    ../sqrtf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  trunc
  SRCS
    trunc.cpp
  HDRS
    ../trunc.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  truncf
  SRCS
    truncf.cpp
  HDRS
    ../truncf.h
  COMPILE_OPTIONS
    -O2
)

add_entrypoint_object(
  tgamma
  SRCS
    tgamma.cpp
  HDRS
    ../tgamma.h
  COMPILE_OPTIONS
    ${bitcode_link_flags}
    -O2
)

add_entrypoint_object(
  tgammaf
  SRCS
    tgammaf.cpp
  HDRS
    ../tgammaf.h
  COMPILE_OPTIONS
    ${bitcode_link_flags}
    -O2
)

add_entrypoint_object(
  lgamma
  SRCS
    lgamma.cpp
  HDRS
    ../lgamma.h
  COMPILE_OPTIONS
    ${bitcode_link_flags}
    -O2
)

add_entrypoint_object(
  lgamma_r
  SRCS
    lgamma_r.cpp
  HDRS
    ../lgamma_r.h
  COMPILE_OPTIONS
    ${bitcode_link_flags}
    -O2
)
