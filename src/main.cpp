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

static SelectWindowUserdata select_window_userdata;
static PageNavigationUserdata page_navigation_userdata;
static GtkWidget *save_icon;
static Cursor crosshair_cursor;
static GtkSpinButton *fps_entry;
static GtkLabel *area_size_label;
static GtkGrid *area_size_grid;
static GtkSpinButton *area_width_entry;
static GtkSpinButton *area_height_entry;
static GtkComboBoxText *record_area_selection_menu;
static GtkComboBoxText *audio_input_menu;
static GtkComboBoxText *quality_input_menu;
static GtkComboBoxText *stream_service_input_menu;
static GtkButton *file_chooser_button;
static GtkButton *replay_file_chooser_button;
static GtkButton *stream_button;
static GtkButton *record_button;
static GtkButton *replay_button;
static GtkButton *replay_back_button;
static GtkButton *record_back_button;
static GtkButton *stream_back_button;
static GtkButton *replay_save_button;
static GtkButton *start_recording_button;
static GtkButton *start_replay_button;
static GtkButton *start_streaming_button;
static GtkEntry *stream_id_entry;
static GtkSpinButton *replay_time_entry;
static bool replaying = false;
static bool recording = false;
static bool streaming = false;
static pid_t gpu_screen_recorder_process = -1;
static Config config;

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

static const XRRModeInfo* get_mode_info(const XRRScreenResources *sr, RRMode id) {
    for(int i = 0; i < sr->nmode; ++i) {
        if(sr->modes[i].id == id)
            return &sr->modes[i];
    }    
    return nullptr;
}

static void for_each_active_monitor_output(Display *display, std::function<void(const XRROutputInfo*, const XRRCrtcInfo*, const XRRModeInfo*)> callback_func) {
    XRRScreenResources *screen_res = XRRGetScreenResources(display, DefaultRootWindow(display));
    if(!screen_res)
        return;

    for(int i = 0; i < screen_res->noutput; ++i) {
        XRROutputInfo *out_info = XRRGetOutputInfo(display, screen_res, screen_res->outputs[i]);
        if(out_info && out_info->crtc && out_info->connection == RR_Connected) {
            XRRCrtcInfo *crt_info = XRRGetCrtcInfo(display, screen_res, out_info->crtc);
            if(crt_info && crt_info->mode) {
                const XRRModeInfo *mode_info = get_mode_info(screen_res, crt_info->mode);
                if(mode_info)
                    callback_func(out_info, crt_info, mode_info);
            }
            if(crt_info)
                XRRFreeCrtcInfo(crt_info);
        }
        if(out_info)
            XRRFreeOutputInfo(out_info);
    }    

    XRRFreeScreenResources(screen_res);
}

static void show_notification(GtkApplication *app, const char *title, const char *body, GNotificationPriority priority) {
    fprintf(stderr, "Notification: title: %s, body: %s\n", title, body);
    GNotification *notification = g_notification_new(title);
    g_notification_set_body(notification, body);
    g_notification_set_priority(notification, priority);
    g_application_send_notification(&app->parent, "gpu-screen-recorder", notification);
}

static void enable_stream_record_button_if_info_filled() {
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(audio_input_menu)) == -1)
        return;

    const gchar *selected_window_area = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_area_selection_menu));
    if(strcmp(selected_window_area, "window") == 0 && select_window_userdata.selected_window == None)
        return;

    const gchar* audio_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_input_menu));
    bool allow_streaming = audio_input_str && strcmp(audio_input_str, "None") != 0;
    
    gtk_widget_set_sensitive(GTK_WIDGET(replay_button), true);
    gtk_widget_set_sensitive(GTK_WIDGET(record_button), true);
    gtk_widget_set_sensitive(GTK_WIDGET(stream_button), allow_streaming);
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

static Window window_get_target_window_parent(Display *display, Window window) {
    if(window == None || window == XDefaultRootWindow(display))
        return None;

    Atom wm_state_atom = XInternAtom(display, "WM_STATE", False);
    if(!wm_state_atom)
        return None;

    if(window_has_atom(display, window, wm_state_atom))
        return window;

    Window root;
    Window parent = None;
    Window *children = nullptr;
    unsigned int num_children = 0;
    if(!XQueryTree(display, window, &root, &parent, &children, &num_children))
        return None;

    if(children)
        XFree(children);

    if(!parent)
        return None;

    return window_get_target_window_parent(display, parent);
}

/* TODO: Look at xwininfo source to figure out how to make this work for different types of window managers */
static GdkFilterReturn filter_callback(GdkXEvent *xevent, GdkEvent *event, gpointer userdata) {
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

    XWindowAttributes attr;
    if(XGetWindowAttributes(select_window_userdata->display, select_window_userdata->selected_window, &attr)) {
        gtk_spin_button_set_value(area_width_entry, attr.width);
        gtk_spin_button_set_value(area_height_entry, attr.height);
    }

    GdkScreen *screen = gdk_screen_get_default();
    GdkWindow *root_window = gdk_screen_get_root_window(screen);
    gdk_window_remove_filter(root_window, filter_callback, select_window_userdata);

    enable_stream_record_button_if_info_filled();

    return GDK_FILTER_REMOVE;
}

