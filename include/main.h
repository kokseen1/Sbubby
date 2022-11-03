#pragma once

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 360

extern double curr_timestamp;

void set_window_title(const char *);

void toggle_pause();

void frame_step();

void frame_back_step();

void seek_start();

void seek_end();

void seek_relative(const double);
