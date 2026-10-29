#include "lvgl/lvgl.h"
#include "input.h"
#include "draw.h"

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/sysinfo.h>

#define MOUSE_SPEED 6
#define _XOPEN_SOURCE 600

static void win_drag_event_cb(lv_event_t * e);
static void close_term_cb(lv_event_t * e);
static void create_terminal(void);
static void term_append(const char * txt);
static void term_exec(const char * cmd);
static int  load_wind_file(const char * path);
static void create_custom_window(const char * title, int w, int h, int x, int y, const char * content);
static void open_context_menu(int x, int y);
void *driver;
void (*p_sound_init)(void);
void (*sound_play_frame)(void* frame);
void (*sound_deinit)(void);

static void win_drag_event_cb(lv_event_t * e);
static void close_term_cb(lv_event_t * e);
static void create_terminal(void);

static lv_obj_t * desktop;
static lv_obj_t * window_layer = NULL; 
static lv_obj_t * icon_layer = NULL; 
static lv_obj_t * taskbar;
static lv_obj_t * time_label;
static lv_obj_t * debug_label;
static lv_obj_t * context_menu = NULL;
static lv_obj_t * wallpaper_img = NULL;
static lv_obj_t * dragging_icon = NULL;
static int drag_start_x = 0;
static int drag_start_y = 0;
static bool was_dragging = false;

static int16_t mouse_x = DRAW_WIDTH / 2;
static int16_t mouse_y = DRAW_HEIGHT / 2;
static uint32_t loop_cnt = 0;

static int mouse_fd = -1;
static int kbd_fd = -1;
static int nano_in_pipe = -1;  
static int nano_out_pipe = -1; 
static pid_t nano_pid = -1;   

static lv_obj_t * term_win = NULL;
static lv_obj_t * term_ta  = NULL;          
static char term_buf[8192];
static int  term_buf_len = 0;

#define DESKTOP_DIR "/mnt/sdcard/cubegm/MicroOS-Linux/Desktop"
#define ICON_W      80   
#define ICON_H      96   
#define ICON_GAP_X  12
#define ICON_GAP_Y  12
#define ICONS_PER_ROW 8

typedef struct {
    lv_obj_t * icon;          
    char       name[128];     
    char       target[256];   
    int        is_link;       
} desktop_icon_t;

#define MAX_DESKTOP_ICONS 48
static desktop_icon_t desktop_icons[MAX_DESKTOP_ICONS];
static int desktop_icon_count = 0;

static const char *key_to_char(uint16_t code, bool shift)
{
    switch (code) {
        case KEY_A: return shift ? "A" : "a";
        case KEY_B: return shift ? "B" : "b";
        case KEY_C: return shift ? "C" : "c";
        case KEY_D: return shift ? "D" : "d";
        case KEY_E: return shift ? "E" : "e";
        case KEY_F: return shift ? "F" : "f";
        case KEY_G: return shift ? "G" : "g";
        case KEY_H: return shift ? "H" : "h";
        case KEY_I: return shift ? "I" : "i";
        case KEY_J: return shift ? "J" : "j";
        case KEY_K: return shift ? "K" : "k";
        case KEY_L: return shift ? "L" : "l";
        case KEY_M: return shift ? "M" : "m";
        case KEY_N: return shift ? "N" : "n";
        case KEY_O: return shift ? "O" : "o";
        case KEY_P: return shift ? "P" : "p";
        case KEY_Q: return shift ? "Q" : "q";
        case KEY_R: return shift ? "R" : "r";
        case KEY_S: return shift ? "S" : "s";
        case KEY_T: return shift ? "T" : "t";
        case KEY_U: return shift ? "U" : "u";
        case KEY_V: return shift ? "V" : "v";
        case KEY_W: return shift ? "W" : "w";
        case KEY_X: return shift ? "X" : "x";
        case KEY_Y: return shift ? "Y" : "y";
        case KEY_Z: return shift ? "Z" : "z";

        case KEY_1: return shift ? "!" : "1";
        case KEY_2: return shift ? "@" : "2";
        case KEY_3: return shift ? "#" : "3";
        case KEY_4: return shift ? "$" : "4";
        case KEY_5: return shift ? "%" : "5";
        case KEY_6: return shift ? "^" : "6";
        case KEY_7: return shift ? "&" : "7";
        case KEY_8: return shift ? "*" : "8";
        case KEY_9: return shift ? "(" : "9";
        case KEY_0: return shift ? ")" : "0";

        case KEY_SPACE:     return " ";
        case KEY_MINUS:     return shift ? "_" : "-";
        case KEY_EQUAL:     return shift ? "+" : "=";
        case KEY_LEFTBRACE: return shift ? "{" : "[";
        case KEY_RIGHTBRACE:return shift ? "}" : "]";
        case KEY_SEMICOLON: return shift ? ":" : ";";
        case KEY_APOSTROPHE:return shift ? "\"" : "'";
        case KEY_GRAVE:     return shift ? "~" : "`";
        case KEY_BACKSLASH: return shift ? "|" : "\\";
        case KEY_COMMA:     return shift ? "<" : ",";
        case KEY_DOT:       return shift ? ">" : ".";
        case KEY_SLASH:     return shift ? "?" : "/";

        default: return NULL;
    }
}

static int create_moslnk(const char * target_path, const char * display_name)
{
    
    char link_path[512];
    int n = 1;
    while (n < 1000) {
        snprintf(link_path, sizeof(link_path), "%s/shortcut%d.moslnk", DESKTOP_DIR, n);
        if (access(link_path, F_OK) != 0) break;
        n++;
    }
    if (n >= 1000) return -1;

    FILE *f = fopen(link_path, "w");
    if (!f) return -1;

    fprintf(f, "TARGET=%s\n", target_path);
    fprintf(f, "NAME=%s\n", display_name ? display_name : "Shortcut");
    fclose(f);
    sync();

    printf("[MicroOS] Created .moslnk: %s\n", link_path);
    return 0;
}

static int read_moslnk(const char * path, char * out_target, size_t tsz, char * out_name, size_t nsz)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[300];
    out_target[0] = 0;
    out_name[0] = 0;

    while (fgets(line, sizeof(line), f)) {
        
        char *p = line + strlen(line) - 1;
        while (p >= line && (*p == '\n' || *p == '\r')) *p-- = 0;

        if (strncmp(line, "TARGET=", 7) == 0) {
            strncpy(out_target, line + 7, tsz - 1);
            out_target[tsz - 1] = 0;
        }
        else if (strncmp(line, "NAME=", 5) == 0) {
            strncpy(out_name, line + 5, nsz - 1);
            out_name[nsz - 1] = 0;
        }
    }
    fclose(f);
    return (out_target[0] != 0) ? 0 : -1;
}

