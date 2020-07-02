#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <assert.h>
#include <string>
#include <pulse/pulseaudio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>

typedef struct {
    Display *display;
    GtkButton *select_window_button;
    Window selected_window;
} SelectWindowUserdata;

typedef struct {
    GtkStack *stack;
    GtkWidget *common_settings_page;
    GtkWidget *recording_page;
    GtkWidget *streaming_page;
} PageNavigationUserdata;

static SelectWindowUserdata select_window_userdata;
static PageNavigationUserdata page_navigation_userdata;
static GtkWidget *save_icon;
static Cursor crosshair_cursor;
static GtkSpinButton *fps_entry;
static GtkComboBoxText *audio_input_menu;
static GtkComboBoxText *stream_service_input_menu;
static int num_audio_input = 0;
static GtkButton *file_chooser_button;
static GtkButton *stream_button;
static GtkButton *record_button;
static GtkButton *record_back_button;
static GtkButton *stream_back_button;
static GtkEntry *stream_id_entry;
static bool recording = false;
static bool streaming = false;
static pid_t gpu_screen_recorder_process = -1;
static pid_t ffmpeg_process = -1;

static void enable_stream_record_button_if_info_filled() {
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(audio_input_menu)) == -1)
        return;

    if(select_window_userdata.selected_window == None)
        return;
    
    gtk_widget_set_sensitive(GTK_WIDGET(record_button), true);
    gtk_widget_set_sensitive(GTK_WIDGET(stream_button), true);
}

/* TODO: Look at xwininfo source to figure out how to make this work for different types of window managers */
static GdkFilterReturn filter_callback(GdkXEvent *xevent, GdkEvent *event, gpointer userdata) {
    SelectWindowUserdata *select_window_userdata = (SelectWindowUserdata*)userdata;
    XEvent *ev = (XEvent*)xevent;
    assert(ev->type == ButtonPress);

    Window target_win = ev->xbutton.subwindow;

    if(target_win == None) {
        fprintf(stderr, "Selected the root window! (no window selected)\n");
        return GDK_FILTER_CONTINUE;
    }

    int status = XUngrabPointer(select_window_userdata->display, CurrentTime);
    if(!status) {
        fprintf(stderr, "failed to ungrab pointer!\n");
        exit(1);
    }

    std::string window_name = "Window ID: " + std::to_string(target_win) + " | Name: ";
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

    fprintf(stderr, "window name: %s\n", window_name.c_str());
    gtk_button_set_label(select_window_userdata->select_window_button, window_name.c_str());
    select_window_userdata->selected_window = target_win;

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

static gboolean on_start_recording_click(GtkButton *button, gpointer userdata) {
    PageNavigationUserdata *page_navigation_userdata = (PageNavigationUserdata*)userdata;
    gtk_stack_set_visible_child(page_navigation_userdata->stack, page_navigation_userdata->recording_page);
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

static gboolean on_file_chooser_button_click(GtkButton *button, gpointer userdata) {
    GtkWidget *file_chooser_dialog = gtk_file_chooser_dialog_new("Where do you want to save the video?", nullptr, GTK_FILE_CHOOSER_ACTION_SAVE,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        nullptr);
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

/* TODO: XSetErrorHandler to prevent program crash, show notifications or inline error message instead of fprintf(stderr), more validations... */

static gboolean on_start_recording_button_click(GtkButton *button, gpointer userdata) {
    if(recording) {
        if(gpu_screen_recorder_process != -1) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
        }
        gtk_button_set_label(button, "Start recording");
        recording = false;
        gpu_screen_recorder_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(record_back_button), true);
        return true;
    }

    const gchar *filename = gtk_button_get_label(file_chooser_button);
    int fps = gtk_spin_button_get_value_as_int(fps_entry);

    gchar* audio_input_str = gtk_combo_box_text_get_active_text(audio_input_menu);
    if(!audio_input_str) {
        fprintf(stderr, "No audio input selected!\n");
    }

    if(select_window_userdata.selected_window == None) {
        fprintf(stderr, "No window selected!\n");
        return true;
    }

    recording = true;
    gtk_widget_set_sensitive(GTK_WIDGET(record_back_button), false);

    std::string window_str = std::to_string(select_window_userdata.selected_window);
    std::string fps_str = std::to_string(fps);

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if(pid == -1) {
        perror("failed to fork");
        exit(3);
    } else if(pid == 0) { /* child process */
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("prctl(PR_SET_PDEATHSIG, SIGTERM) failed");
            exit(3);
        }

        if(getppid() != parent_pid)
            exit(3);

        int output_file = creat(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if(output_file == -1) {
            perror(filename);
            exit(3);
        }
        // Redirect stdout to output_file
        dup2(output_file, STDOUT_FILENO);
        
        const char *args[] = { "gpu-screen-recorder", "-w", window_str.c_str(), "-c", "mp4", "-f", fps_str.c_str(), "-a", audio_input_str, NULL };
        execvp(args[0], (char* const*)args);
        perror("failed to launch gpu-screen-recorder");
    } else { /* parent process */
        gpu_screen_recorder_process = pid;
        gtk_button_set_label(button, "Stop recording");
    }

    g_free(audio_input_str);
    return true;
}

#define PIPE_READ_END 0
#define PIPE_WRITE_END 1

static pid_t launch_ffmpeg_rtmp_process(const char *url, int *pipe_write_end) {
    int pipes[2];
    if(pipe(pipes) == -1) {
        perror("failed to create pipe");
        exit(3);
    }

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if(pid == -1) {
        perror("failed to fork");
        exit(3);
    } else if(pid == 0) { /* child process */
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("prctl(PR_SET_PDEATHSIG, SIGTERM) failed");
            exit(3);
        }

        if(getppid() != parent_pid)
            exit(3);

        dup2(pipes[PIPE_READ_END], STDIN_FILENO);
        close(pipes[PIPE_WRITE_END]);
        
        const char *args[] = { "ffmpeg", "-i", "pipe:0", "-c:v", "copy", "-f", "flv", "-max_muxing_queue_size", "4096", "--", url, NULL };
        execvp(args[0], (char* const*)args);
        perror("failed to launch ffmpeg");
        exit(127);
    } else { /* parent process */
        *pipe_write_end = pipes[PIPE_WRITE_END];
        close(pipes[PIPE_READ_END]);
    }

    return pid;
}

