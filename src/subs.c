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

// Internal function to allocate and prepare a sub node
static inline Sub *alloc_sub()
{
    Sub *sub = (Sub *)malloc(sizeof(Sub));

    // Clear text buffer
    sub->text[0] = '\0';

    return sub;
}

// Sets focused sub and reloads
// Does not check if sub is valid
static void set_focus(Sub *sub)
{
    sub_focused = sub;
    export_sub(SUB_FILENAME_TMP, 1);
    sub_reload();
}

void set_focused_start_ts(double ts)
{
    if (sub_focused == NULL)
        return;
    sub_focused->start_ts = ts;
    export_sub(SUB_FILENAME_TMP, 1);
    sub_reload();
}

void set_focused_end_ts(double ts)
{
    if (sub_focused == NULL)
        return;
    sub_focused->end_ts = ts;
    export_sub(SUB_FILENAME_TMP, 1);
    sub_reload();
}

void seek_focused_start()
{
    if (sub_focused == NULL)
        return;
    seek_absolute(sub_focused->start_ts);
}

void seek_focused_end()
{
    if (sub_focused == NULL)
        return;
    seek_absolute(sub_focused->end_ts);
}

void back_sub(int count)
{
    if (sub_focused == NULL)
        return;

    if (curr_timestamp != sub_focused->start_ts)
    {
        seek_absolute(sub_focused->start_ts);
        count--;
    }

    focus_prev_sub(count);
    seek_focused_start();
}

void focus_prev_sub(int count)
{
    if (sub_focused == NULL)
        return;

    for (int i = 0; i < count; i++)
    {
        if (sub_focused == sub_head)
        {
            show_text("At first sub!", 100);
            return;
        }

        Sub *sub_curr = sub_head;
        while (sub_curr)
        {
            if (sub_curr->next == sub_focused)
            {
                set_focus(sub_curr);
                break;
            }
            sub_curr = sub_curr->next;
        }
    }
}

void focus_next_sub(int count)
{
    if (sub_focused == NULL)
        return;

    for (int i = 0; i < count; i++)
    {
        if (sub_focused->next == NULL)
        {
            show_text("At last sub!", 100);
            return;
        }

        set_focus(sub_focused->next);
    }
}

double get_focused_sub_start()
{
    if (sub_focused == NULL)
        return -1;
    return sub_focused->start_ts;
}

double get_focused_sub_end()
{
    if (sub_focused == NULL)
        return -1;
    return sub_focused->end_ts;
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

// Function to be called when file is loaded
void subs_init()
{
    // Write dummy sub for mpv to parse
    FILE *fp = fopen(SUB_FILENAME_TMP, "w");
    fprintf(fp, SUB_PLACEHOLDER);
    fclose(fp);

    sub_add(SUB_FILENAME_TMP);
}

// Concat text onto the currently focused sub
void sub_insert_text(const char *text)
{
    strncat(sub_focused->text, text, sizeof(sub_focused->text) - strlen(sub_focused->text) - 1);

    export_sub(SUB_FILENAME_TMP, 1);
    sub_reload();
}

// Export the current subtitles to a file
void export_sub(const char *filename, int highlight)
{
    // List is empty or sub is reloading
    if (sub_head == NULL || sub_reload_semaphore != 0)
    {
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
