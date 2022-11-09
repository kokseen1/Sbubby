#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <subs.h>
#include <utils.h>
#include <main.h>
#include <slre.h>

static Sub *sub_head = NULL;
static Sub *sub_focused = NULL;
static int cursor_pos = -1;

// Internal function to allocate and prepare a sub node
static inline Sub *alloc_sub()
{
    Sub *sub = (Sub *)malloc(sizeof(Sub));
    // Clear text buffer
    sub->text[0] = '\0';
    return sub;
}

// Internal function to insert into linked list in order
static void insert_ordered(Sub *sub_new)
{
    if (sub_head == NULL)
    {
        // First node
        sub_head = sub_new;
        sub_head->next = NULL;
        return;
    }

    // Timestamp is earlier than head
    if (sub_new->start_ts < sub_head->start_ts)
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
            if (!sub_curr->next || sub_curr->next->start_ts > sub_new->start_ts)
            {
                sub_new->next = sub_curr->next;
                sub_curr->next = sub_new;
                return;
            }
            sub_curr = sub_curr->next;
        }
    }
}

// Parse a srt file and populate the current sub linked list
void import_sub(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
    {
        // File does not exist, will be created later when exporting
        return;
    }

    char buf[256] = {0};
    char pend_buf[256] = {0};

    Sub *curr_sub = NULL;

    while (fgets(buf, sizeof(buf), fp) != NULL)
    {
        struct slre_cap caps[2];

        // Match index
        if (slre_match("^([0-9]+)\n$", buf, strlen(buf), caps, 1) > 0)
        {
            if (caps[0].len > 0)
            {
                if (pend_buf[0] && curr_sub != NULL)
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
                {
                    insert_ordered(curr_sub);
                }

                curr_sub = alloc_sub();

                char start[256] = {0};
                char end[256] = {0};

                strncpy(start, caps[0].ptr, caps[0].len);
                strncpy(end, caps[1].ptr, caps[1].len);

                curr_sub->start_ts = str_to_timestamp(start);
                curr_sub->end_ts = str_to_timestamp(end);

                if (pend_buf[0])
                    pend_buf[0] = 0;
            }
        }
        else if (slre_match("^([^\n].+)", buf, strlen(buf), caps, 1) > 0)
        {
            if (pend_buf[0])
            {
                if (curr_sub != NULL)
                {
                    strcat(curr_sub->text, pend_buf);
                }
                pend_buf[0] = 0;
            }

            if (curr_sub != NULL && caps[0].len > 0)
            {
                strncat(curr_sub->text, caps[0].ptr, caps[0].len);
            }
        }
    }

    if (curr_sub)
    {
        insert_ordered(curr_sub);
    }

    // Set focus to the first sub
    sub_focused = sub_head;

    fclose(fp);
}
// Export the current subtitles to a file
void export_sub(const char *filename, int highlight)
{
    // Sub is reloading
    if (sub_reload_semaphore != 0)
        return;

    if (sub_head == NULL)
    {
        // Write dummy sub for mpv to parse
        FILE *fp = fopen(filename, "w");
        fprintf(fp, SUB_PLACEHOLDER);
        fclose(fp);
        return;
    }

    int idx = 1;
    Sub *sub_curr = sub_head;

    FILE *fp = fopen(filename, "w");

    // Traverse the linked list and write one by one
    while (sub_curr)
    {
        char start_ts_str[16];
        char end_ts_str[16];

        timetamp_to_str(sub_curr->start_ts, start_ts_str);
        timetamp_to_str(sub_curr->end_ts, end_ts_str);

        fprintf(fp, "%d\n", idx);
        fprintf(fp, "%s --> %s\n", start_ts_str, end_ts_str);

        if (highlight && sub_curr == sub_focused)
        {
            if (cursor_pos != -1)
            {
                // Insert cursor
                fprintf(fp, "<font color=lightgreen>%.*s<font color=yellow>|</font>%s</font>\n\n", cursor_pos, sub_curr->text, &sub_curr->text[cursor_pos]);
            }
            else
            { // Highlight focused sub when editing
                fprintf(fp, "<font color=lightgreen>%s</font>\n\n", sub_curr->text);
            }
        }
        else
        {
            fprintf(fp, "%s\n\n", sub_curr->text);
        }

        sub_curr = sub_curr->next;
        idx++;
    }

    fclose(fp);
}

static inline int sub_in_frame(const Sub *sub, double timestamp)
{
    return sub->start_ts <= timestamp && sub->end_ts > curr_timestamp;
}

