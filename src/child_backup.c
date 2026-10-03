#include "child_backup.h"
#include "child_inotify.h"
#include "string_proc.h"

void child_signal_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
    {
        exit(EXIT_SUCCESS);
    }
}

void copy_file(const char* src, const char* dst)
{
    char buf[MAX_BUF];
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0)
    {
        fprintf(stderr, "Could not open source while copying %s->%s\n", src, dst);
        return;
    }

    int fd_dst = open(dst, O_WRONLY | O_TRUNC | O_CREAT, DEFAULT_PERMISSION);
    if (fd_dst < 0)
    {
        fprintf(stderr, "Could not open destination while copying %s->%s\n", src, dst);
        close(fd_src);
        return;
    }

    ssize_t n_read;
    while ((n_read = bulk_read(fd_src, buf, sizeof(buf))) > 0)
    {
        if (bulk_write(fd_dst, buf, n_read) != n_read)
        {
            fprintf(stderr, "Write error while copying %s->%s\n", src, dst);
            break;
        }
    }

    if (n_read < 0)
    {
        fprintf(stderr, "Read error while copying %s->%s\n", src, dst);
    }

    struct stat st;
    if (fstat(fd_src, &st) == 0)
    {
        struct timespec times[2] = {st.st_atim, st.st_mtim};
        futimens(fd_dst, times);
        fchmod(fd_dst, st.st_mode);
    }

    close(fd_src);
    close(fd_dst);
}

int recursive_delete(const char* path)
{
    struct stat st;
    if (lstat(path, &st) == -1)
    {
        return (errno == ENOENT) ? 0 : -1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        return unlink(path);
    }

    DIR* dir = opendir(path);
    if (!dir)
        return -1;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char subpath[PATH_MAX];
        if (snprintf(subpath, PATH_MAX, "%s/%s", path, entry->d_name) < PATH_MAX)
        {
            recursive_delete(subpath);
        }
    }
    closedir(dir);
    return rmdir(path);
}

void recur_copy(const char* src_root, const char* current_src, const char* dest_root)
{
    DIR* dir = opendir(current_src);
    if (dir == NULL)
        return;

    char relative_path[PATH_MAX];
    relative_path[0] = '\0';
    if (strcmp(src_root, current_src) != 0)
    {
        size_t src_root_len = strlen(src_root);
        if (strlen(current_src) > src_root_len && strncmp(current_src, src_root, src_root_len) == 0)
        {
            strncpy(relative_path, current_src + src_root_len + 1, PATH_MAX - 1);
            relative_path[PATH_MAX - 1] = '\0';
        }
    }
    
    char current_dest[PATH_MAX];
    if (relative_path[0] == '\0')
    {
        strncpy(current_dest, dest_root, PATH_MAX - 1);
        current_dest[PATH_MAX - 1] = '\0';
    }
    else
    {
        if (join_paths(current_dest, dest_root, relative_path) == -1)
        {
            fprintf(stderr, "Path too long: %s/%s\n", dest_root, relative_path);
            closedir(dir);
            return;
        }
    }

    struct stat src_stat;
    if (stat(current_src, &src_stat) == 0)
    {
        if (mkdir(current_dest, src_stat.st_mode) == -1 && errno != EEXIST)
        {
            perror("mkdir");
            closedir(dir);
            return;
        }
        chmod(current_dest, src_stat.st_mode);
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char s_path[PATH_MAX], d_path[PATH_MAX];
        if (join_paths(s_path, current_src, entry->d_name) == -1 ||
            join_paths(d_path, current_dest, entry->d_name) == -1)
        {
            continue;
        }

        struct stat st;
        if (lstat(s_path, &st) == -1)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            recur_copy(src_root, s_path, dest_root);
        }
        else if (S_ISREG(st.st_mode))
        {
            copy_file(s_path, d_path);
        }
        else if (S_ISLNK(st.st_mode))
        {
            char link_target[PATH_MAX];
            ssize_t len = readlink(s_path, link_target, sizeof(link_target) - 1);
            if (len != -1)
            {
                link_target[len] = '\0';
                char real_link_target[PATH_MAX];
                char resolved_target[PATH_MAX];
                
                if (link_target[0] == '/' && realpath(s_path, real_link_target) != NULL &&
                    realpath(link_target, resolved_target) != NULL &&
                    strncmp(resolved_target, src_root, strlen(src_root)) == 0)
                {
                    char new_target[PATH_MAX];
                    snprintf(new_target, PATH_MAX, "%s%s", dest_root, resolved_target + strlen(src_root));
                    symlink(new_target, d_path);
                }
                else
                {
                    symlink(link_target, d_path);
                }
            }
        }
    }
    closedir(dir);
}

