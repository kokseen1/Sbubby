#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <math.h>

#include <slre.h>

#include <SDL2/SDL.h>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#define STR_IMPL_(x) #x
#define STR(x) STR_IMPL_(x)

#define STEP_SMALL 0.1
#define STEP_MEDIUM 1
#define STEP_DEFAULT 3
#define REPLY_USERDATA_SUB_RELOAD 8000

#define CMD_BUF_MAX 1024
#define SMALL_BUF_MAX 32
#define DEFAULT_SUB_FNAME "out.srt"
#define SUB_PLACEHOLDER "1\n00:00:00,000 --> 00:00:00,000\n\n\n"
#define COLOR_SUB_FOCUSED "lightgreen"

typedef struct Sub
{
    double start_d;
    double end_d;
    char text[CMD_BUF_MAX];
    struct Sub *next;
} Sub;

static double duration_d;
static char ts_s_str[SMALL_BUF_MAX];
static double ts_s_d;

static char cmd_buf[CMD_BUF_MAX];

static int mtx_reload = 0;
static int insert_mode = 0;

static char *sub_fname = NULL;
static Sub *sub_head = NULL;
static Sub *sub_focused = NULL;

static Uint32 wakeup_on_mpv_render_update, wakeup_on_mpv_events;
static SDL_Window *window = NULL;
static mpv_handle *mpv = NULL;

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

static void on_mpv_events(void *ctx)
{
    SDL_Event event = {.type = wakeup_on_mpv_events};
    SDL_PushEvent(&event);
}

static void on_mpv_render_update(void *ctx)
{
    SDL_Event event = {.type = wakeup_on_mpv_render_update};
    SDL_PushEvent(&event);
}

static char *ptr_max(char *ptr1, char *ptr2)
{
    return ptr1 > ptr2 ? ptr1 : ptr2;
}

static int in_frame(Sub *sub)
{
    return sub->start_d <= ts_s_d && ts_s_d < sub->end_d;
}

static void insert_ordered(Sub *sub_new)
{
    if (!sub_head)
    {
        // First item
        sub_head = sub_new;
        sub_head->next = NULL;
    }
    else if (sub_new->start_d < sub_head->start_d)
    {
        // Insert at beginning (new head)
        sub_new->next = sub_head;
        sub_head = sub_new;
    }
    else
    {
        // Ordered insert
        Sub *sub_curr = sub_head;
        while (sub_curr)
        {
            if (!sub_curr->next || sub_curr->next->start_d > sub_new->start_d)
            {
                sub_new->next = sub_curr->next;
                sub_curr->next = sub_new;
                break;
            }
            sub_curr = sub_curr->next;
        }
    }
}

static void show_text(char *text, char *duration)
{
    const char *cmd[] = {"show-text", text, duration, NULL};
    mpv_command_async(mpv, 0, cmd);
}

static void toggle_pause()
{
    const char *cmd_pause[] = {"cycle", "pause", NULL};
    mpv_command_async(mpv, 0, cmd_pause);
}
/**
 * Warning: target cannot accept commas! e.g. 00:00:01,000
 */
static void exact_seek(char *target, char *flag)
{
    const char *cmd_seek[] = {"seek", target, flag, "exact", NULL};
    mpv_command_async(mpv, 0, cmd_seek);
}

static void exact_seek_d(double value, char *flag)
{
    char target[SMALL_BUF_MAX];
    snprintf(target, sizeof(target), "%f", value);
    exact_seek(target, flag);
}

static double hhmmss_to_d(char *buf)
{
    double d = 0.0;
    struct slre_cap caps[3];

    if (slre_match("^([0-9]+):([0-9]+):([0-9]+[,|\\.][0-9]+)$", buf, strlen(buf), caps, 3) > 0)
    {
        if (caps[0].len && caps[1].len && caps[2].len)
        {
            char hh[SMALL_BUF_MAX] = {0};
            char mm[SMALL_BUF_MAX] = {0};
            char ss_ms[SMALL_BUF_MAX] = {0};

            strncpy(hh, caps[0].ptr, caps[0].len);
            strncpy(mm, caps[1].ptr, caps[1].len);
            strncpy(ss_ms, caps[2].ptr, caps[2].len);

            char *pos = strchr(ss_ms, ',');
            if (pos)
                *pos = '.';

            d = strtol(hh, NULL, 10) * 3600 + strtol(mm, NULL, 10) * 60 + strtod(ss_ms, NULL);
        }
    }
    else
    {
        printf("Invalid hhmmss format!\n");
    }

    return d;
}