// Fill an array with subs in the given timestamp
static int get_subs_in_frame(Sub *sub_arr[], int sz, double timestamp)
{
    int idx = 0;
    Sub *curr_sub = sub_head;

    while (curr_sub)
    {
        if (sub_in_frame(curr_sub, timestamp))
        {
            if (idx >= sz)
            {
                // Not enough space in array
                break;
            }
            sub_arr[idx] = curr_sub;
            idx++;
        }
        curr_sub = curr_sub->next;
    }

    // Return number of matches
    return idx;
}

int focused_in_frame()
{
    if (sub_focused == NULL)
        return 0;
    return sub_in_frame(sub_focused, curr_timestamp);
}

// Change focus to a specified sub in the current frame
void focus_sub_in_frame(int idx)
{
    if (sub_focused == NULL)
        return;

    Sub *sub_arr[32];
    int sz = get_subs_in_frame(sub_arr, 32, curr_timestamp);

    // Handle default behaviour
    if (idx == -1)
    {
        if (sub_in_frame(sub_focused, curr_timestamp))
        {
            // Do not change focus if focused sub is in frame
            return;
        }

        if (sz > 0)
        {
            // Focus first sub by default
            sub_focused = sub_arr[0];
        }
    }
    // Specified index in range
    else if (sz > idx)
    {
        // Focus specified index
        sub_focused = sub_arr[idx];
    }
    // Out of range
    else
    {
        return;
    }
}

void set_focused_start_ts(double ts)
{
    if (sub_focused == NULL)
        return;
    if (ts > sub_focused->end_ts)
    {
        show_text("Start cannot be after end!", 300);
        return;
    }
    sub_focused->start_ts = ts;
    export_reload_sub();
}

void set_focused_end_ts(double ts)
{
    if (sub_focused == NULL)
        return;
    if (ts < sub_focused->start_ts)
    {
        show_text("End cannot be before start!", 300);
        return;
    }
    sub_focused->end_ts = ts;
    export_reload_sub();
}

static void seek_focused_start()
{
    if (sub_focused == NULL)
        return;
    seek_absolute(sub_focused->start_ts);
}

void seek_focused_end()
{
    if (sub_focused == NULL)
        return;
    // Hack: seek to a little before the end timestamp to show the sub on screen
    seek_absolute(sub_focused->end_ts - 0.08);
}

// Shift focus to the next sub by count
// Returns 0 if focus was changed
int focus_next_sub(int count)
{
    if (sub_focused == NULL)
        return 1;

    Sub *old = sub_focused;

    for (int i = 0; i < count; i++)
    {
        if (sub_focused->next == NULL)
        {
            show_text("At last sub!", 100);
            break;
        }
        sub_focused = sub_focused->next;
    }

    return sub_focused == old;
}

void next_sub(int count)
{
    if (focus_next_sub(count) == 0)
    {
        seek_focused_start();
        export_reload_sub();
    }
}

void back_sub(int count)
{
    if (sub_focused == NULL)
        return;

    if (!dbl_eq(curr_timestamp, sub_focused->start_ts))
    {
        count--;
    }

    int ret = focus_prev_sub(count);
    if (ret == 0)
        export_reload_sub();

    seek_focused_start();

    // // Bug: reloading sub too quickly after seeking will cause sub to disappear
    // // Hack: re-export subs to reload after a short delay
    if (ret == 0)
        export_reload_sub();
}

// Shift focus to the previous sub by count
// Returns 0 if focus was changed
int focus_prev_sub(int count)
{
    if (sub_focused == NULL)
        return 1;

    Sub *old = sub_focused;

    for (int i = 0; i < count; i++)
    {
        if (sub_focused == sub_head)
        {
            show_text("At first sub!", 100);
            break;
        }

        // Inefficient
        Sub *sub_curr = sub_head;
        while (sub_curr)
        {
            if (sub_curr->next == sub_focused)
            {
                sub_focused = sub_curr;
                break;
            }
            sub_curr = sub_curr->next;
        }
    }

    return sub_focused == old;
}

// Helper function to export temp sub and reload
void export_reload_sub()
{
    export_sub(SUB_FILENAME_TMP, 1);
    sub_reload();
}

// Create a new sub at timestamp
void new_sub(const double ts)
{
    Sub *sub = alloc_sub();
    sub->start_ts = ts;
    sub->end_ts = ts + 30;

    insert_ordered(sub);
    sub_focused = sub;
}