static void icon_drag_event_cb(lv_event_t * e)
{
    lv_obj_t * icon = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        
        drag_start_x = lv_obj_get_x(icon);
        drag_start_y = lv_obj_get_y(icon);
        dragging_icon = icon;
        was_dragging = false;
    }
    else if (code == LV_EVENT_PRESSING) {
        if (icon != dragging_icon) return;

        lv_indev_t * indev = lv_indev_active();
        if (!indev) return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);

        int x = lv_obj_get_x(icon) + vect.x;
        int y = lv_obj_get_y(icon) + vect.y;

        
        if (x < 4) x = 4;
        if (y < 4) y = 4;
        if (x > DRAW_WIDTH - ICON_W - 4)  x = DRAW_WIDTH - ICON_W - 4;
        if (y > DRAW_HEIGHT - 50 - ICON_H) y = DRAW_HEIGHT - 50 - ICON_H;

        lv_obj_set_pos(icon, x, y);

        
        int dx = abs(x - drag_start_x);
        int dy = abs(y - drag_start_y);
        if (dx > 8 || dy > 8) {
            was_dragging = true;
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (icon != dragging_icon) return;

        
        int x = lv_obj_get_x(icon);
        int y = lv_obj_get_y(icon);

        const int cell_w = ICON_W + ICON_GAP_X;
        const int cell_h = ICON_H + ICON_GAP_Y;
        const int offset_x = 16;
        const int offset_y = 16;

        int grid_x = (x - offset_x + cell_w / 2) / cell_w;
        int grid_y = (y - offset_y + cell_h / 2) / cell_h;

        if (grid_x < 0) grid_x = 0;
        if (grid_y < 0) grid_y = 0;

        int max_x = (DRAW_WIDTH - offset_x - ICON_W) / cell_w;
        int max_y = (DRAW_HEIGHT - 50 - offset_y - ICON_H) / cell_h;

        if (grid_x > max_x) grid_x = max_x;
        if (grid_y > max_y) grid_y = max_y;

        int new_x = offset_x + grid_x * cell_w;
        int new_y = offset_y + grid_y * cell_h;

        lv_obj_set_pos(icon, new_x, new_y);

        dragging_icon = NULL;
    }
}

static void icon_click_cb(lv_event_t * e)
{
    
    if (was_dragging) {
        was_dragging = false;
        return;
    }

    desktop_icon_t * di = (desktop_icon_t *)lv_event_get_user_data(e);
    if (!di) return;

    if (di->is_link) {
        if (strstr(di->target, ".wind") || access(di->target, R_OK) == 0) {
            load_wind_file(di->target);
        } else {
            char msg[300];
            snprintf(msg, sizeof(msg), "Target:\n%s", di->target);
            create_custom_window(di->name, 360, 160, 100, 80, msg);
        }
    } else {
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", DESKTOP_DIR, di->name);

        if (strstr(di->name, ".wind")) {
            load_wind_file(full);
        } else {
            FILE *f = fopen(full, "r");
            if (f) {
                char buf[1024] = {0};
                fread(buf, 1, sizeof(buf) - 1, f);
                fclose(f);
                create_custom_window(di->name, 400, 260, 80, 50, buf);
            }
        }
    }
}

static lv_obj_t * create_desktop_icon(const char * filename, const char * display_name,
                                      const char * target_path, int is_link,
                                      int grid_x, int grid_y)
{
    if (desktop_icon_count >= MAX_DESKTOP_ICONS) return NULL;

    desktop_icon_t * di = &desktop_icons[desktop_icon_count];
    memset(di, 0, sizeof(*di));

    strncpy(di->name, filename, sizeof(di->name) - 1);
    if (target_path) strncpy(di->target, target_path, sizeof(di->target) - 1);
    di->is_link = is_link;

    
    int x = 16 + grid_x * (ICON_W + ICON_GAP_X);
    int y = 16 + grid_y * (ICON_H + ICON_GAP_Y);

    
    lv_obj_t * icon = lv_obj_create(icon_layer);
    lv_obj_set_size(icon, ICON_W, ICON_H);
    lv_obj_set_pos(icon, x, y);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    
    lv_obj_t * img = lv_image_create(icon);
    lv_obj_set_size(img, 64, 64);           
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);

    
    if (is_link) {
        
        
        lv_image_set_src(img, "A:/mnt/sdcard/cubegm/MicroOS-Linux/res/windowFileLogo.png");
    } else if (strstr(filename, ".wind")) {
        
        lv_image_set_src(img, "A:/mnt/sdcard/cubegm/MicroOS-Linux/res/windowFileLogo.png");
    } else {
        
        lv_image_set_src(img, "A:/mnt/sdcard/cubegm/MicroOS-Linux/res/txtFileLogo.png");
    }

    
    lv_obj_t * lbl = lv_label_create(icon);
    lv_label_set_text(lbl, display_name ? display_name : filename);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, ICON_W - 4);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
    
    lv_obj_set_style_text_font(lbl, LV_FONT_DEFAULT, 0); 
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -2);

    
lv_obj_add_event_cb(icon, icon_drag_event_cb, LV_EVENT_PRESSED, NULL);
lv_obj_add_event_cb(icon, icon_drag_event_cb, LV_EVENT_PRESSING, NULL);
lv_obj_add_event_cb(icon, icon_drag_event_cb, LV_EVENT_RELEASED, NULL);
lv_obj_add_event_cb(icon, icon_drag_event_cb, LV_EVENT_PRESS_LOST, NULL);
lv_obj_add_event_cb(icon, icon_click_cb, LV_EVENT_CLICKED, di);

    di->icon = icon;
    desktop_icon_count++;

    return icon;
}

static void term_append(const char * txt)
{
    if (!term_ta || !txt) return;

    size_t len = strlen(txt);
    if (term_buf_len + len >= sizeof(term_buf) - 1) {
        
        memmove(term_buf, term_buf + 2048, term_buf_len - 2048);
        term_buf_len -= 2048;
        term_buf[term_buf_len] = 0;
    }

    strcat(term_buf, txt);
    term_buf_len += len;

    lv_textarea_set_text(term_ta, term_buf);
    lv_textarea_set_cursor_pos(term_ta, LV_TEXTAREA_CURSOR_LAST);
}

