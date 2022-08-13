// Build with: gcc -o main main.c `pkg-config --libs --cflags mpv sdl2` -std=c99

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <slre.h>

#include <SDL2/SDL.h>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#define REPLY_USERDATA_SUB_RELOAD 8000
#define CMD_BUF_MAX 1024
#define SMALL_BUF_MAX 32
#define SUB_FNAME "out.srt"

typedef struct Sub
{
    char start[SMALL_BUF_MAX];
    char end[SMALL_BUF_MAX];
    char text[CMD_BUF_MAX];
    struct Sub *next;
} Sub;

static char ts_hhmmss[SMALL_BUF_MAX];
static char ts_s[SMALL_BUF_MAX];
static char duration[SMALL_BUF_MAX];

static char cmd_buf[CMD_BUF_MAX];

static int mtx_reload = 0;
static int insert_mode = 0;

static char *main_sub_fname = NULL;
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

static void toggle_pause()
{
    const char *cmd_pause[] = {"cycle", "pause", NULL};
    mpv_command_async(mpv, 0, cmd_pause);
}

static void exact_seek(double quantifier, char *flag)
{
    char target[SMALL_BUF_MAX];
    snprintf(target, sizeof(target), "%f", quantifier);
    const char *cmd_seek[] = {"seek", target, flag, "exact", NULL};
    mpv_command_async(mpv, 0, cmd_seek);
}

void get_full_ts(char *ts_full)
{
    // TODO (trivial): use comma as separator
    char *pos = strchr(ts_s, '.');
    if (pos)
    {
        strncpy(ts_full, ts_hhmmss, SMALL_BUF_MAX);
        strncat(ts_full, pos, 4);
        // snprintf(&ts_full[strlen(ts_full)], 5, ",%s", pos + 1);
    }
}

static void refresh_title()
{
    char *title;
    if (insert_mode)
    {
        char mode[CMD_BUF_MAX] = "INSERT ";
        title = mode;
    }
    else
    {
        char mode[CMD_BUF_MAX] = "NORMAL ";
        title = mode;
    }
    strcat(title, cmd_buf);
    SDL_SetWindowTitle(window, title);
}

void sub_reload()
{
    // printf("Reloading\n");
    const char *cmd[] = {"sub-reload", NULL};
    mtx_reload++;
    mpv_command_async(mpv, REPLY_USERDATA_SUB_RELOAD, cmd);
}

static void clear_cmd_buf()
{
    cmd_buf[0] = 0;
}

static void pop_cmd_buf()
{
    int buf_len = strlen(cmd_buf);
    if (buf_len)
    {
        cmd_buf[buf_len - 1] = 0;
    }
}

