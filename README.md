gtk frontend for [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/). The gpu-screen-recorder needs to be installed to use this gtk frontend.

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia-like instant replay,
where only the last few seconds are saved.

# TODO
* Stop streaming/recording if the child process dies. This could happen when out of disk space, or when streaming network connection is lost
* Stop recording if gpu-screen-recorder exits with an error
* Create directories up to the output file when recording

# Screenshots
![](https://www.dec05eba.com/images/gpu-screen-recorder.png)