static gboolean on_select_window_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;
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

static gboolean on_start_replay_click(GtkButton *button, gpointer userdata) {
    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->replay_page);
    return true;
}

static gboolean on_start_recording_click(GtkButton *button, gpointer userdata) {
    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->recording_page);

    std::string video_filepath = config.record_config.save_directory;
    video_filepath += "/Video_" + get_date_str() + ".mp4";
    gtk_button_set_label(file_chooser_button, video_filepath.c_str());
    return true;
}

static gboolean on_start_streaming_click(GtkButton *button, gpointer userdata) {
    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->streaming_page);
    return true;
}

static gboolean on_streaming_recording_page_back_click(GtkButton *button, gpointer userdata) {
    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->common_settings_page);
    return true;
}

static gboolean file_choose_button_click_handler(GtkButton *button, const char *title, GtkFileChooserAction file_action) {
    GtkWidget *file_chooser_dialog = gtk_file_chooser_dialog_new(title, nullptr, file_action,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        nullptr);

    if(file_action != GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(file_chooser_dialog), "video.mp4");

    int res = gtk_dialog_run(GTK_DIALOG(file_chooser_dialog));
    if(res == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_chooser_dialog));
        printf("filename: %s\n", filename);
        gtk_button_set_label(button, filename);
        g_free(filename);
    }
    gtk_widget_destroy(file_chooser_dialog);
    return true;
}

static gboolean on_file_chooser_button_click(GtkButton *button, gpointer userdata) {
    return file_choose_button_click_handler(button, "Where do you want to save the video?", GTK_FILE_CHOOSER_ACTION_SAVE);
}

static gboolean on_replay_file_chooser_button_click(GtkButton *button, gpointer userdata) {
    return file_choose_button_click_handler(button, "Where do you want to save the replays?", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
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

static Window get_window_with_input_focus(Display *display) {
    Window window = None;
    int rev;
    if(!XGetInputFocus(display, &window, &rev))
        window = None;
    return window_get_target_window_parent(display, window);
}

static gboolean on_start_replay_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;
    const gchar *dir = gtk_button_get_label(replay_file_chooser_button);

    if(replaying) {
        bool exit_success = kill_gpu_screen_recorder_get_result();

        gtk_button_set_label(button, "Start replay");
        replaying = false;
        gpu_screen_recorder_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(replay_back_button), true);
        gtk_widget_set_sensitive(GTK_WIDGET(replay_save_button), false);

        if(!exit_success)
            show_notification(app, "GPU Screen Recorder", "gpu-screen-recorder died unexpectedly while recording", G_NOTIFICATION_PRIORITY_URGENT);

        return true;
    }

    int fps = gtk_spin_button_get_value_as_int(fps_entry);
    int replay_time = gtk_spin_button_get_value_as_int(replay_time_entry);
    int record_width = gtk_spin_button_get_value_as_int(area_width_entry);
    int record_height = gtk_spin_button_get_value_as_int(area_height_entry);

    char dir_tmp[PATH_MAX];
    strcpy(dir_tmp, dir);
    if(create_directory_recursive(dir_tmp) != 0) {
        std::string notification_body = std::string("Failed to start replay. Failed to create ") + dir_tmp;
        show_notification(app, "GPU Screen Recorder", notification_body.c_str(), G_NOTIFICATION_PRIORITY_URGENT);
        return true;
    }

    bool record_window = false;
    std::string window_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_area_selection_menu));
    if(window_str == "window") {
        if(select_window_userdata.selected_window == None) {
            fprintf(stderr, "No window selected!\n");
            return true;
        }
        window_str = std::to_string(select_window_userdata.selected_window);
        record_window = true;
    } else if(window_str == "focused") {
        XWindowAttributes attr;
        Window focused_window = get_window_with_input_focus(gdk_x11_get_default_xdisplay());
        if(!focused_window || !XGetWindowAttributes(gdk_x11_get_default_xdisplay(), focused_window, &attr)) {
            show_notification(app, "GPU Screen Recorder", "No window is focused!", G_NOTIFICATION_PRIORITY_URGENT);
            return true;
        }

        window_str = std::to_string(focused_window);
        record_width = attr.width;
        record_height = attr.height;
        record_window = true;
    }
    std::string fps_str = std::to_string(fps);
    std::string replay_time_str = std::to_string(replay_time);

    const gchar* audio_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_input_menu));
    const gchar* quality_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", record_width, record_height);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", window_str.c_str(), "-c", "mp4", "-q", quality_input_str, "-f", fps_str.c_str(), "-r", replay_time_str.c_str(), "-o", dir
    };

    if(audio_input_str && strcmp(audio_input_str, "None") != 0)
        args.insert(args.end(), { "-a", audio_input_str });

    if(record_window)
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
    return true;
}

