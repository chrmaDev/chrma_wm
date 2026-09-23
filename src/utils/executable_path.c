#include "utils/executable_path.h"

#include <string.h>
#include <unistd.h>

int executable_path(char *out, size_t size, const char *relative)
{
    char *slash;
    size_t base_len, relative_len;
    ssize_t len;

    if (!out || !relative || size < 2)
        return -1;
    len = readlink("/proc/self/exe", out, size - 1);
    if (len < 0 || (size_t)len >= size - 1)
        return -1;
    out[len] = '\0';
    slash = strrchr(out, '/');
    if (!slash)
        return -1;
    base_len = (size_t)(slash - out + 1);
    relative_len = strlen(relative);
    if (relative_len >= size - base_len)
        return -1;
    memcpy(out + base_len, relative, relative_len + 1);
    return 0;
}
