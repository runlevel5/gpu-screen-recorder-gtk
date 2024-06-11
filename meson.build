project('gpu-screen-recorder-gtk', ['c', 'cpp'], version : '3.8.0', default_options : ['warning_level=2'])

add_project_arguments('-Wshadow', language : ['c', 'cpp'])
if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['c', 'cpp'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['c', 'cpp'])
endif

src = [
    'src/egl.c',
    'src/library_loader.c',
    'src/main.cpp',
]
dep = [
    dependency('gtk+-3.0'),
    dependency('x11'),
    dependency('xrandr'),
    dependency('libpulse'),
    dependency('libdrm'),
    dependency('wayland-egl'),
    dependency('wayland-client'),
    dependency('ayatana-appindicator3-0.1'),
]

executable('gpu-screen-recorder-gtk', src, dependencies : dep, install : true)

install_data(files('gpu-screen-recorder-gtk.desktop'), install_dir : '/usr/share/applications')
install_data(files('com.dec05eba.gpu_screen_recorder.appdata.xml'), install_dir : '/usr/share/metainfo')

install_subdir('icons/hicolor', install_dir : '/usr/share/icons')
