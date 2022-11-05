#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <subs.h>
#include <utils.h>
#include <main.h>

static Sub *sub_head = NULL;
static Sub *sub_focused = NULL;

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

// Export the current subtitles to a file
static void export_sub(const char *filename, int highlight)
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

        // Highlight focused sub when editing
        if (highlight && sub_curr == sub_focused)
        {
            fprintf(fp, "<font color=lightgreen>%s</font>\n\n", sub_curr->text);
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

// Internal function to allocate and prepare a sub node
static inline Sub *alloc_sub()
{
    Sub *sub = (Sub *)malloc(sizeof(Sub));
    // Clear text buffer
    sub->text[0] = '\0';
    return sub;
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

    export_reload_sub();
}

void set_focused_start_ts(double ts)
{
    if (sub_focused == NULL)
        return;
    if (ts > sub_focused->end_ts)
        return;
    sub_focused->start_ts = ts;
    export_reload_sub();
}

void set_focused_end_ts(double ts)
{
    if (sub_focused == NULL)
        return;
    if (ts < sub_focused->start_ts)
        return;
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

// Function to be called when file is loaded
void subs_init()
{
    export_sub(SUB_FILENAME_TMP, 1);
    sub_add(SUB_FILENAME_TMP);
}

void sub_pop_char()
{
    if (sub_focused == NULL)
        return;
    pop_char(sub_focused->text);
    export_reload_sub();
}

void sub_pop_word()
{
    if (sub_focused == NULL)
        return;
    pop_word(sub_focused->text);
    export_reload_sub();
}

// Concat text onto the currently focused sub
void sub_insert_text(const char *text)
{
    if (sub_focused == NULL)
    {
        show_text("No sub focused!", 100);
        return;
    }

    strncat(sub_focused->text, text, sizeof(sub_focused->text) - strlen(sub_focused->text) - 1);
    export_reload_sub();
}
