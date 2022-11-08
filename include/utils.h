#pragma once

#include <SDL2/SDL.h>

void set_window_icon(SDL_Window *);

int dbl_eq(const double, const double);

int pop_char_at_idx(char *, int);

int pop_word_at_idx(char *, int);

int pop_char(char *);

void pop_word(char *);

double str_to_timestamp(const char *);

void timetamp_to_str(const double, char *);
