#ifndef STRING_PROC_H
#define STRING_PROC_H

#include "common.h"

int join_paths(char* buffer, const char* left, const char* right);
int make_dest_path(const char* input, char* output);
bool is_dir_empty(const char* path);
ssize_t bulk_read(int fd, char* buf, size_t count);
ssize_t bulk_write(int fd, char* buf, size_t count);
int parse(char* input, char** args);

#endif