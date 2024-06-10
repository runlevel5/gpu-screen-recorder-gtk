#!/bin/sh

[ $(id -u) -ne 0 ] && echo "You need root privileges to run the uninstall script" && exit 1

rm -f "/usr/bin/gpu-screen-recorder-gtk"
rm -f "/usr/share/applications/com.dec05eba.gpu_screen_recorder.desktop"
rm -f "/usr/share/metainfo/com.dec05eba.gpu_screen_recorder.appdata.xml"

rm -f "/usr/share/icons/hicolor/32x32/apps/com.dec05eba.gpu_screen_recorder.png"
rm -f "/usr/share/icons/hicolor/64x64/apps/com.dec05eba.gpu_screen_recorder.png"
rm -f "/usr/share/icons/hicolor/128x128/apps/com.dec05eba.gpu_screen_recorder.png"

rm -rf "/usr/share/com.dec05eba.gpu_screen_recorder"

echo "Successfully uninstalled gpu-screen-recorder-gtk"