static gboolean on_start_streaming_button_click(GtkButton *button, gpointer userdata) {
    if(streaming) {
        if(gpu_screen_recorder_process != -1) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                if(ffmpeg_process != -1)
                    kill(ffmpeg_process, SIGKILL);
                /* Ignore... */
            } else {
                if(ffmpeg_process != -1)
                    waitpid(ffmpeg_process, &status, 0);
            }
        } else {
            if(ffmpeg_process != -1)
                kill(ffmpeg_process, SIGKILL);
        }
        gtk_button_set_label(button, "Start streaming");
        streaming = false;
        gpu_screen_recorder_process = -1;
        ffmpeg_process = -1;
        gtk_widget_set_sensitive(GTK_WIDGET(stream_back_button), true);
        return true;
    }

    const char *stream_id_str = gtk_entry_get_text(stream_id_entry);
    int fps = gtk_spin_button_get_value_as_int(fps_entry);

    gchar* audio_input_str = gtk_combo_box_text_get_active_text(audio_input_menu);
    if(!audio_input_str) {
        fprintf(stderr, "No audio input selected!\n");
    }

    if(select_window_userdata.selected_window == None) {
        fprintf(stderr, "No window selected!\n");
        return true;
    }

    streaming = true;
    gtk_widget_set_sensitive(GTK_WIDGET(stream_back_button), false);

    std::string window_str = std::to_string(select_window_userdata.selected_window);
    std::string fps_str = std::to_string(fps);

    int pipe_write_end;
    std::string stream_url = "rtmp://live.twitch.tv/app/";
    stream_url += stream_id_str;
    ffmpeg_process = launch_ffmpeg_rtmp_process(stream_url.c_str(), &pipe_write_end);

    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if(pid == -1) {
        perror("failed to fork");
        exit(3);
    } else if(pid == 0) { /* child process */
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("prctl(PR_SET_PDEATHSIG, SIGTERM) failed");
            exit(3);
        }

        if(getppid() != parent_pid)
            exit(3);

        // Redirect stdout to output_file
        dup2(pipe_write_end, STDOUT_FILENO);
        
        const char *args[] = { "gpu-screen-recorder", "-w", window_str.c_str(), "-c", "flv", "-f", fps_str.c_str(), "-a", audio_input_str, NULL };
        execvp(args[0], (char* const*)args);
        perror("failed to launch gpu-screen-recorder");
        exit(127);
    } else { /* parent process */
        gpu_screen_recorder_process = pid;
        close(pipe_write_end);
        gtk_button_set_label(button, "Stop streaming");
    }

    g_free(audio_input_str);
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

static void pa_sourcelist_cb(pa_context *ctx, const pa_source_info *source_info, int eol, void *userdata) {
    if(eol > 0)
        return;

    gtk_combo_box_text_append_text(audio_input_menu, source_info->name);
    ++num_audio_input;
}

static void populate_audio_input_menu_with_pulseaudio_monitors() {
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
                pa_op = pa_context_get_source_info_list(ctx, pa_sourcelist_cb, nullptr);
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
            return;
        }

        pa_mainloop_iterate(main_loop, 1, NULL);
    }

    pa_mainloop_free(main_loop);
}

