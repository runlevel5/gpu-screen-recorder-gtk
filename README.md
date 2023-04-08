gtk frontend for [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/about/).

This is a screen recorder that has minimal impact on system performance by recording a window using the GPU only,
similar to shadowplay on windows. This is the fastest screen recording tool for Linux.

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia shadowplay-like instant replay,
where only the last few seconds are saved.

More info at [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/about/).

## Note
This software works only on X11 (Wayland with Xwayland is NOT supported).\
Recording a window doesn't work when using picom in glx mode. However it works in xrender mode or when recording the a monitor/screen (which uses NvFBC).\
For screen capture to work with PRIME (laptops with a nvidia gpu), you must set the primary GPU to use your dedicated nvidia graphics card. You can do this by selecting "NVIDIA (Performance Mode) in nvidia settings:\
![](https://dec05eba.com/images/nvidia-settings-prime.png)\
and then rebooting your laptop.

# Performance
On a system with a i5 4690k CPU and a GTX 1080 GPU:\
When recording Legend of Zelda Breath of the Wild at 4k, fps drops from 30 to 7 when using OBS Studio + nvenc, however when using this screen recorder the fps remains at 30.\
When recording GTA V at 4k on highest settings, fps drops from 60 to 23 when using obs-nvfbc + nvenc, however when using this screen recorder the fps only drops to 58. The quality is also much better when using gpu-screen-recorder.\
It is recommended to save the video to a SSD because of the large file size, which a slow HDD might not be fast enough to handle.\
Note that if you have a very powerful CPU and a not so powerful GPU and play a game that is bottlenecked by your GPU and barely uses your CPU then a CPU based screen recording (such as OBS with libx264 instead of nvenc) might perform slightly better than GPU Screen Recorder. At least on NVIDIA.
## Note about optimal performance on NVIDIA
NVIDIA driver has a "feature" (read: bug) where it will downclock memory transfer rate when a program uses cuda, such as GPU Screen Recorder. See https://git.dec05eba.com/gpu-screen-recorder/about/ for more information and how to overcome this.

## Installation
This program depends on [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/) which needs to be installed first.\
Run `sudo ./install.sh` or if you are running Arch Linux, then you can find gpu screen recorder gtk on aur under the name gpu-screen-recorder-gtk-git (`yay -S gpu-screen-recorder-gtk-git`).\
Dependencies needed when building using `build.sh` or `install.sh`: `gtk3 libx11 libxrandr libpulse`.\
You can also install gpu screen recorder (the gtk gui version) from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder). This flatpak includes gpu-screen-recorder so no need to install that first.\
Note that if you use the flatpak version then you wont be able to use overclocking unless you set "Coolbits" NVIDIA X setting. See https://git.dec05eba.com/gpu-screen-recorder/about/ for more information and how to overcome this.

## Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)
