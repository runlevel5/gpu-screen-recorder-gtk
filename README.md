gtk frontend for [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/).

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia-like instant replay,
where only the last few seconds are saved.

## Note
Might now work when using a compositor such as picom when using the glx backend. Using the xrender backend with picom fixes this issue.\
Does not work when using gtk client side decorations (such as on Pop OS). Either disable those (if possible), install gtk-nocsd or record the whole monitor/screen if you have NvFBC.\
NvFBC doesn't work with PRIME, so if you are using PRIME then you can't record the monitor/screen, you have to record a single window.\

## Installation
This program depends on [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/) which needs to be installed first.\
Run `./build.sh` or if you are running Arch Linux, then you can find gpu screen recorder gtk on aur under the name gpu-screen-recorder-gtk-git (`yay -S gpu-screen-recorder-gtk-git`).\
Dependencies needed when building using `build.sh`: `gtk3 libx11 libxrandr libpulse`.\
If you use a distro that isn't user friendly, such as fedora, then you can install gpu-screen-recorder-gtk with flatpak here: [gpu-screen-recorder-flatpak](https://git.dec05eba.com/gpu-screen-recorder-flatpak/about/) (Note: this install method is slow).\
Recording monitors requires a gpu with NvFBC support (note: this is not required when recording a single window!). Normally only tesla and quadro gpus support this, but by using [nvidia-patch](https://github.com/keylase/nvidia-patch) or [nvlax](https://github.com/illnyang/nvlax) you can do this on all gpus that support nvenc as well (gpus as old as the nvidia 600 series), provided you are not using outdated gpu drivers.

## Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)