static gboolean on_replay_save_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;
    kill(gpu_screen_recorder_process, SIGUSR1);
    show_notification(app, "GPU Screen Recorder", "Saved replay", G_NOTIFICATION_PRIORITY_NORMAL);
    return true;
}

static gboolean on_start_recording_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;
    const gchar *filename = gtk_button_get_label(file_chooser_button);

    if(recording) {
        bool exit_success = kill_gpu_screen_recorder_get_result();

        gtk_button_set_label(button, "Start recording");
        recording = false;
        gpu_screen_recorder_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(record_back_button), true);

        if(exit_success) {
            std::string notification_body = std::string("The recording was saved to ") + filename;
            show_notification(app, "GPU Screen Recorder", notification_body.c_str(), G_NOTIFICATION_PRIORITY_NORMAL);
        } else {
            std::string notification_body = std::string("Failed to save the recording to ") + filename;
            show_notification(app, "GPU Screen Recorder", notification_body.c_str(), G_NOTIFICATION_PRIORITY_URGENT);
        }

        std::string video_filepath = get_home_dir();
        video_filepath += "/Videos/Video_" + get_date_str() + ".mp4";
        gtk_button_set_label(file_chooser_button, video_filepath.c_str());
        return true;
    }

    int fps = gtk_spin_button_get_value_as_int(fps_entry);
    int record_width = gtk_spin_button_get_value_as_int(area_width_entry);
    int record_height = gtk_spin_button_get_value_as_int(area_height_entry);

    char dir_tmp[PATH_MAX];
    strcpy(dir_tmp, filename);
    char *dir = dirname(dir_tmp);
    if(create_directory_recursive(dir) != 0) {
        std::string notification_body = std::string("Failed to start recording. Failed to create ") + dir_tmp;
        show_notification(app, "GPU Screen Recorder", notification_body.c_str(), G_NOTIFICATION_PRIORITY_URGENT);
        return true;
    }

    bool record_window = false;
    std::string window_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_area_selection_menu));
    if(window_str == "window") {
        if(select_window_userdata.selected_window == None) {
            fprintf(stderr, "No window selected!\n");
            return true;
        }
        window_str = std::to_string(select_window_userdata.selected_window);
        record_window = true;
    } else if(window_str == "focused") {
        XWindowAttributes attr;
        Window focused_window = get_window_with_input_focus(gdk_x11_get_default_xdisplay());
        if(!focused_window || !XGetWindowAttributes(gdk_x11_get_default_xdisplay(), focused_window, &attr)) {
            show_notification(app, "GPU Screen Recorder", "No window is focused!", G_NOTIFICATION_PRIORITY_URGENT);
            return true;
        }

        window_str = std::to_string(focused_window);
        record_width = attr.width;
        record_height = attr.height;
        record_window = true;
    }
    std::string fps_str = std::to_string(fps);

    const gchar* audio_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_input_menu));
    const gchar* quality_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", record_width, record_height);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", window_str.c_str(), "-c", "mp4", "-q", quality_input_str, "-f", fps_str.c_str(), "-o", filename
    };

    if(audio_input_str && strcmp(audio_input_str, "None") != 0)
        args.insert(args.end(), { "-a", audio_input_str });

    if(record_window)
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
    return true;
}

static gboolean on_start_streaming_button_click(GtkButton *button, gpointer userdata) {
    GtkApplication *app = (GtkApplication*)userdata;

    if(streaming) {
        bool exit_success = kill_gpu_screen_recorder_get_result();

        gtk_button_set_label(button, "Start streaming");
        streaming = false;
        gpu_screen_recorder_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(stream_back_button), true);

        if(exit_success) {
            show_notification(app, "GPU Screen Recorder", "Stopped streaming", G_NOTIFICATION_PRIORITY_NORMAL);
        } else {
            show_notification(app, "GPU Screen Recorder", "The streaming failed with an error", G_NOTIFICATION_PRIORITY_URGENT);
        }

        return true;
    }

    const char *stream_id_str = gtk_entry_get_text(stream_id_entry);
    int fps = gtk_spin_button_get_value_as_int(fps_entry);
    int record_width = gtk_spin_button_get_value_as_int(area_width_entry);
    int record_height = gtk_spin_button_get_value_as_int(area_height_entry);

    bool record_window = false;
    std::string window_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_area_selection_menu));
    if(window_str == "window") {
        if(select_window_userdata.selected_window == None) {
            fprintf(stderr, "No window selected!\n");
            return true;
        }
        window_str = std::to_string(select_window_userdata.selected_window);
        record_window = true;
    } else if(window_str == "focused") {
        XWindowAttributes attr;
        Window focused_window = get_window_with_input_focus(gdk_x11_get_default_xdisplay());
        if(!focused_window || !XGetWindowAttributes(gdk_x11_get_default_xdisplay(), focused_window, &attr)) {
            show_notification(app, "GPU Screen Recorder", "No window is focused!", G_NOTIFICATION_PRIORITY_URGENT);
            return true;
        }

        window_str = std::to_string(focused_window);
        record_width = attr.width;
        record_height = attr.height;
        record_window = true;
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
    }

    const gchar* audio_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_input_menu));
    const gchar* quality_input_str = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", record_width, record_height);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", window_str.c_str(), "-c", "flv", "-q", quality_input_str, "-f", fps_str.c_str(), "-o", stream_url.c_str()
    };

    if(audio_input_str && strcmp(audio_input_str, "None") != 0)
        args.insert(args.end(), { "-a", audio_input_str });

    if(record_window)
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
    return true;
}

