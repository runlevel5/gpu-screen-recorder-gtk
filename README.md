gtk frontend for [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/).

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia-like instant replay,
where only the last few seconds are saved.

## Note
Does not work when using gtk client side decorations (such as on Pop OS). Either disable those (if possible), install gtk-nocsd or record the whole monitor/screen if you have NvFBC.\
NvFBC doesn't work with PRIME, so if you are using PRIME then you can't record the monitor/screen, you have to record a single window.\

All recording modes record in hevc (h265) in a mp4 format, except live streaming that records in h264 in a flv format.

## Installation
This program depends on [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/) which needs to be installed first.\
Run `./install.sh` as root or if you are running Arch Linux, then you can find gpu screen recorder gtk on aur under the name gpu-screen-recorder-gtk-git (`yay -S gpu-screen-recorder-gtk-git`).\
Dependencies needed when building using `build.sh` or `install.sh`: `gtk3 libx11 libxrandr libpulse`.\
You can also install gpu screen recorder (the gtk gui version) from flathub: https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder. This flatpak includes gpu-screen-recorder so no need to install that first.\
Recording monitors requires a gpu with NvFBC support (note: this is not required when recording a single window!). Normally only tesla and quadro gpus support this, but by using [nvidia-patch](https://github.com/keylase/nvidia-patch) or [nvlax](https://github.com/illnyang/nvlax) you can do this on all gpus that support nvenc as well (gpus as old as the nvidia 600 series), provided you are not using outdated gpu drivers.

## Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)
