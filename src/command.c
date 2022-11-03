#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <slre.h>

#include <command.h>
#include <utils.h>
#include <main.h>
#include <subs.h>

// Global command buffer
static char cmd_buf[128];

static int curr_mode = MODE_NORMAL;

static void set_title(const char *text)
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
    set_window_title(title);
}

static void set_mode(int mode)
{
    curr_mode = mode;
    set_title("");
}

static void clear_cmd_buf()
{
    cmd_buf[0] = 0;
}

// Parse commands starting with :
static void parse_ex(const char *cmd_raw)
{
    struct slre_cap caps[1];
    if (slre_match("^:([a-zA-Z_0-9]*)$", cmd_raw, strlen(cmd_raw), caps, 1) > 0)
    {
        const char *cmd = caps[0].ptr;
        // int cmd_len = caps[0].len;

        if (strcmp(cmd, "wq") == 0)
        {
            printf("Save and quit\n");
            exit(0);
        }
        else if (strcmp(cmd, "q") == 0)
        {
            printf("Quit\n");
            exit(0);
        }
    }
}

// Parse a NORMAL mode command
// Returns 0 if command was parsed successfully
// Returns 1 if expecting more commands
static int parse_normal_cmd(const char *cmd)
{
    // printf("ts %f\n", curr_timestamp);
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
            set_mode(MODE_INSERT);
            break;

        case 'a':
            // New sub at current time
            new_sub(curr_timestamp);
            set_mode(MODE_INSERT);
            break;

        default:
            break;
        }
    }

    // Invalid action, clear buffer
    return 0;
}

// Process keypresses based on mode
void handle_text_input(const char *text)
{
    switch (curr_mode)
    {
    case MODE_NORMAL:
        // Concat input text to command buffer and parse
        strncat(cmd_buf, text, sizeof(cmd_buf) - strlen(cmd_buf) - 1);

        // Do not automatically parse in Ex mode
        if (cmd_buf[0] == ':')
        {
            goto end;
        }

        // Pausing takes precedence over other commands
        if (text[0] == ' ')
        {
            toggle_pause();
            goto clear;
        }

        // Parse the command buffer
        if (parse_normal_cmd(cmd_buf) == 0)
        {
        clear:
            clear_cmd_buf();
        }

    end:
        set_title(cmd_buf);
        break;

    case MODE_INSERT:
        // Edit sub text
        sub_insert_text(text);
        break;
    }
}

// Handle escape keypress
void handle_escape()
{
    switch (curr_mode)
    {
    case MODE_NORMAL:
        clear_cmd_buf();
        break;

    case MODE_INSERT:
        // Exit insert mode
        set_mode(MODE_NORMAL);
        break;
    }

    set_title("");
}

// Handle enter keypress
void handle_return()
{
    switch (curr_mode)
    {
    case MODE_NORMAL:
        // Parse command buffer as Ex command
        parse_ex(cmd_buf);
        clear_cmd_buf();
        set_title("");
        break;

    case MODE_INSERT:
        // Newline
        break;
    }
}

// Handle backspace keypress
void handle_backspace()
{
    switch (curr_mode)
    {
    case MODE_NORMAL:
        pop_char(cmd_buf);
        set_title(cmd_buf);
        break;

    case MODE_INSERT:
        break;
    }
}
