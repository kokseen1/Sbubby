#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <utils.h>

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
