#pragma once

#include <SDL2/SDL.h>

void set_window_icon(SDL_Window *);

int dbl_eq(const double, const double);

int pop_char_at_idx(char *, int);

char *get_next_word(char *, int);

char *get_prev_word(char *, int);

int pop_range(char *, size_t);

int pop_char(char *);

void pop_word(char *);

double str_to_timestamp(const char *);

void timetamp_to_str(const double, char *);