static void d_to_hhmmss(double ts_d, char *ts_hhmmss_out)
{
    int h = ts_d / 3600;
    int m = fmod((ts_d / 60), 60);
    double s = fmod(ts_d, 60);

    sprintf(ts_hhmmss_out, "%02d:%02d:%06.3f", h, m, s);
    char *pos = strchr(ts_hhmmss_out, '.');
    if (pos)
    {
        *pos = ',';
    }
}

static void get_full_ts(char *ts_full)
{
    d_to_hhmmss(ts_s_d, ts_full);
}

static void refresh_title()
{
    char title[CMD_BUF_MAX];

    if (insert_mode)
    {
        sprintf(title, "INSERT %s", cmd_buf);
    }
    else
    {
        sprintf(title, "NORMAL %s", cmd_buf);
    }

    SDL_SetWindowTitle(window, title);
}

static void frame_step()
{
    const char *cmd[] = {"frame-step", NULL};
    mpv_command_async(mpv, 0, cmd);
}

static void frame_back_step()
{
    const char *cmd[] = {"frame-back-step", NULL};
    mpv_command_async(mpv, 0, cmd);
}

static void reload_sub()
{
    const char *cmd[] = {"sub-reload", NULL};
    mtx_reload++;
    mpv_command_async(mpv, REPLY_USERDATA_SUB_RELOAD, cmd);
}

static Sub *alloc_sub()
{
    return (Sub *)calloc(1, sizeof(Sub));
}

static void clear_cmd_buf()
{
    cmd_buf[0] = 0;
}

static void pop_char(char *text)
{
    int len = strlen(text);
    if (len > 1 && text[len - 2] == '\n')
    {
        text[len - 2] = 0;
    }
    else if (len > 0)
    {
        text[len - 1] = 0;
    }
    else
    {
        show_text("String is empty!", "100");
    }
}

static void add_sub(char *fname)
{
    if (!sub_fname)
    {
        const char *cmd[] = {"sub-add", fname, NULL};
        mpv_command_async(mpv, 0, cmd);
        sub_fname = fname;
    }
}

static void import_sub(char *fname)
{
    char buf[CMD_BUF_MAX] = {0};
    char pend_buf[CMD_BUF_MAX] = {0};

    FILE *fp = fopen(fname, "r");
    Sub *curr_sub = NULL;

    while (fgets(buf, CMD_BUF_MAX, fp))
    {
        struct slre_cap caps[2];

        if (slre_match("^([0-9]+)\n$", buf, strlen(buf), caps, 1) > 0)
        {
            if (caps[0].len)
            {
                if (pend_buf[0] && curr_sub)
                {
                    strcat(curr_sub->text, pend_buf);
                }
                strncpy(pend_buf, caps[0].ptr, caps[0].len);
            }
        }
        else if (slre_match("^([0-9]+:[0-9]+:[0-9]+[,|\\.][0-9]+) --> ([0-9]+:[0-9]+:[0-9]+[,|\\.][0-9]+)\n$", buf, strlen(buf), caps, 2) > 0)
        {
            if (caps[0].len && caps[1].len)
            {
                if (curr_sub)
                    insert_ordered(curr_sub);

                curr_sub = alloc_sub();

                char start[SMALL_BUF_MAX] = {0};
                char end[SMALL_BUF_MAX] = {0};

                strncpy(start, caps[0].ptr, caps[0].len);
                strncpy(end, caps[1].ptr, caps[1].len);

                curr_sub->start_d = hhmmss_to_d(start);
                curr_sub->end_d = hhmmss_to_d(end);

                if (pend_buf[0])
                {
                    pend_buf[0] = 0;
                }
            }
        }
        else if (slre_match("^([^\n]*)", buf, strlen(buf), caps, 1) > 0)
        {
            if (pend_buf[0])
            {
                if (curr_sub)
                    strcat(curr_sub->text, pend_buf);
                pend_buf[0] = 0;
            }

            if (curr_sub && caps[0].len)
            {
                strncat(curr_sub->text, caps[0].ptr, caps[0].len);
            }
        }
    }

    if (curr_sub)
    {
        insert_ordered(curr_sub);
    }

    fclose(fp);
}

