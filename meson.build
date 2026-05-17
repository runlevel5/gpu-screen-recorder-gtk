project('gpu-screen-recorder-gtk', 'c', version : '5.7.9', default_options : ['warning_level=2', 'c_std=c11'])

add_project_arguments('-Wshadow', language : 'c')
if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : 'c')
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : 'c')
endif

src = [
    'src/main.c',
    'src/str_util.c',
    'src/xdg_paths.c',
    'src/app_state.c',
    'src/audio_devices.c',
    'src/notifications.c',
    'src/recorder_process.c',
    'src/x11_hotkeys.c',
    'src/ui/page_common_settings.c',
    'src/ui/page_replay.c',
    'src/ui/page_recording.c',
    'src/ui/page_streaming.c',
]

inc = include_directories('src')

cc = meson.get_compiler('c')

# Motif and X11 stack. Motif has no pkg-config file on most distros, so we
# find each library and header explicitly. Xt has a pkg-config in modern
# X.Org releases; fall back to cc.find_library if absent.
xt_dep = dependency('xt', required : false)
if not xt_dep.found()
    xt_dep = cc.find_library('Xt', required : true)
endif

dep = [
    cc.find_library('Xm', required : true, has_headers : ['Xm/Xm.h']),
    xt_dep,
    dependency('x11'),
    cc.find_library('Xmu', required : true),
    cc.find_library('Xpm', required : true),
    cc.find_library('Xext', required : true),
]

prefix = get_option('prefix')
datadir = get_option('datadir')
icons_path = join_paths(prefix, datadir, 'icons')

executable('gpu-screen-recorder-gtk',
    src,
    dependencies : dep,
    include_directories : inc,
    install : true,
    c_args : [
        '-DGSR_ICONS_PATH="' + icons_path + '"',
        '-DGSR_VERSION="' + meson.project_version() + '"',
        '-D_XOPEN_SOURCE=700',
        '-D_DEFAULT_SOURCE',
    ]
)

install_data(files('com.dec05eba.gpu_screen_recorder.desktop'), install_dir : join_paths(prefix, datadir, 'applications'))
install_subdir('icons/hicolor', install_dir : icons_path)
