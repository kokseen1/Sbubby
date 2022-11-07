#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

#include <utils.h>
#include <slre.h>

static inline void *ptr_max(void *x, void *y)
{
    return x > y ? x : y;
}

// Equality comparison with low precision for timestamps
inline int dbl_eq(const double a, const double b)
{
    return fabs(a - b) < 0.01;
}

// Pop the last char from a string
void pop_char(char *str)
{
    size_t len = strlen(str);
    if (len > 0)
    {
        str[len - 1] = '\0';
    }
}

void pop_word(char *str)
{
    size_t len = strlen(str);
    if (len <= 0)
        return;

    // Delete trailing spaces
    while (isspace(str[len - 1]))
        len--;
    str[len] = '\0';

    char *pos = ptr_max(strrchr(str, ' '), strrchr(str, '\n'));
    if (pos)
        *(pos + 1) = '\0';
    else
        str[0] = '\0';
}

// Convert a HH:MM:SS string to a timestamp in seconds
double str_to_timestamp(const char *str)
{
    double timestamp = 0.0;
    struct slre_cap caps[3];

    if (slre_match("^([0-9]+):([0-9]+):([0-9]+[,|\\.][0-9]+)$", str, strlen(str), caps, 3) > 0)
    {
        if (caps[0].len > 0 && caps[1].len > 0 && caps[2].len > 0)
        {
            char hh[16] = {0};
            char mm[16] = {0};
            char ss[16] = {0};

            strncpy(hh, caps[0].ptr, caps[0].len);
            strncpy(mm, caps[1].ptr, caps[1].len);
            strncpy(ss, caps[2].ptr, caps[2].len);

            char *pos = strchr(ss, ',');
            if (pos)
                *pos = '.';

            timestamp = strtol(hh, NULL, 10) * 3600 + strtol(mm, NULL, 10) * 60 + strtod(ss, NULL);
        }
    }

    return timestamp;
}

// Convert timestamp from seconds to HH:MM:SS format
void timetamp_to_str(const double ts, char *ts_str)
{
    const int h = ts / 3600;
    const int m = fmod((ts / 60), 60);
    const double s = fmod(ts, 60);
    sprintf(ts_str, "%02d:%02d:%06.3f", h, m, s);

    // Convert period to comma
    char *ret = strchr(ts_str, '.');
    if (ret)
        *ret = ',';
}
