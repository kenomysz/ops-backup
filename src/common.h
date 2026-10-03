#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_ARGS 16
#define MAX_INPUT_LENGTH 1024
#define MAX_BACKUP_CHILDREN 256
#define MAX_BUF 4096
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define WATCH_MASK (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB)
#define DEFAULT_PERMISSION 0777

typedef struct
{
    pid_t pid;
    char source[PATH_MAX];
    char dest[PATH_MAX];
    int active;
} BackupChild;

typedef struct WatchNode
{
    int wd;
    char path[PATH_MAX];
    struct WatchNode* next;
} WatchNode;

extern BackupChild children[MAX_BACKUP_CHILDREN];
extern pid_t parent_pid;
extern WatchNode* watch_list;

#endif