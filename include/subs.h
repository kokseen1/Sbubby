#pragma once

#define SUB_FILENAME_TMP "_sbubby_tmp.srt"
#define SUB_PLACEHOLDER "1\n00:00:00,000 --> 00:00:00,000\n\n\n"

typedef struct Sub
{
    double start_ts;
    double end_ts;
    char text[512];
    struct Sub *next;
} Sub;

void new_sub(const double);

void sub_insert_text(const char *);

void subs_init();

void seek_focused_end();

void next_sub(int);

void back_sub(int);

int focus_prev_sub(int);

int focus_next_sub(int);

void set_focused_start_ts(double);

void set_focused_end_ts(double);

void import_sub(const char *);

void export_sub(const char *, int);

void export_reload_sub();

void sub_pop_char();

void sub_pop_word();

int focused_in_frame();

void focus_sub_in_frame();

void delete_focused_sub();

void cursor_left();

void cursor_right();

void unset_cursor();

void set_cursor_start();

void set_cursor_end();
