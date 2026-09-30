# The in-app updater core (M5-12, DEC-032): manifest decode, ed25519
# verification, fetch and the atomic apply engine. The crypto face is
# compiled only where OpenSSL libcrypto exists — everywhere else it fails
# closed at the verify boundary (the same optional-boundary discipline as
# the pinned TLS transport).

add_library(mirage_update STATIC)
add_library(Mirage::update ALIAS mirage_update)

target_include_directories(mirage_update
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mirage_update PUBLIC cxx_std_20)
mirage_enable_warnings(mirage_update)

target_sources(mirage_update PRIVATE
    src/update_client.cpp
    src/update_manifest.cpp
    src/update_apply.cpp
    src/sha256.cpp
)

# Crypto face: the ed25519 verifier needs libcrypto. Where it exists the
# real verifier compiles; where it does not (e.g. the MSVC full-tree job)
# a fail-closed stub is compiled instead — the updater refuses every
# signature loudly rather than silently trusting one. Digests (SHA-256)
# stay available on every toolchain via the embedded implementation.
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    target_sources(mirage_update PRIVATE
        src/update_crypto_openssl.cpp
    )
    target_link_libraries(mirage_update PRIVATE OpenSSL::Crypto)
    target_compile_definitions(mirage_update PRIVATE MIRAGE_UPDATE_HAS_ED25519=1)
    message(STATUS "Updater ed25519 verification enabled (OpenSSL)")
else()
    target_sources(mirage_update PRIVATE
        src/update_crypto_stub.cpp
    )
    target_compile_definitions(mirage_update PRIVATE MIRAGE_UPDATE_HAS_ED25519=0)
    message(STATUS "Updater ed25519 verification DISABLED: OpenSSL not found (fail closed)")
endif()

# Fetch port: POSIX sockets vs WinSock2, one per platform.
if(UNIX)
    target_sources(mirage_update PRIVATE src/update_fetcher_posix.cpp)
elseif(WIN32)
    target_sources(mirage_update PRIVATE src/update_fetcher_windows.cpp)
    target_link_libraries(mirage_update PRIVATE ws2_32)
endif()

# The pinned mira/json parser (header-only usage via Mira::core).
target_link_libraries(mirage_update PUBLIC Mira::core)
