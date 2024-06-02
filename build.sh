#!/bin/sh -e

script_dir=$(dirname "$0")
cd "$script_dir"

CC=${CC:-gcc}
CXX=${CXX:-g++}

opts="-O2 -g0 -DNDEBUG -Wall -Wextra -Werror"
[ -n "$DEBUG" ] && opts="-O0 -g3 -Wall -Wextra -Werror";

build_gsr_gtk() {
    dependencies="gtk+-3.0 x11 xrandr libpulse libdrm wayland-egl wayland-client appindicator3-0.1"
    includes="$(pkg-config --cflags $dependencies)"
    libs="$(pkg-config --libs $dependencies) -ldl"
    $CC -c src/egl.c $opts $includes
    $CC -c src/library_loader.c $opts $includes
    $CXX -c src/main.cpp $opts $includes
    $CXX -o gpu-screen-recorder-gtk egl.o library_loader.o main.o $libs $opts
}

build_gsr_gtk
echo "Successfully built gpu-screen-recorder-gtk"
