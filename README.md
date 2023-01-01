gtk frontend for [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/).

This is a screen recorder that has minimal impact on system performance by recording a window using the GPU only,
similar to shadowplay on windows. This is the fastest screen recording tool for Linux.

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia shadowplay-like instant replay,
where only the last few seconds are saved.

More info at [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/).

## Note
This software works only on x11 and with an nvidia gpu.\
Recording a window doesn't work when using picom in glx mode. However it works in xrender mode or when recording the a monitor/screen (which uses NvFBC).\
For screen capture to work with PRIME (laptops with a nvidia gpu), you must set the primary GPU to use your dedicated nvidia graphics card. You can do this by selecting "NVIDIA (Performance Mode) in nvidia settings:\
![](https://dec05eba.com/images/nvidia-settings-prime.png)\
and then rebooting your laptop.

## Installation
This program depends on [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/) which needs to be installed first.\
Run `./install.sh` as root or if you are running Arch Linux, then you can find gpu screen recorder gtk on aur under the name gpu-screen-recorder-gtk-git (`yay -S gpu-screen-recorder-gtk-git`).\
Dependencies needed when building using `build.sh` or `install.sh`: `gtk3 libx11 libxrandr libpulse`.\
You can also install gpu screen recorder (the gtk gui version) from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder). This flatpak includes gpu-screen-recorder so no need to install that first.\

## Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)