static void gtk_widget_set_margin(GtkWidget *widget, int top, int bottom, int left, int right) {
    gtk_widget_set_margin_top(widget, top);
    gtk_widget_set_margin_bottom(widget, bottom);
    gtk_widget_set_margin_start(widget, left);
    gtk_widget_set_margin_end(widget, right);
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

struct AudioInput {
    std::string name;
    std::string description;
};

static void pa_sourcelist_cb(pa_context *ctx, const pa_source_info *source_info, int eol, void *userdata) {
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
}

static void record_area_item_change_callback(GtkComboBox *widget, gpointer userdata) {
    GtkWidget *select_window_buttom = (GtkWidget*)userdata;
    const gchar *selected_window_area = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_area_selection_menu));
    gtk_widget_set_visible(select_window_buttom, strcmp(selected_window_area, "window") == 0);
    gtk_widget_set_visible(GTK_WIDGET(area_size_label), strcmp(selected_window_area, "window") == 0);
    gtk_widget_set_visible(GTK_WIDGET(area_size_grid), strcmp(selected_window_area, "window") == 0);
    enable_stream_record_button_if_info_filled();

    if(strcmp(selected_window_area, "window") == 0) {
        XWindowAttributes attr;
        if(select_window_userdata.selected_window && XGetWindowAttributes(gdk_x11_get_default_xdisplay(), select_window_userdata.selected_window, &attr)) {
            gtk_spin_button_set_value(area_width_entry, attr.width);
            gtk_spin_button_set_value(area_height_entry, attr.height);
        }
    } else if(strcmp(selected_window_area, "focused") == 0) {
        //
    } else if(strcmp(selected_window_area, "screen") == 0 || strcmp(selected_window_area, "screen-direct") == 0) {
        int screen = DefaultScreen(gdk_x11_get_default_xdisplay());
        gtk_spin_button_set_value(area_width_entry, DisplayWidth(gdk_x11_get_default_xdisplay(), screen));
        gtk_spin_button_set_value(area_height_entry, DisplayHeight(gdk_x11_get_default_xdisplay(), screen));
    } else {
        int monitor_name_size = strlen(selected_window_area);
        for_each_active_monitor_output(gdk_x11_get_default_xdisplay(), [selected_window_area, monitor_name_size](const XRROutputInfo *output_info, const XRRCrtcInfo *crtc_info, const XRRModeInfo*) {
            if(monitor_name_size == output_info->nameLen && strncmp(selected_window_area, output_info->name, output_info->nameLen) == 0) {
                gtk_spin_button_set_value(area_width_entry, crtc_info->width);
                gtk_spin_button_set_value(area_height_entry, crtc_info->height);
            }
        });
    }
}

static void audio_input_change_callback(GtkComboBox *widget, gpointer userdata) {
    enable_stream_record_button_if_info_filled();
}

static bool is_nv_fbc_installed() {
    void *lib = dlopen("libnvidia-fbc.so.1", RTLD_NOW);
    if(lib)
        dlclose(lib);
    return lib != nullptr;
}