void sub_add(char *fname)
{
    if (!main_sub_fname)
    {
        const char *cmd[] = {"sub-add", fname, NULL};
        mpv_command_async(mpv, 0, cmd);
        main_sub_fname = fname;
    }
}
static void process_ex()
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
                    printf("quit\n");
                }
            }
        }
        else if (slre_match("^([0-9]*)$", ex_buf, strlen(ex_buf), caps, 1) > 0)
        {
            if (caps[0].len)
            {
                long quantifier = strtol(caps[0].ptr, NULL, 10);
                exact_seek(quantifier, "absolute");
            }
        }
    }
    clear_cmd_buf();
    refresh_title();
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
        long quantifier = 1L;
        // printf("q1: %.*s\n", caps[0].len, caps[0].ptr);
        if (caps[0].len)
        {
            quantifier = strtol(caps[0].ptr, NULL, 10);
        }
        // printf("%ld\n", quantifier);
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
            Sub *sub_new = (Sub *)calloc(1, sizeof(Sub));
            get_full_ts(sub_new->start);
            sprintf(sub_new->end, "%s.000", duration);
            // strcpy(sub_new->end, duration);
            if (!sub_head)
            {
                // First item
                sub_head = sub_new;
                sub_head->next = NULL;
            }
            else
            {
                // Ordered insert
            }
            sub_focused = sub_new;
            printf("[NEW SUB] %s\n", sub_new->start);
            insert_mode = 1;
        }
        else if (strstr(caps[1].ptr, "s"))
        {
            sub_reload();
        }
        else if (strstr(caps[1].ptr, "i"))
        {
            insert_mode = 1;
        }
        else if (strstr(caps[1].ptr, "j"))
        {
            exact_seek(quantifier * -1, "relative");
        }
        else if (strstr(caps[1].ptr, "k"))
        {
            exact_seek(quantifier, "relative");
        }
        else if (strstr(caps[1].ptr, "J"))
        {
            exact_seek(quantifier * -0.1, "relative");
        }
        else if (strstr(caps[1].ptr, "K"))
        {
            exact_seek(quantifier * 0.1, "relative");
        }
        else if (strstr(caps[1].ptr, "gg"))
        {
            exact_seek(0.0, "absolute");
        }
        else if (strstr(caps[1].ptr, "dd"))
        {
            printf("delete\n");
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

void export_sub()
{
    if (mtx_reload)
    {
        // Don't touch file if reload is already in progress
        return;
    }

    int i = 1;
    Sub *sub_curr = sub_head;

    FILE *pFile = fopen(SUB_FNAME, "w");
    while (sub_curr)
    {
        fprintf(pFile, "%d\n", i);
        fprintf(pFile, "%s --> %s\n", sub_curr->start, sub_curr->end);
        fprintf(pFile, "%s\n\n", sub_curr->text);

        sub_curr = sub_curr->next;
        i++;
    }

    fclose(pFile);
}

void insert_text(char *text)
{
    if (sub_focused)
    {
        strcat(sub_focused->text, text);
        export_sub();
        sub_reload();
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
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
        SDL_CreateWindow("hi", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         640, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
        die("failed to create SDL window");

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
            else if (event.key.keysym.sym == SDLK_BACKSPACE)
            {
                if (insert_mode)
                {
                    // Backspace (pop) sub char
                }
                else
                {
                    pop_cmd_buf();
                    refresh_title();
                }
            }
            else if (event.key.keysym.sym == SDLK_RETURN)
            {
                if (insert_mode)
                {
                    // Sub insert newline (\n)
                }
                else
                {
                    process_ex();
                }
            }
            break;
        // if (event.key.keysym.sym == SDLK_s)
        // {
        //     // Also requires MPV_RENDER_PARAM_ADVANCED_CONTROL if you want
        //     // screenshots to be rendered on GPU (like --vo=gpu would do).
        //     const char *cmd_scr[] = {"screenshot-to-file",
        //                              "screenshot.png",
        //                              "window",
        //                              NULL};
        //     printf("attempting to save screenshot to %s\n", cmd_scr[1]);
        //     mpv_command_async(mpv, 0, cmd_scr);
        // }
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
                        mpv_get_property_async(mpv, 0, "duration", MPV_FORMAT_OSD_STRING);
                        sub_add(SUB_FNAME);
                        SDL_StartTextInput();
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
                            if (evp->format == MPV_FORMAT_OSD_STRING)
                            {
                                char *value = *(char **)(evp->data);
                                if (value)
                                {
                                    strncpy(ts_hhmmss, value, sizeof(ts_hhmmss));
                                }
                            }
                            else if (evp->format == MPV_FORMAT_STRING)
                            {
                                char *value = *(char **)(evp->data);
                                if (value)
                                {
                                    strncpy(ts_s, value, sizeof(ts_s));
                                }
                            }
                        }
                        else if (!strcmp(evp->name, "duration"))
                        {
                            if (evp->format == MPV_FORMAT_OSD_STRING)
                            {
                                char *value = *(char **)(evp->data);
                                if (value)
                                {
                                    strncpy(duration, value, sizeof(duration));
                                    printf("Video length: %s\n", value);
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
            // printf("ts_s: %s\n", ts_s);
            // printf("ts_hhmmss: %s\n", ts_hhmmss);

            // mpv_get_property_async(mpv, 0, "playback-time", MPV_FORMAT_STRING);
            mpv_get_property_async(mpv, 0, "time-pos", MPV_FORMAT_STRING);
            mpv_get_property_async(mpv, 0, "time-pos", MPV_FORMAT_OSD_STRING);

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