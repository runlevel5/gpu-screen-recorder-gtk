#include "config.hpp"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <assert.h>
#include <string>
#include <pulse/pulseaudio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <functional>
#include <vector>
extern "C" {
#include "egl.h"
}
#include <xf86drmMode.h>
#include <xf86drm.h>

typedef struct {
    Display *display;
    GtkApplication *app;
    GtkButton *select_window_button;
    Window selected_window;
} SelectWindowUserdata;

typedef struct {
    GtkApplication *app;
    GtkStack *stack;
    GtkWidget *common_settings_page;
    GtkWidget *replay_page;
    GtkWidget *recording_page;
    GtkWidget *streaming_page;
} PageNavigationUserdata;

static GtkWidget *window;
static SelectWindowUserdata select_window_userdata;
static PageNavigationUserdata page_navigation_userdata;
static Cursor crosshair_cursor;
static GtkSpinButton *fps_entry;
static GtkLabel *area_size_label;
static GtkGrid *area_size_grid;
static GtkSpinButton *area_width_entry;
static GtkSpinButton *area_height_entry;
static GtkComboBox *record_area_selection_menu;
static GtkTreeModel *record_area_selection_model;
static GtkComboBoxText *quality_input_menu;
static GtkComboBoxText *video_codec_input_menu;
static GtkComboBoxText *audio_codec_input_menu;
static GtkComboBoxText *color_range_input_menu;
static GtkComboBoxText *framerate_mode_input_menu;
static GtkComboBoxText *stream_service_input_menu;
static GtkComboBoxText *record_container;
static GtkComboBoxText *replay_container;
static GtkLabel *stream_key_label;
static GtkButton *record_file_chooser_button;
static GtkButton *replay_file_chooser_button;
static GtkButton *stream_button;
static GtkButton *record_button;
static GtkButton *replay_button;
static GtkButton *replay_back_button;
static GtkButton *record_back_button;
static GtkButton *stream_back_button;
static GtkButton *replay_save_button;
static GtkButton *start_recording_button;
static GtkButton *pause_recording_button;
static GtkButton *start_replay_button;
static GtkButton *start_streaming_button;
static GtkEntry *stream_id_entry;
static GtkSpinButton *replay_time_entry;
static GtkButton *select_window_button;
static GtkWidget *audio_input_used_list;
static GtkWidget *add_audio_input_button;
static GtkWidget *record_hotkey_button;
static GtkWidget *pause_unpause_hotkey_button;
static GtkWidget *replay_start_stop_hotkey_button;
static GtkWidget *replay_save_hotkey_button;
static GtkWidget *streaming_hotkey_button;
static GtkWidget *merge_audio_tracks_button;
static GtkWidget *show_notification_button;
static GtkGrid *video_codec_grid;
static GtkGrid *audio_codec_grid;
static GtkGrid *color_range_grid;
static GtkGrid *framerate_mode_grid;
static GtkComboBoxText *view_combo_box;
static GtkGrid *overclock_grid;
static GtkWidget *overclock_button;
static GtkGrid *recording_bottom_panel_grid;
static GtkWidget *recording_record_time_label;
static GtkWidget *recording_record_icon;
static GtkGrid *streaming_bottom_panel_grid;
static GtkWidget *streaming_record_time_label;
static GtkGrid *replay_bottom_panel_grid;
static GtkWidget *replay_record_time_label;

static double record_start_time_sec = 0.0;
static double pause_start_sec = 0.0;
static double paused_time_offset_sec = 0.0;

static XIM xim;
static XIC xic;

static bool replaying = false;
static bool recording = false;
static bool paused = false;
static bool streaming = false;
static pid_t gpu_screen_recorder_process = -1;
static int prev_exit_status = -1;
static Config config;
static std::string record_file_current_filename;
static bool nvfbc_installed = false;

static bool wayland = false;
static gsr_egl egl;

struct AudioInput {
    std::string name;
    std::string description;
};

struct PulseAudioServerInfo {
    std::string default_sink_name;
    std::string default_source_name;
};

static std::vector<AudioInput> audio_inputs;
static PulseAudioServerInfo pa_default_sources;

enum class HotkeyMode {
    NoAction,
    NewHotkey,
    Record
};

static HotkeyMode hotkey_mode = HotkeyMode::NoAction;

struct Hotkey {
    uint32_t modkey_mask = 0;
    KeySym keysym = None;
    GtkWidget *hotkey_entry = nullptr;
    GtkWidget *hotkey_active_label = nullptr;
};

static Hotkey *current_hotkey = nullptr;
static Hotkey pressed_hotkey;
static Hotkey latest_hotkey;
static Hotkey streaming_hotkey;
static Hotkey record_hotkey;
static Hotkey pause_unpause_hotkey;
static Hotkey replay_start_stop_hotkey;
static Hotkey replay_save_hotkey;

struct SupportedVideoCodecs {
    bool h264;
    bool hevc;
    bool av1;
};

static SupportedVideoCodecs supported_video_codecs;
static bool got_supported_video_codecs = false;

struct Container {
    const char *container_name;
    const char *file_extension;
};

static const Container supported_containers[] = {
    { "mp4", "mp4" },
    { "flv", "flv" },
    { "matroska", "mkv" }, // TODO: Default to this on amd/intel, add (Recommended on AMD/Intel)
    { "mov", "mov" }
};

struct AudioRow {
    GtkWidget *row;
    GtkComboBoxText *input_list;
};

typedef enum {
    GPU_VENDOR_AMD,
    GPU_VENDOR_INTEL,
    GPU_VENDOR_NVIDIA
} gpu_vendor;

typedef struct {
    gpu_vendor vendor;
    int gpu_version; /* 0 if unknown */
} gpu_info;

static gpu_info gpu_inf;

static bool is_program_installed(const StringView program_name) {
    const char *path = getenv("PATH");
    if(!path)
        return false;

    bool program_installed = false;
    char full_program_path[PATH_MAX];
    string_split_char(path, ':', [&](StringView line) -> bool {
        snprintf(full_program_path, sizeof(full_program_path), "%.*s/%.*s", (int)line.size, line.str, (int)program_name.size, program_name.str);
        if(access(full_program_path, F_OK) == 0) {
            program_installed = true;
            return false;
        }
        return true;
    });
    return program_installed;
}

static bool is_inside_flatpak(void) {
    return getenv("FLATPAK_ID") != NULL;
}

static bool is_pkexec_installed() {
    if(is_inside_flatpak())
        return system("flatpak-spawn --host pkexec --version") == 0;
    else
        return is_program_installed({ "pkexec", 6 });
}

static bool flatpak_is_installed_as_system(void) {
    static bool installed_as_system = false;
    static bool checked = false;
    if(!checked) {
        checked = true;
        installed_as_system = system("flatpak-spawn --host flatpak run --system --command=pwd com.dec05eba.gpu_screen_recorder") == 0;
    }
    return installed_as_system;
}

static double clock_get_monotonic_seconds(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 0.000000001;
}

static void pa_state_cb(pa_context *c, void *userdata) {
    pa_context_state state = pa_context_get_state(c);
    int *pa_ready = (int*)userdata;
    switch(state) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        default:
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            *pa_ready = 2;
            break;
        case PA_CONTEXT_READY:
            *pa_ready = 1;
            break;
    }
}

static void pa_sourcelist_cb(pa_context*, const pa_source_info *source_info, int eol, void *userdata) {
    if(eol > 0)
        return;

    std::vector<AudioInput> *inputs = (std::vector<AudioInput>*)userdata;
    inputs->push_back({ source_info->name, source_info->description });
}

static std::vector<AudioInput> get_pulseaudio_inputs() {
    std::vector<AudioInput> inputs;
    pa_mainloop *main_loop = pa_mainloop_new();

    pa_context *ctx = pa_context_new(pa_mainloop_get_api(main_loop), "gpu-screen-recorder-gtk");
    pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    int state = 0;
    int pa_ready = 0;
    pa_context_set_state_callback(ctx, pa_state_cb, &pa_ready);

    pa_operation *pa_op = NULL;

    for(;;) {
        // Not ready
        if(pa_ready == 0) {
            pa_mainloop_iterate(main_loop, 1, NULL);
            continue;
        }

        switch(state) {
            case 0: {
                pa_op = pa_context_get_source_info_list(ctx, pa_sourcelist_cb, &inputs);
                ++state;
                break;
            }
        }

        // Couldn't get connection to the server
        if(pa_ready == 2 || (state == 1 && pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE)) {
            if(pa_op)
                pa_operation_unref(pa_op);
            pa_context_disconnect(ctx);
            pa_context_unref(ctx);
            pa_mainloop_free(main_loop);
            return inputs;
        }

        pa_mainloop_iterate(main_loop, 1, NULL);
    }

    pa_mainloop_free(main_loop);
    return {};
}

static void server_info_callback(pa_context*, const pa_server_info *server_info, void *userdata) {
    PulseAudioServerInfo *u = (PulseAudioServerInfo*)userdata;
    if(server_info->default_sink_name)
        u->default_sink_name = std::string(server_info->default_sink_name) + ".monitor";
    if(server_info->default_source_name)
        u->default_source_name = server_info->default_source_name;
}

static PulseAudioServerInfo get_pulseaudio_default_inputs() {
    PulseAudioServerInfo server_info;
    pa_mainloop *main_loop = pa_mainloop_new();

    pa_context *ctx = pa_context_new(pa_mainloop_get_api(main_loop), "gpu-screen-recorder-gtk");
    pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    int state = 0;
    int pa_ready = 0;
    pa_context_set_state_callback(ctx, pa_state_cb, &pa_ready);

    pa_operation *pa_op = NULL;

    for(;;) {
        // Not ready
        if(pa_ready == 0) {
            pa_mainloop_iterate(main_loop, 1, NULL);
            continue;
        }

        switch(state) {
            case 0: {
                pa_op = pa_context_get_server_info(ctx, server_info_callback, &server_info);
                ++state;
                break;
            }
        }

        // Couldn't get connection to the server
        if(pa_ready == 2 || (state == 1 && pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE)) {
            if(pa_op)
                pa_operation_unref(pa_op);
            pa_context_disconnect(ctx);
            pa_context_unref(ctx);
            pa_mainloop_free(main_loop);
            return server_info;
        }

        pa_mainloop_iterate(main_loop, 1, NULL);
    }

    pa_mainloop_free(main_loop);
    return server_info;
}

static void used_audio_input_loop_callback(GtkWidget *row, gpointer userdata) {
    const AudioRow *audio_row = (AudioRow*)g_object_get_data(G_OBJECT(row), "audio-row");
    std::function<void(const AudioRow*)> &callback = *(std::function<void(const AudioRow*)>*)userdata;
    callback(audio_row);
}

static void for_each_used_audio_input(GtkListBox *list_box, std::function<void(const AudioRow*)> callback) {
    gtk_container_foreach(GTK_CONTAINER(list_box), used_audio_input_loop_callback, &callback);
}

static void drag_begin (GtkWidget *widget, GdkDragContext *context, gpointer) {
    GtkAllocation alloc;
    int x, y;
    double sx, sy;

    GtkWidget *row = gtk_widget_get_ancestor(widget, GTK_TYPE_LIST_BOX_ROW);
    gtk_widget_get_allocation(row, &alloc);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
    cairo_t *cr = cairo_create(surface);

    gtk_style_context_add_class(gtk_widget_get_style_context (row), "drag-icon");
    gtk_widget_draw(row, cr);
    gtk_style_context_remove_class(gtk_widget_get_style_context (row), "drag-icon");

    gtk_widget_translate_coordinates(widget, row, 0, 0, &x, &y);
    cairo_surface_get_device_scale(surface, &sx, &sy);
    cairo_surface_set_device_offset(surface, -x * sx, -y * sy);
    gtk_drag_set_icon_surface(context, surface);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

static void drag_data_get(GtkWidget *widget, GdkDragContext*, GtkSelectionData *selection_data, guint, guint, gpointer) {
    gtk_selection_data_set(selection_data, gdk_atom_intern_static_string("GTK_LIST_BOX_ROW"),
                          32,
                          (const guchar *)&widget,
                          sizeof(gpointer));
}

static void drag_data_received(GtkWidget *widget, GdkDragContext*,
    gint, gint,
    GtkSelectionData *selection_data,
    guint, guint32, gpointer)
{
    GtkWidget *target = widget;

    int pos = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW (target));
    GtkWidget *row = *(GtkWidget**)gtk_selection_data_get_data(selection_data);
    GtkWidget *source = gtk_widget_get_ancestor(row, GTK_TYPE_LIST_BOX_ROW);

    if (source == target)
        return;

    GtkWidget *list_box = gtk_widget_get_parent(source);
    g_object_ref(source);
    gtk_container_remove(GTK_CONTAINER(list_box), source);
    gtk_list_box_insert(GTK_LIST_BOX(list_box), source, pos);
    g_object_unref(source);
}

static std::string record_area_selection_menu_get_active_id() {
    std::string id_str;
    GtkTreeIter iter;
    if(!gtk_combo_box_get_active_iter(record_area_selection_menu, &iter))
        return id_str;

    gchar *id;
    gtk_tree_model_get(record_area_selection_model, &iter, 1, &id, -1);
    id_str = id;
    g_free(id);
    return id_str;
}

static void record_area_selection_menu_set_active_id(const gchar *id) {
    GtkTreeIter iter;
    if(!gtk_tree_model_get_iter_first(record_area_selection_model, &iter))
        return;

    do {
        gchar *row_id = nullptr;
        gtk_tree_model_get(record_area_selection_model, &iter, 1, &row_id, -1);

        const bool found_row = strcmp(row_id, id) == 0;
        g_free(row_id);
        if(found_row) {
            gtk_combo_box_set_active_iter(record_area_selection_menu, &iter);
            break;
        }
    } while(gtk_tree_model_iter_next(record_area_selection_model, &iter));
}

static void enable_stream_record_button_if_info_filled() {
    if(!wayland) {
        const std::string selected_window_area = record_area_selection_menu_get_active_id();
        if(strcmp(selected_window_area.c_str(), "window") == 0 && select_window_userdata.selected_window == None) {
            gtk_widget_set_sensitive(GTK_WIDGET(replay_button), false);
            gtk_widget_set_sensitive(GTK_WIDGET(record_button), false);
            gtk_widget_set_sensitive(GTK_WIDGET(stream_button), false);
            return;
        }
    }

    gtk_widget_set_sensitive(GTK_WIDGET(replay_button), true);
    gtk_widget_set_sensitive(GTK_WIDGET(record_button), true);
    gtk_widget_set_sensitive(GTK_WIDGET(stream_button), true);
}

static GtkWidget* create_used_audio_input_row(void) {
    char entry_name[] = "GTK_LIST_BOX_ROW";
    const GtkTargetEntry entries[] = {
        { entry_name, GTK_TARGET_SAME_APP, 0 }
    };

    GtkWidget *row = gtk_list_box_row_new();

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_add(GTK_CONTAINER(row), box);

    GtkWidget *handle = gtk_event_box_new();
    GtkWidget *image = gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(handle), image);
    gtk_container_add(GTK_CONTAINER(box), handle);

    GtkComboBoxText *input_list = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    for(const auto &audio_input : audio_inputs) {
        gtk_combo_box_text_append(input_list, audio_input.name.c_str(), audio_input.description.c_str());
    }
    gtk_widget_set_hexpand(GTK_WIDGET(input_list), true);
    gtk_combo_box_set_active(GTK_COMBO_BOX(input_list), 0);
    //gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), id);
    gtk_container_add(GTK_CONTAINER(box), GTK_WIDGET(input_list));

    GtkWidget *remove_button = gtk_button_new_with_label("Remove");
    gtk_widget_set_halign(remove_button, GTK_ALIGN_END);
    gtk_container_add(GTK_CONTAINER(box), remove_button);

    gtk_drag_source_set(handle, GDK_BUTTON1_MASK, entries, 1, GDK_ACTION_MOVE);
    g_signal_connect(handle, "drag-begin", G_CALLBACK(drag_begin), NULL);
    g_signal_connect(handle, "drag-data-get", G_CALLBACK(drag_data_get), NULL);

    gtk_drag_dest_set(row, GTK_DEST_DEFAULT_ALL, entries, 1, GDK_ACTION_MOVE);
    g_signal_connect(row, "drag-data-received", G_CALLBACK(drag_data_received), NULL);

    AudioRow *audio_row = new AudioRow();
    audio_row->row = row;
    audio_row->input_list = input_list;
    g_object_set_data(G_OBJECT(row), "audio-row", audio_row);

    g_signal_connect(remove_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userdata){
        AudioRow *audio_row = (AudioRow*)userdata;
        gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(audio_row->row)), audio_row->row);
        delete audio_row;
        return true;
    }), audio_row);

    return row;
}

// Return true from |callback_func| to continue to the next row
static void for_each_item_in_combo_box(GtkComboBox *combo_box, std::function<bool(gint row_index, const gchar *row_id, const gchar *row_text)> callback_func) {
    const int id_column = gtk_combo_box_get_id_column(GTK_COMBO_BOX(combo_box));
    const int text_column = gtk_combo_box_get_entry_text_column(GTK_COMBO_BOX(combo_box));

    GtkTreeModel *tree_model = gtk_combo_box_get_model(combo_box);
    GtkTreeIter tree_iter;
    if(!gtk_tree_model_get_iter_first(tree_model, &tree_iter))
        return;

    gint row_index = 0;
    do {
        gchar *row_id = nullptr;
        gtk_tree_model_get(tree_model, &tree_iter, id_column, &row_id, -1);

        gchar *row_text = nullptr;
        gtk_tree_model_get(tree_model, &tree_iter, text_column, &row_text, -1);

        bool cont = true;
        if(row_id && row_text)
            cont = callback_func(row_index, row_id, row_text);

        g_free(row_id);
        g_free(row_text);

        if(!cont)
            break;

        ++row_index;
    } while(gtk_tree_model_iter_next(tree_model, &tree_iter));
}

