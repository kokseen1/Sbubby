#include <stdio.h>
#include <string.h>
#include <math.h>

// Pop the last char from a string
void str_pop(char *str)
{
    size_t len = strlen(str);
    if (len > 0)
    {
        str[len - 1] = '\0';
    }
}

// Convert timestamp from seconds to HH:MM:SS format
void timetamp_to_str(double ts, char *ts_str)
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