static void term_exec(const char * cmd)
{
    if (!cmd) return;

    
    while (*cmd == ' ') cmd++;
    if (*cmd == 0) {
        term_append("user@microos:~$ ");
        return;
    }

    char cmd_copy[256];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = 0;

    
    if (strcmp(cmd_copy, "clear") == 0) {
        term_buf[0] = 0;
        term_buf_len = 0;
        lv_textarea_set_text(term_ta, "");
        term_append("user@microos:~$ ");
        return;
    }

    
    if (strncmp(cmd_copy, "cd ", 3) == 0 || strcmp(cmd_copy, "cd") == 0) {
        char *path = cmd_copy + 3;
        while (*path == ' ') path++; 
        
        if (*path == 0) path = "/mnt/sdcard"; 

        if (chdir(path) != 0) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "cd: %s: No such file or directory\n", path);
            term_append(err_msg);
        }
        
        char prompt[256];
        char cwd[128];
        getcwd(cwd, sizeof(cwd));
        snprintf(prompt, sizeof(prompt), "user@microos:%s~$ ", cwd);
        term_append(prompt);
        return;
    }

    
    
    
if (strncmp(cmd_copy, "nano ", 5) == 0 || strcmp(cmd_copy, "nano") == 0) {
        int input_pipe[2];
        int output_pipe[2];

        if (pipe(input_pipe) < 0 || pipe(output_pipe) < 0) {
            term_append("Error: Could not create system pipes.\n");
            return;
        }

        nano_pid = fork();
        if (nano_pid == 0) {
            
            
            dup2(input_pipe[0], STDIN_FILENO);
            dup2(output_pipe[1], STDOUT_FILENO);
            dup2(output_pipe[1], STDERR_FILENO);

            
            close(input_pipe[0]); close(input_pipe[1]);
            close(output_pipe[0]); close(output_pipe[1]);

            
            setenv("TERM", "linux", 1); 
			setenv("TERMINFO_DIRS", "/usr/share/terminfo:/lib/terminfo", 1);
            setenv("NCURSES_NO_UTF8_ACS", "1", 1); 

            char *filename = cmd_copy + 5;
            while (*filename == ' ') filename++;

            
            char *binary_path = "/mnt/sdcard/cubegm/MicroOS-Linux/lib/bin/nano";
            
            if (*filename != 0) {
                char *args[] = {binary_path, filename, NULL};
                execv(binary_path, args);
            } else {
                char *args[] = {binary_path, NULL};
                execv(binary_path, args);
            }
            exit(1); 
        } else {
            
            close(input_pipe[0]);  
            close(output_pipe[1]); 

            
            nano_in_pipe = input_pipe[1];   
            nano_out_pipe = output_pipe[0]; 

            term_append("Launching Nano in headless streaming mode...\n");

            
            fcntl(nano_out_pipe, F_SETFL, O_NONBLOCK);

            
            usleep(80000); 
            
            char read_buf[512];
            int bytes = read(nano_out_pipe, read_buf, sizeof(read_buf) - 1);
            if (bytes > 0) {
                read_buf[bytes] = 0;
                term_append(read_buf); 
            } else {
                term_append("Nano is running. Press any key or Enter to refresh interface.\n");
            }
        }
        return;
    }
    

    
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd_copy);

    FILE *f = popen(full_cmd, "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            term_append(buf);
        }
        pclose(f);
    } else {
        term_append("Error: Cannot execute system shell.\n");
    }

    char prompt[256];
    char cwd[128];
    getcwd(cwd, sizeof(cwd));
    snprintf(prompt, sizeof(prompt), "user@microos:%s~$ ", cwd);
    term_append(prompt);
}


static void term_run_btn_cb(lv_event_t * e)
{
    
    
    term_exec("help");
}

static void close_term_cb(lv_event_t * e)
{
    if (term_win) {
        lv_obj_delete(term_win);
        term_win = NULL;
        term_ta = NULL;
    }
}