static gint combo_box_text_get_row_by_label(GtkComboBox *combo_box, const char *label, std::string &id) {
    gint found_index = -1;
    for_each_item_in_combo_box(combo_box, [&found_index, &label, &id](gint row_index, const gchar *row_id, const gchar *row_text) {
        if(strcmp(row_text, label) == 0) {
            id = row_id;
            found_index = row_index;
            return false;
        }
        return true;
    });
    return found_index;
}

static bool is_directory(const char *filepath) {
    struct stat file_stat;
    memset(&file_stat, 0, sizeof(file_stat));
    int ret = stat(filepath, &file_stat);
    if(ret == 0)
        return S_ISDIR(file_stat.st_mode);
    return false;
}

static std::string get_date_str() {
    char str[128];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(str, sizeof(str)-1, "%Y-%m-%d_%H-%M-%S", t);
    return str;
}

static void save_configs() {
    config.main_config.record_area_option = record_area_selection_menu_get_active_id();
    if(!wayland) {
        config.main_config.record_area_width = gtk_spin_button_get_value_as_int(area_width_entry);
        config.main_config.record_area_height = gtk_spin_button_get_value_as_int(area_height_entry);
    }
    config.main_config.fps = gtk_spin_button_get_value_as_int(fps_entry);
    config.main_config.merge_audio_tracks = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(merge_audio_tracks_button));

    config.main_config.audio_input.clear();
    for_each_used_audio_input(GTK_LIST_BOX(audio_input_used_list), [](const AudioRow *audio_row) {
        config.main_config.audio_input.push_back(gtk_combo_box_text_get_active_text(audio_row->input_list));
    });
    config.main_config.color_range = gtk_combo_box_get_active_id(GTK_COMBO_BOX(color_range_input_menu));
    config.main_config.quality = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));
    config.main_config.codec = gtk_combo_box_get_active_id(GTK_COMBO_BOX(video_codec_input_menu));
    config.main_config.audio_codec = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_codec_input_menu));
    config.main_config.framerate_mode = gtk_combo_box_get_active_id(GTK_COMBO_BOX(framerate_mode_input_menu));
    config.main_config.advanced_view = strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(view_combo_box)), "advanced") == 0;
    config.main_config.overclock = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(overclock_button));
    config.main_config.show_notifications = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_notification_button));

    config.streaming_config.streaming_service = gtk_combo_box_get_active_id(GTK_COMBO_BOX(stream_service_input_menu));
    config.streaming_config.stream_key = gtk_entry_get_text(stream_id_entry);
    if(!wayland) {
        config.streaming_config.start_recording_hotkey.keysym = streaming_hotkey.keysym;
        config.streaming_config.start_recording_hotkey.modifiers = streaming_hotkey.modkey_mask;
    }

    config.record_config.save_directory = gtk_button_get_label(record_file_chooser_button);
    config.record_config.container = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_container));
    if(!wayland) {
        config.record_config.start_recording_hotkey.keysym = record_hotkey.keysym;
        config.record_config.start_recording_hotkey.modifiers = record_hotkey.modkey_mask;

        config.record_config.pause_recording_hotkey.keysym = pause_unpause_hotkey.keysym;
        config.record_config.pause_recording_hotkey.modifiers = pause_unpause_hotkey.modkey_mask;
    }

    config.replay_config.save_directory = gtk_button_get_label(replay_file_chooser_button);
    config.replay_config.container = gtk_combo_box_get_active_id(GTK_COMBO_BOX(replay_container));
    config.replay_config.replay_time = gtk_spin_button_get_value_as_int(replay_time_entry);
    if(!wayland) {
        config.replay_config.start_recording_hotkey.keysym = replay_start_stop_hotkey.keysym;
        config.replay_config.start_recording_hotkey.modifiers = replay_start_stop_hotkey.modkey_mask;

        config.replay_config.save_recording_hotkey.keysym = replay_save_hotkey.keysym;
        config.replay_config.save_recording_hotkey.modifiers = replay_save_hotkey.modkey_mask;
    }

    save_config(config);
}

typedef struct {
    const char *name;
    int name_len;
    vec2i pos;
    vec2i size;
    XRRCrtcInfo *crt_info; /* Only on x11 */
    uint32_t connector_id; /* Only on drm */
} gsr_monitor;

typedef enum {
    GSR_CONNECTION_X11,
    GSR_CONNECTION_WAYLAND,
    GSR_CONNECTION_DRM
} gsr_connection_type;

using active_monitor_callback = std::function<void(const gsr_monitor *monitor, void *userdata)>;

static const XRRModeInfo* get_mode_info(const XRRScreenResources *sr, RRMode id) {
    for(int i = 0; i < sr->nmode; ++i) {
        if(sr->modes[i].id == id)
            return &sr->modes[i];
    }    
    return NULL;
}

static void for_each_active_monitor_output_x11(Display *display, active_monitor_callback callback, void *userdata) {
    XRRScreenResources *screen_res = XRRGetScreenResources(display, DefaultRootWindow(display));
    if(!screen_res)
        return;

    char display_name[256];
    for(int i = 0; i < screen_res->noutput; ++i) {
        XRROutputInfo *out_info = XRRGetOutputInfo(display, screen_res, screen_res->outputs[i]);
        if(out_info && out_info->crtc && out_info->connection == RR_Connected) {
            XRRCrtcInfo *crt_info = XRRGetCrtcInfo(display, screen_res, out_info->crtc);
            if(crt_info && crt_info->mode) {
                const XRRModeInfo *mode_info = get_mode_info(screen_res, crt_info->mode);
                if(mode_info && out_info->nameLen < (int)sizeof(display_name)) {
                    memcpy(display_name, out_info->name, out_info->nameLen);
                    display_name[out_info->nameLen] = '\0';

                    gsr_monitor monitor;
                    monitor.name = display_name;
                    monitor.name_len = out_info->nameLen;
                    monitor.pos = { (int)crt_info->x, (int)crt_info->y };
                    monitor.size = { (int)crt_info->width, (int)crt_info->height };
                    monitor.crt_info = crt_info;
                    monitor.connector_id = 0; // TODO: Get connector id
                    callback(&monitor, userdata);
                }
            }
            if(crt_info)
                XRRFreeCrtcInfo(crt_info);
        }
        if(out_info)
            XRRFreeOutputInfo(out_info);
    }    

    XRRFreeScreenResources(screen_res);
}

typedef struct {
    int type;
    int count;
} drm_connector_type_count;

#define CONNECTOR_TYPE_COUNTS 32

static drm_connector_type_count* drm_connector_types_get_index(drm_connector_type_count *type_counts, int *num_type_counts, int connector_type) {
    for(int i = 0; i < *num_type_counts; ++i) {
        if(type_counts[i].type == connector_type)
            return &type_counts[i];
    }

    if(*num_type_counts == CONNECTOR_TYPE_COUNTS)
        return NULL;

    const int index = *num_type_counts;
    type_counts[index].type = connector_type;
    type_counts[index].count = 0;
    ++*num_type_counts;
    return &type_counts[index];
}

static bool connector_get_property_by_name(int drmfd, drmModeConnectorPtr props, const char *name, uint64_t *result) {
    for(int i = 0; i < props->count_props; ++i) {
        drmModePropertyPtr prop = drmModeGetProperty(drmfd, props->props[i]);
        if(prop) {
            if(strcmp(name, prop->name) == 0) {
                *result = props->prop_values[i];
                drmModeFreeProperty(prop);
                return true;
            }
            drmModeFreeProperty(prop);
        }
    }
    return false;
}

static void for_each_active_monitor_output_wayland(const gsr_egl *egl, active_monitor_callback callback, void *userdata) {
    for(int i = 0; i < egl->wayland.num_outputs; ++i) {
        if(!egl->wayland.outputs[i].name)
            continue;

        gsr_monitor monitor;
        monitor.name = egl->wayland.outputs[i].name;
        monitor.name_len = strlen(egl->wayland.outputs[i].name);
        monitor.pos = { egl->wayland.outputs[i].pos.x, egl->wayland.outputs[i].pos.y };
        monitor.size = { egl->wayland.outputs[i].size.x, egl->wayland.outputs[i].size.y };
        monitor.crt_info = NULL;
        monitor.connector_id = 0;
        callback(&monitor, userdata);
    }
}

static void for_each_active_monitor_output_drm(const gsr_egl *egl, active_monitor_callback callback, void *userdata) {
    int fd = open(egl->card_path, O_RDONLY);
    if(fd == -1)
        return;

    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

    drm_connector_type_count type_counts[CONNECTOR_TYPE_COUNTS];
    int num_type_counts = 0;

    char display_name[256];
    drmModeResPtr resources = drmModeGetResources(fd);
    if(resources) {
        for(int i = 0; i < resources->count_connectors; ++i) {
            drmModeConnectorPtr connector = drmModeGetConnectorCurrent(fd, resources->connectors[i]);
            if(!connector)
                continue;

            drm_connector_type_count *connector_type = drm_connector_types_get_index(type_counts, &num_type_counts, connector->connector_type);
            const char *connection_name = drmModeGetConnectorTypeName(connector->connector_type);
            const int connection_name_len = strlen(connection_name);
            if(connector_type)
                ++connector_type->count;

            if(connector->connection != DRM_MODE_CONNECTED) {
                drmModeFreeConnector(connector);
                continue;
            }

            uint64_t crtc_id = 0;
            connector_get_property_by_name(fd, connector, "CRTC_ID", &crtc_id);

            drmModeCrtcPtr crtc = drmModeGetCrtc(fd, crtc_id);
            if(connector_type && crtc_id > 0 && crtc && connection_name_len + 5 < (int)sizeof(display_name)) {
                const int display_name_len = snprintf(display_name, sizeof(display_name), "%s-%d", connection_name, connector_type->count);
                gsr_monitor monitor;
                monitor.name = display_name;
                monitor.name_len = display_name_len;
                monitor.pos = { (int)crtc->x, (int)crtc->y };
                monitor.size = { (int)crtc->width, (int)crtc->height };
                monitor.crt_info = NULL;
                monitor.connector_id = connector->connector_id;
                callback(&monitor, userdata);
            }

            if(crtc)
                drmModeFreeCrtc(crtc);

            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(resources);
    }

    close(fd);
}

static void for_each_active_monitor_output(const gsr_egl *egl, gsr_connection_type connection_type, active_monitor_callback callback, void *userdata) {
    switch(connection_type) {
        case GSR_CONNECTION_X11:
            for_each_active_monitor_output_x11(egl->x11.dpy, callback, userdata);
            break;
        case GSR_CONNECTION_WAYLAND:
            for_each_active_monitor_output_wayland(egl, callback, userdata);
            break;
        case GSR_CONNECTION_DRM:
            for_each_active_monitor_output_drm(egl, callback, userdata);
            break;
    }
}

/* output should be >= 128 bytes */
static bool gsr_get_valid_card_path(char *output) {
    for(int i = 0; i < 10; ++i) {
        drmVersion *ver = NULL;
        drmModePlaneResPtr planes = NULL;
        bool found_screen_card = false;

        sprintf(output, DRM_DEV_NAME, DRM_DIR_NAME, i);
        int fd = open(output, O_RDONLY);
        if(fd == -1)
            continue;

        ver = drmGetVersion(fd);
        if(!ver || strstr(ver->name, "nouveau"))
            goto next;

        drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

        planes = drmModeGetPlaneResources(fd);
        if(!planes)
            goto next;

        for(uint32_t i = 0; i < planes->count_planes; ++i) {
            drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[i]);
            if(!plane)
                continue;

            if(plane->fb_id)
                found_screen_card = true;

            drmModeFreePlane(plane);
            if(found_screen_card)
                break;
        }

        next:
        if(planes)
            drmModeFreePlaneResources(planes);
        if(ver)
            drmFreeVersion(ver);
        close(fd);
        if(found_screen_card)
            return true;
    }
    return false;
}

static void show_notification(GtkApplication *app, const char *title, const char *body, GNotificationPriority priority) {
    fprintf(stderr, "Notification: title: %s, body: %s\n", title, body);
    GNotification *notification = g_notification_new(title);
    g_notification_set_body(notification, body);
    g_notification_set_priority(notification, priority);
    g_application_send_notification(&app->parent, "gpu-screen-recorder", notification);
}

static bool window_has_atom(Display *display, Window window, Atom atom) {
    Atom type;
    unsigned long len, bytes_left;
    int format;
    unsigned char *properties = nullptr;
    if(XGetWindowProperty(display, window, atom, 0, 1024, False, AnyPropertyType, &type, &format, &len, &bytes_left, &properties) < Success)
        return false;

    if(properties)
        XFree(properties);

    return type != None;
}

static Window window_get_target_window_child(Display *display, Window window) {
    if(window == None)
        return None;

    Atom wm_state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    if(!wm_state_atom)
        return None;

    if(window_has_atom(display, window, wm_state_atom))
        return window;

    Window root;
    Window parent;
    Window *children = nullptr;
    unsigned int num_children = 0;
    if(!XQueryTree(display, window, &root, &parent, &children, &num_children) || !children)
        return None;

    Window found_window = None;
    for(int i = num_children - 1; i >= 0; --i) {
        if(children[i] && window_has_atom(display, children[i], wm_state_atom)) {
            found_window = children[i];
            goto finished;
        }
    }

    for(int i = num_children - 1; i >= 0; --i) {
        if(children[i]) {
            Window win = window_get_target_window_child(display, children[i]);
            if(win) {
                found_window = win;
                goto finished;
            }
        }
    }

    finished:
    XFree(children);
    return found_window;
}

/* TODO: Look at xwininfo source to figure out how to make this work for different types of window managers */
static GdkFilterReturn filter_callback(GdkXEvent *xevent, GdkEvent*, gpointer userdata) {
    SelectWindowUserdata *select_window_userdata = (SelectWindowUserdata*)userdata;
    XEvent *ev = (XEvent*)xevent;
    //assert(ev->type == ButtonPress);
    if(ev->type != ButtonPress)
        return GDK_FILTER_CONTINUE;

    Window target_win = ev->xbutton.subwindow;
    Window new_window = window_get_target_window_child(select_window_userdata->display, target_win);
    if(new_window)
        target_win = new_window;

    int status = XUngrabPointer(select_window_userdata->display, CurrentTime);
    if(!status) {
        fprintf(stderr, "failed to ungrab pointer!\n");
        show_notification(select_window_userdata->app, "GPU Screen Recorder", "Failed to ungrab pointer!", G_NOTIFICATION_PRIORITY_URGENT);
        exit(1);
    }

    if(target_win == None) {
        show_notification(select_window_userdata->app, "GPU Screen Recorder", "No window selected!", G_NOTIFICATION_PRIORITY_URGENT);
        GdkScreen *screen = gdk_screen_get_default();
        GdkWindow *root_window = gdk_screen_get_root_window(screen);
        gdk_window_remove_filter(root_window, filter_callback, select_window_userdata);
        return GDK_FILTER_REMOVE;
    }

    std::string window_name;
    XTextProperty wm_name_prop;
    if(XGetWMName(select_window_userdata->display, target_win, &wm_name_prop) && wm_name_prop.nitems > 0) {
        char **list_return = NULL;
        int num_items = 0;
        int ret = XmbTextPropertyToTextList(select_window_userdata->display, &wm_name_prop, &list_return, &num_items);
        if((ret == Success || ret > 0) && list_return) {
            for(int i = 0; i < num_items; ++i) {
                window_name += list_return[i];
            }
            XFreeStringList(list_return);
        } else {
            window_name += (char*)wm_name_prop.value;
        }
    } else {
        window_name += "(no name)";
    }

    fprintf(stderr, "window name: %s, window id: %ld\n", window_name.c_str(), target_win);
    gtk_button_set_label(select_window_userdata->select_window_button, window_name.c_str());
    select_window_userdata->selected_window = target_win;

    GdkScreen *screen = gdk_screen_get_default();
    GdkWindow *root_window = gdk_screen_get_root_window(screen);
    gdk_window_remove_filter(root_window, filter_callback, select_window_userdata);

    enable_stream_record_button_if_info_filled();

    return GDK_FILTER_REMOVE;
}

static gboolean on_select_window_button_click(GtkButton *button, gpointer) {
    Display *display = gdk_x11_get_default_xdisplay();
    select_window_userdata.display = display;
    select_window_userdata.select_window_button = button;
    select_window_userdata.selected_window = None;

    GdkScreen *screen = gdk_screen_get_default();
    GdkWindow *root_window = gdk_screen_get_root_window(screen);
    gdk_window_set_events(root_window, GDK_BUTTON_PRESS_MASK);
    gdk_window_add_filter(root_window, filter_callback, &select_window_userdata);

    Window root = GDK_WINDOW_XID(root_window);

    /* Grab the pointer using target cursor, letting it room all over */
    int status = XGrabPointer(display, root, False,
                ButtonPressMask, GrabModeAsync,
                GrabModeAsync, root, crosshair_cursor, CurrentTime);
    if (status != GrabSuccess) {
        fprintf(stderr, "failed to grab pointer!\n");
        //GNotification *notification = g_notification_new("Failed to grab pointer to window selection!");
        //g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_HIGH);
        //g_application_send_notification(app, "select-window", notification);
    }

    return true;
}

