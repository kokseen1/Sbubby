#pragma once

typedef struct Sub
{
    double start_ts;
    double end_ts;
    char text[512];
    struct Sub *next;
} Sub;

void new_sub(const double);