static void create_terminal(void)
{
    if (term_win) {
        lv_obj_move_foreground(term_win);
        return;
    }

    term_win = lv_obj_create(desktop);
    lv_obj_set_size(term_win, 640, 400);
    lv_obj_set_pos(term_win, 30, 25);
    lv_obj_set_style_bg_color(term_win, lv_color_hex(0x000000), 0);      
    lv_obj_set_style_border_width(term_win, 1, 0);
    lv_obj_set_style_border_color(term_win, lv_color_hex(0x888888), 0);
    lv_obj_set_style_pad_all(term_win, 0, 0);
    lv_obj_clear_flag(term_win, LV_OBJ_FLAG_SCROLLABLE);

    
    lv_obj_t * header = lv_obj_create(term_win);
    lv_obj_set_size(header, lv_pct(100), 28);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, win_drag_event_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "user@microos: ~");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    
    lv_obj_t * close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 22, 22);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xcc2222), 0);
    lv_obj_set_style_radius(close_btn, 4, 0);

    lv_obj_t * close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, close_term_cb, LV_EVENT_CLICKED, NULL);

    
    term_ta = lv_textarea_create(term_win);
    lv_obj_set_size(term_ta, lv_pct(100), lv_pct(100) - 28);
    lv_obj_align(term_ta, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_set_style_bg_color(term_ta, lv_color_hex(0x000000), 0);       
    lv_obj_set_style_text_color(term_ta, lv_color_hex(0xffffff), 0);     
    lv_obj_set_style_text_font(term_ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(term_ta, 8, 0);
    lv_obj_set_style_border_width(term_ta, 0, 0);

    lv_textarea_set_text(term_ta, "");
    lv_textarea_set_cursor_click_pos(term_ta, false);
    lv_obj_clear_flag(term_ta, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    
    term_buf[0] = 0;
    term_buf_len = 0;
    term_append("MicroOS Linux 4.4.186\n");
    term_append("Type 'help' for available commands.\n\n");
    term_append("user@microos:~$ ");
}


static uint32_t my_tick_get_cb(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000UL + tv.tv_usec / 1000UL);
}


static void mouse_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    static bool usb_left_button_pressed = false;
    static bool shift_pressed = false;
    static uint32_t last_reconnect_time = 0;
    static char cmd_line[128];
    static int  cmd_len = 0;

    
    if (mouse_fd < 0 || kbd_fd < 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t now = (uint32_t)(tv.tv_sec * 1000UL + tv.tv_usec / 1000UL);

        if (now - last_reconnect_time > 1500) {
            last_reconnect_time = now;

            
            for (int i = 0; i < 4; i++) {
                char path[32];
                snprintf(path, sizeof(path), "/dev/input/event%d", i);
                int fd = open(path, O_RDONLY | O_NONBLOCK);
                if (fd < 0) continue;

                
                if (mouse_fd < 0) {
                    mouse_fd = fd;
                    printf("[MicroOS] Input device %s → mouse\n", path);
                } else if (kbd_fd < 0) {
                    kbd_fd = fd;
                    printf("[MicroOS] Input device %s → keyboard\n", path);
                } else {
                    close(fd);
                }
            }
        }
    }

    
    if (mouse_fd >= 0) {
        struct input_event ev;
        int bytes_read;

        while ((bytes_read = read(mouse_fd, &ev, sizeof(ev))) > 0) {
            if (ev.type == EV_REL) {
                if (ev.code == REL_X) mouse_x += ev.value;
                if (ev.code == REL_Y) mouse_y += ev.value;
            }
            else if (ev.type == EV_KEY) {
                if (ev.code == BTN_LEFT) {
                    usb_left_button_pressed = (ev.value > 0);
                }
            }
        }

        if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN)) {
            close(mouse_fd);
            mouse_fd = -1;
            usb_left_button_pressed = false;
        }
    }

    if (kbd_fd >= 0) {
        struct input_event ev;
        int bytes_read;

        while ((bytes_read = read(kbd_fd, &ev, sizeof(ev))) > 0) {
            if (ev.type == EV_KEY) {

                if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                    shift_pressed = (ev.value > 0);
                    continue;
                }

                
                if (ev.value != 1) continue;

                
                
                
                if (nano_in_pipe >= 0) {
                    if (ev.code == KEY_ENTER) {
                        write(nano_in_pipe, "\n", 1);
                    }
                    else if (ev.code == KEY_BACKSPACE) {
                        write(nano_in_pipe, "\177", 1); 
                    }
                    else {
                        const char *ch = key_to_char(ev.code, shift_pressed);
                        if (ch) {
                            write(nano_in_pipe, ch, strlen(ch));
                        }
                    }
                    continue; 
                }
                

                if (ev.code == KEY_ENTER) {
                    if (term_win && term_ta) {
                        cmd_line[cmd_len] = 0;
                        term_append("\n");
                        term_exec(cmd_line);
                        cmd_len = 0;
                        cmd_line[0] = 0;
                    }
                }
                else if (ev.code == KEY_BACKSPACE) {
                    if (cmd_len > 0) {
                        cmd_len--;
                        cmd_line[cmd_len] = 0;

                        
                        if (term_buf_len > 0) {
                            term_buf_len--;
                            term_buf[term_buf_len] = 0;
                            lv_textarea_set_text(term_ta, term_buf);
                            lv_textarea_set_cursor_pos(term_ta, LV_TEXTAREA_CURSOR_LAST);
                        }
                    }
                }
                else {
                    const char *ch = key_to_char(ev.code, shift_pressed);
                    if (ch && cmd_len < (int)sizeof(cmd_line) - 1) {
                        cmd_line[cmd_len++] = ch[0];
                        cmd_line[cmd_len] = 0;

                        if (term_win && term_ta) {
                            term_append(ch);
                        }
                    }
                }
            }
        }

        if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN)) {
            close(kbd_fd);
            kbd_fd = -1;
        }
    }

    
    uint32_t keys = input_raw();

    if (keys & INPUT_LEFT)  mouse_x -= MOUSE_SPEED;
    if (keys & INPUT_RIGHT) mouse_x += MOUSE_SPEED;
    if (keys & INPUT_UP)    mouse_y -= MOUSE_SPEED;
    if (keys & INPUT_DOWN)  mouse_y += MOUSE_SPEED;

    
    if (mouse_x < 0)            mouse_x = 0;
    if (mouse_x >= DRAW_WIDTH)  mouse_x = DRAW_WIDTH - 1;
    if (mouse_y < 0)            mouse_y = 0;
    if (mouse_y >= DRAW_HEIGHT) mouse_y = DRAW_HEIGHT - 1;

    data->point.x = mouse_x;
    data->point.y = mouse_y;

    if ((keys & INPUT_A) || usb_left_button_pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
	
    static bool b_was_pressed = false;
    bool b_now = (keys & INPUT_B) != 0;
    if (b_now && !b_was_pressed) {
        int mx = mouse_x;
        int my = mouse_y;
        if (mx + 225 > DRAW_WIDTH) mx = DRAW_WIDTH - 225;
        if (my + 110 > DRAW_HEIGHT) my = DRAW_HEIGHT - 110;
        open_context_menu(mx, my);
    }
    b_was_pressed = b_now;
}


static void clock_timer_cb(lv_timer_t * t)
{
    time_t rawtime;
    struct tm * ti;
    char buf[16];
    time(&rawtime);
    ti = localtime(&rawtime);
    strftime(buf, sizeof(buf), "%H:%M:%S", ti);
    lv_label_set_text(time_label, buf);
}


static void close_win_cb(lv_event_t * e)
{
    lv_obj_t * win = lv_event_get_user_data(e);
    lv_obj_delete(win);
}


static void win_drag_event_cb(lv_event_t * e)
{
    lv_obj_t * header = lv_event_get_target(e);
    lv_obj_t * win = lv_obj_get_parent(header);

    if (lv_event_get_code(e) == LV_EVENT_PRESSING) {
        lv_indev_t * indev = lv_indev_active();
        if (!indev) return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);

        int x = lv_obj_get_x(win) + vect.x;
        int y = lv_obj_get_y(win) + vect.y;

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > DRAW_WIDTH - 60)  x = DRAW_WIDTH - 60;
        if (y > DRAW_HEIGHT - 90) y = DRAW_HEIGHT - 90;

        lv_obj_set_pos(win, x, y);
    }
}


static void create_custom_window(const char * title, int w, int h, int x, int y, const char * content)
{
    lv_obj_t * win = lv_obj_create(window_layer);
    lv_obj_set_size(win, w, h);
    lv_obj_set_pos(win, x, y);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xf5f5f5), 0);
    lv_obj_set_style_border_width(win, 2, 0);
    lv_obj_set_style_border_color(win, lv_color_hex(0x777777), 0);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_scrollbar_mode(win, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * header = lv_obj_create(win);
    lv_obj_set_size(header, lv_pct(100), 36);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 6, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, win_drag_event_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t * title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title ? title : "Window");
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t * close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 28, 28);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xe53935), 0);
    lv_obj_set_style_radius(close_btn, 4, 0);

    lv_obj_t * close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, close_win_cb, LV_EVENT_CLICKED, win);

    lv_obj_t * cont = lv_obj_create(win);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100) - 36);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_white(), 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * txt = lv_label_create(cont);
    lv_label_set_text(txt, content ? content : "");
    lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(txt, lv_pct(100));
}