static bool key_is_modifier(KeySym key_sym) {
    return key_sym >= XK_Shift_L && key_sym <= XK_Super_R && key_sym != XK_Caps_Lock && key_sym != XK_Shift_Lock;
}

static uint32_t modkey_to_mask(KeySym key_sym) {
    assert(key_is_modifier(key_sym));
    return 1 << (key_sym - XK_Shift_L);
}

static uint32_t key_mod_mask_to_x11_mask(uint32_t mask) {
    uint32_t key_mod_masks = 0;
    if(mask & (modkey_to_mask(XK_Control_L) | modkey_to_mask(XK_Control_R)))
        key_mod_masks |= ControlMask;
    if(mask & (modkey_to_mask(XK_Alt_L) | modkey_to_mask(XK_Alt_R)))
        key_mod_masks |= Mod1Mask;
    if(mask & (modkey_to_mask(XK_Shift_L) | modkey_to_mask(XK_Shift_R)))
        key_mod_masks |= ShiftMask;
    if(mask & (modkey_to_mask(XK_Super_L) | modkey_to_mask(XK_Super_R) | modkey_to_mask(XK_Meta_L)| modkey_to_mask(XK_Meta_R)))
        key_mod_masks |= Mod4Mask;
    //if(mask & (modkey_to_mask(XK_Caps_Lock) | modkey_to_mask(XK_Shift_Lock)))
    //    key_mod_masks |= LockMask;
    return key_mod_masks;
}

static unsigned int key_state_without_locks(unsigned int key_state) {
    return key_state & ~(Mod2Mask|LockMask);
}

struct CustomKeyName {
    KeySym key_sym;
    const char *name;
};

static int key_get_name(KeySym key_sym, char *buffer, int buffer_size) {
    if(buffer_size == 0 || key_sym == None)
        return 0;

    #define CUSTOM_KEY_NAME_LEN 23
    const CustomKeyName key_names[CUSTOM_KEY_NAME_LEN] = {
        { XK_Caps_Lock,      "Caps Lock"     },
        { XK_Shift_Lock,     "Caps Lock"     },
        { XK_Return,         "Return"        },
        { XK_BackSpace,      "BackSpace"     },
        { XK_Tab,            "Tab"           },
        { XK_Delete,         "Delete"        },
        { XK_dead_acute,     "`"             },
        { XK_dead_diaeresis, "^"             },
        { XK_Prior,          "PageUp"        },
        { XK_Next,           "PageDown"      },
        { ' ',               "Space"         },
        { XK_KP_Insert,      "KeyPad 0"      },
        { XK_KP_End,         "KeyPad 1"      },
        { XK_KP_Down,        "KeyPad 2"      },
        { XK_KP_Next,        "KeyPad 3"      },
        { XK_KP_Left,        "KeyPad 4"      },
        { XK_KP_Begin,       "KeyPad 5"      },
        { XK_KP_Right,       "KeyPad 6"      },
        { XK_KP_Home,        "KeyPad 7"      },
        { XK_KP_Up,          "KeyPad 8"      },
        { XK_KP_Prior,       "KeyPad 9"      },
        { XK_KP_Enter,       "KeyPad Return" },
        { XK_KP_Delete,      "KeyPad Delete" }
    };

    for(int i = 0; i < CUSTOM_KEY_NAME_LEN; ++i) {
        const CustomKeyName custom_key_name = key_names[i];
        if(key_sym == custom_key_name.key_sym) {
            const int key_len = strlen(custom_key_name.name);
            if(buffer_size < key_len)
                return 0;

            memcpy(buffer, custom_key_name.name, key_len);
            return key_len;
        }
    }

    XKeyPressedEvent event;
    event.type = KeyPress;
    event.display = gdk_x11_get_default_xdisplay();
    event.state = 0;
    event.keycode = XKeysymToKeycode(event.display, key_sym);

    KeySym ignore;
    Status return_status;
    int buflen = Xutf8LookupString(xic, &event, buffer, buffer_size, &ignore, &return_status);
    if(return_status != XBufferOverflow && buflen > 0)
        return buflen;

    const char *keysym_str = XKeysymToString(key_sym);
    if(keysym_str) {
        int keysym_str_len = strlen(keysym_str);
        if(buffer_size >= keysym_str_len) {
            memcpy(buffer, keysym_str, keysym_str_len);
            return keysym_str_len;
        }
    }

    return 0;
}

static int xerror_dummy(Display*, XErrorEvent*) {
    return 0;
}

static bool x_failed = false;
static int xerror_grab_error(Display*, XErrorEvent*) {
    x_failed = true;
    return 0;
}

static void ungrab_keyboard(Display *display) {
    if(wayland)
        return;

    if(current_hotkey) {
        gtk_grab_remove(current_hotkey->hotkey_entry);
        gtk_widget_set_visible(current_hotkey->hotkey_active_label, false);
    }

    XErrorHandler prev_error_handler = XSetErrorHandler(xerror_dummy);
    XUngrabKeyboard(display, CurrentTime);
    XSync(display, False);
    XSetErrorHandler(prev_error_handler);
}

static bool grab_ungrab_hotkey_combo(Display *display, Hotkey hotkey, bool grab) {
    if(wayland)
        return true;

    if(hotkey.keysym == None && hotkey.modkey_mask == 0)
        return true;

    unsigned int numlockmask = 0;
    KeyCode numlock_keycode = XKeysymToKeycode(display, XK_Num_Lock);
    XModifierKeymap *modmap = XGetModifierMapping(display);
    if(modmap) {
        for(int i = 0; i < 8; ++i) {
            for(int j = 0; j < modmap->max_keypermod; ++j) {
                if(modmap->modifiermap[i * modmap->max_keypermod + j] == numlock_keycode)
                    numlockmask = (1 << i); 
            }
        }
        XFreeModifiermap(modmap);
    }

    unsigned int key_mod_masks = 0;
    KeySym key_sym = hotkey.keysym;
    if(key_sym == None) {
        // TODO: Set key_sym to one of the modkey mask values and set key_mod_masks to the other modkeys
    } else {
        key_mod_masks = key_mod_mask_to_x11_mask(hotkey.modkey_mask);
    }

    XSync(display, False);
    x_failed = false;
    XErrorHandler prev_error_handler = XSetErrorHandler(xerror_grab_error);
    
	Window root_window = DefaultRootWindow(display);
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    if(key_sym != None) {
        for(int i = 0; i < 4; ++i) {
            if(grab) {
                XGrabKey(display, XKeysymToKeycode(display, key_sym), key_mod_masks|modifiers[i], root_window, False, GrabModeAsync, GrabModeAsync);
            } else {
                XUngrabKey(display, XKeysymToKeycode(display, key_sym), key_mod_masks|modifiers[i], root_window);
            }
        }
    }
    XSync(display, False);

    bool success = !x_failed;

    if(!success && key_sym != None) {
        for(int i = 0; i < 4; ++i) {
            XUngrabKey(display, XKeysymToKeycode(display, key_sym), key_mod_masks|modifiers[i], root_window);
        }
    }
    XSync(display, False);

    XSetErrorHandler(prev_error_handler);
    return success;
}

static void ungrab_keys(Display *display) {
    if(wayland)
        return;

    grab_ungrab_hotkey_combo(display, streaming_hotkey, false);
    grab_ungrab_hotkey_combo(display, record_hotkey, false);
    grab_ungrab_hotkey_combo(display, pause_unpause_hotkey, false);
    grab_ungrab_hotkey_combo(display, replay_start_stop_hotkey, false);
    grab_ungrab_hotkey_combo(display, replay_save_hotkey, false);
}

static void set_hotkey_text_from_hotkey_data(GtkEntry *entry, Hotkey hotkey) {
    struct ModkeyName {
        KeySym key_sym;
        const char *name;
    };

    const ModkeyName modkey_names[] = {
        { XK_Control_L, "Ctrl" },
        { XK_Control_R, "Ctrl" },
        { XK_Super_L, "Super" },
        { XK_Super_R, "Super" },
        { XK_Meta_L, "Super" },
        { XK_Meta_R, "Super" },
        { XK_Shift_L, "Shift" },
        { XK_Shift_R, "Shift" },
        { XK_Alt_L, "Alt" },
        { XK_Alt_R, "Alt" },
    };

    std::string hotkey_combo_str;

    for(auto modkey_name : modkey_names) {
        if(hotkey.modkey_mask & modkey_to_mask(modkey_name.key_sym)) {
            if(!hotkey_combo_str.empty())
                hotkey_combo_str += " + ";
            hotkey_combo_str += modkey_name.name;
        }
    }

    if(!hotkey_combo_str.empty())
        hotkey_combo_str += " + ";

    char buffer[128];
    int buflen = key_get_name(hotkey.keysym, buffer, sizeof(buffer));
    if(buflen > 0)
        hotkey_combo_str.append(buffer, buflen);

    gtk_entry_set_text(entry, hotkey_combo_str.c_str());
}

struct HotkeyResult {
    bool record_hotkey_success = false;
    bool pause_unpause_hotkey_success = false;
    bool streaming_hotkey_success = false;
    bool replay_start_stop_hotkey_success = false;
    bool replay_save_hotkey_success = false;
};

static HotkeyResult replace_grabbed_keys_depending_on_active_page() {
    HotkeyResult hotkey_result;
    ungrab_keys(gdk_x11_get_default_xdisplay());
    const GtkWidget *visible_page = gtk_stack_get_visible_child(page_navigation_userdata.stack);
    if(visible_page == page_navigation_userdata.recording_page) {
        bool grab_record_success = grab_ungrab_hotkey_combo(gdk_x11_get_default_xdisplay(), record_hotkey, true);
        bool grab_pause_success = grab_ungrab_hotkey_combo(gdk_x11_get_default_xdisplay(), pause_unpause_hotkey, true);
        hotkey_mode = HotkeyMode::Record;

        hotkey_result.record_hotkey_success = grab_record_success;
        hotkey_result.pause_unpause_hotkey_success = grab_pause_success;
    } else if(visible_page == page_navigation_userdata.streaming_page) {
        bool grab_record_success = grab_ungrab_hotkey_combo(gdk_x11_get_default_xdisplay(), streaming_hotkey, true);
        hotkey_mode = HotkeyMode::Record;
        hotkey_result.streaming_hotkey_success = grab_record_success;
    } else if(visible_page == page_navigation_userdata.replay_page) {
        bool grab_record_success = grab_ungrab_hotkey_combo(gdk_x11_get_default_xdisplay(), replay_start_stop_hotkey, true);
        bool grab_save_success = grab_ungrab_hotkey_combo(gdk_x11_get_default_xdisplay(), replay_save_hotkey, true);
        hotkey_mode = HotkeyMode::Record;

        hotkey_result.replay_start_stop_hotkey_success = grab_record_success;
        hotkey_result.replay_save_hotkey_success = grab_save_success;
    }
    return hotkey_result;
}

static bool show_pkexec_flatpak_error_if_needed() {
    const std::string window_str = record_area_selection_menu_get_active_id();
    if((wayland || gpu_inf.vendor != GPU_VENDOR_NVIDIA) && window_str != "window" && window_str != "focused") {
        if(!is_pkexec_installed()) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "pkexec needs to be installed to record a monitor with an AMD/Intel GPU. Please install and run polkit. Alternatively, record a single window which doesn't require root access.");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return true;
        }

        if(is_inside_flatpak() && !flatpak_is_installed_as_system()) {
            if(wayland) {
                GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "GPU Screen Recorder needs to be installed system-wide to record your monitor on Wayland. To install GPU Screen recorder system-wide, you can run this command:\n"
                    "flatpak install flathub --system com.dec05eba.gpu_screen_recorder\n");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            } else {
                GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "GPU Screen Recorder needs to be installed system-wide to record your monitor on AMD/Intel. To install GPU Screen recorder system-wide, you can run this command:\n"
                    "flatpak install flathub --system com.dec05eba.gpu_screen_recorder\n"
                    "Alternatively, record a single window which doesn't have this restriction.");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            }
            return true;
        }
    }
    return false;
}

static gboolean on_start_replay_click(GtkButton*, gpointer userdata) {
    if(show_pkexec_flatpak_error_if_needed())
        return true;

    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->replay_page);
    if(!wayland) {
        HotkeyResult hotkey_result = replace_grabbed_keys_depending_on_active_page();
        if(!hotkey_result.replay_start_stop_hotkey_success) {
            gtk_entry_set_text(GTK_ENTRY(replay_start_stop_hotkey.hotkey_entry), "");
            replay_start_stop_hotkey.keysym = 0;
            replay_start_stop_hotkey.modkey_mask = 0;
        }
        if(!hotkey_result.replay_save_hotkey_success) {
            gtk_entry_set_text(GTK_ENTRY(replay_save_hotkey.hotkey_entry), "");
            replay_save_hotkey.keysym = 0;
            replay_save_hotkey.modkey_mask = 0;
        }
    }
    return true;
}

static gboolean on_start_recording_click(GtkButton*, gpointer userdata) {
    if(show_pkexec_flatpak_error_if_needed())
        return true;

    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->recording_page);
    if(!wayland) {
        HotkeyResult hotkey_result = replace_grabbed_keys_depending_on_active_page();
        if(!hotkey_result.record_hotkey_success) {
            gtk_entry_set_text(GTK_ENTRY(record_hotkey.hotkey_entry), "");
            record_hotkey.keysym = 0;
            record_hotkey.modkey_mask = 0;
        }
        if(!hotkey_result.pause_unpause_hotkey_success) {
            gtk_entry_set_text(GTK_ENTRY(pause_unpause_hotkey.hotkey_entry), "");
            pause_unpause_hotkey.keysym = 0;
            pause_unpause_hotkey.modkey_mask = 0;
        }
    }
    return true;
}

void on_stream_key_icon_click(GtkWidget *widget, gpointer) {
    gboolean visible = gtk_entry_get_visibility(GTK_ENTRY(widget));

    gtk_entry_set_visibility(GTK_ENTRY(widget), !visible);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(widget), GTK_ENTRY_ICON_SECONDARY, visible ? "view-reveal-symbolic" : "view-conceal-symbolic");
}

static gboolean on_start_streaming_click(GtkButton*, gpointer userdata) {
    if(show_pkexec_flatpak_error_if_needed())
        return true;

    int num_audio_tracks = 0;
    for_each_used_audio_input(GTK_LIST_BOX(audio_input_used_list), [&num_audio_tracks](const AudioRow*) {
        ++num_audio_tracks;
    });

    if(num_audio_tracks > 1 && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(merge_audio_tracks_button))) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Streaming doesn't work with more than 1 audio track. Please remove all audio tracks or only use 1 audio track or select to merge audio tracks.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return true;
    }

    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->streaming_page);
    if(!wayland) {
        HotkeyResult hotkey_result = replace_grabbed_keys_depending_on_active_page();
        if(!hotkey_result.streaming_hotkey_success) {
            gtk_entry_set_text(GTK_ENTRY(streaming_hotkey.hotkey_entry), "");
            streaming_hotkey.keysym = 0;
            streaming_hotkey.modkey_mask = 0;
        }
    }
    return true;
}

static gboolean on_streaming_recording_replay_page_back_click(GtkButton*, gpointer userdata) {
    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->common_settings_page);
    ungrab_keys(gdk_x11_get_default_xdisplay());
    hotkey_mode = HotkeyMode::NoAction;
    return true;
}

static gboolean file_choose_button_click_handler(GtkButton *button, const char *title, GtkFileChooserAction file_action, std::string container_file_ext) {
    GtkWidget *file_chooser_dialog = gtk_file_chooser_dialog_new(title, nullptr, file_action,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        nullptr);

    if(file_action != GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) {
        const std::string name = "Video_" + get_date_str() + "." + container_file_ext;
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(file_chooser_dialog), name.c_str());
    }

    int res = gtk_dialog_run(GTK_DIALOG(file_chooser_dialog));
    if(res == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_chooser_dialog));
        gtk_button_set_label(button, filename);
        g_free(filename);
    }
    gtk_widget_destroy(file_chooser_dialog);
    return true;
}

static gboolean on_record_file_choose_button_click(GtkButton *button, gpointer) {
    gboolean res = file_choose_button_click_handler(button, "Where do you want to save the video?", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, gtk_combo_box_text_get_active_text(record_container));
    config.record_config.save_directory = gtk_button_get_label(button);
    return res;
}

static gboolean on_replay_file_chooser_button_click(GtkButton *button, gpointer) {
    gboolean res = file_choose_button_click_handler(button, "Where do you want to save the replays?", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, gtk_combo_box_text_get_active_text(replay_container));
    config.replay_config.save_directory = gtk_button_get_label(button);
    return res;
}

