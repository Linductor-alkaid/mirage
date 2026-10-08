# Shared compiler warning settings for Mirage's own targets. Pinned
# third_party dependencies keep their own warning policies.
#
# GCC/Clang carry the flag set the Linux gate has always used; MSVC (the
# Windows product toolchain, DEC-017) gets its conformance equivalent — the
# GCC-style flags are invalid there (D8021) and were never compiled away.

function(mirage_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /w14242 # conversion: possible loss of data
            /w14254 # operator conversion: loss of data
            /w14263 # member initialization order
            /w14265 # 'virtual' in non-base-class member function
            /w14302 # off-screen pointer arithmetic in __analysis_assume
            /w14826 # conversion is sign-extended
        )
        if(MIRAGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Woverloaded-virtual
        )
        if(MIRAGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        # -Wuseless-cast is retired for GCC: the pinned mira headers now carry
        # deliberate static_casts for libc++ compatibility (for example the
        # Timestamp nanoseconds helpers in core_contracts.hpp), and GCC flags
        # them in every consuming translation unit. Pinned code cannot change;
        # see feedback ledger entry MIRA-20261008-002 for the removal
        # condition (upstream drops the libstdc++-redundant cast).
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(${target} PRIVATE -Wno-useless-cast)
        endif()
    endif()
endfunction()
