add_library(lob_compiler_options INTERFACE)

target_compile_options(lob_compiler_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic -Wshadow
        -Wno-unused-parameter
        -march=native
        -fno-rtti
    >
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:
        -O3
        -funroll-loops
        -fomit-frame-pointer
        -DNDEBUG
    >
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:
        -O0 -g3 -fsanitize=address,undefined
    >
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:RelWithDebInfo>>:
        -O2 -g
    >
)

target_link_options(lob_compiler_options INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:
        -fsanitize=address,undefined
    >
)
