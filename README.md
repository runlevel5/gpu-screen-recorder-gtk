![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

gtk frontend for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/).

This is a screen recorder that has minimal impact on system performance by recording your monitor using the GPU only,
similar to shadowplay on windows. This is the fastest screen recording tool for Linux.

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia shadowplay-like instant replay,
where only the last few minutes are saved.

More info at [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/).

## Note
This software works on x11 and wayland. Hotkeys are also not supported on wayland (wayland doesn't really support this properly yet). Use X11 if you want a proper desktop experience in general.
### TEMPORARY ISSUES
1) Videos are in variable framerate format. Use MPV to play such videos, otherwise you might experience stuttering in the video if you are using a buggy video player. You can try saving the video into a .mkv file instead as some software may have better support for .mkv files (such as kdenlive). You can use the "-fm cfr" option to to use constant framerate mode.
### AMD/Intel/Wayland root permission
When recording a window under AMD/Intel no special user permission is required, however when recording a monitor (or when using wayland) the program needs root permission (to access KMS).
This is safe in GPU Screen Recorder as the part that needs root access has been moved to its own small program that only does one thing.
For you as a user this only means that if you installed GPU Screen Recorder as a flatpak then a prompt asking for root password will show up when you start recording.
Note that this only applies to when recording a monitor. On Wayland you can use the desktop portal capture option to record a monitor without root permission.
# Performance
On a system with a i5 4690k CPU and a GTX 1080 GPU:\
When recording Legend of Zelda Breath of the Wild at 4k, fps drops from 30 to 7 when using OBS Studio + nvenc, however when using this screen recorder the fps remains at 30.\
When recording GTA V at 4k on highest settings, fps drops from 60 to 23 when using obs-nvfbc + nvenc, however when using this screen recorder the fps only drops to 58. The quality is also much better when using gpu-screen-recorder.\
It is recommended to save the video to a SSD because of the large file size, which a slow HDD might not be fast enough to handle.
## Note about optimal performance on NVIDIA
NVIDIA driver has a "feature" (read: bug) where it will downclock memory transfer rate when a program uses cuda (or nvenc, which uses cuda), such as GPU Screen Recorder. See https://git.dec05eba.com/gpu-screen-recorder/about/ for more information and how to overcome this.

## Installation
This program depends on [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/) which needs to be installed first.\
Run `sudo ./install.sh` or if you are running Arch Linux, then you can find gpu screen recorder gtk on aur under the name gpu-screen-recorder-gtk-git (`yay -S gpu-screen-recorder-gtk-git`).\
Dependencies needed when building using `install.sh`: `meson gtk3 libx11 libpulse ayatana-appindicator3-0.1`.\
You can also install gpu screen recorder (the gtk gui version) from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder). This flatpak includes gpu-screen-recorder so no need to install that first.

## Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)

# Donations
If you want to donate you can donate via bitcoin or monero.
* Bitcoin: bc1qqvuqnwrdyppf707ge27fqz2n9y9gu7lf5ypyuf
* Monero: 4An9kp2qW1C9Gah7ewv4JzcNFQ5TAX7ineGCqXWK6vQnhsGGcRpNgcn8r9EC3tMcgY7vqCKs3nSRXhejMHBaGvFdN2egYet