static bool kill_gpu_screen_recorder_get_result() {
    bool exit_success = true;
    if(gpu_screen_recorder_process != -1) {
        int status;
        int wait_result = waitpid(gpu_screen_recorder_process, &status, WNOHANG);
        if(wait_result == -1) {
            perror("waitpid failed");
            exit_success = false;
        } else if(wait_result == 0) {
            // the process is still running
            kill(gpu_screen_recorder_process, SIGINT);
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                exit_success = false;
            } else {
                exit_success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
        } else {
            exit_success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
    }
    return exit_success;
}

static const gchar* audio_row_get_id(const AudioRow *audio_row) {
    const char *text = gtk_combo_box_text_get_active_text(audio_row->input_list);
    if(strcmp(text, "Default output") == 0 && !pa_default_sources.default_sink_name.empty())
        return pa_default_sources.default_sink_name.c_str();
    else if(strcmp(text, "Default input") == 0 && !pa_default_sources.default_source_name.empty())
        return pa_default_sources.default_source_name.c_str();
    else
        return gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_row->input_list));
}

static void add_audio_command_line_args(std::vector<const char*> &args, std::string &merge_audio_tracks_arg_value) {
    pa_default_sources = get_pulseaudio_default_inputs();

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(merge_audio_tracks_button))) {
        for_each_used_audio_input(GTK_LIST_BOX(audio_input_used_list), [&merge_audio_tracks_arg_value](const AudioRow *audio_row) {
            if(!merge_audio_tracks_arg_value.empty())
                merge_audio_tracks_arg_value += '|';
            merge_audio_tracks_arg_value += audio_row_get_id(audio_row);
        });

        if(!merge_audio_tracks_arg_value.empty())
            args.insert(args.end(), { "-a", merge_audio_tracks_arg_value.c_str() });
    } else {
        for_each_used_audio_input(GTK_LIST_BOX(audio_input_used_list), [&args](const AudioRow *audio_row) {
            args.insert(args.end(), { "-a", audio_row_get_id(audio_row) });
        });
    }
}

static gboolean on_start_replay_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;
    const gchar *dir = gtk_button_get_label(replay_file_chooser_button);

    int exit_status = prev_exit_status;
    prev_exit_status = -1;
    if(replaying) {
        bool exit_success = kill_gpu_screen_recorder_get_result();

        gtk_button_set_label(button, "Start replay");
        replaying = false;
        gpu_screen_recorder_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(replay_back_button), true);
        gtk_widget_set_sensitive(GTK_WIDGET(replay_save_button), false);

        gtk_widget_set_opacity(GTK_WIDGET(replay_bottom_panel_grid), 0.5);
        gtk_label_set_text(GTK_LABEL(replay_record_time_label), "00:00:00");

        if(exit_status == 10) {
            show_notification(app, "GPU Screen Recorder",
                "You need to have pkexec installed and a polkit agent running to record your monitor", G_NOTIFICATION_PRIORITY_URGENT);
        } else if(!exit_success) {
            show_notification(app, "GPU Screen Recorder",
                "Failed to start replay. Either your graphics card doesn't support GPU Screen Recorder with the settings you used or you don't have enough disk space to record a video", G_NOTIFICATION_PRIORITY_URGENT);
        }

        return true;
    }

    save_configs();

    int fps = gtk_spin_button_get_value_as_int(fps_entry);
    int replay_time = gtk_spin_button_get_value_as_int(replay_time_entry);
    int record_width = wayland ? 0 : gtk_spin_button_get_value_as_int(area_width_entry);
    int record_height = wayland ? 0 : gtk_spin_button_get_value_as_int(area_height_entry);

    char dir_tmp[PATH_MAX];
    strcpy(dir_tmp, dir);
    if(create_directory_recursive(dir_tmp) != 0) {
        std::string notification_body = std::string("Failed to start replay. Failed to create ") + dir_tmp;
        show_notification(app, "GPU Screen Recorder", notification_body.c_str(), G_NOTIFICATION_PRIORITY_URGENT);
        return true;
    }

    bool follow_focused = false;
    std::string window_str = record_area_selection_menu_get_active_id();
    if(window_str == "window") {
        if(select_window_userdata.selected_window == None) {
            fprintf(stderr, "No window selected!\n");
            return true;
        }
        window_str = std::to_string(select_window_userdata.selected_window);
    } else if(window_str == "focused") {
        follow_focused = true;
    }
    std::string fps_str = std::to_string(fps);
    std::string replay_time_str = std::to_string(replay_time);

    const gchar* container_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(replay_container));
    const gchar* color_range_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(color_range_input_menu));
    const gchar* quality_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));
    const gchar* video_codec_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(video_codec_input_menu));
    const gchar* audio_codec_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_codec_input_menu));
    const gchar* framerate_mode_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(framerate_mode_input_menu));

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", record_width, record_height);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", window_str.c_str(), "-c", container_str, "-q", quality_input_str, "-k", video_codec_input_str, "-ac", audio_codec_input_str, "-f", fps_str.c_str(), "-cr", color_range_input_str, "-r", replay_time_str.c_str(), "-o", dir
    };

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(overclock_button)))
        args.insert(args.end(), { "-oc", "yes" });

    if(strcmp(framerate_mode_input_str, "auto") != 0)
        args.insert(args.end(), { "-fm", framerate_mode_input_str });

    std::string merge_audio_tracks_arg_value;
    add_audio_command_line_args(args, merge_audio_tracks_arg_value);

    if(follow_focused)
        args.insert(args.end(), { "-s", area });

    args.push_back(NULL);

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if(pid == -1) {
        perror("failed to fork");
        show_notification(app, "GPU Screen Recorder", "Failed to start replay (failed to fork)", G_NOTIFICATION_PRIORITY_URGENT);
        return true;
    } else if(pid == 0) { /* child process */
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("prctl(PR_SET_PDEATHSIG, SIGTERM) failed");
            _exit(3);
        }

        if(getppid() != parent_pid)
            _exit(3);
        
        execvp(args[0], (char* const*)args.data());
        perror("failed to launch gpu-screen-recorder");
        _exit(127);
    } else { /* parent process */
        gpu_screen_recorder_process = pid;
        gtk_button_set_label(button, "Stop replay");
    }

    replaying = true;
    gtk_widget_set_sensitive(GTK_WIDGET(replay_back_button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(replay_save_button), true);
    gtk_widget_set_opacity(GTK_WIDGET(replay_bottom_panel_grid), 1.0);
    record_start_time_sec = clock_get_monotonic_seconds();
    return true;
}

static gboolean on_replay_save_button_click(GtkButton*, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;
    kill(gpu_screen_recorder_process, SIGUSR1);
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_notification_button)))
        show_notification(app, "GPU Screen Recorder", "Saved replay", G_NOTIFICATION_PRIORITY_NORMAL);
    return true;
}

static gboolean on_pause_unpause_button_click(GtkButton*, gpointer) {
    kill(gpu_screen_recorder_process, SIGUSR2);
    paused = !paused;
    if(paused) {
        gtk_button_set_label(pause_recording_button, "Unpause recording");
        gtk_image_set_from_icon_name(GTK_IMAGE(recording_record_icon), "media-playback-pause", GTK_ICON_SIZE_SMALL_TOOLBAR);
        pause_start_sec = clock_get_monotonic_seconds();
    } else {
        gtk_button_set_label(pause_recording_button, "Pause recording");
        gtk_image_set_from_icon_name(GTK_IMAGE(recording_record_icon), "media-record", GTK_ICON_SIZE_SMALL_TOOLBAR);
        paused_time_offset_sec += (clock_get_monotonic_seconds() - pause_start_sec);
    }
    return true;
}

static gboolean on_start_recording_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;
    const gchar *dir = gtk_button_get_label(record_file_chooser_button);

    int exit_status = prev_exit_status;
    prev_exit_status = -1;
    if(recording) {
        bool exit_success = kill_gpu_screen_recorder_get_result();

        gtk_button_set_label(button, "Start recording");
        recording = false;
        gpu_screen_recorder_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(record_back_button), true);

        paused = false;
        gtk_widget_set_sensitive(GTK_WIDGET(pause_recording_button), false);
        gtk_button_set_label(pause_recording_button, "Pause recording");

        gtk_widget_set_opacity(GTK_WIDGET(recording_bottom_panel_grid), 0.5);
        gtk_image_set_from_icon_name(GTK_IMAGE(recording_record_icon), "media-record", GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_label_set_text(GTK_LABEL(recording_record_time_label), "00:00:00");

        if(exit_status == 10) {
            show_notification(app, "GPU Screen Recorder",
                "You need to have pkexec installed and a polkit agent running to record your monitor", G_NOTIFICATION_PRIORITY_URGENT);
        } else if(exit_success) {
            if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_notification_button))) {
                const std::string notification_body = std::string("The recording was saved to ") + record_file_current_filename;
                show_notification(app, "GPU Screen Recorder", notification_body.c_str(), G_NOTIFICATION_PRIORITY_NORMAL);
            }
        } else {
            show_notification(app, "GPU Screen Recorder", "Failed to save video. Either your graphics card doesn't support GPU Screen Recorder with the settings you used or you don't have enough disk space to record a video", G_NOTIFICATION_PRIORITY_URGENT);
        }
        return true;
    }

    save_configs();

    int fps = gtk_spin_button_get_value_as_int(fps_entry);
    int record_width = wayland ? 0 : gtk_spin_button_get_value_as_int(area_width_entry);
    int record_height = wayland ? 0 : gtk_spin_button_get_value_as_int(area_height_entry);

    char dir_tmp[PATH_MAX];
    strcpy(dir_tmp, dir);
    if(create_directory_recursive(dir_tmp) != 0) {
        std::string notification_body = std::string("Failed to start recording. Failed to create ") + dir_tmp;
        show_notification(app, "GPU Screen Recorder", notification_body.c_str(), G_NOTIFICATION_PRIORITY_URGENT);
        return true;
    }

    record_file_current_filename = std::string(dir_tmp) + "/Video_" + get_date_str() + "." + gtk_combo_box_text_get_active_text(record_container);

    bool follow_focused = false;
    std::string window_str = record_area_selection_menu_get_active_id();
    if(window_str == "window") {
        if(select_window_userdata.selected_window == None) {
            fprintf(stderr, "No window selected!\n");
            return true;
        }
        window_str = std::to_string(select_window_userdata.selected_window);
    } else if(window_str == "focused") {
        follow_focused = true;
    }

    std::string fps_str = std::to_string(fps);

    const gchar* container_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_container));
    const gchar* color_range_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(color_range_input_menu));
    const gchar* quality_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));
    const gchar* video_codec_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(video_codec_input_menu));
    const gchar* audio_codec_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_codec_input_menu));
    const gchar* framerate_mode_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(framerate_mode_input_menu));

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", record_width, record_height);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", window_str.c_str(), "-c", container_str, "-q", quality_input_str, "-k", video_codec_input_str, "-ac", audio_codec_input_str, "-f", fps_str.c_str(), "-cr", color_range_input_str, "-o", record_file_current_filename.c_str()
    };

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(overclock_button)))
        args.insert(args.end(), { "-oc", "yes" });

    if(strcmp(framerate_mode_input_str, "auto") != 0)
        args.insert(args.end(), { "-fm", framerate_mode_input_str });

    std::string merge_audio_tracks_arg_value;
    add_audio_command_line_args(args, merge_audio_tracks_arg_value);

    if(follow_focused)
        args.insert(args.end(), { "-s", area });

    args.push_back(NULL);

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if(pid == -1) {
        perror("failed to fork");
        show_notification(app, "GPU Screen Recorder", "Failed to start recording (failed to fork)", G_NOTIFICATION_PRIORITY_URGENT);
        return true;
    } else if(pid == 0) { /* child process */
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("prctl(PR_SET_PDEATHSIG, SIGTERM) failed");
            _exit(3);
        }

        if(getppid() != parent_pid)
            _exit(3);
        
        execvp(args[0], (char* const*)args.data());
        perror("failed to launch gpu-screen-recorder");
        _exit(127);
    } else { /* parent process */
        gpu_screen_recorder_process = pid;
        gtk_button_set_label(button, "Stop recording");
    }

    recording = true;
    gtk_widget_set_sensitive(GTK_WIDGET(record_back_button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(pause_recording_button), true);
    gtk_widget_set_opacity(GTK_WIDGET(recording_bottom_panel_grid), 1.0);
    record_start_time_sec = clock_get_monotonic_seconds();
    paused_time_offset_sec = 0.0;
    return true;
}

static gboolean on_start_streaming_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;

    int exit_status = prev_exit_status;
    prev_exit_status = -1;
    if(streaming) {
        bool exit_success = kill_gpu_screen_recorder_get_result();

        gtk_button_set_label(button, "Start streaming");
        streaming = false;
        gpu_screen_recorder_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(stream_back_button), true);

        gtk_widget_set_opacity(GTK_WIDGET(streaming_bottom_panel_grid), 0.5);
        gtk_label_set_text(GTK_LABEL(streaming_record_time_label), "00:00:00");

        if(exit_status == 10) {
            show_notification(app, "GPU Screen Recorder",
                "You need to have pkexec installed and a polkit agent running to record your monitor", G_NOTIFICATION_PRIORITY_URGENT);
        } else if(exit_success) {
            show_notification(app, "GPU Screen Recorder", "Stopped streaming", G_NOTIFICATION_PRIORITY_NORMAL);
        } else {
            show_notification(app, "GPU Screen Recorder", "Failed to stream video. There is either an error in your streaming config or your graphics card doesn't support GPU Screen Recorder with the settings you used", G_NOTIFICATION_PRIORITY_URGENT);
        }

        return true;
    }

    save_configs();

    const char *stream_id_str = gtk_entry_get_text(stream_id_entry);
    int fps = gtk_spin_button_get_value_as_int(fps_entry);
    int record_width = wayland ? 0 : gtk_spin_button_get_value_as_int(area_width_entry);
    int record_height = wayland ? 0 : gtk_spin_button_get_value_as_int(area_height_entry);

    bool follow_focused = false;
    std::string window_str = record_area_selection_menu_get_active_id();
    if(window_str == "window") {
        if(select_window_userdata.selected_window == None) {
            fprintf(stderr, "No window selected!\n");
            return true;
        }
        window_str = std::to_string(select_window_userdata.selected_window);
    } else if(window_str == "focused") {
        follow_focused = true;
    }
    std::string fps_str = std::to_string(fps);

    std::string stream_url;
    const gchar *stream_service = gtk_combo_box_get_active_id(GTK_COMBO_BOX(stream_service_input_menu));
    if(strcmp(stream_service, "twitch") == 0) {
        stream_url = "rtmp://live.twitch.tv/app/";
        stream_url += stream_id_str;
    } else if(strcmp(stream_service, "youtube") == 0) {
        stream_url = "rtmp://a.rtmp.youtube.com/live2/";
        stream_url += stream_id_str;
    } else if(strcmp(stream_service, "custom") == 0) {
        stream_url = stream_id_str;
        if(stream_url.size() >= 7 && strncmp(stream_url.c_str(), "rtmp://", 7) == 0)
        {}
        else if(stream_url.size() >= 8 && strncmp(stream_url.c_str(), "rtmps://", 8) == 0)
        {}
        else if(stream_url.size() >= 6 && strncmp(stream_url.c_str(), "srt://", 6) == 0)
        {}
        else
            stream_url = "rtmp://" + stream_url;
    }

    const gchar* quality_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));
    const gchar* color_range_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(color_range_input_menu));
    const gchar* video_codec_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(video_codec_input_menu));
    const gchar* audio_codec_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_codec_input_menu));
    const gchar* framerate_mode_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(framerate_mode_input_menu));

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", record_width, record_height);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", window_str.c_str(), "-c", "flv", "-q", quality_input_str, "-k", video_codec_input_str, "-ac", audio_codec_input_str, "-f", fps_str.c_str(), "-cr", color_range_input_str, "-o", stream_url.c_str()
    };

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(overclock_button)))
        args.insert(args.end(), { "-oc", "yes" });

    if(strcmp(framerate_mode_input_str, "auto") != 0)
        args.insert(args.end(), { "-fm", framerate_mode_input_str });

    std::string merge_audio_tracks_arg_value;
    add_audio_command_line_args(args, merge_audio_tracks_arg_value);

    if(follow_focused)
        args.insert(args.end(), { "-s", area });

    args.push_back(NULL);

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if(pid == -1) {
        perror("failed to fork");
        show_notification(app, "GPU Screen Recorder", "Failed to start streaming (failed to fork)", G_NOTIFICATION_PRIORITY_URGENT);
        return true;
    } else if(pid == 0) { /* child process */
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("prctl(PR_SET_PDEATHSIG, SIGTERM) failed");
            _exit(3);
        }

        if(getppid() != parent_pid)
            _exit(3);

        execvp(args[0], (char* const*)args.data());
        perror("failed to launch gpu-screen-recorder");
        _exit(127);
    } else { /* parent process */
        gpu_screen_recorder_process = pid;
        gtk_button_set_label(button, "Stop streaming");
    }

    streaming = true;
    gtk_widget_set_sensitive(GTK_WIDGET(stream_back_button), false);
    gtk_widget_set_opacity(GTK_WIDGET(streaming_bottom_panel_grid), 1.0);
    record_start_time_sec = clock_get_monotonic_seconds();
    return true;
}

static void gtk_widget_set_margin(GtkWidget *widget, int top, int bottom, int left, int right) {
    gtk_widget_set_margin_top(widget, top);
    gtk_widget_set_margin_bottom(widget, bottom);
    gtk_widget_set_margin_start(widget, left);
    gtk_widget_set_margin_end(widget, right);
}

