#include <stdio.h>

#include <SDL2/SDL.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <main.h>
#include <command.h>
#include <utils.h>
#include <subs.h>

// Extern globals

double curr_timestamp;
int sub_reload_semaphore = 0;
char *export_filename = NULL;

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

// Function to be called when file is loaded
static inline void main_init()
{
    if (export_filename != NULL)
    {
        // Attempt to import specified sub for editing
        import_sub(export_filename);
    }
    else
    {
        // Fetch the current video filename and set it as the default export filename
        export_filename = (char *)malloc(256 * sizeof(char));
        mpv_get_property_async(mpv, REPLY_USERDATA_UPDATE_FILENAME, "filename", MPV_FORMAT_STRING);
    }

    subs_init();
}

// External functions are defined below

void show_text(const char *text, const int duration)
{
    char duration_str[32];
    snprintf(duration_str, 32, "%d", duration);
    const char *cmd[] = {"show-text", text, duration_str, NULL};
    mpv_command_async(mpv, 0, cmd);
}

void set_window_title(const char *title)
{
    SDL_SetWindowTitle(window, title);
}

void toggle_fullscreen()
{
    static int isFullscreen;
    if (isFullscreen == 0)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    else
        SDL_SetWindowFullscreen(window, 0);
    isFullscreen ^= 1;
}

// Playback

void toggle_pause()
{
    const char *cmd[] = {"cycle", "pause", NULL};
    mpv_command_async(mpv, 0, cmd);
}

void frame_step()
{
    const char *cmd[] = {"frame-step", NULL};
    mpv_command_async(mpv, 0, cmd);
}

void frame_back_step()
{
    const char *cmd[] = {"frame-back-step", NULL};
    mpv_command_async(mpv, 0, cmd);
}

void seek_start()
{
    const char *cmd[] = {"seek", "0", "absolute-percent", "exact", NULL};
    mpv_command_async(mpv, 0, cmd);
}

void seek_end()
{
    const char *cmd[] = {"seek", "100", "absolute-percent", "exact", NULL};
    mpv_command_async(mpv, 0, cmd);
}

void seek_absolute(const double value)
{
    char value_str[32];
    snprintf(value_str, 32, "%f", value);
    const char *cmd[] = {"seek", value_str, "absolute", "exact", NULL};
    mpv_command_async(mpv, 0, cmd);
}

// Seek relative seconds from current position
void seek_relative(const double value)
{
    char value_str[32];
    snprintf(value_str, 32, "%f", value);
    const char *cmd[] = {"seek", value_str, "relative", "exact", NULL};
    mpv_command_async(mpv, 0, cmd);
}

// Subtitling

void sub_add(const char *filename)
{
    const char *cmd[] = {"sub-add", filename, NULL};
    mpv_command_async(mpv, 0, cmd);
}

// Internal function to call reload a second time
static void sub_reload2()
{
    sub_reload_semaphore++;
    const char *cmd[] = {"sub-reload", NULL};
    mpv_command_async(mpv, REPLY_USERDATA_SUB_RELOAD2, cmd);
}

void sub_reload()
{
    sub_reload_semaphore++;
    const char *cmd[] = {"sub-reload", NULL};
    mpv_command_async(mpv, REPLY_USERDATA_SUB_RELOAD, cmd);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        die("Usage: sbubby video.mp4 [sub.srt]");
    if (argc > 2)
        export_filename = argv[2];

    const char *video_fname = argv[1];

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
                         WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
        die("failed to create SDL window");

    set_window_icon(window);

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

    // Loop the video
    const char *cmd_loop[] = {"set", "loop", "inf", NULL};
    mpv_command_async(mpv, 0, cmd_loop);

    // Play this file.
    const char *cmd[] = {"loadfile", video_fname, NULL};
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
            // Continuous text input
            handle_text_input(event.text.text);
            break;
        case SDL_KEYDOWN:
            // Single keypresses
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                handle_escape();
                break;
            case SDLK_w:
                if (SDL_GetModState() & KMOD_CTRL)
                    handle_ctrl_backspace();
                break;
            case SDLK_BACKSPACE:
                if (SDL_GetModState() & KMOD_CTRL)
                {
                    handle_ctrl_backspace();
                    break;
                }
                handle_backspace();
                break;
            case SDLK_RETURN:
                handle_return();
                break;
            case SDLK_p:
                // Universal pause shortcut
                if (SDL_GetModState() & KMOD_CTRL)
                    toggle_pause();
                break;
            case SDLK_c:
                if (SDL_GetModState() & KMOD_CTRL)
                    handle_ctrl_c();
                break;

            default:
                break;
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
                        main_init();
                    }
                    if (mp_event->event_id == MPV_EVENT_COMMAND_REPLY)
                    {
                        if (mp_event->reply_userdata == REPLY_USERDATA_SUB_RELOAD)
                        {
                            sub_reload2();
                            sub_reload_semaphore--;
                        }
                        else if (mp_event->reply_userdata == REPLY_USERDATA_SUB_RELOAD2)
                        {
                            sub_reload_semaphore--;
                        }
                    }
                    if (mp_event->event_id == MPV_EVENT_GET_PROPERTY_REPLY)
                    {
                        mpv_event_property *evp = (mpv_event_property *)(mp_event->data);

                        if (mp_event->reply_userdata == REPLY_USERDATA_UPDATE_TIMESTAMP)
                        {
                            curr_timestamp = *(double *)(evp->data);
                        }
                        else if (mp_event->reply_userdata == REPLY_USERDATA_UPDATE_FILENAME)
                        {
                            snprintf(export_filename, 256, "%s.srt", *(char **)(evp->data));
                        }
                    }
                    if (mp_event->event_id == MPV_EVENT_NONE)
                        break;
                    if (mp_event->event_id == MPV_EVENT_LOG_MESSAGE)
                    {
                        // mpv_event_log_message *msg = mp_event->data;
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
            // Get timestamp every frame
            mpv_get_property_async(mpv, REPLY_USERDATA_UPDATE_TIMESTAMP, "time-pos", MPV_FORMAT_DOUBLE);

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