static const uint8_t WIND_KEY[] = "MOS-WINDOW-MAGIC";
static const size_t  WIND_KEY_LEN = sizeof(WIND_KEY) - 1;

static void xor_decrypt(uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        data[i] ^= WIND_KEY[i % WIND_KEY_LEN];
    }
}


static void wind_btn_cb(lv_event_t * e)
{
    const char * action = lv_event_get_user_data(e);
    if (!action) return;

    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * win = lv_obj_get_parent(lv_obj_get_parent(btn)); 

    if (strncmp(action, "msg:", 4) == 0) {
        
        create_custom_window("Message", 320, 160, 150, 120, action + 4);
    }
    else if (strcmp(action, "close") == 0) {
        lv_obj_delete(win);
    }
    else if (strncmp(action, "cmd:", 4) == 0) {
        if (term_win == NULL) create_terminal();
        term_append(action + 4);
        term_append("\n");
        term_exec(action + 4);
    }
    else if (strncmp(action, "start:", 6) == 0) {
        char path[512];
        snprintf(path, sizeof(path), "/mnt/sdcard/cubegm/MicroOS-Linux/Desktop/%s.wind", action + 6);
        if (access(path, R_OK) != 0) {
            snprintf(path, sizeof(path), "/mnt/sdcard/cubegm/MicroOS-Linux/Desktop/%s.wind", action + 6);
        }
        load_wind_file(path);
    }
}


static int load_wind_file(const char * path)
{
    FILE * f = fopen(path, "rb");
    if (!f) {
        printf("[MicroOS] Cannot open %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 8) {
        fclose(f);
        return -1;
    }

    uint8_t *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, fsize, f) != (size_t)fsize) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[fsize] = 0;

    char title[128] = "Window";
    int width = 400, height = 280, x = 80, y = 50;

    char *text = NULL;
    size_t text_len = 0;

    
    if (fsize >= 8 && memcmp(buf, "EWND", 4) == 0) {
        uint32_t plain_len;
        memcpy(&plain_len, buf + 4, 4);

        if (plain_len > 8192 || (size_t)(plain_len + 8) > (size_t)fsize) {
            free(buf);
            return -1;
        }

        xor_decrypt(buf + 8, plain_len);
        buf[8 + plain_len] = 0;
        text = (char *)(buf + 8);
        text_len = plain_len;
    } else {
        text = (char *)buf;
        text_len = fsize;
    }

    printf("[MicroOS] Loading: %s (%zu bytes)\n", path, text_len);

    
    char *line = text;
    char *end  = text + text_len;

    
    while (line < end) {
        char *next = strchr(line, '\n');
        if (!next) next = end;

        
        char tmp[256];
        size_t len = next - line;
        if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, line, len);
        tmp[len] = 0;

        
        char *p = tmp + strlen(tmp) - 1;
        while (p >= tmp && (*p == '\r' || *p == ' ')) *p-- = 0;

        if (strncmp(tmp, "TITLE=", 6) == 0) {
            strncpy(title, tmp + 6, sizeof(title) - 1);
            title[sizeof(title)-1] = 0;
        }
        else if (strncmp(tmp, "WIDTH=", 6) == 0) width = atoi(tmp + 6);
        else if (strncmp(tmp, "HEIGHT=", 7) == 0) height = atoi(tmp + 7);
        else if (strncmp(tmp, "X=", 2) == 0) x = atoi(tmp + 2);
        else if (strncmp(tmp, "Y=", 2) == 0) y = atoi(tmp + 2);

        line = next + 1;
    }

    if (width < 160) width = 160;
    if (height < 100) height = 100;

    
    lv_obj_t * win = lv_obj_create(window_layer);
    lv_obj_set_size(win, width, height);
    lv_obj_set_pos(win, x, y);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xf0f0f0), 0);
    lv_obj_set_style_border_width(win, 2, 0);
    lv_obj_set_style_border_color(win, lv_color_hex(0x666666), 0);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    
    lv_obj_t * header = lv_obj_create(win);
    lv_obj_set_size(header, lv_pct(100), 34);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 6, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, win_drag_event_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t * title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    
    lv_obj_t * close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 26, 26);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xe53935), 0);
    lv_obj_t * close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, close_win_cb, LV_EVENT_CLICKED, win);

    
    lv_obj_t * cont = lv_obj_create(win);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100) - 34);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    
    line = text;
    while (line < end) {
        char *next = strchr(line, '\n');
        if (!next) next = end;

        char tmp[256];
        size_t len = next - line;
        if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, line, len);
        tmp[len] = 0;

        
        char *p = tmp + strlen(tmp) - 1;
        while (p >= tmp && (*p == '\r' || *p == ' ')) *p-- = 0;

        if (strncmp(tmp, "LABEL=", 6) == 0) {
            char *payload = tmp + 6;
            char *comma = strchr(payload, ',');
            if (comma) {
                *comma = 0;
                int lx = 10, ly = 10;
                sscanf(comma + 1, "%d,%d", &lx, &ly);

                lv_obj_t * lbl = lv_label_create(cont);
                lv_label_set_text(lbl, payload);
				lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x222222), 0);
                lv_obj_set_pos(lbl, lx, ly);
                printf("[MicroOS] LABEL '%s' at %d,%d\n", payload, lx, ly);
            }
        }
        else if (strncmp(tmp, "BUTTON=", 7) == 0) {
            char *payload = tmp + 7;

            
            char *parts[6] = {0};
            char *ptr = payload + strlen(payload);

            for (int i = 5; i >= 1; i--) {
                while (ptr > payload && *ptr != ',') ptr--;
                if (ptr > payload && *ptr == ',') {
                    *ptr = 0;
                    parts[i] = ptr + 1;
                    ptr--;
                }
            }
            parts[0] = payload;

            const char *text_btn = parts[0] ? parts[0] : "Button";
            int bx = parts[1] ? atoi(parts[1]) : 20;
            int by = parts[2] ? atoi(parts[2]) : 20;
            int bw = parts[3] ? atoi(parts[3]) : 120;
            int bh = parts[4] ? atoi(parts[4]) : 40;
            const char *action = parts[5] ? parts[5] : "close";

            lv_obj_t * btn = lv_button_create(cont);
            lv_obj_set_size(btn, bw, bh);
            lv_obj_set_pos(btn, bx, by);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2196F3), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 6, 0);

            lv_obj_t * blbl = lv_label_create(btn);
            lv_label_set_text(blbl, text_btn);
            lv_obj_set_style_text_color(blbl, lv_color_white(), 0);
            lv_obj_center(blbl);

            char * act_copy = malloc(strlen(action) + 1);
            if (act_copy) {
                strcpy(act_copy, action);
                lv_obj_add_event_cb(btn, wind_btn_cb, LV_EVENT_CLICKED, act_copy);
            }

            printf("[MicroOS] BUTTON '%s' at %d,%d %dx%d action='%s'\n",
                   text_btn, bx, by, bw, bh, action);
        }

        line = next + 1;
    }

    free(buf);
    printf("[MicroOS] Window '%s' created successfully\n", title);
    return 0;
}

