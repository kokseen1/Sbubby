#pragma once

#include <SDL2/SDL.h>

void set_window_icon(SDL_Window *);

int dbl_eq(const double, const double);

void pop_char(char *);

void pop_word(char *);

double str_to_timestamp(const char *);

void timetamp_to_str(const double, char *);
