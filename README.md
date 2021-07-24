gtk frontend for [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/).

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia-like instant replay,
where only the last few seconds are saved.

## Installation
gpu screen recorder gtk can be built using [sibs](https://git.dec05eba.com/sibs) or if you are running Arch Linux, then you can find it on aur under the name gpu-screen-recorder-gtk-git (`yay -S gpu-screen-recorder-gtk-git`). [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/) needs to be installed to use gpu screen recorder gtk.\
Recording monitors requires a gpu with NvFBC support. Normally only tesla and quadro gpus support this, but by using https://github.com/keylase/nvidia-patch you can do this on all gpus that support nvenc as well (gpus as old as the nvidia 600 series), provided you are not using outdated gpu drivers.

## Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)
