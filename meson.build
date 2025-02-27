project('gpu-screen-recorder-gtk', ['c', 'cpp'], version : '5.1.6', default_options : ['warning_level=2'])

add_project_arguments('-Wshadow', language : ['c', 'cpp'])
if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['c', 'cpp'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['c', 'cpp'])
endif

src = [
    'src/global_shortcuts.c',
    'src/main.cpp',
]

dep = [
    dependency('gtk+-3.0'),
    dependency('x11'),
    dependency('ayatana-appindicator3-0.1'),
]

prefix = get_option('prefix')
datadir = get_option('datadir')
icons_path = join_paths(prefix, datadir, 'icons')

executable('gpu-screen-recorder-gtk',
    src,
    dependencies : dep,
    install : true,
    cpp_args : [
        '-DGSR_ICONS_PATH="' + icons_path + '"',
        '-DGSR_VERSION="' + meson.project_version() + '"'
    ]
)

install_data(files('com.dec05eba.gpu_screen_recorder.desktop'), install_dir : join_paths(prefix, datadir, 'applications'))
install_data(files('com.dec05eba.gpu_screen_recorder.appdata.xml'), install_dir : join_paths(prefix, datadir, 'metainfo'))
install_subdir('icons/hicolor', install_dir : icons_path)

gnome = import('gnome')
gnome.post_install(gtk_update_icon_cache : true, update_desktop_database : true)