static void audio_input_change_callback(GtkComboBox *widget, gpointer userdata) {
    enable_stream_record_button_if_info_filled();
}

static GtkWidget* create_common_settings_page(GtkStack *stack, GtkApplication *app) {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "common-settings");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkGrid *select_window_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(select_window_grid), 0, 0, 2, 1);
    gtk_grid_attach(select_window_grid, gtk_label_new("Window: "), 0, 0, 1, 1);
    GtkButton *select_window_button = GTK_BUTTON(gtk_button_new_with_label("Select window..."));
    gtk_widget_set_hexpand(GTK_WIDGET(select_window_button), true);
    g_signal_connect(select_window_button, "clicked", G_CALLBACK(on_select_window_button_click), app);
    gtk_grid_attach(select_window_grid, GTK_WIDGET(select_window_button), 1, 0, 1, 1);

    GtkGrid *fps_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(fps_grid), 0, 1, 2, 1);
    gtk_grid_attach(fps_grid, gtk_label_new("Frame rate: "), 0, 0, 1, 1);
    fps_entry = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(10.0, 250.0, 1.0));
    gtk_spin_button_set_value(fps_entry, 60.0);
    gtk_widget_set_hexpand(GTK_WIDGET(fps_entry), true);
    gtk_grid_attach(fps_grid, GTK_WIDGET(fps_entry), 1, 0, 1, 1);

    GtkGrid *audio_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(audio_grid), 0, 2, 2, 1);
    gtk_grid_attach(audio_grid, gtk_label_new("Audio input: "), 0, 0, 1, 1);
    audio_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    populate_audio_input_menu_with_pulseaudio_monitors();
    g_signal_connect(audio_input_menu, "changed", G_CALLBACK(audio_input_change_callback), nullptr);
    gtk_widget_set_hexpand(GTK_WIDGET(audio_input_menu), true);
    gtk_grid_attach(audio_grid, GTK_WIDGET(audio_input_menu), 1, 0, 1, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, 3, 2, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);

    stream_button = GTK_BUTTON(gtk_button_new_with_label("Stream"));
    gtk_widget_set_hexpand(GTK_WIDGET(stream_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(stream_button), 0, 0, 1, 1);

    record_button = GTK_BUTTON(gtk_button_new_with_label("Record"));
    gtk_widget_set_hexpand(GTK_WIDGET(record_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(record_button), 1, 0, 1, 1);

    gtk_widget_set_sensitive(GTK_WIDGET(record_button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(stream_button), false);

    if(num_audio_input != 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(audio_input_menu), 0);
    }

    return GTK_WIDGET(grid);
}

static GtkWidget* create_recording_page(GtkStack *stack) {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "recording");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkGrid *file_chooser_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(file_chooser_grid), 0, 0, 2, 1);
    gtk_grid_set_column_spacing(file_chooser_grid, 10);
    GtkWidget *file_chooser_label = gtk_label_new("Where do you want to save the video?");
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(file_chooser_label), 0, 0, 1, 1);
    file_chooser_button = GTK_BUTTON(gtk_button_new_with_label("video.mp4"));
    gtk_button_set_image(file_chooser_button, save_icon);
    gtk_button_set_always_show_image(file_chooser_button, true);
    gtk_button_set_image_position(file_chooser_button, GTK_POS_RIGHT);
    gtk_widget_set_hexpand(GTK_WIDGET(file_chooser_button), true);
    g_signal_connect(file_chooser_button, "clicked", G_CALLBACK(on_file_chooser_button_click), nullptr);
    gtk_grid_attach(file_chooser_grid, GTK_WIDGET(file_chooser_button), 1, 0, 1, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, 1, 2, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);
    record_back_button = GTK_BUTTON(gtk_button_new_with_label("Back"));
    gtk_widget_set_hexpand(GTK_WIDGET(record_back_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(record_back_button), 0, 0, 1, 1);
    GtkButton *start_recording_button = GTK_BUTTON(gtk_button_new_with_label("Start recording"));
    gtk_widget_set_hexpand(GTK_WIDGET(start_recording_button), true);
    g_signal_connect(start_recording_button, "clicked", G_CALLBACK(on_start_recording_button_click), nullptr);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(start_recording_button), 1, 0, 1, 1);

    return GTK_WIDGET(grid);
}

static void stream_service_change_callback(GtkComboBox *widget, gpointer userdata) {
    enable_stream_record_button_if_info_filled();
}

