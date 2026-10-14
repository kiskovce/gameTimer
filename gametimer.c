// Opakujuci sa casovac: po uplynuti periody vyskoci nad ostatne okna, zapipa a blika.
#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define CSS \
    ".alert-on  { background: #d32f2f; }" \
    ".alert-on label  { color: white; }" \
    ".alert-off { background: #fff176; }" \
    ".alert-off label { color: black; }" \
    ".countdown { font-size: 24px; font-weight: bold; }"

typedef struct {
    GtkWidget *win, *root, *countdown, *h, *m, *s, *blink, *sound, *start_btn, *dismiss_btn;
    gint64 deadline;   // monotonic us, 0 = zastaveny
    gint64 alert_end;  // monotonic us, 0 = bez alarmu
    gint64 last_beep;
    gboolean blink_state;
    char *beep_path;
    char *config_path;
} App;

static int period_secs(App *a) {
    return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->h)) * 3600 +
           gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->m)) * 60 +
           gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->s));
}

static void save_settings(App *a) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "timer", "hours", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->h)));
    g_key_file_set_integer(kf, "timer", "minutes", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->m)));
    g_key_file_set_integer(kf, "timer", "seconds", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->s)));
    g_key_file_set_integer(kf, "timer", "blink_seconds", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->blink)));
    g_key_file_set_boolean(kf, "timer", "sound", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a->sound)));
    char *dir = g_path_get_dirname(a->config_path);
    g_mkdir_with_parents(dir, 0700);
    g_key_file_save_to_file(kf, a->config_path, NULL);
    g_free(dir);
    g_key_file_free(kf);
}

static int get_int(GKeyFile *kf, const char *key, int def) {
    GError *err = NULL;
    int v = g_key_file_get_integer(kf, "timer", key, &err);
    if (err) {
        g_error_free(err);
        return def;
    }
    return v;
}

static void make_beep_file(App *a) {
    const int rate = 44100, n = (int)(rate * 0.35), freq = 880;
    uint32_t data_len = n * 2, riff_len = 36 + data_len, fmt_len = 16, byte_rate = rate * 2;
    uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    a->beep_path = g_strdup_printf("%s/gametimer-beep-%d.wav", g_get_tmp_dir(), (int)getuid());
    FILE *f = fopen(a->beep_path, "wb");
    if (!f) return;
    fwrite("RIFF", 1, 4, f); fwrite(&riff_len, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data_len, 4, 1, f);
    for (int i = 0; i < n; i++) {
        double fade = fmin(1.0, fmin(i / 500.0, (n - i) / 500.0));
        int16_t s = (int16_t)(20000 * fade * sin(2 * M_PI * freq * i / rate));
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static void play_sound(App *a) {
    static const char *players[] = {"paplay", "pw-play", "aplay", NULL};
    for (int i = 0; players[i]; i++) {
        char *path = g_find_program_in_path(players[i]);
        if (!path) continue;
        char *argv_quiet[] = {path, (char *)"-q", a->beep_path, NULL};
        char *argv_plain[] = {path, a->beep_path, NULL};
        char **argv = i == 2 ? argv_quiet : argv_plain;
        g_spawn_async(NULL, argv, NULL,
                      G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
        g_free(path);
        return;
    }
    gdk_display_beep(gdk_display_get_default());
}

static void stop_alert(App *a) {
    a->alert_end = 0;
    gtk_window_set_keep_above(GTK_WINDOW(a->win), FALSE);
    gtk_widget_hide(a->dismiss_btn);
}

static void fire(App *a) {
    gint64 now = g_get_monotonic_time();
    a->deadline = now + (gint64)MAX(1, period_secs(a)) * G_USEC_PER_SEC;
    a->alert_end = now + (gint64)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->blink)) * G_USEC_PER_SEC;
    a->last_beep = 0;
    gtk_widget_show(a->dismiss_btn);
    gtk_window_deiconify(GTK_WINDOW(a->win));
    gtk_window_set_keep_above(GTK_WINDOW(a->win), TRUE);
    gtk_window_present(GTK_WINDOW(a->win));
}

static gboolean on_tick(gpointer data) {
    App *a = data;
    gint64 now = g_get_monotonic_time();
    if (a->deadline) {
        if (a->deadline <= now) {
            fire(a);
            now = g_get_monotonic_time();
        }
        int r = (int)ceil((double)(a->deadline - now) / G_USEC_PER_SEC);
        if (r < 0) r = 0;
        char buf[16];
        g_snprintf(buf, sizeof buf, "%02d:%02d:%02d", r / 3600, r % 3600 / 60, r % 60);
        gtk_label_set_text(GTK_LABEL(a->countdown), buf);
    }
    if (a->alert_end) {
        if (now >= a->alert_end) {
            stop_alert(a);
        } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a->sound)) &&
                   now - a->last_beep >= G_USEC_PER_SEC) {
            a->last_beep = now;
            play_sound(a);
        }
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_blink(gpointer data) {
    App *a = data;
    GtkStyleContext *ctx = gtk_widget_get_style_context(a->root);
    gtk_style_context_remove_class(ctx, "alert-on");
    gtk_style_context_remove_class(ctx, "alert-off");
    if (a->alert_end) {
        a->blink_state = !a->blink_state;
        gtk_style_context_add_class(ctx, a->blink_state ? "alert-on" : "alert-off");
    }
    return G_SOURCE_CONTINUE;
}