// Delete and free currently focused sub and focus nearest sub
void delete_focused_sub()
{
    if (sub_focused == NULL)
        return;
    if (sub_head == NULL)
        return;

    Sub *prev_sub = NULL;
    Sub *curr_sub = sub_head;

    while (curr_sub)
    {
        if (curr_sub == sub_focused)
        {
            // Target is head
            if (prev_sub == NULL)
            {
                // New head
                sub_head = curr_sub->next;
                sub_focused = sub_head;
                break;
            }
            // Target is not head
            prev_sub->next = curr_sub->next;
            sub_focused = prev_sub;
            break;
        }
        // Iterate while tracking current and previous nodes
        prev_sub = curr_sub;
        curr_sub = curr_sub->next;
    }

    free(curr_sub);
}

// Initialize and load temp sub for displaying
void subs_init()
{
    export_sub(SUB_FILENAME_TMP, 1);
    sub_add(SUB_FILENAME_TMP);
}

// Pop the char after the cursor
void sub_delete_char()
{
    if (sub_focused == NULL)
        return;

    if (pop_char_at_idx(sub_focused->text, cursor_pos) == 0)
    {
        // Cursor position does not move
        export_reload_sub();
    }
}

// Pop the char before the cursor
void sub_backspace_char()
{
    if (sub_focused == NULL)
        return;

    if (pop_char_at_idx(sub_focused->text, cursor_pos - 1) == 0)
    {
        cursor_pos--;
        export_reload_sub();
    }
}

void sub_delete_word()
{
    if (sub_focused == NULL)
        return;

    char *end = get_next_word(sub_focused->text, cursor_pos);
    size_t sz = end - sub_focused->text - cursor_pos;
    if (pop_range(sub_focused->text + cursor_pos, sz) == 0)
    {
        // Cursor position does not change
        export_reload_sub();
    }
}

void sub_backspace_word()
{
    if (sub_focused == NULL)
        return;

    char *start = get_prev_word(sub_focused->text, cursor_pos - 1);
    size_t sz = cursor_pos - (start - sub_focused->text);
    if (pop_range(start, sz) == 0)
    {
        cursor_pos -= sz;
        export_reload_sub();
    }
}

// Concat text onto the currently focused sub
void sub_insert_text(const char *text)
{
    if (sub_focused == NULL)
    {
        show_text("No sub focused!", 100);
        return;
    }

    size_t len_text = strlen(text);
    size_t len_sub = strlen(sub_focused->text);

    // Not enough space
    if (len_text + len_sub >= sizeof(sub_focused->text))
        return;

    // Bytes to copy, including null terminator
    size_t sz = len_sub - cursor_pos + 1;

    // Shift current string at cursor position strlen(text) characters forward
    memmove(sub_focused->text + cursor_pos + len_text, sub_focused->text + cursor_pos, sz);

    // Insert text at cursor position
    strncpy(sub_focused->text + cursor_pos, text, len_text);

    // char tmp[sizeof(sub_focused->text)];
    // sprintf(tmp, "%.*s%s%s", cursor_pos, sub_focused->text, text, &sub_focused->text[cursor_pos]);
    // strncpy(sub_focused->text, tmp, sizeof(sub_focused->text));

    // Shift cursor position with text
    cursor_pos += len_text;
    export_reload_sub();
}

void cursor_prev_word()
{
    if (sub_focused == NULL)
        return;
    if (cursor_pos == 0)
        return;
    char *start = get_prev_word(sub_focused->text, cursor_pos - 1);
    cursor_pos = start - sub_focused->text;
    export_reload_sub();
}

void cursor_next_word()
{
    if (sub_focused == NULL)
        return;
    if (cursor_pos == strlen(sub_focused->text))
        return;
    char *end = get_next_word(sub_focused->text, cursor_pos);
    cursor_pos = end - sub_focused->text;
    export_reload_sub();
}

void cursor_left()
{
    if (cursor_pos == 0)
        return;
    cursor_pos--;
    export_reload_sub();
}

void cursor_right()
{
    if (sub_focused == NULL)
        return;
    if (cursor_pos == strlen(sub_focused->text))
        return;

    cursor_pos++;
    export_reload_sub();
}

void unset_cursor()
{
    cursor_pos = -1;
}

void set_cursor_start()
{
    cursor_pos = 0;
}

void set_cursor_end()
{
    if (sub_focused == NULL)
        return;
    cursor_pos = strlen(sub_focused->text);
}