static void record_area_item_change_callback(GtkComboBox *widget, gpointer userdata) {
    (void)widget;
    GtkWidget *select_window_button = (GtkWidget*)userdata;
    const std::string selected_window_area = record_area_selection_menu_get_active_id();
    gtk_widget_set_visible(select_window_button, strcmp(selected_window_area.c_str(), "window") == 0);
    gtk_widget_set_visible(GTK_WIDGET(area_size_label), strcmp(selected_window_area.c_str(), "focused") == 0);
    gtk_widget_set_visible(GTK_WIDGET(area_size_grid), strcmp(selected_window_area.c_str(), "focused") == 0);
    enable_stream_record_button_if_info_filled();
}

static void view_combo_box_change_callback(GtkComboBox *widget, gpointer userdata) {
    (void)userdata;
    const gchar *selected_view = gtk_combo_box_get_active_id(widget);
    const bool advanced_view = strcmp(selected_view, "advanced") == 0;
    gtk_widget_set_visible(GTK_WIDGET(color_range_grid), advanced_view);
    gtk_widget_set_visible(GTK_WIDGET(video_codec_grid), advanced_view);
    gtk_widget_set_visible(GTK_WIDGET(audio_codec_grid), advanced_view);
    gtk_widget_set_visible(GTK_WIDGET(framerate_mode_grid), advanced_view);
    gtk_widget_set_visible(GTK_WIDGET(overclock_grid), advanced_view && gpu_inf.vendor == GPU_VENDOR_NVIDIA && !wayland);
    gtk_widget_set_visible(GTK_WIDGET(show_notification_button), advanced_view);
}

static void stream_service_item_change_callback(GtkComboBox *widget, gpointer userdata) {
    (void)userdata;
    const gchar *selected_stream_service = gtk_combo_box_get_active_id(widget);
    gtk_label_set_text(stream_key_label, strcmp(selected_stream_service, "custom") == 0 ? "Url: " : "Stream key: ");
}

static bool is_nv_fbc_installed() {
    void *lib = dlopen("libnvidia-fbc.so.1", RTLD_LAZY);
    if(lib)
        dlclose(lib);
    return lib != nullptr;
}

static bool is_nvenc_installed() {
    void *lib = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
    if(lib)
        dlclose(lib);
    return lib != nullptr;
}

static bool is_cuda_installed() {
    void *lib = dlopen("libcuda.so.1", RTLD_LAZY);
    if(!lib)
        lib = dlopen("libcuda.so", RTLD_LAZY);

    if(lib)
        dlclose(lib);

    return lib != nullptr;
}

typedef gboolean (*KeyPressHandler)(GtkButton *button, gpointer userdata);
static void keypress_toggle_recording(bool recording_state, GtkButton *record_button, KeyPressHandler keypress_handler, GtkApplication *app) {
    if(!gtk_widget_get_sensitive(GTK_WIDGET(record_button)))
        return;

    if(!recording_state) {
        keypress_handler(record_button, app);
    } else if(recording_state) {
        keypress_handler(record_button, app);
    }
}

static GdkFilterReturn hotkey_filter_callback(GdkXEvent *xevent, GdkEvent*, gpointer userdata) {
    if(wayland)
        return GDK_FILTER_CONTINUE;

    if(hotkey_mode == HotkeyMode::NoAction)
        return GDK_FILTER_CONTINUE;

    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    XEvent *ev = (XEvent*)xevent;
    if(ev->type != KeyPress && ev->type != KeyRelease)
        return GDK_FILTER_CONTINUE;

    Display *display = gdk_x11_get_default_xdisplay();
    KeySym key_sym = XLookupKeysym(&ev->xkey, 0);

    if(hotkey_mode == HotkeyMode::Record && ev->type == KeyRelease) {
        const GtkWidget *visible_page = gtk_stack_get_visible_child(page_navigation_userdata->stack);
        if(visible_page == page_navigation_userdata->recording_page) {
            if(key_sym == record_hotkey.keysym && key_state_without_locks(ev->xkey.state) == key_mod_mask_to_x11_mask(record_hotkey.modkey_mask)) {
                keypress_toggle_recording(recording, start_recording_button, on_start_recording_button_click, page_navigation_userdata->app);
            } else if(key_sym == pause_unpause_hotkey.keysym && key_state_without_locks(ev->xkey.state) == key_mod_mask_to_x11_mask(pause_unpause_hotkey.modkey_mask) && recording) {
                on_pause_unpause_button_click(nullptr, page_navigation_userdata->app);
            }
        } else if(visible_page == page_navigation_userdata->streaming_page) {
            if(key_sym == streaming_hotkey.keysym && key_state_without_locks(ev->xkey.state) == key_mod_mask_to_x11_mask(streaming_hotkey.modkey_mask)) {
                keypress_toggle_recording(streaming, start_streaming_button, on_start_streaming_button_click, page_navigation_userdata->app);
            }
        } else if(visible_page == page_navigation_userdata->replay_page) {
            if(key_sym == replay_start_stop_hotkey.keysym && key_state_without_locks(ev->xkey.state) == key_mod_mask_to_x11_mask(replay_start_stop_hotkey.modkey_mask)) {
                keypress_toggle_recording(replaying, start_replay_button, on_start_replay_button_click, page_navigation_userdata->app);
            } else if(key_sym == replay_save_hotkey.keysym && key_state_without_locks(ev->xkey.state) == key_mod_mask_to_x11_mask(replay_save_hotkey.modkey_mask) && replaying && gpu_screen_recorder_process != -1) {
                on_replay_save_button_click(nullptr, page_navigation_userdata->app);
            }
        }
        return GDK_FILTER_CONTINUE;
    }

    if(hotkey_mode != HotkeyMode::NewHotkey)
        return GDK_FILTER_CONTINUE;

    if(ev->type == KeyPress && key_sym == XK_Escape) {
        if(pressed_hotkey.modkey_mask == None && pressed_hotkey.keysym == None) {
            ungrab_keyboard(display);
            current_hotkey = nullptr;
            hotkey_mode = HotkeyMode::Record;
        }
        return GDK_FILTER_CONTINUE;
    }

    if(ev->type == KeyPress && key_sym == XK_BackSpace) {
        if(pressed_hotkey.modkey_mask == None && pressed_hotkey.keysym == None) {
            ungrab_keyboard(display);
            gtk_entry_set_text(GTK_ENTRY(current_hotkey->hotkey_entry), "");
            current_hotkey->keysym = None;
            current_hotkey->modkey_mask = 0;
            current_hotkey = nullptr;
            hotkey_mode = HotkeyMode::Record;
            save_configs();
        }
        return GDK_FILTER_CONTINUE;
    }

    if(ev->type == KeyPress) {
        // Ignore already pressed key
        if(key_is_modifier(key_sym)) {
            if(pressed_hotkey.modkey_mask & modkey_to_mask(key_sym))
                return GDK_FILTER_CONTINUE;
            pressed_hotkey.modkey_mask |= modkey_to_mask(key_sym);
        } else {
            if(key_sym == pressed_hotkey.keysym)
                return GDK_FILTER_CONTINUE;
            pressed_hotkey.keysym = key_sym;
        }

        latest_hotkey = pressed_hotkey;
        set_hotkey_text_from_hotkey_data(GTK_ENTRY(current_hotkey->hotkey_entry), latest_hotkey);
    }
    
    if(ev->type == KeyRelease) {
        if(key_is_modifier(key_sym)) {
            pressed_hotkey.modkey_mask &= ~modkey_to_mask(key_sym);
        } else if(key_sym == pressed_hotkey.keysym) {
            pressed_hotkey.keysym = None;
        }

        if(pressed_hotkey.modkey_mask == None && pressed_hotkey.keysym == None && latest_hotkey.keysym == None) {
            set_hotkey_text_from_hotkey_data(GTK_ENTRY(current_hotkey->hotkey_entry), *current_hotkey);
            return GDK_FILTER_CONTINUE;
        }

        if(pressed_hotkey.modkey_mask == None && pressed_hotkey.keysym == None) {
            ungrab_keyboard(display);
            ungrab_keys(gdk_x11_get_default_xdisplay());

            bool hotkey_already_used_by_another_hotkey = false;
            if(current_hotkey == &replay_start_stop_hotkey)
                hotkey_already_used_by_another_hotkey = (latest_hotkey.keysym == replay_save_hotkey.keysym && latest_hotkey.modkey_mask == replay_save_hotkey.modkey_mask);
            else if(current_hotkey == &replay_save_hotkey)
                hotkey_already_used_by_another_hotkey = (latest_hotkey.keysym == replay_start_stop_hotkey.keysym && latest_hotkey.modkey_mask == replay_start_stop_hotkey.modkey_mask);

            if(hotkey_already_used_by_another_hotkey) {
                std::string hotkey_text = gtk_entry_get_text(GTK_ENTRY(current_hotkey->hotkey_entry));
                std::string error_text = "Hotkey " + hotkey_text + " can't be used because it's used for something else. Please choose another hotkey.";
                set_hotkey_text_from_hotkey_data(GTK_ENTRY(current_hotkey->hotkey_entry), *current_hotkey);
                GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", error_text.c_str());
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);

                current_hotkey = nullptr;
                return GDK_FILTER_CONTINUE;
            }

            Hotkey prev_current_hotkey = *current_hotkey;
            current_hotkey->keysym = latest_hotkey.keysym;
            current_hotkey->modkey_mask = latest_hotkey.modkey_mask;

            HotkeyResult hotkey_result = replace_grabbed_keys_depending_on_active_page();
            bool hotkey_success = false;
            if(current_hotkey == &record_hotkey)
                hotkey_success = hotkey_result.record_hotkey_success;
            else if(current_hotkey == &pause_unpause_hotkey)
                hotkey_success = hotkey_result.pause_unpause_hotkey_success;
            else if(current_hotkey == &streaming_hotkey)
                hotkey_success = hotkey_result.streaming_hotkey_success;
            else if(current_hotkey == &replay_start_stop_hotkey)
                hotkey_success = hotkey_result.replay_start_stop_hotkey_success;
            else if(current_hotkey == &replay_save_hotkey)
                hotkey_success = hotkey_result.replay_save_hotkey_success;

            if(hotkey_success) {
                save_configs();
            } else {
                *current_hotkey = prev_current_hotkey;
                std::string hotkey_text = gtk_entry_get_text(GTK_ENTRY(current_hotkey->hotkey_entry));
                std::string error_text = "Hotkey " + hotkey_text + " can't be used because it's used by another program. Please choose another hotkey.";
                set_hotkey_text_from_hotkey_data(GTK_ENTRY(current_hotkey->hotkey_entry), *current_hotkey);
                GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", error_text.c_str());
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            }

            current_hotkey = nullptr;
            return GDK_FILTER_CONTINUE;
        }
    }

    return GDK_FILTER_CONTINUE;
}

static gboolean on_hotkey_entry_click(GtkWidget *button, gpointer) {
    if(wayland)
        return true;

    hotkey_mode = HotkeyMode::NewHotkey;

    pressed_hotkey.hotkey_entry = nullptr;
    pressed_hotkey.hotkey_active_label = nullptr;
    pressed_hotkey.keysym = None;
    pressed_hotkey.modkey_mask = 0;
    latest_hotkey = pressed_hotkey;

    if(button == record_hotkey_button) {
        current_hotkey = &record_hotkey;
    } else if(button == pause_unpause_hotkey_button) {
        current_hotkey = &pause_unpause_hotkey;
    } else if(button == streaming_hotkey_button) {
        current_hotkey = &streaming_hotkey;
    } else if(button == replay_start_stop_hotkey_button) {
        current_hotkey = &replay_start_stop_hotkey;
    } else if(button == replay_save_hotkey_button) {
        current_hotkey = &replay_save_hotkey;
    } else {
        current_hotkey = nullptr;
    }

    if(current_hotkey) {
        gtk_grab_add(current_hotkey->hotkey_entry);
        gtk_widget_set_visible(current_hotkey->hotkey_active_label, true);
    }

    Display *display = gdk_x11_get_default_xdisplay();
    XErrorHandler prev_error_handler = XSetErrorHandler(xerror_dummy);
    XGrabKeyboard(display, DefaultRootWindow(display), False, GrabModeAsync, GrabModeAsync, CurrentTime);
    XSync(display, False);
    XSetErrorHandler(prev_error_handler);
    return true;
}

static bool audio_inputs_contains(const std::vector<AudioInput> &audio_inputs, const std::string &audio_input_name) {
    for(auto &audio_input : audio_inputs) {
        if(audio_input.name == audio_input_name)
            return true;
    }
    return false;
}

static gsr_connection_type get_connection_type() {
    if(wayland || gpu_inf.vendor != GPU_VENDOR_NVIDIA) {
        return GSR_CONNECTION_DRM;
    } else {
        return GSR_CONNECTION_X11;
    }
}

static bool get_supported_video_codecs(SupportedVideoCodecs *supported_video_codecs) {
    supported_video_codecs->h264 = false;
    supported_video_codecs->hevc = false;
    supported_video_codecs->av1 = false;

    FILE *f = popen("gpu-screen-recorder --list-supported-video-codecs", "r");
    if(!f) {
        fprintf(stderr, "error: 'gpu-screen-recorder --list-supported-video-codecs' failed\n");
        return false;
    }

    char output[1024];
    ssize_t bytes_read = fread(output, 1, sizeof(output) - 1, f);
    if(bytes_read < 0 || ferror(f)) {
        fprintf(stderr, "error: failed to read 'gpu-screen-recorder --list-supported-video-codecs' output\n");
        pclose(f);
        return false;
    }
    output[bytes_read] = '\0';

    if(strstr(output, "h264"))
        supported_video_codecs->h264 = true;
    if(strstr(output, "hevc"))
        supported_video_codecs->hevc = true;
    if(strstr(output, "av1"))
        supported_video_codecs->av1 = true;

    pclose(f);
    return true;
}

static void record_area_set_sensitive(GtkCellLayout *cell_layout, GtkCellRenderer *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data) {
    (void)cell_layout;
    (void)data;
    if(wayland) {
        gchar *id;
        gtk_tree_model_get(tree_model, iter, 1, &id, -1);
        gboolean sensitive = g_strcmp0("window", id) != 0 && g_strcmp0("focused", id) != 0;
        g_free(id);
        g_object_set(cell, "sensitive", sensitive, NULL);
    } else {
        g_object_set(cell, "sensitive", true, NULL);
    }
}

