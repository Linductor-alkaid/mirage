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
    src/update_crypto_openssl.cpp
    src/update_manifest.cpp
    src/update_apply.cpp
    src/update_fetcher_posix.cpp
)

if(UNIX)
    target_sources(mirage_update PRIVATE src/update_crypto_openssl.cpp)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        target_link_libraries(mirage_update PRIVATE OpenSSL::Crypto)
        target_compile_definitions(mirage_update PRIVATE MIRAGE_UPDATE_HAS_ED25519=1)
        message(STATUS "Updater ed25519 verification enabled (OpenSSL)")
    else()
        target_compile_definitions(mirage_update PRIVATE MIRAGE_UPDATE_HAS_ED25519=0)
        message(STATUS "Updater ed25519 verification DISABLED: OpenSSL not found (fail closed)")
    endif()
elseif(WIN32)
    # Windows: the fetch port compiles over WinSock2; the crypto face is
    # fail-closed in this delivery — the Windows updater build wires the
    # ed25519 verifier when the packaging step ships an OpenSSL Crypto
    # runtime (tracked in the M5-12 verification record).
    target_sources(mirage_update PRIVATE src/update_fetcher_windows.cpp)
    target_compile_definitions(mirage_update PRIVATE MIRAGE_UPDATE_HAS_ED25519=0)
    target_link_libraries(mirage_update PRIVATE ws2_32)
    message(STATUS "Updater Windows fetch enabled; ed25519 verification DISABLED (fail closed)")
endif()

# The pinned mira/json parser (header-only usage via Mira::core).
target_link_libraries(mirage_update PUBLIC Mira::core)
