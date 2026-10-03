#ifndef CHILD_INOTIFY_H
#define CHILD_INOTIFY_H

#include "common.h"

char* get_path_by_wd(int wd);
void remove_watch_by_wd(int fd, int wd);
int add_watch_recur(int fd, const char* path);
void process_single_event(int fd, struct inotify_event* ev, const char* src_root, const char* dest_root);
void childwork_inotify(int fd, const char* src_root, const char* dest_root);

#endif