static GtkWidget* create_common_settings_page(GtkStack *stack, GtkApplication *app, const gpu_info &gpu_inf) {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "common-settings");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    int grid_row = 0;
    int record_area_row = 0;
    int audio_input_area_row = 0;

    GtkGrid *simple_advanced_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(simple_advanced_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(simple_advanced_grid, gtk_label_new("View: "), 0, 0, 1, 1);
    view_combo_box = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(view_combo_box, "simple", "Simple");
    gtk_combo_box_text_append(view_combo_box, "advanced", "Advanced");
    gtk_widget_set_hexpand(GTK_WIDGET(view_combo_box), true);
    gtk_grid_attach(simple_advanced_grid, GTK_WIDGET(view_combo_box), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(view_combo_box), 0);
    g_signal_connect(view_combo_box, "changed", G_CALLBACK(view_combo_box_change_callback), view_combo_box);

    GtkFrame *record_area_frame = GTK_FRAME(gtk_frame_new("Record area"));
    gtk_grid_attach(grid, GTK_WIDGET(record_area_frame), 0, grid_row++, 2, 1);

    GtkGrid *record_area_grid = GTK_GRID(gtk_grid_new());
    gtk_widget_set_vexpand(GTK_WIDGET(record_area_grid), false);
    gtk_widget_set_hexpand(GTK_WIDGET(record_area_grid), true);
    gtk_grid_set_row_spacing(record_area_grid, 10);
    gtk_grid_set_column_spacing(record_area_grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(record_area_grid), 10, 10, 10, 10);
    gtk_container_add(GTK_CONTAINER(record_area_frame), GTK_WIDGET(record_area_grid));

    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkTreeIter iter;
    record_area_selection_model = GTK_TREE_MODEL(store);

    if(wayland) {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Window (Unavailable on Wayland)", -1);
        gtk_list_store_set(store, &iter, 1, "window", -1);

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Follow focused window (Unavailable on Wayland)", -1);
        gtk_list_store_set(store, &iter, 1, "focused", -1);
    } else {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Window", -1, "window", -1);
        gtk_list_store_set(store, &iter, 1, "window", -1);

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Follow focused window", -1, "focused", -1);
        gtk_list_store_set(store, &iter, 1, "focused", -1);
    }

    const bool allow_screen_capture = wayland || nvfbc_installed || gpu_inf.vendor != GPU_VENDOR_NVIDIA;
    if(allow_screen_capture) {
        if(!wayland && gpu_inf.vendor == GPU_VENDOR_NVIDIA) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, "All monitors", -1);
            gtk_list_store_set(store, &iter, 1, "screen", -1);
        }

        gsr_connection_type connection_type = get_connection_type();

        for_each_active_monitor_output(&egl, connection_type, [&](const gsr_monitor *monitor, void*) {
            std::string label = "Monitor ";
            label.append(monitor->name, monitor->name_len);
            label += " (";
            label += std::to_string(monitor->size.x);
            label += "x";
            label += std::to_string(monitor->size.y);
            if(wayland) {
                label += ", requires root access";
            } else if(gpu_inf.vendor != GPU_VENDOR_NVIDIA) {
                label += ", requires root access";
            }
            label += ")";

            // Leak on purpose, what are you gonna do? stab me?
            char *id = (char*)malloc(monitor->name_len + 1);
            if(!id) {
                fprintf(stderr, "Failed to allocate memory\n");
                abort();
            }
            memcpy(id, monitor->name, monitor->name_len);
            id[monitor->name_len] = '\0';

            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, label.c_str(), -1);
            gtk_list_store_set(store, &iter, 1, id, -1);
        }, NULL);
    }

    record_area_selection_menu = GTK_COMBO_BOX(gtk_combo_box_new_with_model(record_area_selection_model));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(record_area_selection_menu), renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(record_area_selection_menu), renderer, "text", 0, NULL);
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(record_area_selection_menu), renderer, record_area_set_sensitive, NULL, NULL);

    gtk_combo_box_set_active(record_area_selection_menu, allow_screen_capture ? 2 : 0);

    gtk_widget_set_hexpand(GTK_WIDGET(record_area_selection_menu), true);
    gtk_grid_attach(record_area_grid, GTK_WIDGET(record_area_selection_menu), 0, record_area_row++, 3, 1);

    if(!wayland) {
        select_window_button = GTK_BUTTON(gtk_button_new_with_label("Select window..."));
        gtk_widget_set_hexpand(GTK_WIDGET(select_window_button), true);
        g_signal_connect(select_window_button, "clicked", G_CALLBACK(on_select_window_button_click), app);
        gtk_grid_attach(record_area_grid, GTK_WIDGET(select_window_button), 0, record_area_row++, 3, 1);

        g_signal_connect(record_area_selection_menu, "changed", G_CALLBACK(record_area_item_change_callback), select_window_button);

        area_size_label = GTK_LABEL(gtk_label_new("Area size: "));
        gtk_label_set_xalign(area_size_label, 0.0f);
        gtk_grid_attach(record_area_grid, GTK_WIDGET(area_size_label), 0, record_area_row++, 2, 1);

        area_size_grid = GTK_GRID(gtk_grid_new());
        gtk_grid_attach(record_area_grid, GTK_WIDGET(area_size_grid), 0, record_area_row++, 3, 1);

        area_width_entry = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(5.0, 10000.0, 1.0));
        gtk_spin_button_set_value(area_width_entry, 1920.0);
        gtk_widget_set_hexpand(GTK_WIDGET(area_width_entry), true);
        gtk_grid_attach(area_size_grid, GTK_WIDGET(area_width_entry), 0, 0, 1, 1);

        gtk_grid_attach(area_size_grid, gtk_label_new("x"), 1, 0, 1, 1);

        area_height_entry = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(5.0, 10000.0, 1.0));
        gtk_spin_button_set_value(area_height_entry, 1080.0);
        gtk_widget_set_hexpand(GTK_WIDGET(area_height_entry), true);
        gtk_grid_attach(area_size_grid, GTK_WIDGET(area_height_entry), 2, 0, 1, 1);
    }

    GtkFrame *audio_input_frame = GTK_FRAME(gtk_frame_new("Audio"));
    gtk_grid_attach(grid, GTK_WIDGET(audio_input_frame), 0, grid_row++, 2, 1);

    GtkGrid *audio_grid = GTK_GRID(gtk_grid_new());
    gtk_widget_set_vexpand(GTK_WIDGET(audio_grid), false);
    gtk_widget_set_hexpand(GTK_WIDGET(audio_grid), true);
    gtk_grid_set_row_spacing(audio_grid, 10);
    gtk_grid_set_column_spacing(audio_grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(audio_grid), 10, 10, 10, 10);
    gtk_container_add(GTK_CONTAINER(audio_input_frame), GTK_WIDGET(audio_grid));

    GtkGrid *add_audio_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(add_audio_grid, 10);
    gtk_grid_set_column_spacing(add_audio_grid, 10);
    gtk_grid_attach(audio_grid, GTK_WIDGET(add_audio_grid), 0, audio_input_area_row++, 1, 1);

    // TODO:
    //const PulseAudioServerInfo pa_server_info = get_pulseaudio_default_inputs();

    // if(!pa_server_info.default_sink_name.empty() && audio_inputs_contains(audio_inputs, pa_server_info.default_sink_name)) {
    //     gtk_combo_box_text_append(audio_input_menu_todo, pa_server_info.default_sink_name.c_str(), "Default output");
    //     ++num_audio_inputs_addable;
    // }

    // if(!pa_server_info.default_source_name.empty() && audio_inputs_contains(audio_inputs, pa_server_info.default_source_name)) {
    //     gtk_combo_box_text_append(audio_input_menu_todo, pa_server_info.default_source_name.c_str(), "Default input");
    //     ++num_audio_inputs_addable;
    // }

    // for(const AudioInput &audio_input : audio_inputs) {
    //     std::string text = audio_input.description;
    //     gtk_combo_box_text_append(audio_input_menu_todo, audio_input.name.c_str(), audio_input.description.c_str());
    //     ++num_audio_inputs_addable;
    // }

    add_audio_input_button = gtk_button_new_with_label("Add audio track");
    gtk_grid_attach(add_audio_grid, add_audio_input_button, 0, 0, 1, 1);
    g_signal_connect(add_audio_input_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer){
        GtkWidget *row = create_used_audio_input_row();
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(audio_input_used_list), row, -1);
        return true;
    }), nullptr);

    audio_input_used_list = gtk_list_box_new();
    gtk_widget_set_hexpand (audio_input_used_list, TRUE);
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (audio_input_used_list), GTK_SELECTION_NONE);
    gtk_grid_attach(audio_grid, audio_input_used_list, 0, audio_input_area_row++, 2, 1);

    merge_audio_tracks_button = gtk_check_button_new_with_label("Merge audio tracks");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(merge_audio_tracks_button), true);
    gtk_widget_set_halign(merge_audio_tracks_button, GTK_ALIGN_START);
    gtk_grid_attach(grid, merge_audio_tracks_button, 0, grid_row++, 2, 1);

    GtkGrid *fps_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(fps_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(fps_grid, gtk_label_new("Frame rate: "), 0, 0, 1, 1);
    fps_entry = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1.0, 5000.0, 1.0));
    gtk_spin_button_set_value(fps_entry, 60.0);
    gtk_widget_set_hexpand(GTK_WIDGET(fps_entry), true);
    gtk_grid_attach(fps_grid, GTK_WIDGET(fps_entry), 1, 0, 1, 1);

    color_range_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(color_range_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(color_range_grid, gtk_label_new("Color range: "), 0, 0, 1, 1);
    color_range_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(color_range_input_menu, "limited", "Limited");
    gtk_combo_box_text_append(color_range_input_menu, "full", "Full");
    gtk_widget_set_hexpand(GTK_WIDGET(color_range_input_menu), true);
    gtk_grid_attach(color_range_grid, GTK_WIDGET(color_range_input_menu), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(color_range_input_menu), 0);

    GtkGrid *quality_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(quality_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(quality_grid, gtk_label_new("Video quality: "), 0, 0, 1, 1);
    quality_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(quality_input_menu, "medium", "Medium");
    gtk_combo_box_text_append(quality_input_menu, "high", "High (Recommended for live streaming)");
    gtk_combo_box_text_append(quality_input_menu, "very_high", "Very High (Recommended)");
    gtk_combo_box_text_append(quality_input_menu, "ultra", "Ultra");
    gtk_widget_set_hexpand(GTK_WIDGET(quality_input_menu), true);
    gtk_grid_attach(quality_grid, GTK_WIDGET(quality_input_menu), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(quality_input_menu), 0);

    video_codec_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(video_codec_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(video_codec_grid, gtk_label_new("Video codec: "), 0, 0, 1, 1);
    video_codec_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(video_codec_input_menu, "auto", "Auto (Recommended)");
    if(got_supported_video_codecs) {
        if(supported_video_codecs.h264) {
            gtk_combo_box_text_append(video_codec_input_menu, "h264", "H264");
        }
        if(supported_video_codecs.hevc) {
            gtk_combo_box_text_append(video_codec_input_menu, "hevc", "HEVC");
            if(wayland && gpu_inf.vendor != GPU_VENDOR_NVIDIA)
                gtk_combo_box_text_append(video_codec_input_menu, "hevc_hdr", "HEVC (HDR)");
        }
        if(supported_video_codecs.av1) {
            gtk_combo_box_text_append(video_codec_input_menu, "av1", "AV1");
            if(wayland && gpu_inf.vendor != GPU_VENDOR_NVIDIA)
                gtk_combo_box_text_append(video_codec_input_menu, "av1_hdr", "AV1 (HDR)");
        }
    } else {
        gtk_combo_box_text_append(video_codec_input_menu, "h264", "H264");
        gtk_combo_box_text_append(video_codec_input_menu, "hevc", "HEVC");
        if(wayland && gpu_inf.vendor != GPU_VENDOR_NVIDIA)
            gtk_combo_box_text_append(video_codec_input_menu, "hevc_hdr", "HEVC (HDR)");
    }
    gtk_widget_set_hexpand(GTK_WIDGET(video_codec_input_menu), true);
    gtk_grid_attach(video_codec_grid, GTK_WIDGET(video_codec_input_menu), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(video_codec_input_menu), 0);

    audio_codec_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(audio_codec_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(audio_codec_grid, gtk_label_new("Audio codec: "), 0, 0, 1, 1);
    audio_codec_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(audio_codec_input_menu, "opus", "Opus (Recommended)");
    gtk_combo_box_text_append(audio_codec_input_menu, "aac", "AAC");
    gtk_combo_box_text_append(audio_codec_input_menu, "flac", "FLAC");
    gtk_widget_set_hexpand(GTK_WIDGET(audio_codec_input_menu), true);
    gtk_grid_attach(audio_codec_grid, GTK_WIDGET(audio_codec_input_menu), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(audio_codec_input_menu), 0);

    framerate_mode_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(framerate_mode_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(framerate_mode_grid, gtk_label_new("Frame rate mode: "), 0, 0, 1, 1);
    framerate_mode_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(framerate_mode_input_menu, "auto", "Auto (Recommended)");
    gtk_combo_box_text_append(framerate_mode_input_menu, "cfr", "Constant");
    gtk_combo_box_text_append(framerate_mode_input_menu, "vfr", "Variable");
    gtk_widget_set_hexpand(GTK_WIDGET(framerate_mode_input_menu), true);
    gtk_grid_attach(framerate_mode_grid, GTK_WIDGET(framerate_mode_input_menu), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(framerate_mode_input_menu), 0);

    overclock_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(overclock_grid), 0, grid_row++, 2, 1);
    overclock_button = gtk_check_button_new_with_label("Overclock memory transfer rate to workaround NVIDIA driver performance bug");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(overclock_button), false);
    gtk_widget_set_halign(overclock_button, GTK_ALIGN_START);
    gtk_grid_attach(overclock_grid, overclock_button, 0, 0, 1, 1);
    GtkButton *overclock_info_button = GTK_BUTTON(gtk_button_new_with_label("?"));
    gtk_grid_attach(overclock_grid, GTK_WIDGET(overclock_info_button), 1, 0, 1, 1);

    g_signal_connect(overclock_info_button, "clicked", G_CALLBACK(+[](GtkButton *button, gpointer userdata){
        (void)button;
        (void)userdata;
        GtkWidget *dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "NVIDIA driver has a \"feature\" (read: bug) where it will downclock memory transfer rate when a program uses CUDA, such as GPU Screen Recorder.\n"
            "To work around this bug, GPU Screen Recorder can overclock your GPU memory transfer rate to it's normal optimal level. To enable overclocking for optimal performance enable the \"Overclock memory transfer rate to workaround NVIDIA driver performance bug\" option.\n"
            "You also need to have \"Coolbits\" NVIDIA X setting set to \"12\" to enable overclocking.\n"
            "Click <a href=\"https://wiki.archlinux.org/title/NVIDIA/Tips_and_tricks#Enabling_overclocking\">here</a> to see how to set this up manually or if you are using flatpak.\n"
            "\n"
            "Note that this only works when Xorg server is running as root, and using this option will only give you a performance boost if the game you are recording is bottlenecked by your GPU.\n"
            "\n"
            "Note! use at your own risk!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        return true;
    }), nullptr);

    show_notification_button = gtk_check_button_new_with_label("Show video saved notification");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_notification_button), true);
    gtk_widget_set_halign(show_notification_button, GTK_ALIGN_START);
    gtk_grid_attach(grid, show_notification_button, 0, grid_row++, 2, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, grid_row++, 2, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);

    stream_button = GTK_BUTTON(gtk_button_new_with_label("Stream"));
    gtk_widget_set_hexpand(GTK_WIDGET(stream_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(stream_button), 0, 0, 1, 1);

    record_button = GTK_BUTTON(gtk_button_new_with_label("Record"));
    gtk_widget_set_hexpand(GTK_WIDGET(record_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(record_button), 1, 0, 1, 1);

    replay_button = GTK_BUTTON(gtk_button_new_with_label("Replay"));
    gtk_widget_set_hexpand(GTK_WIDGET(replay_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(replay_button), 2, 0, 1, 1);

    gtk_widget_set_sensitive(GTK_WIDGET(replay_button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(record_button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(stream_button), false);

    return GTK_WIDGET(grid);
}

static GtkWidget* create_replay_page(GtkApplication *app, GtkStack *stack) {
    int row = 0;

    std::string video_filepath = get_videos_dir();

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "replay");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkWidget *hotkey_active_label = NULL;
    if(wayland) {
        gtk_grid_attach(grid, gtk_label_new("Wayland compositors don't support global hotkeys yet, use X11"), 0, row++, 5, 1);
        gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 5, 1);
    } else {
        hotkey_active_label = gtk_label_new("Press a key combination to set a new hotkey, backspace to remove the hotkey or esc to cancel");
        gtk_grid_attach(grid, hotkey_active_label, 0, row++, 5, 1);

        GtkWidget *a = gtk_label_new("Press");
        gtk_widget_set_halign(a, GTK_ALIGN_END);

        GtkWidget *b = gtk_label_new("to start/stop the replay and");
        gtk_widget_set_halign(b, GTK_ALIGN_START);

        GtkWidget *c = gtk_label_new("to save");
        gtk_widget_set_halign(c, GTK_ALIGN_START);

        replay_start_stop_hotkey_button = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(replay_start_stop_hotkey_button), "Alt + F1");
        g_signal_connect(replay_start_stop_hotkey_button, "button-press-event", G_CALLBACK(on_hotkey_entry_click), replay_start_stop_hotkey_button);

        replay_save_hotkey_button = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(replay_save_hotkey_button), "Alt + F2");
        gtk_widget_set_halign(replay_save_hotkey_button, GTK_ALIGN_START);
        g_signal_connect(replay_save_hotkey_button, "button-press-event", G_CALLBACK(on_hotkey_entry_click), replay_save_hotkey_button);

        gtk_grid_attach(grid, a, 0, row, 1, 1);
        gtk_grid_attach(grid, replay_start_stop_hotkey_button, 1, row, 1, 1);
        gtk_grid_attach(grid, b, 2, row, 1, 1);
        gtk_grid_attach(grid, replay_save_hotkey_button, 3, row, 1, 1);
        gtk_grid_attach(grid, c, 4, row, 1, 1);
        ++row;
        gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 5, 1);
    }

    replay_start_stop_hotkey.modkey_mask = modkey_to_mask(XK_Alt_L);
    replay_start_stop_hotkey.keysym = XK_F1;
    replay_start_stop_hotkey.hotkey_entry = replay_start_stop_hotkey_button;
    replay_start_stop_hotkey.hotkey_active_label = hotkey_active_label;

    replay_save_hotkey.modkey_mask = modkey_to_mask(XK_Alt_L);
    replay_save_hotkey.keysym = XK_F2;
    replay_save_hotkey.hotkey_entry = replay_save_hotkey_button;
    replay_save_hotkey.hotkey_active_label = hotkey_active_label;

    GtkWidget *save_icon = gtk_image_new_from_icon_name("document-save", GTK_ICON_SIZE_BUTTON);

    GtkGrid *file_chooser_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(file_chooser_grid), 0, row++, 5, 1);
    gtk_grid_set_column_spacing(file_chooser_grid, 10);
    GtkWidget *file_chooser_label = gtk_label_new("Where do you want to save the replays?");
    gtk_grid_attach(file_chooser_grid, file_chooser_label, 0, 0, 1, 1);
    replay_file_chooser_button = GTK_BUTTON(gtk_button_new_with_label(video_filepath.c_str()));
    gtk_button_set_image(replay_file_chooser_button, save_icon);
    gtk_button_set_always_show_image(replay_file_chooser_button, true);
    gtk_button_set_image_position(replay_file_chooser_button, GTK_POS_RIGHT);
    gtk_widget_set_hexpand(GTK_WIDGET(replay_file_chooser_button), true);
    g_signal_connect(replay_file_chooser_button, "clicked", G_CALLBACK(on_replay_file_chooser_button_click), nullptr);
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(replay_file_chooser_button), 1, 0, 1, 1);

    GtkGrid *container_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(container_grid), 0, row++, 5, 1);
    gtk_grid_attach(container_grid, gtk_label_new("Container: "), 0, 0, 1, 1);
    replay_container = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    for(auto &supported_container : supported_containers) {
        gtk_combo_box_text_append(replay_container, supported_container.container_name, supported_container.file_extension);
    }
    gtk_widget_set_hexpand(GTK_WIDGET(replay_container), true);
    gtk_grid_attach(container_grid, GTK_WIDGET(replay_container), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(replay_container), 0); // TODO:

    GtkGrid *replay_time_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(replay_time_grid), 0, row++, 5, 1);
    gtk_grid_attach(replay_time_grid, gtk_label_new("Replay time in seconds: "), 0, 0, 1, 1);
    replay_time_entry = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(5.0, 1200.0, 1.0));
    gtk_spin_button_set_value(replay_time_entry, 30.0);
    gtk_widget_set_hexpand(GTK_WIDGET(replay_time_entry), true);
    gtk_grid_attach(replay_time_grid, GTK_WIDGET(replay_time_entry), 1, 0, 1, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());

    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, row++, 5, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);
    replay_back_button = GTK_BUTTON(gtk_button_new_with_label("Back"));
    gtk_widget_set_hexpand(GTK_WIDGET(replay_back_button), true);

    gtk_grid_attach(start_button_grid, GTK_WIDGET(replay_back_button), 0, 0, 1, 1);
    start_replay_button = GTK_BUTTON(gtk_button_new_with_label("Start replay"));
    gtk_widget_set_hexpand(GTK_WIDGET(start_replay_button), true);
    g_signal_connect(start_replay_button, "clicked", G_CALLBACK(on_start_replay_button_click), app);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(start_replay_button), 1, 0, 1, 1);

    replay_save_button = GTK_BUTTON(gtk_button_new_with_label("Save replay"));
    gtk_widget_set_hexpand(GTK_WIDGET(replay_save_button), true);
    g_signal_connect(replay_save_button, "clicked", G_CALLBACK(on_replay_save_button_click), app);
    gtk_widget_set_sensitive(GTK_WIDGET(replay_save_button), false);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(replay_save_button), 2, 0, 1, 1);

    gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 5, 1);

    replay_bottom_panel_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(replay_bottom_panel_grid), 0, row++, 5, 1);
    gtk_grid_set_column_spacing(replay_bottom_panel_grid, 5);
    gtk_widget_set_opacity(GTK_WIDGET(replay_bottom_panel_grid), 0.5);
    gtk_widget_set_halign(GTK_WIDGET(replay_bottom_panel_grid), GTK_ALIGN_END);

    GtkWidget *record_icon = gtk_image_new_from_icon_name("media-record", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_valign(record_icon, GTK_ALIGN_CENTER);
    gtk_grid_attach(replay_bottom_panel_grid, record_icon, 0, 0, 1, 1);

    replay_record_time_label = gtk_label_new("00:00:00");
    gtk_widget_set_valign(replay_record_time_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(replay_bottom_panel_grid, replay_record_time_label, 1, 0, 1, 1);

    return GTK_WIDGET(grid);
}

static GtkWidget* create_recording_page(GtkApplication *app, GtkStack *stack) {
    int row = 0;

    std::string video_filepath = get_videos_dir();

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "recording");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkWidget *hotkey_active_label = NULL;
    if(wayland) {
        gtk_grid_attach(grid, gtk_label_new("Wayland compositors don't support global hotkeys yet, use X11"), 0, row++, 5, 1);
        gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 5, 1);
    } else {
        hotkey_active_label = gtk_label_new("Press a key combination to set a new hotkey, backspace to remove the hotkey or esc to cancel");
        gtk_grid_attach(grid, hotkey_active_label, 0, row++, 5, 1);

        GtkWidget *a = gtk_label_new("Press");
        gtk_widget_set_halign(a, GTK_ALIGN_END);

        GtkWidget *b = gtk_label_new("to start/stop recording and");
        gtk_widget_set_halign(b, GTK_ALIGN_START);

        GtkWidget *c = gtk_label_new("to pause/unpause");
        gtk_widget_set_halign(c, GTK_ALIGN_START);

        record_hotkey_button = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(record_hotkey_button), "Alt + F1");
        g_signal_connect(record_hotkey_button, "button-press-event", G_CALLBACK(on_hotkey_entry_click), record_hotkey_button);

        pause_unpause_hotkey_button = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(pause_unpause_hotkey_button), "Alt + F2");
        gtk_widget_set_halign(pause_unpause_hotkey_button, GTK_ALIGN_START);
        g_signal_connect(pause_unpause_hotkey_button, "button-press-event", G_CALLBACK(on_hotkey_entry_click), pause_unpause_hotkey_button);

        gtk_grid_attach(grid, a, 0, row, 1, 1);
        gtk_grid_attach(grid, record_hotkey_button, 1, row, 1, 1);
        gtk_grid_attach(grid, b, 2, row, 1, 1);
        gtk_grid_attach(grid, pause_unpause_hotkey_button, 3, row, 1, 1);
        gtk_grid_attach(grid, c, 4, row, 1, 1);
        ++row;
        gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 5, 1);
    }

    record_hotkey.modkey_mask = modkey_to_mask(XK_Alt_L);
    record_hotkey.keysym = XK_F1;
    record_hotkey.hotkey_entry = record_hotkey_button;
    record_hotkey.hotkey_active_label = hotkey_active_label;

    pause_unpause_hotkey.modkey_mask = modkey_to_mask(XK_Alt_L);
    pause_unpause_hotkey.keysym = XK_F2;
    pause_unpause_hotkey.hotkey_entry = pause_unpause_hotkey_button;
    pause_unpause_hotkey.hotkey_active_label = hotkey_active_label;

    GtkWidget *save_icon = gtk_image_new_from_icon_name("document-save", GTK_ICON_SIZE_BUTTON);

    GtkGrid *file_chooser_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(file_chooser_grid), 0, row++, 5, 1);
    gtk_grid_set_column_spacing(file_chooser_grid, 10);
    GtkWidget *file_chooser_label = gtk_label_new("Where do you want to save the video?");
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(file_chooser_label), 0, 0, 1, 1);
    record_file_chooser_button = GTK_BUTTON(gtk_button_new_with_label(video_filepath.c_str()));
    gtk_button_set_image(record_file_chooser_button, save_icon);
    gtk_button_set_always_show_image(record_file_chooser_button, true);
    gtk_button_set_image_position(record_file_chooser_button, GTK_POS_RIGHT);
    gtk_widget_set_hexpand(GTK_WIDGET(record_file_chooser_button), true);
    g_signal_connect(record_file_chooser_button, "clicked", G_CALLBACK(on_record_file_choose_button_click), nullptr);
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(record_file_chooser_button), 1, 0, 1, 1);

    GtkGrid *container_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(container_grid), 0, row++, 5, 1);
    gtk_grid_attach(container_grid, gtk_label_new("Container: "), 0, 0, 1, 1);
    record_container = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    for(auto &supported_container : supported_containers) {
        gtk_combo_box_text_append(record_container, supported_container.container_name, supported_container.file_extension);
    }
    gtk_widget_set_hexpand(GTK_WIDGET(record_container), true);
    gtk_grid_attach(container_grid, GTK_WIDGET(record_container), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(record_container), 0); // TODO:

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, row++, 5, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);

    record_back_button = GTK_BUTTON(gtk_button_new_with_label("Back"));
    gtk_widget_set_hexpand(GTK_WIDGET(record_back_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(record_back_button), 0, 0, 1, 1);

    start_recording_button = GTK_BUTTON(gtk_button_new_with_label("Start recording"));
    gtk_widget_set_hexpand(GTK_WIDGET(start_recording_button), true);
    g_signal_connect(start_recording_button, "clicked", G_CALLBACK(on_start_recording_button_click), app);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(start_recording_button), 1, 0, 1, 1);

    pause_recording_button = GTK_BUTTON(gtk_button_new_with_label("Pause recording"));
    gtk_widget_set_hexpand(GTK_WIDGET(pause_recording_button), true);
    g_signal_connect(pause_recording_button, "clicked", G_CALLBACK(on_pause_unpause_button_click), app);
    gtk_widget_set_sensitive(GTK_WIDGET(pause_recording_button), false);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(pause_recording_button), 2, 0, 1, 1);

    gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 5, 1);

    recording_bottom_panel_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(recording_bottom_panel_grid), 0, row++, 5, 1);
    gtk_grid_set_column_spacing(recording_bottom_panel_grid, 5);
    gtk_widget_set_opacity(GTK_WIDGET(recording_bottom_panel_grid), 0.5);
    gtk_widget_set_halign(GTK_WIDGET(recording_bottom_panel_grid), GTK_ALIGN_END);

    recording_record_icon = gtk_image_new_from_icon_name("media-record", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_valign(recording_record_icon, GTK_ALIGN_CENTER);
    gtk_grid_attach(recording_bottom_panel_grid, recording_record_icon, 0, 0, 1, 1);

    recording_record_time_label = gtk_label_new("00:00:00");
    gtk_widget_set_valign(recording_record_time_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(recording_bottom_panel_grid, recording_record_time_label, 1, 0, 1, 1);

    return GTK_WIDGET(grid);
}

static GtkWidget* create_streaming_page(GtkApplication *app, GtkStack *stack) {
    int row = 0;

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "streaming");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkWidget *hotkey_active_label = NULL;
    if(wayland) {
        gtk_grid_attach(grid, gtk_label_new("Wayland compositors don't support global hotkeys yet, use X11"), 0, row++, 3, 1);
        gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 3, 1);
    } else {
        hotkey_active_label = gtk_label_new("Press a key combination to set a new hotkey, backspace to remove the hotkey or esc to cancel");
        gtk_grid_attach(grid, hotkey_active_label, 0, row++, 3, 1);

        GtkWidget *a = gtk_label_new("Press");
        gtk_widget_set_halign(a, GTK_ALIGN_END);

        GtkWidget *b = gtk_label_new("to start/stop streaming");
        gtk_widget_set_halign(b, GTK_ALIGN_START);

        streaming_hotkey_button = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(streaming_hotkey_button), "Alt + F1");
        g_signal_connect(streaming_hotkey_button, "button-press-event", G_CALLBACK(on_hotkey_entry_click), streaming_hotkey_button);
        gtk_grid_attach(grid, a, 0, row, 1, 1);
        gtk_grid_attach(grid, streaming_hotkey_button, 1, row, 1, 1);
        gtk_grid_attach(grid, b, 2, row, 1, 1);
        ++row;
        gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 3, 1);
    }

    streaming_hotkey.modkey_mask = modkey_to_mask(XK_Alt_L);
    streaming_hotkey.keysym = XK_F1;
    streaming_hotkey.hotkey_entry = streaming_hotkey_button;
    streaming_hotkey.hotkey_active_label = hotkey_active_label;

    GtkGrid *stream_service_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(stream_service_grid), 0, row++, 3, 1);
    gtk_grid_attach(stream_service_grid, gtk_label_new("Stream service: "), 0, 0, 1, 1);
    stream_service_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(stream_service_input_menu, "twitch", "Twitch");
    gtk_combo_box_text_append(stream_service_input_menu, "youtube", "Youtube");
    gtk_combo_box_text_append(stream_service_input_menu, "custom", "Custom");
    gtk_combo_box_set_active(GTK_COMBO_BOX(stream_service_input_menu), 0);
    gtk_widget_set_hexpand(GTK_WIDGET(stream_service_input_menu), true);
    g_signal_connect(stream_service_input_menu, "changed", G_CALLBACK(stream_service_item_change_callback), NULL);
    gtk_grid_attach(stream_service_grid, GTK_WIDGET(stream_service_input_menu), 1, 0, 1, 1);

    GtkGrid *stream_id_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(stream_id_grid), 0, row++, 3, 1);
    stream_key_label = GTK_LABEL(gtk_label_new("Stream key: "));
    gtk_grid_attach(stream_id_grid, GTK_WIDGET(stream_key_label), 0, 0, 1, 1);
    stream_id_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_visibility(stream_id_entry, FALSE);
    gtk_entry_set_input_purpose(stream_id_entry, GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_icon_from_icon_name(stream_id_entry, GTK_ENTRY_ICON_SECONDARY, "view-reveal-symbolic");
    gtk_entry_set_icon_activatable(stream_id_entry, GTK_ENTRY_ICON_SECONDARY, true);
    g_signal_connect(stream_id_entry, "icon-press", G_CALLBACK(on_stream_key_icon_click), nullptr);
    gtk_widget_set_hexpand(GTK_WIDGET(stream_id_entry), true);
    gtk_grid_attach(stream_id_grid, GTK_WIDGET(stream_id_entry), 1, 0, 1, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, row++, 3, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);
    stream_back_button = GTK_BUTTON(gtk_button_new_with_label("Back"));
    gtk_widget_set_hexpand(GTK_WIDGET(stream_back_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(stream_back_button), 0, 0, 1, 1);
    start_streaming_button = GTK_BUTTON(gtk_button_new_with_label("Start streaming"));
    gtk_widget_set_hexpand(GTK_WIDGET(start_streaming_button), true);
    g_signal_connect(start_streaming_button, "clicked", G_CALLBACK(on_start_streaming_button_click), app);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(start_streaming_button), 1, 0, 1, 1);

    gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row++, 3, 1);

    streaming_bottom_panel_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(streaming_bottom_panel_grid), 0, row++, 3, 1);
    gtk_grid_set_column_spacing(streaming_bottom_panel_grid, 5);
    gtk_widget_set_opacity(GTK_WIDGET(streaming_bottom_panel_grid), 0.5);
    gtk_widget_set_halign(GTK_WIDGET(streaming_bottom_panel_grid), GTK_ALIGN_END);

    GtkWidget *record_icon = gtk_image_new_from_icon_name("media-record", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_valign(record_icon, GTK_ALIGN_CENTER);
    gtk_grid_attach(streaming_bottom_panel_grid, record_icon, 0, 0, 1, 1);

    streaming_record_time_label = gtk_label_new("00:00:00");
    gtk_widget_set_valign(streaming_record_time_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(streaming_bottom_panel_grid, streaming_record_time_label, 1, 0, 1, 1);

    return GTK_WIDGET(grid);
}

static gboolean on_destroy_window(GtkWidget*, GdkEvent*, gpointer) {
    if(gpu_screen_recorder_process != -1) {
        kill(gpu_screen_recorder_process, SIGINT);
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
            perror("waitpid failed");
            /* Ignore... */
        }
    }
    save_configs();
    return true;
}

