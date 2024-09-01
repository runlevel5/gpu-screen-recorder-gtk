![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

GTK frontend for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/).

This is a screen recorder that has minimal impact on system performance by recording your monitor using the GPU only,
similar to shadowplay on windows. This is the fastest screen recording tool for Linux.

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia shadowplay-like instant replay,
where only the last few minutes are saved.

More info at [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/).

# Installation
This program depends on [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/) which needs to be installed first.\
Run `sudo ./install.sh` or if you are running Arch Linux, then you can find gpu screen recorder gtk on aur under the name gpu-screen-recorder-gtk-git (`yay -S gpu-screen-recorder-gtk-git`).\
You can also install gpu screen recorder (the gtk gui version) from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder). This flatpak includes gpu-screen-recorder so no need to install that first.

# Dependencies
GPU Screen Recorder GTK uses meson build system so you need to install `meson` to build GPU Screen Recorder GTK:

## Build dependencies
These are the dependencies needed to build GPU Screen Recorder GTK:

* gtk3
* libx11
* ayatana-appindicator3-0.1c

## Runtime dependencies
There are also additional dependencies needed at runtime:

* [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/)

# Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)

# Donations
If you want to donate you can donate via bitcoin or monero.
* Bitcoin: bc1qqvuqnwrdyppf707ge27fqz2n9y9gu7lf5ypyuf
* Monero: 4An9kp2qW1C9Gah7ewv4JzcNFQ5TAX7ineGCqXWK6vQnhsGGcRpNgcn8r9EC3tMcgY7vqCKs3nSRXhejMHBaGvFdN2egYet
