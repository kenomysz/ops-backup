#include "string_proc.h"

int join_paths(char* buffer, const char* left, const char* right)
{
    int len = snprintf(buffer, PATH_MAX, "%s/%s", left, right);
    if (len < 0 || (size_t)len >= PATH_MAX)
    {
        return -1;
    }
    return 0;
}

int make_dest_path(const char* input, char* output)
{
    if (realpath(input, output) != NULL)
    {
        return 0;
    }
    char buf[PATH_MAX];
    if (strlen(input) >= PATH_MAX)
    {
        return -1;
    }
    strncpy(buf, input, PATH_MAX - 1);
    buf[PATH_MAX - 1] = '\0';

    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/')
    {
        buf[--len] = '\0';
    }

    if (buf[0] == '/')
    {
        strncpy(output, buf, PATH_MAX);
        return 0;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        return -1;
    }

    return join_paths(output, cwd, buf);
}

bool is_dir_empty(const char* path)
{
    DIR* dir = opendir(path);
    if (dir == NULL)
    {
        return false;
    }

    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        count++;
        if (count > 0)
            break;
    }
    closedir(dir);
    return (count == 0);
}

ssize_t bulk_read(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

int parse(char* input, char** args)
{
    int count = 0;
    char* p = input;

    while (*p != '\0' && count < MAX_ARGS)
    {
        while (*p == ' ' || *p == '\t' || *p == '\n')
        {
            *p = '\0';
            p++;
        }
        if (*p == '\0')
            break;

        if (*p == '"' || *p == '\'')
        {
            char quote = *p;
            p++;
            args[count++] = p;
            while (*p != '\0' && *p != quote)
                p++;
            if (*p == quote)
            {
                *p = '\0';
                p++;
            }
        }
        else
        {
            args[count++] = p;
            while (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\0')
                p++;
        }
    }
    args[count] = NULL;
    return count;
}