static void handle_child_process_death(gpointer userdata) {
    if(gpu_screen_recorder_process == -1)
        return;

    int status = 0;
    if(waitpid(gpu_screen_recorder_process, &status, WNOHANG) == 0)
        return;

    prev_exit_status = -1;
    if(WIFEXITED(status))
        prev_exit_status = WEXITSTATUS(status);

    if(replaying) {
        on_start_replay_button_click(start_replay_button, userdata);
    } else if(recording) {
        on_start_recording_button_click(start_recording_button, userdata);
    } else if(streaming) {
        on_start_streaming_button_click(start_streaming_button, userdata);
    }
}

static void seconds_to_record_time_format(int seconds, char *buffer, size_t buffer_size) {
    const int hours = seconds / 60 / 60;
    seconds -= (hours * 60 * 60);

    const int mins = seconds / 60;
    seconds -= (mins * 60);

    snprintf(buffer, buffer_size, "%02d:%02d:%02d", hours, mins, seconds);
}

static void handle_record_timer() {
    if(streaming) {
        const double recording_time_passed_seconds = clock_get_monotonic_seconds() - record_start_time_sec;
        char record_time_str[32];
        seconds_to_record_time_format(recording_time_passed_seconds, record_time_str, sizeof(record_time_str));
        gtk_label_set_text(GTK_LABEL(streaming_record_time_label), record_time_str);
    }

    if(recording && !paused) {
        const double recording_time_passed_seconds = (clock_get_monotonic_seconds() - record_start_time_sec) - paused_time_offset_sec;
        char record_time_str[32];
        seconds_to_record_time_format(recording_time_passed_seconds, record_time_str, sizeof(record_time_str));
        gtk_label_set_text(GTK_LABEL(recording_record_time_label), record_time_str);
    }

    if(replaying) {
        const double recording_time_passed_seconds = clock_get_monotonic_seconds() - record_start_time_sec;
        char record_time_str[32];
        seconds_to_record_time_format(recording_time_passed_seconds, record_time_str, sizeof(record_time_str));
        gtk_label_set_text(GTK_LABEL(replay_record_time_label), record_time_str);
    }
}

static gboolean timer_timeout_handler(gpointer userdata) {
    handle_child_process_death(userdata);
    handle_record_timer();
    return G_SOURCE_CONTINUE;
}

static void add_audio_input_track(const char *name) {
    GtkWidget *row = create_used_audio_input_row();

    const AudioRow *audio_row = (AudioRow*)g_object_get_data(G_OBJECT(row), "audio-row");
    std::string audio_id;
    gint target_combo_box_index = combo_box_text_get_row_by_label(GTK_COMBO_BOX(audio_row->input_list), name, audio_id);
    if(target_combo_box_index != -1)
        gtk_combo_box_set_active(GTK_COMBO_BOX(audio_row->input_list), target_combo_box_index);

    gtk_widget_show_all(row);
    gtk_list_box_insert (GTK_LIST_BOX(audio_input_used_list), row, -1);
}