static GtkWidget* create_common_settings_page(GtkStack *stack, GtkApplication *app) {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "common-settings");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    int grid_row = 0;
    int record_area_row = 0;

    GtkFrame *record_area_frame = GTK_FRAME(gtk_frame_new("Record area"));
    gtk_grid_attach(grid, GTK_WIDGET(record_area_frame), 0, grid_row++, 2, 1);

    GtkGrid *record_area_grid = GTK_GRID(gtk_grid_new());
    gtk_widget_set_vexpand(GTK_WIDGET(record_area_grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(record_area_grid), true);
    gtk_grid_set_row_spacing(record_area_grid, 10);
    gtk_grid_set_column_spacing(record_area_grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(record_area_grid), 10, 10, 10, 10);
    gtk_container_add(GTK_CONTAINER(record_area_frame), GTK_WIDGET(record_area_grid));

    record_area_selection_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(record_area_selection_menu, "window", "Window");
    gtk_combo_box_text_append(record_area_selection_menu, "focused", "Focused window");
    if(is_nv_fbc_installed()) {
        gtk_combo_box_text_append(record_area_selection_menu, "screen", "All monitors (HEVC, NvFBC)");
        gtk_combo_box_text_append(record_area_selection_menu, "screen-direct", "All monitors, direct mode (HEVC, NvFBC, VRR workaround)");
        for_each_active_monitor_output(gdk_x11_get_default_xdisplay(), [](const XRROutputInfo *output_info, const XRRCrtcInfo*, const XRRModeInfo *mode_info) {
            std::string label = "Monitor ";
            label.append(output_info->name, output_info->nameLen);
            label += " (";
            label.append(mode_info->name, mode_info->nameLength);
            label += ", NvFBC)";

            // Leak on purpose, what are you gonna do? stab me?
            char *id = (char*)malloc(output_info->nameLen + 1);
            if(!id) {
                fprintf(stderr, "Failed to allocate memory\n");
                abort();
            }
            memcpy(id, output_info->name, output_info->nameLen);
            id[output_info->nameLen] = '\0';

            gtk_combo_box_text_append(record_area_selection_menu, id, label.c_str());
        });
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(record_area_selection_menu), 0);
    gtk_widget_set_hexpand(GTK_WIDGET(record_area_selection_menu), true);
    gtk_grid_attach(record_area_grid, GTK_WIDGET(record_area_selection_menu), 0, record_area_row++, 3, 1);

    GtkButton *select_window_button = GTK_BUTTON(gtk_button_new_with_label("Select window..."));
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

    GtkGrid *fps_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(fps_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(fps_grid, gtk_label_new("Frame rate: "), 0, 0, 1, 1);
    fps_entry = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1.0, 5000.0, 1.0));
    gtk_spin_button_set_value(fps_entry, 60.0);
    gtk_widget_set_hexpand(GTK_WIDGET(fps_entry), true);
    gtk_grid_attach(fps_grid, GTK_WIDGET(fps_entry), 1, 0, 1, 1);

    GtkGrid *audio_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(audio_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(audio_grid, gtk_label_new("Audio input: "), 0, 0, 1, 1);
    audio_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(audio_input_menu, "None", "None");
    for(const AudioInput &audio_input : get_pulseaudio_inputs()) {
        gtk_combo_box_text_append(audio_input_menu, audio_input.name.c_str(), audio_input.description.c_str());
    }
    g_signal_connect(audio_input_menu, "changed", G_CALLBACK(audio_input_change_callback), nullptr);
    gtk_widget_set_hexpand(GTK_WIDGET(audio_input_menu), true);
    gtk_grid_attach(audio_grid, GTK_WIDGET(audio_input_menu), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(audio_input_menu), 0);

    GtkGrid *quality_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(quality_grid), 0, grid_row++, 2, 1);
    gtk_grid_attach(quality_grid, gtk_label_new("Quality: "), 0, 0, 1, 1);
    quality_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(quality_input_menu, "medium", "High");
    gtk_combo_box_text_append(quality_input_menu, "high", "Ultra");
    gtk_combo_box_text_append(quality_input_menu, "ultra", "Placebo");
    gtk_widget_set_hexpand(GTK_WIDGET(quality_input_menu), true);
    gtk_grid_attach(quality_grid, GTK_WIDGET(quality_input_menu), 1, 0, 1, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(quality_input_menu), 0);

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
    std::string video_filepath = get_home_dir();
    video_filepath += "/Videos";

    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "replay");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkWidget *hotkey_label = gtk_label_new("Press Shift+Alt+F1 to start/stop the replay and Alt+F1 to save");
    gtk_grid_attach(grid, hotkey_label, 0, 0, 3, 1);
    gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 1, 3, 1);

    GtkGrid *file_chooser_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(file_chooser_grid), 0, 2, 3, 1);
    gtk_grid_set_column_spacing(file_chooser_grid, 10);
    GtkWidget *file_chooser_label = gtk_label_new("Where do you want to save the replays?");
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(file_chooser_label), 0, 0, 1, 1);
    replay_file_chooser_button = GTK_BUTTON(gtk_button_new_with_label(video_filepath.c_str()));
    gtk_button_set_image(replay_file_chooser_button, save_icon);
    gtk_button_set_always_show_image(replay_file_chooser_button, true);
    gtk_button_set_image_position(replay_file_chooser_button, GTK_POS_RIGHT);
    gtk_widget_set_hexpand(GTK_WIDGET(replay_file_chooser_button), true);
    g_signal_connect(replay_file_chooser_button, "clicked", G_CALLBACK(on_replay_file_chooser_button_click), nullptr);
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(replay_file_chooser_button), 1, 0, 1, 1);

    GtkGrid *replay_time_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(replay_time_grid), 0, 3, 3, 1);
    gtk_grid_attach(replay_time_grid, gtk_label_new("Replay time: "), 0, 0, 1, 1);
    replay_time_entry = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(5.0, 1200.0, 1.0));
    gtk_spin_button_set_value(replay_time_entry, 30.0);
    gtk_widget_set_hexpand(GTK_WIDGET(replay_time_entry), true);
    gtk_grid_attach(replay_time_grid, GTK_WIDGET(replay_time_entry), 1, 0, 1, 1);


    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());

    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, 4, 3, 1);
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

    return GTK_WIDGET(grid);
}