static GtkWidget* create_streaming_page(GtkStack *stack) {
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_stack_add_named(stack, GTK_WIDGET(grid), "streaming");
    gtk_widget_set_vexpand(GTK_WIDGET(grid), true);
    gtk_widget_set_hexpand(GTK_WIDGET(grid), true);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 10);
    gtk_widget_set_margin(GTK_WIDGET(grid), 10, 10, 10, 10);

    GtkGrid *stream_service_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(stream_service_grid), 0, 0, 2, 1);
    gtk_grid_attach(stream_service_grid, gtk_label_new("Stream service: "), 0, 0, 1, 1);
    stream_service_input_menu = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(stream_service_input_menu, "Twitch");
    //gtk_combo_box_text_append_text(stream_service_input_menu, "Custom");
    g_signal_connect(stream_service_input_menu, "changed", G_CALLBACK(stream_service_change_callback), nullptr);
    gtk_combo_box_set_active(GTK_COMBO_BOX(stream_service_input_menu), 0);
    gtk_widget_set_hexpand(GTK_WIDGET(stream_service_input_menu), true);
    gtk_grid_attach(stream_service_grid, GTK_WIDGET(stream_service_input_menu), 1, 0, 1, 1);

    GtkGrid *stream_id_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(stream_id_grid), 0, 1, 2, 1);
    gtk_grid_attach(stream_id_grid, gtk_label_new("Stream id: "), 0, 0, 1, 1);
    stream_id_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(stream_id_entry), true);
    gtk_grid_attach(stream_id_grid, GTK_WIDGET(stream_id_entry), 1, 0, 1, 1);

    GtkGrid *start_button_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(grid, GTK_WIDGET(start_button_grid), 0, 2, 2, 1);
    gtk_grid_set_column_spacing(start_button_grid, 10);
    stream_back_button = GTK_BUTTON(gtk_button_new_with_label("Back"));
    gtk_widget_set_hexpand(GTK_WIDGET(stream_back_button), true);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(stream_back_button), 0, 0, 1, 1);
    GtkButton *start_streaming_button = GTK_BUTTON(gtk_button_new_with_label("Start streaming"));
    gtk_widget_set_hexpand(GTK_WIDGET(start_streaming_button), true);
    g_signal_connect(start_streaming_button, "clicked", G_CALLBACK(on_start_streaming_button_click), nullptr);
    gtk_grid_attach(start_button_grid, GTK_WIDGET(start_streaming_button), 1, 0, 1, 1);

    return GTK_WIDGET(grid);
}

static gboolean on_destroy_window(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if(gpu_screen_recorder_process != -1) {
        kill(gpu_screen_recorder_process, SIGINT);
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
            perror("waitpid failed");
            if(ffmpeg_process != -1)
                kill(ffmpeg_process, SIGKILL);
            /* Ignore... */
        } else {
            if(ffmpeg_process != -1)
                waitpid(ffmpeg_process, &status, 0);
        }
    } else {
        if(ffmpeg_process != -1)
            kill(ffmpeg_process, SIGKILL);
    }
    return true;
}

static void activate(GtkApplication *app, gpointer userdata) { 
    GtkWidget *window = gtk_application_window_new(app);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy_window), nullptr);
    gtk_window_set_title(GTK_WINDOW(window), "Gpu screen recorder");
    gtk_window_set_resizable(GTK_WINDOW(window), false);

    crosshair_cursor = XCreateFontCursor(gdk_x11_get_default_xdisplay(), XC_crosshair);
    save_icon = gtk_image_new_from_stock("gtk-save", GTK_ICON_SIZE_BUTTON);

    GtkStack *stack = GTK_STACK(gtk_stack_new());
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(stack));
    gtk_stack_set_transition_type(stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(stack, 300);
    gtk_stack_set_homogeneous(stack, false);
    GtkWidget *common_settings_page = create_common_settings_page(stack, app);
    GtkWidget *recording_page = create_recording_page(stack);
    GtkWidget *streaming_page = create_streaming_page(stack);
    gtk_stack_set_visible_child(stack, common_settings_page);

    page_navigation_userdata.stack = stack;
    page_navigation_userdata.common_settings_page = common_settings_page;
    page_navigation_userdata.recording_page = recording_page;
    page_navigation_userdata.streaming_page = streaming_page;
    g_signal_connect(record_button, "clicked", G_CALLBACK(on_start_recording_click), &page_navigation_userdata);
    g_signal_connect(record_back_button, "clicked", G_CALLBACK(on_streaming_recording_page_back_click), &page_navigation_userdata);

    g_signal_connect(stream_button, "clicked", G_CALLBACK(on_start_streaming_click), &page_navigation_userdata);
    g_signal_connect(stream_back_button, "clicked", G_CALLBACK(on_streaming_recording_page_back_click), &page_navigation_userdata);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("org.dec05eba.gpu-screen-recorder", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
