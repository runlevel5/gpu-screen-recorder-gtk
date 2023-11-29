#!/bin/sh -e

script_dir=$(dirname "$0")
cd "$script_dir"

CC=${CC:-gcc}
CXX=${CXX:-g++}

opts="-O2 -g0 -DNDEBUG -Wall -Wextra -Werror -s"
[ -n "$DEBUG" ] && opts="-O0 -g3 -Wall -Wextra -Werror";

build_wayland_protocol() {
    wayland-scanner private-code external/wlr-export-dmabuf-unstable-v1.xml external/wlr-export-dmabuf-unstable-v1-protocol.c
    wayland-scanner client-header external/wlr-export-dmabuf-unstable-v1.xml external/wlr-export-dmabuf-unstable-v1-client-protocol.h
}

build_gsr_gtk() {
    dependencies="gtk+-3.0 x11 xrandr libpulse libcap libdrm wayland-egl wayland-client"
    includes="$(pkg-config --cflags $dependencies)"
    libs="$(pkg-config --libs $dependencies) -ldl"
    $CC -c src/egl.c $opts $includes
    $CC -c src/library_loader.c $opts $includes
    $CC -c external/wlr-export-dmabuf-unstable-v1-protocol.c $opts $includes
    $CXX -c src/main.cpp $opts $includes
    $CXX -o gpu-screen-recorder-gtk egl.o library_loader.o wlr-export-dmabuf-unstable-v1-protocol.o main.o $libs $opts
}

build_wayland_protocol
build_gsr_gtk
echo "Successfully built gpu-screen-recorder-gtk"