static void export_sub()
{
    if (mtx_reload)
    {
        return;
    }

    FILE *pFile = fopen(DEFAULT_SUB_FNAME, "w");

    if (sub_head)
    {
        int i = 1;
        Sub *sub_curr = sub_head;

        while (sub_curr)
        {
            char start_hhmmss[SMALL_BUF_MAX];
            char end_hhmmss[SMALL_BUF_MAX];

            d_to_hhmmss(sub_curr->start_d, start_hhmmss);
            d_to_hhmmss(sub_curr->end_d, end_hhmmss);

            fprintf(pFile, "%d\n", i);
            fprintf(pFile, "%s --> %s\n", start_hhmmss, end_hhmmss);
            if (sub_curr == sub_focused)
                fprintf(pFile, "<font color=" COLOR_SUB_FOCUSED ">%s</font>\n\n", sub_curr->text);
            else
                fprintf(pFile, "%s\n\n", sub_curr->text);

            sub_curr = sub_curr->next;
            i++;
        }
    }
    else
    {
        fprintf(pFile, SUB_PLACEHOLDER);
    }

    fclose(pFile);
}

static Sub *delete_sub(Sub *sub_target)
{
    Sub *sub_curr = sub_head;
    if (sub_head == sub_target)
    {
        sub_head = sub_target->next;
        sub_curr = sub_head;
    }
    else
    {
        while (sub_curr)
        {
            if (!sub_curr->next)
            {
                show_text("Target sub not found!", "100");
                return sub_head;
            }

            if (sub_curr->next == sub_target)
            {
                sub_curr->next = sub_target->next;
                break;
            }

            sub_curr = sub_curr->next;
        }
    }

    free(sub_target);
    return sub_curr;
}

static void export_and_reload()
{
    export_sub();
    reload_sub();
}

static void set_sub_focus(Sub *sub)
{
    sub_focused = sub;
    export_and_reload();
}

static int process_ex()
{
    if (cmd_buf[0] == ':')
    {
        char *ex_buf = cmd_buf + 1;
        struct slre_cap caps[1];
        // printf("ex_buf: %s;\n", ex_buf);
        if (slre_match("^([a-zA-Z]*)$", ex_buf, strlen(ex_buf), caps, 1) > 0)
        {
            if (caps[0].len)
            {
                if (!strcmp(caps[0].ptr, "w"))
                {
                    printf("save\n");
                }
                else if (!strcmp(caps[0].ptr, "q"))
                {
                    return -1;
                }
            }
        }
        else if (slre_match("^([0-9]*)$", ex_buf, strlen(ex_buf), caps, 1) > 0)
        {
            if (caps[0].len)
            {
                // TODO: Support precise timestamps
                long pos = strtol(caps[0].ptr, NULL, 10);
                exact_seek_d(pos, "absolute");
            }
        }
    }
    clear_cmd_buf();
    refresh_title();

    return 0;
}

static void gen_placeholder(char *fname)
{
    FILE *pFile = fopen(fname, "w");
    fprintf(pFile, SUB_PLACEHOLDER);
    fclose(pFile);
}