static void load_config(const gpu_info &gpu_inf) {
    bool config_empty = false;
    config = read_config(config_empty);

    std::string first_monitor;
    if(!wayland && strcmp(config.main_config.record_area_option.c_str(), "window") == 0) {
        //
    } else if(!wayland && strcmp(config.main_config.record_area_option.c_str(), "focused") == 0) {
        //
    } else if(!wayland && gpu_inf.vendor == GPU_VENDOR_NVIDIA && strcmp(config.main_config.record_area_option.c_str(), "screen") == 0) {
        //
    } else {
        gsr_connection_type connection_type = get_connection_type();

        bool found_monitor = false;
        int monitor_name_size = strlen(config.main_config.record_area_option.c_str());
        for_each_active_monitor_output(&egl, connection_type, [&](const gsr_monitor *monitor, void*) {
            if(first_monitor.empty()) {
                first_monitor.assign(monitor->name, monitor->name_len);
            }

            if(monitor_name_size == monitor->name_len && strncmp(config.main_config.record_area_option.c_str(), monitor->name, monitor->name_len) == 0) {
                found_monitor = true;
            }
        }, NULL);

        if(!found_monitor)
            config.main_config.record_area_option.clear();
    }

    if(config.main_config.record_area_option.empty()) {
        const bool allow_screen_capture = wayland || nvfbc_installed || gpu_inf.vendor != GPU_VENDOR_NVIDIA;
        if(allow_screen_capture) {
            config.main_config.record_area_option = first_monitor;
        } else {
            config.main_config.record_area_option = "window";
        }
    }

    if(!wayland) {
        gtk_widget_set_visible(GTK_WIDGET(select_window_button), strcmp(config.main_config.record_area_option.c_str(), "window") == 0);
        gtk_widget_set_visible(GTK_WIDGET(area_size_label), strcmp(config.main_config.record_area_option.c_str(), "focused") == 0);
        gtk_widget_set_visible(GTK_WIDGET(area_size_grid), strcmp(config.main_config.record_area_option.c_str(), "focused") == 0);
    }

    if(config.main_config.record_area_width == 0)
        config.main_config.record_area_width = 1920;

    if(config.main_config.record_area_height == 0)
        config.main_config.record_area_height = 1080;

    if(config.main_config.fps < 1)
        config.main_config.fps = 1;
    else if(config.main_config.fps > 5000)
        config.main_config.fps = 5000;

    if(config.main_config.color_range != "limited" && config.main_config.color_range != "full")
        config.main_config.color_range = "limited";

    if(config.main_config.quality != "medium" && config.main_config.quality != "high" && config.main_config.quality != "very_high" && config.main_config.quality != "ultra")
        config.main_config.quality = "very_high";

    if(config.main_config.codec != "auto" && config.main_config.codec != "h264" && config.main_config.codec != "h265" && config.main_config.codec != "hevc" && config.main_config.codec != "av1" && config.main_config.codec != "hevc_hdr" && config.main_config.codec != "av1_hdr")
        config.main_config.codec = "auto";

    if((!wayland || gpu_inf.vendor == GPU_VENDOR_NVIDIA) && (config.main_config.codec == "hevc_hdr" || config.main_config.codec == "av1_hdr"))
        config.main_config.codec = "auto";

    if(config.main_config.audio_codec != "opus" && config.main_config.audio_codec != "aac" && config.main_config.audio_codec != "flac")
        config.main_config.audio_codec = "opus";

    if(config.main_config.framerate_mode != "auto" && config.main_config.framerate_mode != "cfr" && config.main_config.framerate_mode != "vfr")
        config.main_config.framerate_mode = "auto";

    if(config.streaming_config.streaming_service != "twitch" && config.streaming_config.streaming_service != "youtube" && config.streaming_config.streaming_service != "custom")
        config.streaming_config.streaming_service = "twitch";

    if(config.streaming_config.streaming_service == "custom")
        gtk_label_set_text(stream_key_label, "Url: ");

    if(config.record_config.save_directory.empty() || !is_directory(config.record_config.save_directory.c_str()))
        config.record_config.save_directory = get_videos_dir();

    if(config.replay_config.save_directory.empty() || !is_directory(config.replay_config.save_directory.c_str()))
        config.replay_config.save_directory = get_videos_dir();

    if(config.replay_config.replay_time < 5)
        config.replay_config.replay_time = 5;
    else if(config.replay_config.replay_time > 1200)
        config.replay_config.replay_time = 1200;

    record_area_selection_menu_set_active_id(config.main_config.record_area_option.c_str());
    if(!wayland) {
        gtk_spin_button_set_value(area_width_entry, config.main_config.record_area_width);
        gtk_spin_button_set_value(area_height_entry, config.main_config.record_area_height);
    }
    gtk_spin_button_set_value(fps_entry, config.main_config.fps);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(merge_audio_tracks_button), config.main_config.merge_audio_tracks);

    for(const std::string &audio_input : config.main_config.audio_input) {
        add_audio_input_track(audio_input.c_str());
    }

    if(config_empty && config.main_config.audio_input.empty())
        add_audio_input_track("Default output");

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(color_range_input_menu), config.main_config.color_range.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(quality_input_menu), config.main_config.quality.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(video_codec_input_menu), config.main_config.codec.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(audio_codec_input_menu), config.main_config.audio_codec.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(framerate_mode_input_menu), config.main_config.framerate_mode.c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(overclock_button), config.main_config.overclock);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_notification_button), config.main_config.show_notifications);

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(stream_service_input_menu), config.streaming_config.streaming_service.c_str());
    gtk_entry_set_text(stream_id_entry, config.streaming_config.stream_key.c_str());
    if(!wayland && streaming_hotkey_button && !config_empty) {
        streaming_hotkey.keysym = config.streaming_config.start_recording_hotkey.keysym;
        streaming_hotkey.modkey_mask = config.streaming_config.start_recording_hotkey.modifiers;
        set_hotkey_text_from_hotkey_data(GTK_ENTRY(streaming_hotkey_button), streaming_hotkey);
    }

    gtk_button_set_label(record_file_chooser_button, config.record_config.save_directory.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(record_container), config.record_config.container.c_str());
    if(!wayland && record_hotkey_button && !config_empty) {
        record_hotkey.keysym = config.record_config.start_recording_hotkey.keysym;
        record_hotkey.modkey_mask = config.record_config.start_recording_hotkey.modifiers;
        set_hotkey_text_from_hotkey_data(GTK_ENTRY(record_hotkey_button), record_hotkey);
    }
    if(!wayland && pause_unpause_hotkey_button && !config_empty) {
        pause_unpause_hotkey.keysym = config.record_config.pause_recording_hotkey.keysym;
        pause_unpause_hotkey.modkey_mask = config.record_config.pause_recording_hotkey.modifiers;
        set_hotkey_text_from_hotkey_data(GTK_ENTRY(pause_unpause_hotkey_button), pause_unpause_hotkey);
    }

    gtk_button_set_label(replay_file_chooser_button, config.replay_config.save_directory.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(replay_container), config.replay_config.container.c_str());
    gtk_spin_button_set_value(replay_time_entry, config.replay_config.replay_time);
    if(!wayland && replay_start_stop_hotkey_button && !config_empty) {
        replay_start_stop_hotkey.keysym = config.replay_config.start_recording_hotkey.keysym;
        replay_start_stop_hotkey.modkey_mask = config.replay_config.start_recording_hotkey.modifiers;
        set_hotkey_text_from_hotkey_data(GTK_ENTRY(replay_start_stop_hotkey_button), replay_start_stop_hotkey);
    }
    if(!wayland && replay_save_hotkey_button && !config_empty) {
        replay_save_hotkey.keysym = config.replay_config.save_recording_hotkey.keysym;
        replay_save_hotkey.modkey_mask = config.replay_config.save_recording_hotkey.modifiers;
        set_hotkey_text_from_hotkey_data(GTK_ENTRY(replay_save_hotkey_button), replay_save_hotkey);
    }

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(view_combo_box), config.main_config.advanced_view ? "advanced" : "simple");

    view_combo_box_change_callback(GTK_COMBO_BOX(view_combo_box), view_combo_box);
    if(!wayland) {
        gtk_widget_set_visible(record_hotkey.hotkey_active_label, false);
        gtk_widget_set_visible(pause_unpause_hotkey.hotkey_active_label, false);
        gtk_widget_set_visible(streaming_hotkey.hotkey_active_label, false);
        gtk_widget_set_visible(replay_start_stop_hotkey.hotkey_active_label, false);
        gtk_widget_set_visible(replay_save_hotkey.hotkey_active_label, false);
    }
    enable_stream_record_button_if_info_filled();

    if(!supported_video_codecs.h264 && !supported_video_codecs.hevc && gpu_inf.vendor != GPU_VENDOR_NVIDIA && config.main_config.codec != "av1") {
        if(supported_video_codecs.av1) {
            GtkWidget *dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Your distro has disabled support for H264 and HEVC video codecs. Switched video codec to AV1. If you wish to use H264/HEVC video codecs then follow your distros guide to install a non-crippled mesa (with H264 and HEVC support) or switch to a less user hostile distro or recompile mesa from source. For example on fedora you may need to follow this: <a href=\"https://rpmfusion.org/Howto/Multimedia\">Hardware Accelerated Codec</a>.");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            config.main_config.codec = "av1";
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(video_codec_input_menu), config.main_config.codec.c_str());
        } else {
            GtkWidget *dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "Either your GPU doesn't support H264/HEVC video codecs or your distro has disabled support for those video codecs. If you wish to use H264/HEVC video codecs then follow your distros guide to install a non-crippled mesa (with H264 and HEVC support) or switch to a less user hostile distro or recompile mesa from source, if you know that your GPU supports H264/HEVC. For example on fedora you may need to follow this: <a href=\"https://rpmfusion.org/Howto/Multimedia\">Hardware Accelerated Codec</a>.");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            g_application_quit(G_APPLICATION(select_window_userdata.app));
            return;
        }
    }
}

static bool gl_get_gpu_info(gsr_egl *egl, gpu_info *info) {
    const char *software_renderers[] = { "llvmpipe", "SWR", "softpipe", NULL };
    bool supported = true;
    const unsigned char *gl_vendor = egl->glGetString(GL_VENDOR);
    const unsigned char *gl_renderer = egl->glGetString(GL_RENDERER);

    info->gpu_version = 0;

    if(!gl_vendor) {
        fprintf(stderr, "Error: failed to get gpu vendor\n");
        supported = false;
        goto end;
    }

    if(gl_renderer) {
        for(int i = 0; software_renderers[i]; ++i) {
            if(strstr((const char*)gl_renderer, software_renderers[i])) {
                fprintf(stderr, "gsr error: your opengl environment is not properly setup. It's using %s (software rendering) for opengl instead of your graphics card. Please make sure your graphics driver is properly installed\n", software_renderers[i]);
                supported = false;
                goto end;
            }
        }
    }

    if(strstr((const char*)gl_vendor, "AMD"))
        info->vendor = GPU_VENDOR_AMD;
    else if(strstr((const char*)gl_vendor, "Intel"))
        info->vendor = GPU_VENDOR_INTEL;
    else if(strstr((const char*)gl_vendor, "NVIDIA"))
        info->vendor = GPU_VENDOR_NVIDIA;
    else {
        fprintf(stderr, "Error: unknown gpu vendor: %s\n", gl_vendor);
        supported = false;
        goto end;
    }

    if(gl_renderer) {
        if(info->vendor == GPU_VENDOR_NVIDIA)
            sscanf((const char*)gl_renderer, "%*s %*s %*s %d", &info->gpu_version);
    }

    end:
    return supported;
}

static bool is_xwayland(Display *dpy) {
    int opcode, event, error;
    if(XQueryExtension(dpy, "XWAYLAND", &opcode, &event, &error))
        return true;

    bool xwayland_found = false;
    for_each_active_monitor_output_x11(dpy, [&xwayland_found](const gsr_monitor *monitor, void*) {
        if(monitor->name_len >= 8 && strncmp(monitor->name, "XWAYLAND", 8) == 0)
            xwayland_found = true;
        else if(memmem(monitor->name, monitor->name_len, "X11", 3))
            xwayland_found = true;
    }, NULL);
    return xwayland_found;
}

static const char* gpu_vendor_to_name(gpu_vendor vendor) {
    switch(vendor) {
        case GPU_VENDOR_AMD:        return "AMD";
        case GPU_VENDOR_INTEL:      return "Intel";
        case GPU_VENDOR_NVIDIA:     return "NVIDIA";
    }
    return "";
}

static void activate(GtkApplication *app, gpointer) {
    Display *dpy = XOpenDisplay(NULL);
    wayland = !dpy || is_xwayland(dpy);
    if(!wayland && !dpy) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Neither X11 nor Wayland is running.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_application_quit(G_APPLICATION(app));
        return;
    }

    nvfbc_installed = !wayland && is_nv_fbc_installed();

    if(!gsr_egl_load(&egl, dpy, wayland)) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to load OpenGL. Make sure your GPU drivers are properly installed.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_application_quit(G_APPLICATION(app));
        return;
    }

    if(!gl_get_gpu_info(&egl, &gpu_inf)) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to get OpenGL information. Make sure your GPU drivers are properly installed.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_application_quit(G_APPLICATION(app));
        return;
    }

    if((gpu_inf.vendor != GPU_VENDOR_NVIDIA) || wayland) {
        if(!gsr_get_valid_card_path(egl.card_path)) {
            GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "Failed to find a valid DRM card.");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            g_application_quit(G_APPLICATION(app));
            return;
        }
    } else {
        egl.card_path[0] = '\0';
    }

    if(gpu_inf.vendor == GPU_VENDOR_NVIDIA) {
        if(!is_cuda_installed()) {
            GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "CUDA is not installed on your system. GPU Screen Recorder requires CUDA to be installed to work with a NVIDIA GPU.");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            g_application_quit(G_APPLICATION(app));
            return;
        }

        if(!is_nvenc_installed()) {
            GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "NVENC is not installed on your system. GPU Screen Recorder requires NVENC to be installed to work with a NVIDIA GPU.");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            g_application_quit(G_APPLICATION(app));
            return;
        }
    }

    got_supported_video_codecs = get_supported_video_codecs(&supported_video_codecs);

    std::string window_title = "GPU Screen Recorder | Running on ";
    window_title += gpu_vendor_to_name(gpu_inf.vendor);

    window = gtk_application_window_new(app);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy_window), nullptr);
    gtk_window_set_title(GTK_WINDOW(window), window_title.c_str());
    gtk_window_set_resizable(GTK_WINDOW(window), false);

    select_window_userdata.app = app;
    audio_inputs = get_pulseaudio_inputs();
    pa_default_sources = get_pulseaudio_default_inputs();

    if(!pa_default_sources.default_source_name.empty() && audio_inputs_contains(audio_inputs, pa_default_sources.default_source_name))
        audio_inputs.insert(audio_inputs.begin(), { pa_default_sources.default_source_name.c_str(), "Default input" });

    if(!pa_default_sources.default_sink_name.empty() && audio_inputs_contains(audio_inputs, pa_default_sources.default_sink_name))
        audio_inputs.insert(audio_inputs.begin(), { pa_default_sources.default_sink_name.c_str(), "Default output" });

    if(!wayland)
        crosshair_cursor = XCreateFontCursor(gdk_x11_get_default_xdisplay(), XC_crosshair);

    GtkStack *stack = GTK_STACK(gtk_stack_new());
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(stack));
    gtk_stack_set_transition_type(stack, GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_set_transition_duration(stack, 0);
    gtk_stack_set_homogeneous(stack, false);
    GtkWidget *common_settings_page = create_common_settings_page(stack, app, gpu_inf);
    GtkWidget *replay_page = create_replay_page(app, stack);
    GtkWidget *recording_page = create_recording_page(app, stack);
    GtkWidget *streaming_page = create_streaming_page(app, stack);
    gtk_stack_set_visible_child(stack, common_settings_page);

    page_navigation_userdata.app = app;
    page_navigation_userdata.stack = stack;
    page_navigation_userdata.common_settings_page = common_settings_page;
    page_navigation_userdata.replay_page = replay_page;
    page_navigation_userdata.recording_page = recording_page;
    page_navigation_userdata.streaming_page = streaming_page;

    g_signal_connect(replay_button, "clicked", G_CALLBACK(on_start_replay_click), &page_navigation_userdata);
    g_signal_connect(replay_back_button, "clicked", G_CALLBACK(on_streaming_recording_replay_page_back_click), &page_navigation_userdata);

    g_signal_connect(record_button, "clicked", G_CALLBACK(on_start_recording_click), &page_navigation_userdata);
    g_signal_connect(record_back_button, "clicked", G_CALLBACK(on_streaming_recording_replay_page_back_click), &page_navigation_userdata);

    g_signal_connect(stream_button, "clicked", G_CALLBACK(on_start_streaming_click), &page_navigation_userdata);
    g_signal_connect(stream_back_button, "clicked", G_CALLBACK(on_streaming_recording_replay_page_back_click), &page_navigation_userdata);

    if(!wayland) {
        xim = XOpenIM(gdk_x11_get_default_xdisplay(), NULL, NULL, NULL);
        xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, NULL);

        GdkWindow *root_window = gdk_get_default_root_window();
        gdk_window_add_filter(root_window, hotkey_filter_callback, &page_navigation_userdata);
    }

    g_timeout_add(500, timer_timeout_handler, app);

    gtk_widget_show_all(window);
    load_config(gpu_inf);
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C");
    GtkApplication *app = gtk_application_new("com.dec05eba.gpu_screen_recorder", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