void childwork_backup(char* source, char* dest)
{
    struct sigaction sa;
    sa.sa_handler = child_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGINT, SIG_IGN);

    struct stat src_st;
    if (stat(source, &src_st) == -1 || !S_ISDIR(src_st.st_mode))
    {
        fprintf(stderr, "Source '%s' is not a directory or doesn't exist\n", source);
        exit(EXIT_FAILURE);
    }

    struct stat dst_st;
    bool new_dir = false;

    if (stat(dest, &dst_st) == -1)
    {
        if (mkdir(dest, src_st.st_mode) == -1)
        {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
        chmod(dest, src_st.st_mode);
        new_dir = true;
    }
    else if (!is_dir_empty(dest))
    {
        fprintf(stderr, "Destination folder not empty: %s\n", dest);
        exit(EXIT_FAILURE);
    }

    char dest_real[PATH_MAX], source_real[PATH_MAX];
    if (realpath(dest, dest_real) == NULL || realpath(source, source_real) == NULL)
    {
        fprintf(stderr, "Cannot resolve paths\n");
        if (new_dir)
            rmdir(dest);
        exit(EXIT_FAILURE);
    }

    if (strstr(dest_real, source_real) == dest_real || strstr(source_real, dest_real) == source_real)
    {
        fprintf(stderr, "Cannot backup into nested directory\n");
        if (new_dir)
            rmdir(dest);
        exit(EXIT_FAILURE);
    }

    recur_copy(source_real, source_real, dest_real);
    printf("Backup created: %s -> %s\n", source_real, dest_real);

    int fd = inotify_init();
    if (fd < 0)
    {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    childwork_inotify(fd, source_real, dest_real);

    close(fd);
    exit(EXIT_SUCCESS);
}

bool needs_update(const char* backup_path, const char* restore_path)
{
    struct stat b_st, r_st;
    if (lstat(backup_path, &b_st) != 0)
        return true;
    if (lstat(restore_path, &r_st) != 0)
        return true;
    if ((b_st.st_mode & S_IFMT) != (r_st.st_mode & S_IFMT))
        return true;
    if (b_st.st_size != r_st.st_size)
        return true;
    if (b_st.st_mtim.tv_sec != r_st.st_mtim.tv_sec)
        return true;
    if (b_st.st_mtim.tv_nsec != r_st.st_mtim.tv_nsec)
        return true;
    return false;
}

void recursive_restore(const char* backup_current, const char* restore_current, const char* backup_root,
                       const char* restore_root)
{
    DIR* dir_res = opendir(restore_current);
    if (dir_res)
    {
        struct dirent* entry;
        while ((entry = readdir(dir_res)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char restore_path[PATH_MAX], backup_path[PATH_MAX];
            if (join_paths(restore_path, restore_current, entry->d_name) == -1 ||
                join_paths(backup_path, backup_current, entry->d_name) == -1)
            {
                continue;
            }

            if (access(backup_path, F_OK) != 0)
            {
                recursive_delete(restore_path);
            }
        }
        closedir(dir_res);
    }

    DIR* dir_back = opendir(backup_current);
    if (!dir_back)
        return;

    struct dirent* entry;
    while ((entry = readdir(dir_back)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char backup_path[PATH_MAX], restore_path[PATH_MAX];
        if (join_paths(backup_path, backup_current, entry->d_name) == -1 ||
            join_paths(restore_path, restore_current, entry->d_name) == -1)
        {
            continue;
        }

        struct stat st;
        if (lstat(backup_path, &st) == -1)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            struct stat dst_st;
            if (lstat(restore_path, &dst_st) == 0 && !S_ISDIR(dst_st.st_mode))
            {
                recursive_delete(restore_path);
            }
            mkdir(restore_path, st.st_mode);
            chmod(restore_path, st.st_mode);
            recursive_restore(backup_path, restore_path, backup_root, restore_root);
        }
        else if (S_ISREG(st.st_mode))
        {
            if (needs_update(backup_path, restore_path))
            {
                copy_file(backup_path, restore_path);
            }
        }
        else if (S_ISLNK(st.st_mode))
        {
            char target[PATH_MAX];
            ssize_t len = readlink(backup_path, target, sizeof(target) - 1);
            if (len != -1)
            {
                target[len] = '\0';
                unlink(restore_path);
                symlink(target, restore_path);
            }
        }
    }
    closedir(dir_back);
}

void restore_loop(char** args, int argc)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: restore <source path> <backup path>\n");
        return;
    }

    char restore_dir[PATH_MAX], backup_dir[PATH_MAX];
    if (!realpath(args[2], backup_dir))
    {
        perror("Cannot resolve backup path");
        return;
    }

    struct stat st;
    if (stat(backup_dir, &st) == -1 || !S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "Backup directory does not exist: %s\n", args[2]);
        return;
    }

    if (stat(args[1], &st) == -1)
    {
        if (mkdir(args[1], 0755) == -1)
        {
            perror("Cannot create restore directory");
            return;
        }
    }

    if (!realpath(args[1], restore_dir))
    {
        perror("Cannot resolve restore path");
        return;
    }

    printf("Restoring from %s to %s...\n", backup_dir, restore_dir);
    recursive_restore(backup_dir, restore_dir, backup_dir, restore_dir);
    printf("Restore completed.\n");
}