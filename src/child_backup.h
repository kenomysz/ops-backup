#ifndef CHILD_BACKUP_H
#define CHILD_BACKUP_H

#include "common.h"

void copy_file(const char* src, const char* dst);
int recursive_delete(const char* path);
void recur_copy(const char* src_root, const char* current_src, const char* dest_root);
void childwork_backup(char* source, char* dest);
void child_signal_handler(int sig);

bool needs_update(const char* backup_path, const char* restore_path);
void recursive_restore(const char* backup_current, const char* restore_current, const char* backup_root, const char* restore_root);
void restore_loop(char** args, int argc);

#endif