cmake_minimum_required(VERSION 3.16)
project(playos-compositor C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── Find dependencies ──────────────────────────────────────────
find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND_SERVER wayland-server REQUIRED)
pkg_check_modules(WLROOTS wlroots REQUIRED)
pkg_check_modules(PIXMAN pixman-1 REQUIRED)
pkg_check_modules(XKBCOMMON xkbcommon REQUIRED)
pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols REQUIRED)

# ── Wayland scanner for protocols ──────────────────────────────
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
set(PROTOCOL_XML  "${PROTOCOL_DIR}/playos-v1.xml")

# System xdg-shell protocol
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY ${GEN_DIR})

# Generate server headers and code from playos-v1.xml
add_custom_command(
    OUTPUT ${GEN_DIR}/playos-v1-protocol.h ${GEN_DIR}/playos-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} server-header ${PROTOCOL_XML} ${GEN_DIR}/playos-v1-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code  ${PROTOCOL_XML} ${GEN_DIR}/playos-v1-protocol.c
    DEPENDS ${PROTOCOL_XML}
    COMMENT "Generating Wayland protocol code from playos-v1.xml"
)

# Generate xdg-shell protocol header (wlroots requires it)
add_custom_command(
    OUTPUT ${GEN_DIR}/xdg-shell-protocol.h ${GEN_DIR}/xdg-shell-protocol.c
    COMMAND ${WAYLAND_SCANNER} server-header ${XDG_SHELL_XML} ${GEN_DIR}/xdg-shell-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code  ${XDG_SHELL_XML} ${GEN_DIR}/xdg-shell-protocol.c
    DEPENDS ${XDG_SHELL_XML}
    COMMENT "Generating xdg-shell protocol code"
)

# ── Compositor source files ────────────────────────────────────
set(COMPOSITOR_SOURCES
    src/main.c
    src/compositor.c
    src/trusted_client.c
    src/readiness.c
    ${GEN_DIR}/playos-v1-protocol.h
    ${GEN_DIR}/playos-v1-protocol.c
    ${GEN_DIR}/xdg-shell-protocol.h
    ${GEN_DIR}/xdg-shell-protocol.c
)

add_executable(playos-compositor ${COMPOSITOR_SOURCES})

target_include_directories(playos-compositor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GEN_DIR}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${WLROOTS_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${WAYLAND_PROTOCOLS_INCLUDE_DIRS}
)

target_link_libraries(playos-compositor PRIVATE
    ${WAYLAND_SERVER_LIBRARIES}
    ${WLROOTS_LIBRARIES}
    ${PIXMAN_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
)

target_compile_definitions(playos-compositor PRIVATE
    WLR_USE_UNSTABLE
    _POSIX_C_SOURCE=199309L
    _DEFAULT_SOURCE
)

# ── Compiler flags ─────────────────────────────────────────────
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(playos-compositor PRIVATE -Wall -Wextra)
endif()

# ── Installation ───────────────────────────────────────────────
install(TARGETS playos-compositor RUNTIME DESTINATION bin)

# ── Tests ──────────────────────────────────────────────────────
option(BUILD_TESTS "Build test programs" ON)

if(BUILD_TESTS)
    add_subdirectory(tools/test-client)

    # Headless integration test
    add_executable(compositor-headless-test
        tests/headless/test_headless.c
        src/compositor.c
    )
    target_include_directories(compositor-headless-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GEN_DIR}
        ${WAYLAND_SERVER_INCLUDE_DIRS}
        ${WLROOTS_INCLUDE_DIRS}
        ${PIXMAN_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${WAYLAND_PROTOCOLS_INCLUDE_DIRS}
    )
    target_link_libraries(compositor-headless-test PRIVATE
        ${WAYLAND_SERVER_LIBRARIES}
        ${WLROOTS_LIBRARIES}
        ${PIXMAN_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
    )
    target_compile_definitions(compositor-headless-test PRIVATE
        WLR_USE_UNSTABLE
        _POSIX_C_SOURCE=199309L
        _DEFAULT_SOURCE
    )

    # Nested Wayland validation test (developer iteration)
    add_executable(compositor-nested-test
        tests/nested/test_nested.c
        src/compositor.c
        ${GEN_DIR}/xdg-shell-protocol.h
        ${GEN_DIR}/xdg-shell-protocol.c
    )
    target_include_directories(compositor-nested-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GEN_DIR}
        ${WAYLAND_SERVER_INCLUDE_DIRS}
        ${WLROOTS_INCLUDE_DIRS}
        ${PIXMAN_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${WAYLAND_PROTOCOLS_INCLUDE_DIRS}
    )
    target_link_libraries(compositor-nested-test PRIVATE
        ${WAYLAND_SERVER_LIBRARIES}
        ${WLROOTS_LIBRARIES}
        ${PIXMAN_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
    )
    target_compile_definitions(compositor-nested-test PRIVATE
        WLR_USE_UNSTABLE
        _POSIX_C_SOURCE=199309L
        _DEFAULT_SOURCE
    )
endif()