static void menu_btn_cb(lv_event_t * e)
{
    static int cnt = 1;
    char title[32];
    sprintf(title, "Window #%d", cnt++);
    create_custom_window(title, 380, 220, 50 + (cnt*20)%200, 40 + (cnt*15)%120,
                         "Simple window\nCreated in MENU");
}

static void terminal_btn_cb(lv_event_t * e)
{
    create_terminal();
}


static void my_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    uint16_t * dst = draw_get_buffer();
    if (!dst) {
        lv_display_flush_ready(disp);
        return;
    }

    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    const uint16_t * src = (const uint16_t *)px_map;

    for (int y = 0; y < h; y++) {
        memcpy(dst + (area->y1 + y) * DRAW_WIDTH + area->x1,
               src + y * w,
               w * 2);
    }
    lv_display_flush_ready(disp);
}

static void apply_wallpaper(const char * path) {
    char msg[256];

    if (access(path, R_OK) != 0) {
        snprintf(msg, sizeof(msg), "File not found:\n%s\nerrno=%d (%s)",
                 path, errno, strerror(errno));
        create_custom_window("Wallpaper Error", 400, 160, 80, 80, msg);
        return;
    }

    static char img_path[256];
    snprintf(img_path, sizeof(img_path), "A:%s", path);
    lv_obj_set_style_bg_image_src(desktop, img_path, 0);
    lv_obj_invalidate(desktop);

    snprintf(msg, sizeof(msg), "Wallpaper set:\n%s", path);
    create_custom_window("Wallpaper", 420, 130, 80, 80, msg);
}

static void cycle_wallpaper(void) {
    static char current[256] = "";
    const char * dir_path = "/mnt/sdcard/cubegm/MicroOS-Linux/Background";

    DIR * d = opendir(dir_path);
    if (!d) {
        char msg[200];
        snprintf(msg, sizeof(msg), "Cannot open folder:\n%s\nerrno=%d (%s)",
                 dir_path, errno, strerror(errno));
        create_custom_window("Wallpaper Error", 400, 160, 80, 80, msg);
        return;
    }

    char files[32][256];
    int count = 0;

    struct dirent * ent;
    while ((ent = readdir(d)) != NULL && count < 32) {
        if (ent->d_type == DT_DIR) continue;
        size_t len = strlen(ent->d_name);
        if (len < 5) continue;
        if (strcasecmp(ent->d_name + len - 4, ".bmp") != 0) continue;
        snprintf(files[count], 256, "%s/%s", dir_path, ent->d_name);
        count++;
    }
    closedir(d);

    if (count == 0) {
        char msg[200];
        snprintf(msg, sizeof(msg), "No PNG files found in:\n%s", dir_path);
        create_custom_window("Wallpaper Error", 400, 140, 80, 80, msg);
        return;
    }

    int next = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(files[i], current) == 0) {
            next = (i + 1) % count;
            break;
        }
    }

    strncpy(current, files[next], sizeof(current) - 1);
    apply_wallpaper(current);
}

static void create_desktop_file(void)
{
    const char * dir = "/mnt/sdcard/cubegm/MicroOS-Linux/Desktop";

    
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Cannot create folder:\n%s\nerrno=%d", dir, errno);
        create_custom_window("Error", 340, 140, 140, 120, msg);
        printf("[MicroOS] mkdir failed: %s (%d)\n", dir, errno);
        return;
    }

    
    char path[512];
    int n = 1;
    int found = 0;

    while (n < 1000) {
        snprintf(path, sizeof(path), "%s/file%d.txt", dir, n);
        if (access(path, F_OK) != 0) {   
            found = 1;
            break;
        }
        n++;
    }

    if (!found) {
        create_custom_window("Error", 300, 120, 150, 130, "Too many files");
        return;
    }

    
    FILE * f = fopen(path, "w");
    if (!f) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Cannot create file:\n%s\nerrno = %d\n%s",
                 path, errno, strerror(errno));
        create_custom_window("Error", 380, 160, 100, 100, msg);
        printf("[MicroOS] fopen failed: %s (%d)\n", path, errno);
        return;
    }

    fprintf(f, "New file created by MicroOS\n");
    fclose(f);
	sync();

    
    char msg[64];
    snprintf(msg, sizeof(msg), "Created:\nfile%d.txt", n);
    create_custom_window("Done", 280, 120, 160, 150, msg);

    printf("[MicroOS] Created file: %s\n", path);
}

static void close_context_menu(void) {
	if (context_menu) {
		lv_obj_delete(context_menu);
		context_menu = NULL;
	}
}

static lv_obj_t * add_shortcut_win = NULL;


static void file_select_cb(lv_event_t * e)
{
    const char * filename = lv_event_get_user_data(e);
    if (!filename) return;

    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", DESKTOP_DIR, filename);

    
    
    if (create_moslnk(fullpath, filename) == 0) {
        
        
        
        
        char msg[128];
        snprintf(msg, sizeof(msg), "Shortcut created for:\n%s", filename);
        create_custom_window("Success", 320, 140, 100, 100, msg);
    } else {
        create_custom_window("Error", 300, 120, 110, 110, "Failed to create shortcut");
    }

    
    if (add_shortcut_win) {
        lv_obj_delete(add_shortcut_win);
        add_shortcut_win = NULL;
    }
}