static void on_start_stop(GtkButton *btn, gpointer data) {
    App *a = data;
    (void)btn;
    if (!a->deadline) {
        if (period_secs(a) <= 0) return;
        save_settings(a);
        a->deadline = g_get_monotonic_time() + (gint64)period_secs(a) * G_USEC_PER_SEC;
        gtk_button_set_label(GTK_BUTTON(a->start_btn), "Stop");
    } else {
        a->deadline = 0;
        stop_alert(a);
        gtk_button_set_label(GTK_BUTTON(a->start_btn), "Štart");
        gtk_label_set_text(GTK_LABEL(a->countdown), "00:00:00");
    }
}

static void on_dismiss(GtkButton *btn, gpointer data) {
    (void)btn;
    stop_alert(data);
}

static void on_destroy(GtkWidget *w, gpointer data) {
    (void)w;
    save_settings(data);
    gtk_main_quit();
}

static GtkWidget *make_spin(double lo, double hi, int value) {
    GtkWidget *sp = gtk_spin_button_new_with_range(lo, hi, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp), value);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sp), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(sp), hi > 99 ? 4 : 2);
    return sp;
}

int main(int argc, char **argv) {
    // Pod Waylandom GNOME blokuje "focus stealing"; cez XWayland funguje present()/keep_above.
    if (g_getenv("DISPLAY")) g_setenv("GDK_BACKEND", "x11", FALSE);
    gtk_init(&argc, &argv);

    App *a = g_new0(App, 1);
    a->config_path = g_build_filename(g_get_user_config_dir(), "gametimer", "settings.ini", NULL);
    make_beep_file(a);

    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, a->config_path, G_KEY_FILE_NONE, NULL);
    GError *err = NULL;
    gboolean sound = g_key_file_get_boolean(kf, "timer", "sound", &err);
    if (err) {
        sound = TRUE;
        g_error_free(err);
    }

    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(prov),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    a->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(a->win), "Game Timer");
    gtk_window_set_default_size(GTK_WINDOW(a->win), 180, 160);

    a->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(a->root), 8);
    gtk_container_add(GTK_CONTAINER(a->win), a->root);

    a->countdown = gtk_label_new("00:00:00");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->countdown), "countdown");
    gtk_box_pack_start(GTK_BOX(a->root), a->countdown, FALSE, FALSE, 4);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    a->h = make_spin(0, 99, get_int(kf, "hours", 0));
    a->m = make_spin(0, 59, get_int(kf, "minutes", 25));
    a->s = make_spin(0, 59, get_int(kf, "seconds", 0));
    a->blink = make_spin(1, 3600, get_int(kf, "blink_seconds", 10));
    g_key_file_free(kf);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("h:m:s"), 0, 0, 1, 1);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(row), a->h, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), a->m, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), a->s, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), row, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Blik (s)"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), a->blink, 1, 1, 1, 1);
    a->sound = gtk_check_button_new_with_label("Zvuk");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->sound), sound);
    gtk_grid_attach(GTK_GRID(grid), a->sound, 0, 2, 2, 1);
    gtk_box_pack_start(GTK_BOX(a->root), grid, FALSE, FALSE, 0);

    a->start_btn = gtk_button_new_with_label("Štart");
    g_signal_connect(a->start_btn, "clicked", G_CALLBACK(on_start_stop), a);
    gtk_box_pack_start(GTK_BOX(a->root), a->start_btn, FALSE, FALSE, 0);

    a->dismiss_btn = gtk_button_new_with_label("Potvrdiť");
    g_signal_connect(a->dismiss_btn, "clicked", G_CALLBACK(on_dismiss), a);
    gtk_box_pack_start(GTK_BOX(a->root), a->dismiss_btn, FALSE, FALSE, 0);

    g_signal_connect(a->win, "destroy", G_CALLBACK(on_destroy), a);
    g_timeout_add(200, on_tick, a);
    g_timeout_add(400, on_blink, a);
    gtk_widget_show_all(a->win);
    gtk_widget_hide(a->dismiss_btn);

    gtk_main();
    return 0;
}
