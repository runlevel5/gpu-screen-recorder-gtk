#!/bin/sh -e

dependencies="gtk+-3.0 x11 xrandr libpulse"
includes="$(pkg-config --cflags $dependencies)"
libs="$(pkg-config --libs $dependencies) -ldl"
gcc -c src/egl.c -O2 -g0 -DNDEBUG $includes
g++ -c src/main.cpp -O2 -g0 -DNDEBUG $includes
g++ -o gpu-screen-recorder-gtk -O2 egl.o main.o -s $libs