static GtkWidget* create_recording_page(GtkApplication *app, GtkStack *stack) {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "recording");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkWidget *hotkey_label = gtk_label_new("Press Alt+F1 to start/stop recording");
    gtk_grid_attach(grid, hotkey_label, 0, 0, 2, 1);
    gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 1, 2, 1);

    GtkGrid *file_chooser_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(file_chooser_grid), 0, 2, 2, 1);
    gtk_grid_set_column_spacing(file_chooser_grid, 10);
    GtkWidget *file_chooser_label = gtk_label_new("Where do you want to save the video?");
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(file_chooser_label), 0, 0, 1, 1);
    file_chooser_button = GTK_BUTTON(gtk_button_new_with_label(""));
    gtk_button_set_image(file_chooser_button, save_icon);
    gtk_button_set_always_show_image(file_chooser_button, true);
    gtk_button_set_image_position(file_chooser_button, GTK_POS_RIGHT);
    gtk_widget_set_hexpand(GTK_WIDGET(file_chooser_button), true);
    g_signal_connect(file_chooser_button, "clicked", G_CALLBACK(on_file_chooser_button_click), nullptr);
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(file_chooser_button), 1, 0, 1, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, 3, 2, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);
    record_back_button = GTK_BUTTON(gtk_button_new_with_label("Back"));
    gtk_widget_set_hexpand(GTK_WIDGET(record_back_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(record_back_button), 0, 0, 1, 1);
    start_recording_button = GTK_BUTTON(gtk_button_new_with_label("Start recording"));
    gtk_widget_set_hexpand(GTK_WIDGET(start_recording_button), true);
    g_signal_connect(start_recording_button, "clicked", G_CALLBACK(on_start_recording_button_click), app);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(start_recording_button), 1, 0, 1, 1);

    return GTK_WIDGET(grid);
}

static void stream_service_change_callback(GtkComboBox *widget, gpointer userdata) {
    enable_stream_record_button_if_info_filled();
}

static GtkWidget* create_streaming_page(GtkApplication *app, GtkStack *stack) {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "streaming");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkWidget *hotkey_label = gtk_label_new("Press Alt+F1 to start/stop streaming");
    gtk_grid_attach(grid, hotkey_label, 0, 0, 2, 1);
    gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 1, 2, 1);

    GtkGrid *stream_service_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(stream_service_grid), 0, 2, 2, 1);
    gtk_grid_attach(stream_service_grid, gtk_label_new("Stream service: "), 0, 0, 1, 1);
    stream_service_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(stream_service_input_menu, "twitch", "Twitch");
    gtk_combo_box_text_append(stream_service_input_menu, "youtube", "Youtube");
    g_signal_connect(stream_service_input_menu, "changed", G_CALLBACK(stream_service_change_callback), nullptr);
    gtk_combo_box_set_active(GTK_COMBO_BOX(stream_service_input_menu), 0);
    gtk_widget_set_hexpand(GTK_WIDGET(stream_service_input_menu), true);
    gtk_grid_attach(stream_service_grid, GTK_WIDGET(stream_service_input_menu), 1, 0, 1, 1);

    GtkGrid *stream_id_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(stream_id_grid), 0, 3, 2, 1);
    gtk_grid_attach(stream_id_grid, gtk_label_new("Stream key: "), 0, 0, 1, 1);
    stream_id_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(stream_id_entry), true);
    gtk_grid_attach(stream_id_grid, GTK_WIDGET(stream_id_entry), 1, 0, 1, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, 4, 2, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);
    stream_back_button = GTK_BUTTON(gtk_button_new_with_label("Back"));
    gtk_widget_set_hexpand(GTK_WIDGET(stream_back_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(stream_back_button), 0, 0, 1, 1);
    start_streaming_button = GTK_BUTTON(gtk_button_new_with_label("Start streaming"));
    gtk_widget_set_hexpand(GTK_WIDGET(start_streaming_button), true);
    g_signal_connect(start_streaming_button, "clicked", G_CALLBACK(on_start_streaming_button_click), app);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(start_streaming_button), 1, 0, 1, 1);

    return GTK_WIDGET(grid);
}

