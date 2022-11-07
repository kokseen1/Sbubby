#pragma once

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 360

#define REPLY_USERDATA_SUB_RELOAD 8000
#define REPLY_USERDATA_SUB_RELOAD2 8001
#define REPLY_USERDATA_UPDATE_FILENAME 8002
#define REPLY_USERDATA_UPDATE_TIMESTAMP 8003

extern double curr_timestamp;

// Prevent writing to file when reloading
extern int sub_reload_semaphore;

// Filename to export as
extern char* export_filename;

void show_text(const char *, const int);

void set_window_title(const char *);

void toggle_fullscreen();

void toggle_pause();

void frame_step();

void frame_back_step();

void seek_start();

void seek_end();

void seek_absolute(const double);

void seek_relative(const double);

void sub_add(const char *);

void sub_reload();
