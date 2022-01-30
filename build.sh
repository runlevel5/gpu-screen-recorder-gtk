#!/bin/sh -e

dependencies="gtk+-3.0 x11 xrandr libpulse"
includes="$(pkg-config --cflags $dependencies)"
libs="$(pkg-config --libs $dependencies)"
g++ -o gpu-screen-recorder-gtk -O2 src/main.cpp -s $includes $libs