static gboolean on_destroy_window(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if(gpu_screen_recorder_process != -1) {
        kill(gpu_screen_recorder_process, SIGINT);
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
            perror("waitpid failed");
            /* Ignore... */
        }
    }

    const gchar *record_filename = gtk_button_get_label(file_chooser_button);

    char dir_tmp[PATH_MAX];
    strcpy(dir_tmp, record_filename);
    char *record_dir = dirname(dir_tmp);

    config.main_config.record_area_option = gtk_combo_box_get_active_id(GTK_COMBO_BOX(record_area_selection_menu));
    config.main_config.fps = gtk_spin_button_get_value_as_int(fps_entry);
    config.main_config.audio_input = gtk_combo_box_get_active_id(GTK_COMBO_BOX(audio_input_menu));
    config.main_config.quality = gtk_combo_box_get_active_id(GTK_COMBO_BOX(quality_input_menu));

    config.streaming_config.streaming_service = gtk_combo_box_get_active_id(GTK_COMBO_BOX(stream_service_input_menu));
    config.streaming_config.stream_key = gtk_entry_get_text(stream_id_entry);

    config.record_config.save_directory = record_dir;

    config.replay_config.save_directory = gtk_button_get_label(replay_file_chooser_button);
    config.replay_config.replay_time = gtk_spin_button_get_value_as_int(replay_time_entry);

    save_config(config);

    return true;
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

static bool hotkey_pressed = false;
static GdkFilterReturn hotkey_filter_callback(GdkXEvent *xevent, GdkEvent *event, gpointer userdata) {
    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    XEvent *ev = (XEvent*)xevent;

    if((ev->type == KeyPress || ev->type == KeyRelease) && XLookupKeysym(&ev->xkey, 0) == XK_F1 && (ev->xkey.state & Mod1Mask)) {
        if(ev->type == KeyPress) {
            if(hotkey_pressed)
                return GDK_FILTER_CONTINUE;
            hotkey_pressed = true;
        } else if(ev->type == KeyRelease) {
            hotkey_pressed = false;
            return GDK_FILTER_CONTINUE;
        }

        GtkWidget *visible_page = gtk_stack_get_visible_child(page_navigation_userdata->stack);
        if(visible_page == page_navigation_userdata->recording_page) {
            keypress_toggle_recording(recording, start_recording_button, on_start_recording_button_click, page_navigation_userdata->app);
        } else if(visible_page == page_navigation_userdata->streaming_page) {
            keypress_toggle_recording(streaming, start_streaming_button, on_start_streaming_button_click, page_navigation_userdata->app);
        } else if(visible_page == page_navigation_userdata->replay_page && (ev->xkey.state & ShiftMask)) {
            keypress_toggle_recording(replaying, start_replay_button, on_start_replay_button_click, page_navigation_userdata->app);
        } else if(visible_page == page_navigation_userdata->replay_page && replaying && gpu_screen_recorder_process != -1) {
            on_replay_save_button_click(nullptr, page_navigation_userdata->app);
        }
    }

    return GDK_FILTER_CONTINUE;
}

static int xerror_dummy(Display *dpy, XErrorEvent *ee) {
    return 0;
}

static void grabkeys(Display *display) {
	unsigned int numlockmask = 0;
    KeyCode numlock_keycode = XKeysymToKeycode(display, XK_Num_Lock);
    XModifierKeymap *modmap = XGetModifierMapping(display);
    for(int i = 0; i < 8; ++i) {
        for(int j = 0; j < modmap->max_keypermod; ++j) {
            if(modmap->modifiermap[i * modmap->max_keypermod + j] == numlock_keycode)
                numlockmask = (1 << i); 
        }
    }
	XFreeModifiermap(modmap);

    XErrorHandler prev_error_handler = XSetErrorHandler(xerror_dummy);
    
	Window root_window = DefaultRootWindow(display);
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    for(int i = 0; i < 4; ++i) {
        XGrabKey(display, XKeysymToKeycode(display, XK_F1), Mod1Mask|modifiers[i], root_window, False, GrabModeAsync, GrabModeAsync);
        XGrabKey(display, XKeysymToKeycode(display, XK_F1), Mod1Mask|ShiftMask|modifiers[i], root_window, False, GrabModeAsync, GrabModeAsync);
    }
    
    XSync(display, False);
    XSetErrorHandler(prev_error_handler);
}

