find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT wayland-client REQUIRED)

# Generate xdg-shell client bindings
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
    OUTPUT ${GEN_DIR}/xdg-shell-client-protocol.h ${GEN_DIR}/xdg-shell-client-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${GEN_DIR}/xdg-shell-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code  ${XDG_SHELL_XML} ${GEN_DIR}/xdg-shell-client-protocol.c
    DEPENDS ${XDG_SHELL_XML}
    COMMENT "Generating xdg-shell client protocol code"
)

add_executable(playos-test-client
    src/main.c
    ${GEN_DIR}/xdg-shell-client-protocol.h
    ${GEN_DIR}/xdg-shell-client-protocol.c
)

target_include_directories(playos-test-client PRIVATE
    ${GEN_DIR}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
)

target_link_libraries(playos-test-client PRIVATE
    ${WAYLAND_CLIENT_LIBRARIES}
)

target_compile_definitions(playos-test-client PRIVATE
    _POSIX_C_SOURCE=199309L
    _DEFAULT_SOURCE
)

install(TARGETS playos-test-client RUNTIME DESTINATION bin)