static void close_add_shortcut_win_cb(lv_event_t * e)
{
    if (add_shortcut_win) {
        lv_obj_delete(add_shortcut_win);
        add_shortcut_win = NULL;
    }
}


static void open_add_to_desktop_window(void)
{
    if (add_shortcut_win) {
        lv_obj_move_foreground(add_shortcut_win);
        return;
    }

    
    add_shortcut_win = lv_obj_create(desktop);
    lv_obj_set_size(add_shortcut_win, 380, 280);
    lv_obj_set_pos(add_shortcut_win, (DRAW_WIDTH - 380) / 2, (DRAW_HEIGHT - 280) / 2);
    lv_obj_set_style_bg_color(add_shortcut_win, lv_color_hex(0xf0f0f0), 0);
    lv_obj_set_style_border_width(add_shortcut_win, 2, 0);
    lv_obj_set_style_border_color(add_shortcut_win, lv_color_hex(0x555555), 0);
    lv_obj_set_style_pad_all(add_shortcut_win, 0, 0);
    lv_obj_clear_flag(add_shortcut_win, LV_OBJ_FLAG_SCROLLABLE);

    
    lv_obj_t * header = lv_obj_create(add_shortcut_win);
    lv_obj_set_size(header, lv_pct(100), 32);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x4CAF50), 0); 
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, win_drag_event_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "Add to desktop..");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    
    lv_obj_t * close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 24, 24);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xcc2222), 0);
    lv_obj_t * close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, close_add_shortcut_win_cb, LV_EVENT_CLICKED, NULL);

    
    lv_obj_t * list_cont = lv_obj_create(add_shortcut_win);
    lv_obj_set_size(list_cont, lv_pct(100), lv_pct(100) - 32);
    lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list_cont, lv_color_white(), 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN); 
    lv_obj_set_style_pad_all(list_cont, 8, 0);
    lv_obj_set_style_pad_row(list_cont, 4, 0); 

    
    DIR *d = opendir(DESKTOP_DIR);
    if (d) {
        struct dirent *dir;
        int file_idx = 0;
        while ((dir = readdir(d)) != NULL) {
            
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

            
            if (strstr(dir->d_name, ".txt") || strstr(dir->d_name, ".wind")) {
                
                
                lv_obj_t * btn = lv_button_create(list_cont);
                lv_obj_set_size(btn, lv_pct(100), 36);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xf5f5f5), 0);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xddffdd), LV_STATE_PRESSED);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xcccccc), 0);
                lv_obj_set_style_radius(btn, 4, 0);
                lv_obj_set_style_shadow_width(btn, 0, 0);

                
                lv_obj_t * icon_lbl = lv_label_create(btn);
                if (strstr(dir->d_name, ".wind")) {
                    lv_label_set_text(icon_lbl, LV_SYMBOL_FILE);
                } else {
                    lv_label_set_text(icon_lbl, LV_SYMBOL_EDIT);
                }
                lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0x444444), 0);
                lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 6, 0);

                
                lv_obj_t * name_lbl = lv_label_create(btn);
                lv_label_set_text(name_lbl, dir->d_name);
                lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x111111), 0);
                lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 28, 0);

                
                char * name_copy = malloc(strlen(dir->d_name) + 1);
                if (name_copy) {
                    strcpy(name_copy, dir->d_name);
                    lv_obj_add_event_cb(btn, file_select_cb, LV_EVENT_CLICKED, name_copy);
                }
                file_idx++;
            }
        }
        closedir(d);

        if (file_idx == 0) {
            lv_obj_t * empty_lbl = lv_label_create(list_cont);
            lv_label_set_text(empty_lbl, "No .txt or .wind files found.");
            lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x888888), 0);
        }
    } else {
        lv_obj_t * err_lbl = lv_label_create(list_cont);
        lv_label_set_text(err_lbl, "Failed to open directory.");
        lv_obj_set_style_text_color(err_lbl, lv_color_hex(0xcc0000), 0);
    }
}


static void ctx_menu_btn_cb(lv_event_t * e) {
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    close_context_menu();
    
    switch (action) {
        case 1: create_desktop_file(); break;
        case 2: cycle_wallpaper(); break;
        case 3: open_add_to_desktop_window(); break; 
    }
}

static void open_context_menu(int x, int y) {
    if (context_menu) {
        close_context_menu();
        return;
    }
    
    context_menu = lv_obj_create(desktop);
    lv_obj_set_size(context_menu, 220, 142); 
    lv_obj_set_pos(context_menu, x, y);
    lv_obj_set_style_bg_color(context_menu, lv_color_hex(0xfafafa), 0);
    lv_obj_set_style_border_color(context_menu, lv_color_hex(0x999999), 0);
    lv_obj_set_style_border_width(context_menu, 1, 0);
    lv_obj_set_style_pad_all(context_menu, 6, 0);
    lv_obj_set_style_radius(context_menu, 6, 0);
    
    struct { const char * label; int id; } items[] = {
        { "Create file on Desktop", 1 },
        { "Change Wallpaper", 2 },
        { "Add to desktop..", 3 }, 
    };
    int item_count = 3;
    
    for (int i = 0; i < item_count; i++) {
        lv_obj_t * btn = lv_button_create(context_menu);
        lv_obj_set_size(btn, 208, 38);
        lv_obj_set_pos(btn, 0, i * 44);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xfafafa), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xe0e8ff), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        
        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, items[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x222222), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
        
        lv_obj_add_event_cb(btn, ctx_menu_btn_cb, LV_EVENT_CLICKED,
                           (void *)(intptr_t)items[i].id);
    }
}

void reload_desktop_icons(void)
{
    
    for (int i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].icon) {
            lv_obj_delete(desktop_icons[i].icon);
        }
    }
    desktop_icon_count = 0;

    
    DIR *d = opendir(DESKTOP_DIR);
    if (!d) {
        printf("[MicroOS] Error: Cannot open Desktop dir: %s\n", DESKTOP_DIR);
        return;
    }

    struct dirent *dir;
    int grid_x = 0;
    int grid_y = 0;

    while ((dir = readdir(d)) != NULL) {
        
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

        int is_link = 0;
        char target_path[256] = {0};   
        char display_name[128] = {0};  

        size_t len = strlen(dir->d_name);
        
        
        if (len > 7 && strcasecmp(dir->d_name + len - 7, ".moslnk") == 0) {
            is_link = 1;
            char full_lnk_path[512];
            snprintf(full_lnk_path, sizeof(full_lnk_path), "%s/%s", DESKTOP_DIR, dir->d_name);
            
            if (read_moslnk(full_lnk_path, target_path, sizeof(target_path), display_name, sizeof(display_name)) != 0) {
                continue; 
            }
        } 
        else if (strstr(dir->d_name, ".txt") || strstr(dir->d_name, ".wind")) {
            is_link = 0;
            strncpy(display_name, dir->d_name, sizeof(display_name) - 1);
        } else {
            continue; 
        }

        
        create_desktop_icon(dir->d_name, display_name, is_link ? target_path : NULL, is_link, grid_x, grid_y);

        
        grid_x++;
        if (grid_x >= ICONS_PER_ROW) {
            grid_x = 0;
            grid_y++;
        }
    }
    closedir(d);
    
    
    lv_obj_invalidate(desktop);
}