static gboolean handle_child_process_death(gpointer userdata) {
    if(gpu_screen_recorder_process != -1) {
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, WNOHANG) != 0) {
            if(replaying) {
                on_start_replay_button_click(start_replay_button, userdata);
            } else if(recording) {
                on_start_recording_button_click(start_recording_button, userdata);
            } else if(streaming) {
                on_start_streaming_button_click(start_streaming_button, userdata);
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

static void load_config() {
    config = read_config();

    if(strcmp(config.main_config.record_area_option.c_str(), "window") == 0) {
        //
    } else if(strcmp(config.main_config.record_area_option.c_str(), "focused") == 0) {
        //
    } else if(strcmp(config.main_config.record_area_option.c_str(), "screen") == 0 || strcmp(config.main_config.record_area_option.c_str(), "screen-direct") == 0) {
        //
    } else {
        bool found_monitor = false;
        int monitor_name_size = strlen(config.main_config.record_area_option.c_str());
        for_each_active_monitor_output(gdk_x11_get_default_xdisplay(), [&](const XRROutputInfo *output_info, const XRRCrtcInfo *crtc_info, const XRRModeInfo*) {
            if(monitor_name_size == output_info->nameLen && strncmp(config.main_config.record_area_option.c_str(), output_info->name, output_info->nameLen) == 0) {
                found_monitor = true;
            }
        });

        if(!found_monitor)
            config.main_config.record_area_option = "window";
    }

    if(config.main_config.fps < 1)
        config.main_config.fps = 1;
    else if(config.main_config.fps > 5000)
        config.main_config.fps = 5000;

    if(config.main_config.audio_input != "None") {
        bool found_audio_input = false;
        for(const AudioInput &audio_input : get_pulseaudio_inputs()) {
            if(config.main_config.audio_input == audio_input.name) {
                found_audio_input = true;
                break;
            }
        }

        if(!found_audio_input)
            config.main_config.audio_input = "None";
    }

    if(config.main_config.quality != "medium" && config.main_config.quality != "high" && config.main_config.quality != "ultra")
        config.main_config.quality = "medium";

    if(config.streaming_config.streaming_service != "twitch" && config.streaming_config.streaming_service != "youtube")
        config.streaming_config.streaming_service = "twitch";

    if(config.record_config.save_directory.empty() || !is_directory(config.record_config.save_directory.c_str()))
        config.record_config.save_directory = get_home_dir() + "/Videos";

    if(config.replay_config.save_directory.empty() || !is_directory(config.replay_config.save_directory.c_str()))
        config.replay_config.save_directory = get_home_dir() + "/Videos";

    if(config.replay_config.replay_time < 5)
        config.replay_config.replay_time = 5;
    else if(config.replay_config.replay_time> 1200)
        config.replay_config.replay_time = 1200;

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(record_area_selection_menu), config.main_config.record_area_option.c_str());
    gtk_spin_button_set_value(fps_entry, config.main_config.fps);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(audio_input_menu), config.main_config.audio_input.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(quality_input_menu), config.main_config.quality.c_str());

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(stream_service_input_menu), config.streaming_config.streaming_service.c_str());
    gtk_entry_set_text(stream_id_entry, config.streaming_config.stream_key.c_str());

    std::string video_filepath = config.record_config.save_directory;
    video_filepath += "/Video_" + get_date_str() + ".mp4";
    gtk_button_set_label(file_chooser_button, video_filepath.c_str());

    gtk_button_set_label(replay_file_chooser_button, config.replay_config.save_directory.c_str());
    gtk_spin_button_set_value(replay_time_entry, config.replay_config.replay_time);
}

static void activate(GtkApplication *app, gpointer userdata) {
    GtkWidget *window = gtk_application_window_new(app);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy_window), nullptr);
    gtk_window_set_title(GTK_WINDOW(window), "GPU Screen Recorder");
    gtk_window_set_resizable(GTK_WINDOW(window), false);

    select_window_userdata.app = app;

    crosshair_cursor = XCreateFontCursor(gdk_x11_get_default_xdisplay(), XC_crosshair);
    save_icon = gtk_image_new_from_icon_name("gtk-save", GTK_ICON_SIZE_BUTTON);

    GtkStack *stack = GTK_STACK(gtk_stack_new());
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(stack));
    gtk_stack_set_transition_type(stack, GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_set_transition_duration(stack, 0);
    gtk_stack_set_homogeneous(stack, false);
    GtkWidget *common_settings_page = create_common_settings_page(stack, app);
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
    g_signal_connect(replay_back_button, "clicked", G_CALLBACK(on_streaming_recording_page_back_click), &page_navigation_userdata);

    g_signal_connect(record_button, "clicked", G_CALLBACK(on_start_recording_click), &page_navigation_userdata);
    g_signal_connect(record_back_button, "clicked", G_CALLBACK(on_streaming_recording_page_back_click), &page_navigation_userdata);

    g_signal_connect(stream_button, "clicked", G_CALLBACK(on_start_streaming_click), &page_navigation_userdata);
    g_signal_connect(stream_back_button, "clicked", G_CALLBACK(on_streaming_recording_page_back_click), &page_navigation_userdata);

    Display *display = gdk_x11_get_default_xdisplay();
    grabkeys(display);
    GdkWindow *root_window = gdk_get_default_root_window();
    //gdk_window_set_events(root_window, GDK_BUTTON_PRESS_MASK);
    gdk_window_add_filter(root_window, hotkey_filter_callback, &page_navigation_userdata);

    g_timeout_add(1000, handle_child_process_death, app);

    load_config();
    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C");
    GtkApplication *app = gtk_application_new("org.dec05eba.gpu-screen-recorder", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