static void set_window_icon()
{
    static uint8_t pixels[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xfc, 0xf1, 0xff, 0xff, 0xfb, 0xed, 0xff, 0xff, 0xfe, 0xfb, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xe4, 0x81, 0xff, 0xff, 0xdc, 0x5a, 0xff, 0xff, 0xd7, 0x43, 0xff, 0xff, 0xe2, 0x79, 0xff,
        0xfc, 0xf0, 0xbd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xfe, 0xe1, 0x73, 0xff, 0xff, 0xd8, 0x48, 0xff, 0xff, 0xd3, 0x30, 0xff, 0xb5, 0xbb, 0x19, 0xff,
        0xa5, 0xb8, 0x1c, 0xff, 0x9e, 0xd7, 0xb7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0xf2, 0xe8, 0xff,
        0x90, 0xd1, 0xad, 0xff, 0xc9, 0xe8, 0xd7, 0xff, 0xf5, 0xfb, 0xf7, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xf9, 0xe4, 0xff, 0xff, 0xd9, 0x4d, 0xff, 0xff, 0xd7, 0x45, 0xff,
        0xff, 0xd8, 0x4c, 0xff, 0xff, 0xd8, 0x4c, 0xff, 0xff, 0xdb, 0x55, 0xff, 0xb2, 0xc7, 0x56, 0xff,
        0x6a, 0xb7, 0x61, 0xff, 0x8d, 0xd0, 0xab, 0xff, 0xff, 0xff, 0xff, 0xff, 0x98, 0xd4, 0xb2, 0xff,
        0x43, 0xb2, 0x74, 0xff, 0x2e, 0xaa, 0x64, 0xfe, 0xe7, 0xf5, 0xed, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xf2, 0xc2, 0xff, 0xff, 0xdb, 0x56, 0xff, 0xff, 0xd6, 0x41, 0xff,
        0xff, 0xf8, 0xdf, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0x75, 0xc6, 0x98, 0xff,
        0x4e, 0xb6, 0x7b, 0xff, 0x2f, 0xaa, 0x65, 0xff, 0x62, 0xbe, 0x8a, 0xff, 0x2f, 0xa9, 0x65, 0xff,
        0x34, 0xab, 0x68, 0xff, 0x7f, 0xca, 0xa0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf3, 0xc6, 0xff, 0xff, 0xdc, 0x5c, 0xff,
        0xff, 0xdd, 0x5f, 0xff, 0xff, 0xf4, 0xcb, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0xcb, 0xa0, 0xff,
        0x46, 0xb3, 0x76, 0xff, 0x7a, 0xc8, 0x9d, 0xff, 0x59, 0xbb, 0x85, 0xfe, 0x5f, 0xbd, 0x88, 0xff,
        0x5e, 0xbd, 0x87, 0xff, 0x8c, 0xcf, 0xaa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xf9, 0xff, 0xff, 0xd3, 0x2f, 0xff,
        0xff, 0xe1, 0x73, 0xff, 0xff, 0xe8, 0x95, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0xfc, 0xfa, 0xff,
        0x34, 0xab, 0x68, 0xff, 0xd7, 0xee, 0xe1, 0xff, 0x61, 0xbf, 0x8b, 0xfe, 0x74, 0xc5, 0x96, 0xff,
        0x6b, 0xc3, 0x92, 0xfe, 0x8b, 0xcf, 0xa9, 0xff, 0xd4, 0xed, 0xdf, 0xff, 0xc8, 0xe8, 0xd6, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf4, 0xcd, 0xff, 0xff, 0xe3, 0x7c, 0xff,
        0xff, 0xe2, 0x78, 0xff, 0xff, 0xd8, 0x45, 0xff, 0xff, 0xf2, 0xc5, 0xff, 0xfe, 0xfe, 0xfe, 0xff,
        0x3c, 0xaf, 0x6d, 0xfe, 0x3d, 0xaf, 0x6f, 0xff, 0x5f, 0xbd, 0x88, 0xff, 0xac, 0xdd, 0xc1, 0xfe,
        0x66, 0xc0, 0x8e, 0xff, 0x80, 0xca, 0xa1, 0xff, 0x27, 0xa6, 0x5f, 0xff, 0x9b, 0xd6, 0xb5, 0xfe,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe2, 0x75, 0xfe, 0xff, 0xd9, 0x4d, 0xff,
        0xff, 0xd9, 0x4d, 0xff, 0xff, 0xe8, 0x94, 0xff, 0xfe, 0xd0, 0x25, 0xff, 0xfe, 0xd8, 0x49, 0xff,
        0x2e, 0xa1, 0x3c, 0xff, 0x28, 0xa0, 0x3e, 0xff, 0x41, 0xb1, 0x73, 0xff, 0x2a, 0xa7, 0x61, 0xff,
        0x3d, 0xaf, 0x6f, 0xff, 0x25, 0xa6, 0x5d, 0xff, 0xa2, 0xd8, 0xb9, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xbb, 0xff, 0xff, 0xe6, 0x8c, 0xff,
        0xff, 0xe2, 0x78, 0xff, 0xff, 0xcb, 0x08, 0xff, 0xff, 0xd5, 0x39, 0xff, 0xff, 0xdc, 0x5a, 0xff,
        0xc1, 0xbe, 0x15, 0xff, 0xaa, 0xc6, 0x56, 0xff, 0x92, 0xd2, 0xae, 0xff, 0xa6, 0xda, 0xbc, 0xff,
        0x53, 0xb8, 0x7f, 0xff, 0x4c, 0xb6, 0x7a, 0xff, 0xe6, 0xf4, 0xec, 0xfe, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xb4, 0xfe,
        0xff, 0xdd, 0x62, 0xff, 0xff, 0xe1, 0x74, 0xff, 0xff, 0xfb, 0xed, 0xff, 0xff, 0xe9, 0x99, 0xff,
        0xff, 0xe5, 0x86, 0xff, 0xff, 0xf2, 0xc3, 0xff, 0x3c, 0xaf, 0x6e, 0xff, 0x64, 0xc1, 0x8d, 0xfe,
        0x2d, 0xa9, 0x64, 0xff, 0x26, 0xa6, 0x5e, 0xff, 0xe6, 0xf5, 0xec, 0xfe, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd8, 0x49, 0xff,
        0xff, 0xda, 0x52, 0xff, 0xff, 0xdc, 0x5a, 0xff, 0xff, 0xec, 0xa6, 0xff, 0xff, 0xd9, 0x4c, 0xff,
        0xff, 0xd9, 0x4d, 0xff, 0xe8, 0xd6, 0x5b, 0xff, 0x8f, 0xd0, 0xab, 0xff, 0x1e, 0xa2, 0x58, 0xff,
        0x6a, 0xc2, 0x90, 0xff, 0x52, 0xb8, 0x7f, 0xff, 0xf8, 0xfc, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd9, 0x49, 0xff,
        0xff, 0xdf, 0x6b, 0xff, 0xff, 0xe1, 0x72, 0xfe, 0xff, 0xe2, 0x74, 0xfe, 0xff, 0xe9, 0x97, 0xff,
        0xff, 0xeb, 0xa3, 0xff, 0xdc, 0xe4, 0xaa, 0xff, 0x9c, 0xd6, 0xb6, 0xff, 0xad, 0xdd, 0xc2, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xee, 0xae, 0xff,
        0xff, 0xe3, 0x7c, 0xff, 0xff, 0xdb, 0x55, 0xff, 0xff, 0xd7, 0x42, 0xff, 0xff, 0xe1, 0x75, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xed, 0xab, 0xff, 0xff, 0xd9, 0x4b, 0xff, 0xff, 0xde, 0x62, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xfa, 0xe8, 0xff, 0xff, 0xf0, 0xba, 0xff, 0xff, 0xf9, 0xe4, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(pixels, 16, 16, 32, 16 * 4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    SDL_SetWindowIcon(window, surface);
    SDL_FreeSurface(surface);
}

static void init()
{
    mpv_get_property_async(mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    gen_placeholder(DEFAULT_SUB_FNAME);
    add_sub(DEFAULT_SUB_FNAME);
    SDL_StartTextInput();
}

static void get_subs_in_frame(Sub **sub_arr, unsigned int *len)
{
    int i = 0;
    if (sub_head)
    {
        Sub *sub_curr = sub_head;
        while (sub_curr)
        {
            if (in_frame(sub_curr))
            {
                sub_arr[i] = sub_curr;
                i++;
            }
            sub_curr = sub_curr->next;
        }
    }

    *len = i;
    sub_arr[i] = NULL;
}

static void focus_next(int seek)
{
    if (sub_focused->next)
    {
        set_sub_focus(sub_focused->next);
        if (seek)
            exact_seek_d(sub_focused->start_d, "absolute");
    }
    else
    {
        show_text("At last sub!\n", "100");
    }
}

static void focus_back(int seek)
{
    if (sub_focused == sub_head)
    {
        show_text("At first sub!\n", "100");
    }
    else
    {
        Sub *sub_curr = sub_head;
        while (sub_curr)
        {
            if (sub_curr->next == sub_focused)
            {
                set_sub_focus(sub_curr);
                if (seek)
                    exact_seek_d(sub_focused->start_d, "absolute");
                break;
            }
            sub_curr = sub_curr->next;
        }
    }
}

static void process_cmd(char *c)
{
    strcat(cmd_buf, c);
    if (cmd_buf[0] == ':')
    {
        goto end;
    }
    struct slre_cap caps[2];
    if (slre_match("^([0-9]*)([a-zA-Z\\. ]*)$", cmd_buf, strlen(cmd_buf), caps, 2) > 0)
    {
        long q = -1;
        // printf("q1: %.*s\n", caps[0].len, caps[0].ptr);
        if (caps[0].len)
        {
            q = strtol(caps[0].ptr, NULL, 10);
        }
        // printf("q = %ld\n", q);
        // printf("q2: %.*s\n", caps[1].len, caps[1].ptr);

        if (strstr(caps[1].ptr, " "))
        {
            toggle_pause();
        }
        else if (strstr(caps[1].ptr, "."))
        {
            // repeat
        }
        else if (strstr(caps[1].ptr, "a"))
        {
            Sub *sub_new = alloc_sub();
            sub_new->start_d = ts_s_d;
            sub_new->end_d = duration_d;

            insert_ordered(sub_new);

            set_sub_focus(sub_new);
            insert_mode = 1;
        }
        else if (strstr(caps[1].ptr, "s"))
        {
            frame_step();
        }
        else if (strstr(caps[1].ptr, "S"))
        {
            frame_back_step();
        }
        else if (strstr(caps[1].ptr, "i"))
        {
            Sub *sub_arr[100];
            unsigned int len;
            get_subs_in_frame(sub_arr, &len);
            if (len)
            {
                if (q == -1)
                {
                    for (int i = 0; i < len; i++)
                    {
                        if (sub_focused == sub_arr[i])
                        {
                            q = i;
                            break;
                        }
                    }
                    if (q == -1)
                        q = 0;
                }

                if (q < len)
                {
                    set_sub_focus(sub_arr[q]);
                    insert_mode = 1;
                }
                else
                {
                    show_text("Out of bounds!", "100");
                }
            }
            else
            {
                show_text("No sub in frame!", "100");
            }
        }
        else if (strstr(caps[1].ptr, "w"))
        {
            if (sub_focused)
            {
                focus_next(1);
            }
        }
        else if (strstr(caps[1].ptr, "W"))
        {
            if (sub_focused)
            {
                focus_next(0);
            }
        }
        else if (strstr(caps[1].ptr, "b"))
        {
            if (sub_focused)
            {
                focus_back(1);
            }
        }
        else if (strstr(caps[1].ptr, "B"))
        {
            if (sub_focused)
            {
                focus_back(0);
            }
        }
        else if (strstr(caps[1].ptr, "o"))
        {
            if (sub_focused)
            {
                if (q == -1)
                    q = 0;
                exact_seek_d(sub_focused->start_d - (q * STEP_MEDIUM), "absolute");
            }
        }
        else if (strstr(caps[1].ptr, "O"))
        {
            if (sub_focused)
            {
                if (q == -1)
                    q = 0;
                exact_seek_d(sub_focused->end_d - (q * STEP_MEDIUM), "absolute");
            }
        }
        else if (strstr(caps[1].ptr, "h"))
        {
            if (sub_focused)
            {
                // TODO: Write sub clone function
                Sub *sub_new = alloc_sub();

                sub_new->start_d = ts_s_d;
                sub_new->end_d = sub_focused->end_d;
                strncpy(sub_new->text, sub_focused->text, sizeof(sub_new->text));

                insert_ordered(sub_new);

                delete_sub(sub_focused);
                set_sub_focus(sub_new);
            }
        }
        else if (strstr(caps[1].ptr, "l"))
        {
            if (sub_focused)
            {
                sub_focused->end_d = ts_s_d;
                export_and_reload();
            }
        }
        else if (strstr(caps[1].ptr, "Hj"))
        {
            if (sub_focused)
            {
                if (q == -1)
                    sub_focused->start_d -= STEP_SMALL;
                else
                    sub_focused->start_d -= q * STEP_SMALL;

                export_and_reload();
            }
        }
        else if (strstr(caps[1].ptr, "Hk"))
        {
            if (sub_focused)
            {
                if (q == -1)
                    sub_focused->start_d += STEP_SMALL;
                else
                    sub_focused->start_d += q * STEP_SMALL;

                export_and_reload();
            }
        }
        else if (strstr(caps[1].ptr, "Lj"))
        {
            if (sub_focused)
            {
                if (q == -1)
                    sub_focused->end_d -= STEP_SMALL;
                else
                    sub_focused->end_d -= q * STEP_SMALL;

                export_and_reload();
            }
        }
        else if (strstr(caps[1].ptr, "Lk"))
        {
            if (sub_focused)
            {
                if (q == -1)
                    sub_focused->end_d += STEP_SMALL;
                else
                    sub_focused->end_d += q * STEP_SMALL;

                export_and_reload();
            }
        }
        else if (strstr(caps[1].ptr, "j"))
        {
            q == -1 ? exact_seek("-" STR(STEP_DEFAULT), "relative") : exact_seek_d(q * -1, "relative");
        }
        else if (strstr(caps[1].ptr, "k"))
        {
            q == -1 ? exact_seek(STR(STEP_DEFAULT), "relative") : exact_seek_d(q * 1, "relative");
        }
        else if (strstr(caps[1].ptr, "J"))
        {
            q == -1 ? exact_seek("-" STR(STEP_SMALL), "relative") : exact_seek_d(q * -STEP_SMALL, "relative");
        }
        else if (strstr(caps[1].ptr, "K"))
        {
            q == -1 ? exact_seek(STR(STEP_SMALL), "relative") : exact_seek_d(q * STEP_SMALL, "relative");
        }
        else if (strstr(caps[1].ptr, "gg"))
        {
            exact_seek("0", "absolute");
        }
        else if (strstr(caps[1].ptr, "dd"))
        {
            if (sub_focused)
            {
                set_sub_focus(delete_sub(sub_focused));
            }
        }
        else
        {
            goto end;
        }
        clear_cmd_buf();
    }
    else
    {
        // No match
        printf("Error parsing [%s]\n", cmd_buf);
        clear_cmd_buf();
    }
end:
    refresh_title();
}

static void insert_text(char *text)
{
    if (sub_focused)
    {
        if (in_frame(sub_focused))
        {
            strcat(sub_focused->text, text);
            export_and_reload();
        }
        else
        {
            show_text("Sub not in frame!", "100");
        }
    }
}

static void pop_word(char *text)
{
    int len = strlen(text);
    if (len)
    {
        while (isspace(text[len - 1]))
            len--;
        text[len] = 0;

        char *pos = ptr_max(strrchr(text, ' '), strrchr(text, '\n'));
        if (pos)
        {
            *(pos + 1) = 0;
        }
        else
        {
            text[0] = 0;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        die("pass a single media file as argument");

    mpv = mpv_create();
    if (!mpv)
        die("context init failed");

    // Some minor options can only be set before mpv_initialize().
    if (mpv_initialize(mpv) < 0)
        die("mpv init failed");

    mpv_request_log_messages(mpv, "debug");

    // Jesus Christ SDL, you suck!
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "no");

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        die("SDL init failed");

    window =
        SDL_CreateWindow("Sbubby", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         640, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
        die("failed to create SDL window");

    set_window_icon();

    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
    if (!glcontext)
        die("failed to create SDL GL context");

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &(mpv_opengl_init_params){
                                                  .get_proc_address = get_proc_address_mpv,
                                              }},
        // Tell libmpv that you will call mpv_render_context_update() on render
        // context update callbacks, and that you will _not_ block on the core
        // ever (see <libmpv/render.h> "Threading" section for what libmpv
        // functions you can call at all when this is active).
        // In particular, this means you must call e.g. mpv_command_async()
        // instead of mpv_command().
        // If you want to use synchronous calls, either make them on a separate
        // thread, or remove the option below (this will disable features like
        // DR and is not recommended anyway).
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &(int){1}},
        {0}};

    // This makes mpv use the currently set GL context. It will use the callback
    // (passed via params) to resolve GL builtin functions, as well as extensions.
    mpv_render_context *mpv_gl;
    if (mpv_render_context_create(&mpv_gl, mpv, params) < 0)
        die("failed to initialize mpv GL context");

    // We use events for thread-safe notification of the SDL main loop.
    // Generally, the wakeup callbacks (set further below) should do as least
    // work as possible, and merely wake up another thread to do actual work.
    // On SDL, waking up the mainloop is the ideal course of action. SDL's
    // SDL_PushEvent() is thread-safe, so we use that.
    wakeup_on_mpv_render_update = SDL_RegisterEvents(1);
    wakeup_on_mpv_events = SDL_RegisterEvents(1);
    if (wakeup_on_mpv_render_update == (Uint32)-1 ||
        wakeup_on_mpv_events == (Uint32)-1)
        die("could not register events");

    // When normal mpv events are available.
    mpv_set_wakeup_callback(mpv, on_mpv_events, NULL);

    // When there is a need to call mpv_render_context_update(), which can
    // request a new frame to be rendered.
    // (Separate from the normal event handling mechanism for the sake of
    //  users which run OpenGL on a different thread.)
    mpv_render_context_set_update_callback(mpv_gl, on_mpv_render_update, NULL);

    // Play this file.
    const char *cmd[] = {"loadfile", argv[1], NULL};
    mpv_command_async(mpv, 0, cmd);

    while (1)
    {
        SDL_Event event;
        if (SDL_WaitEvent(&event) != 1)
            die("event loop error");
        int redraw = 0;
        switch (event.type)
        {
        case SDL_QUIT:
            goto done;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
                redraw = 1;
            break;
        case SDL_TEXTINPUT:
            if (insert_mode)
            {
                insert_text(event.text.text);
            }
            else
            {
                process_cmd(event.text.text);
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE)
            {
                if (insert_mode)
                {
                    insert_mode = 0;
                }
                else
                {
                    clear_cmd_buf();
                }
                refresh_title();
            }
            else if (event.key.keysym.sym == SDLK_w)
            {
                if (insert_mode)
                {
                    if (sub_focused)
                    {
                        if (SDL_GetModState() & KMOD_CTRL)
                        {
                            pop_word(sub_focused->text);
                            export_and_reload();
                        }
                    }
                }
            }
            else if (event.key.keysym.sym == SDLK_BACKSPACE)
            {
                if (insert_mode)
                {
                    if (sub_focused)
                    {
                        if (SDL_GetModState() & KMOD_CTRL)
                        {
                            pop_word(sub_focused->text);
                        }
                        else
                        {
                            pop_char(sub_focused->text);
                        }
                        export_and_reload();
                    }
                }
                else
                {
                    pop_char(cmd_buf);
                    refresh_title();
                }
            }
            else if (event.key.keysym.sym == SDLK_RETURN)
            {
                if (insert_mode)
                {
                    insert_text("\n");
                }
                else
                {
                    if (process_ex() == -1)
                    {
                        goto done;
                    }
                }
            }
            break;
        default:
            // Happens when there is new work for the render thread (such as
            // rendering a new video frame or redrawing it).
            if (event.type == wakeup_on_mpv_render_update)
            {
                uint64_t flags = mpv_render_context_update(mpv_gl);
                if (flags & MPV_RENDER_UPDATE_FRAME)
                    redraw = 1;
            }
            // Happens when at least 1 new event is in the mpv event queue.
            if (event.type == wakeup_on_mpv_events)
            {
                // Handle all remaining mpv events.
                while (1)
                {
                    mpv_event *mp_event = mpv_wait_event(mpv, 0);
                    if (mp_event->event_id == MPV_EVENT_FILE_LOADED)
                    {
                        init();
                        if (argc >= 3)
                        {
                            import_sub(argv[2]);
                            if (!sub_focused && sub_head)
                                sub_focused = sub_head;
                            export_and_reload();
                        }
                    }
                    if (mp_event->event_id == MPV_EVENT_COMMAND_REPLY)
                    {
                        if (mp_event->reply_userdata == REPLY_USERDATA_SUB_RELOAD)
                        {
                            mtx_reload--;
                        }
                    }
                    if (mp_event->event_id == MPV_EVENT_GET_PROPERTY_REPLY)
                    {
                        mpv_event_property *evp = (mpv_event_property *)(mp_event->data);

                        if (!strcmp(evp->name, "time-pos"))
                        {
                            if (evp->format == MPV_FORMAT_DOUBLE)
                            {
                                double value = *(double *)(evp->data);
                                if (value)
                                {
                                    ts_s_d = value;
                                }
                            }
                        }
                        else if (!strcmp(evp->name, "duration"))
                        {
                            if (evp->format == MPV_FORMAT_DOUBLE)
                            {
                                double value = *(double *)(evp->data);
                                if (value)
                                {
                                    duration_d = value;
                                }
                            }
                        }
                    }
                    if (mp_event->event_id == MPV_EVENT_NONE)
                        break;
                    if (mp_event->event_id == MPV_EVENT_LOG_MESSAGE)
                    {
                        mpv_event_log_message *msg = mp_event->data;
                        // Print log messages about DR allocations, just to
                        // test whether it works. If there is more than 1 of
                        // these, it works. (The log message can actually change
                        // any time, so it's possible this logging stops working
                        // in the future.)
                        // if (strstr(msg->text, "DR image"))
                        // printf("log: %s", msg->text);
                        continue;
                    }
                    // printf("event: %s\n", mpv_event_name(mp_event->event_id));
                }
            }
        }
        if (redraw)
        {
            // mpv_get_property_async(mpv, 0, "playback-time", MPV_FORMAT_DOUBLE);
            mpv_get_property_async(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);

            int w, h;
            SDL_GetWindowSize(window, &w, &h);
            mpv_render_param params[] = {
                // Specify the default framebuffer (0) as target. This will
                // render onto the entire screen. If you want to show the video
                // in a smaller rectangle or apply fancy transformations, you'll
                // need to render into a separate FBO and draw it manually.
                {MPV_RENDER_PARAM_OPENGL_FBO, &(mpv_opengl_fbo){
                                                  .fbo = 0,
                                                  .w = w,
                                                  .h = h,
                                              }},
                // Flip rendering (needed due to flipped GL coordinate system).
                {MPV_RENDER_PARAM_FLIP_Y, &(int){1}},
                {0}};
            // See render_gl.h on what OpenGL environment mpv expects, and
            // other API details.
            mpv_render_context_render(mpv_gl, params);
            SDL_GL_SwapWindow(window);
        }
    }
done:

    // Destroy the GL renderer and all of the GL objects it allocated. If video
    // is still running, the video track will be deselected.
    mpv_render_context_free(mpv_gl);

    mpv_destroy(mpv);

    printf("properly terminated\n");
    return 0;
}