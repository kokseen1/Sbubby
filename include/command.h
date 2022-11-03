#pragma once

#define MODE_NORMAL 0
#define MODE_INSERT 1

#define DEFAULT_UNIT_j -1
#define DEFAULT_UNIT_k 1
#define DEFAULT_COUNT_jk 3

#define DEFAULT_UNIT_J -0.1
#define DEFAULT_UNIT_K 0.1
#define DEFAULT_COUNT_JK 1

void handle_text_input(const char *);

void handle_escape();

void handle_return();

void handle_backspace();
