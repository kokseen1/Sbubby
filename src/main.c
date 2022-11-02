#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <slre.h>

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 360

#define DEFAULT_UNIT_j -1
#define DEFAULT_UNIT_k 1
#define DEFAULT_COUNT_jk 3

#define DEFAULT_UNIT_J -0.1
#define DEFAULT_UNIT_K 0.1
#define DEFAULT_COUNT_JK 1

// Global command buffer
static char cmd_buf[32] = {0};

// Filename of the current video
static char *video_fname = NULL;

// 0 NORMAL
// 1 INSERT
static int curr_mode = 0;

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
    const char *cmd[] = {"cycle", "pause", NULL};
    mpv_command_async(mpv, 0, cmd);
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

static void seek_start()
{
    const char *cmd[] = {"seek", "0", "absolute-percent", "exact", NULL};
    mpv_command_async(mpv, 0, cmd);
}

static void seek_end()
{
    const char *cmd[] = {"seek", "100", "absolute-percent", "exact", NULL};
    mpv_command_async(mpv, 0, cmd);
}

// Seek relative seconds from current position
static void seek_relative(double value)
{
    char value_str[32];
    snprintf(value_str, 32, "%f", value);
    const char *cmd[] = {"seek", value_str, "relative", "exact", NULL};
    mpv_command_async(mpv, 0, cmd);
}

inline static void set_window_icon()
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

void set_window_title(const char *text)
{
    char title[256] = {0};

    switch (curr_mode)
    {
    case 0:
        strncat(title, "NORMAL ", sizeof(title) - strlen(title) - 1);
        break;
    case 1:
        strncat(title, "INSERT ", sizeof(title) - strlen(title) - 1);
        break;
    default:
        break;
    }

    strncat(title, text, sizeof(title) - strlen(title) - 1);
    SDL_SetWindowTitle(window, title);
}

// Pop the last char from a string
static void str_pop(char *str)
{
    size_t len = strlen(str);
    if (len > 0)
    {
        str[len - 1] = '\0';
    }
}

void clear_cmd_buf()
{
    cmd_buf[0] = 0;
}

// Parse commands starting with :
void parse_ex(const char *cmd_raw)
{
    struct slre_cap caps[1];
    if (slre_match("^:([a-zA-Z_0-9]*)$", cmd_raw, strlen(cmd_raw), caps, 1) > 0)
    {
        const char *cmd = caps[0].ptr;
        // int cmd_len = caps[0].len;

        if (strcmp(cmd, "wq") == 0)
        {
            printf("Save and quit\n");
        }
        else if (strcmp(cmd, "q") == 0)
        {
            printf("Quit\n");
        }
    }
}

// Parse a NORMAL mode command
// Returns 0 if command was parsed successfully
// Returns 1 if expecting more commands
int parse_cmd(const char *cmd)
{
    struct slre_cap caps[2];
    if (slre_match("^([0-9]*)([a-zA-Z]*)$", cmd, strlen(cmd), caps, 2) > 0)
    {
        long count = -1;
        const char *action = caps[1].ptr;
        int action_len = caps[1].len;

        if (action[0] == '\0')
        {
            // Empty action, wait for more
            return 1;
        }

        if (caps[0].len > 0)
        {
            // Get count value
            count = strtol(caps[0].ptr, NULL, 10);
        }

        // Parse first character
        switch (action[0])
        {
        case 'j':
            if (count == -1)
                count = DEFAULT_COUNT_jk;
            seek_relative(count * DEFAULT_UNIT_j);
            return 0;

        case 'k':
            if (count == -1)
                count = DEFAULT_COUNT_jk;
            seek_relative(count * DEFAULT_UNIT_k);
            return 0;

        case 'J':
            if (count == -1)
                count = DEFAULT_COUNT_JK;
            seek_relative(count * DEFAULT_UNIT_J);
            return 0;

        case 'K':
            if (count == -1)
                count = DEFAULT_COUNT_JK;
            seek_relative(count * DEFAULT_UNIT_K);
            return 0;

        case 'h':
            frame_back_step();
            return 0;

        case 'l':
            frame_step();
            return 0;

        case 'g':
            // Check bounds
            if (action_len < 2)
                return 1;

            switch (action[1])
            {
            case 'g':
                seek_start();
                return 0;

            default:
                break;
            }

        case 'G':
            seek_end();
            return 0;

        case 'i':
            // Enter INSERT mode
            curr_mode = 1;
            set_window_title("");
            break;

        default:
            break;
        }
    }

    // Invalid action, clear buffer
    return 0;
}

// Concat input character to command buffer and parse
void handle_text_input(const char *text)
{
    strncat(cmd_buf, text, sizeof(cmd_buf) - strlen(cmd_buf) - 1);

    if (cmd_buf[0] == ':')
    {
        goto end;
    }

    if (text[0] == ' ')
    {
        toggle_pause();
        goto clear;
    }

    if (parse_cmd(cmd_buf) == 0)
    {
    clear:
        // Clear buffer
        clear_cmd_buf();
    }

end:
    set_window_title(cmd_buf);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        die("Usage: sbubby video.mp4 [sub.srt]");
    video_fname = argv[1];

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
            if (curr_mode == 0)
            {
                handle_text_input(event.text.text);
            }
            else if (curr_mode == 1)
            {
            }
            break;
        case SDL_KEYDOWN:
            // Single keypresses
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                if (curr_mode == 0)
                {
                    clear_cmd_buf();
                    set_window_title("");
                }
                else if (curr_mode == 1)
                {
                    curr_mode = 0;
                    set_window_title("");
                }
                break;
            case SDLK_BACKSPACE:
                str_pop(cmd_buf);
                set_window_title(cmd_buf);
                break;
            case SDLK_RETURN:
                parse_ex(cmd_buf);
                clear_cmd_buf();
                set_window_title("");
                break;

            default:
                break;
            }
            // else if (event.key.keysym.sym == SDLK_w)
            // {
            // }
            // else if (event.key.keysym.sym == SDLK_RETURN)
            // {
            // }
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
                        if (argc >= 3)
                        {
                        }
                    }
                    if (mp_event->event_id == MPV_EVENT_COMMAND_REPLY)
                    {
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
                                }
                            }
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