int sound_init(void) {
    driver = dlopen("/mnt/sdcard/cubegm/driver.so", RTLD_LAZY);
    
    if (!driver)
        return -1;
    
    p_sound_init = dlsym(driver, "sound_driver_init");
    sound_play_frame = dlsym(driver, "sound_driver_playframe");
    sound_deinit = dlsym(driver, "sound_driver_deinit");

    return 0;
}


int main(int argc, char ** argv)
{
    if (draw_init() < 0) return -1;
	
	if (sound_init() == 0 && p_sound_init != NULL) {
        p_sound_init(); 
        
    } else {
       
    }

    lv_init();
	
    mouse_fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    if(mouse_fd < 0) {
        mouse_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    }
	
    lv_tick_set_cb(my_tick_get_cb);

    
    lv_display_t * display = lv_display_create(DRAW_WIDTH, DRAW_HEIGHT);
    static uint8_t draw_buf[DRAW_WIDTH * (DRAW_HEIGHT / 10) * 2]; 
    lv_display_set_buffers(display, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, my_flush_cb);

    input_init();

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, mouse_read_cb);
    lv_indev_set_display(indev, display);
	
    lv_obj_t * cursor_obj = lv_image_create(lv_layer_sys());
    lv_image_set_src(cursor_obj, "A:/mnt/sdcard/cubegm/MicroOS-Linux/res/cursor.png");
    lv_obj_remove_flag(cursor_obj, LV_OBJ_FLAG_CLICKABLE); 
    lv_indev_set_cursor(indev, cursor_obj);

    
    desktop = lv_obj_create(lv_screen_active());
    lv_obj_set_size(desktop, DRAW_WIDTH, DRAW_HEIGHT);
    lv_obj_set_style_radius(desktop, 0, 0);
    lv_obj_set_style_pad_all(desktop, 0, 0);
    lv_obj_clear_flag(desktop, LV_OBJ_FLAG_SCROLLABLE);

    
    static lv_style_t style_desktop;
    lv_style_init(&style_desktop);
    
    lv_obj_set_style_bg_image_src(desktop, "A:/mnt/sdcard/cubegm/MicroOS-Linux/Background/Default.bmp", 0);
    lv_obj_set_style_bg_color(desktop, lv_color_hex(0x006666), 0); 
	
	icon_layer = lv_obj_create(desktop);
    lv_obj_set_size(icon_layer, DRAW_WIDTH, DRAW_HEIGHT - 42);
    lv_obj_set_pos(icon_layer, 0, 0);
    lv_obj_set_style_bg_opa(icon_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_layer, 0, 0);
    lv_obj_set_style_pad_all(icon_layer, 0, 0);
    lv_obj_remove_flag(icon_layer, LV_OBJ_FLAG_SCROLLABLE);

    window_layer = lv_obj_create(desktop);
    lv_obj_set_size(window_layer, DRAW_WIDTH, DRAW_HEIGHT - 42); 
    lv_obj_set_pos(window_layer, 0, 0);
    lv_obj_set_style_bg_opa(window_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(window_layer, 0, 0);
    lv_obj_set_style_pad_all(window_layer, 0, 0);
    lv_obj_remove_flag(window_layer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_remove_flag(window_layer, LV_OBJ_FLAG_CLICKABLE);

    taskbar = lv_obj_create(desktop);
    lv_obj_set_size(taskbar, DRAW_WIDTH, 42);
    lv_obj_align(taskbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(taskbar, lv_color_hex(0xd0d0d0), 0);
    lv_obj_set_style_radius(taskbar, 0, 0);
    lv_obj_set_style_pad_all(taskbar, 4, 0);
    lv_obj_clear_flag(taskbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * menu_btn = lv_button_create(taskbar);
    lv_obj_set_size(menu_btn, 90, 32);
    lv_obj_align(menu_btn, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(menu_btn, lv_color_hex(0xb0b0b0), 0);
    lv_obj_t * menu_lbl = lv_label_create(menu_btn);
    lv_label_set_text(menu_lbl, "MENU");
    lv_obj_center(menu_lbl);
    lv_obj_add_event_cb(menu_btn, menu_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * term_btn = lv_button_create(taskbar);
    lv_obj_set_size(term_btn, 100, 32);
    lv_obj_align(term_btn, LV_ALIGN_LEFT_MID, 110, 0);
    lv_obj_set_style_bg_color(term_btn, lv_color_hex(0x333333), 0);
    lv_obj_t * term_lbl = lv_label_create(term_btn);
    lv_label_set_text(term_lbl, "Terminal");
    lv_obj_set_style_text_color(term_lbl, lv_color_hex(0x00ff00), 0);
    lv_obj_center(term_lbl);
    lv_obj_add_event_cb(term_btn, terminal_btn_cb, LV_EVENT_CLICKED, NULL);

    time_label = lv_label_create(taskbar);
    lv_label_set_text(time_label, "00:00:00");
    lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_timer_create(clock_timer_cb, 1000, NULL);
	
	reload_desktop_icons();

    const uint32_t target_frame_ms = 16;   

    uint32_t last_present = 0;

    while (1) {
        uint32_t time_till_next = lv_timer_handler();

    
        if (nano_out_pipe >= 0) {
            char live_buf[256];
            int live_bytes = read(nano_out_pipe, live_buf, sizeof(live_buf) - 1);
            if (live_bytes > 0) {
                live_buf[live_bytes] = 0;
                term_append(live_buf); 
            }
        }

        draw_present();

        if (time_till_next > 16) {
            time_till_next = 16;
        } else if (time_till_next == 0) {
            time_till_next = 1; 
        }
        
        usleep(time_till_next * 1000);
    }
    return 0;
}