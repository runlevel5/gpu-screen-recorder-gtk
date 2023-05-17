#!/bin/sh -e

opts="-O2 -g0 -DNDEBUG -Wall -Wextra -Werror -s"
[ -n "$DEBUG" ] && opts="-O0 -g3 -Wall -Wextra -Werror";

dependencies="gtk+-3.0 x11 xrandr libpulse"
includes="$(pkg-config --cflags $dependencies)"
libs="$(pkg-config --libs $dependencies) -ldl"
gcc -c src/egl.c $opts $includes
g++ -c src/main.cpp $opts $includes
g++ -o gpu-screen-recorder-gtk -O2 egl.o main.o $libs $opts
