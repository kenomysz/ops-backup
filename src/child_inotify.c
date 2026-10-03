#include "child_inotify.h"
#include "child_backup.h"
#include "string_proc.h"

WatchNode* watch_list = NULL;

char* get_path_by_wd(int wd)
{
    WatchNode* curr = watch_list;
    while (curr)
    {
        if (curr->wd == wd)
            return curr->path;
        curr = curr->next;
    }
    return NULL;
}

void remove_watch_by_wd(int fd, int wd)
{
    WatchNode** curr = &watch_list;
    while (*curr)
    {
        if ((*curr)->wd == wd)
        {
            WatchNode* to_free = *curr;
            inotify_rm_watch(fd, wd);
            *curr = (*curr)->next;
            free(to_free);
            return;
        }
        curr = &(*curr)->next;
    }
}

int add_watch_recur(int fd, const char* path)
{
    int wd = inotify_add_watch(fd, path, WATCH_MASK);
    if (wd == -1)
        return -1;

    char* existing_path_ptr = get_path_by_wd(wd);
    if (existing_path_ptr != NULL)
    {
        strncpy(existing_path_ptr, path, PATH_MAX - 1);
        existing_path_ptr[PATH_MAX - 1] = '\0';
        return wd;
    }

    WatchNode* node = malloc(sizeof(WatchNode));
    if (node)
    {
        node->wd = wd;
        strncpy(node->path, path, PATH_MAX - 1);
        node->path[PATH_MAX - 1] = '\0';
        node->next = watch_list;
        watch_list = node;
    }

    DIR* dir = opendir(path);
    if (!dir)
        return wd;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (entry->d_type == DT_DIR)
        {
            char subpath[PATH_MAX];
            if (snprintf(subpath, PATH_MAX, "%s/%s", path, entry->d_name) < PATH_MAX)
            {
                add_watch_recur(fd, subpath);
            }
        }
    }
    closedir(dir);
    return wd;
}

void process_single_event(int fd, struct inotify_event* ev, const char* src_root, const char* dest_root)
{
    if (!ev->len)
        return;

    char* watched_dir = get_path_by_wd(ev->wd);
    if (!watched_dir)
        return;

    char s_path[PATH_MAX], d_path[PATH_MAX];
    if (snprintf(s_path, PATH_MAX, "%s/%s", watched_dir, ev->name) >= PATH_MAX ||
        snprintf(d_path, PATH_MAX, "%s/%s/%s", dest_root, (watched_dir + strlen(src_root)), ev->name) >= PATH_MAX)
    {
        return;
    }

    if (ev->mask & (IN_DELETE | IN_MOVED_FROM))
    {
        recursive_delete(d_path);
    }
    else if ((ev->mask & IN_ISDIR) && (ev->mask & (IN_CREATE | IN_MOVED_TO)))
    {
        struct stat st;
        mode_t mode = DEFAULT_PERMISSION;
        if (lstat(s_path, &st) == 0)
        {
            mode = st.st_mode;
        }
        if (mkdir(d_path, mode) == 0)
        {
             chmod(d_path, mode);
        }
        recur_copy(src_root, s_path, dest_root);
        add_watch_recur(fd, s_path);
    }
    else if (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_ATTRIB))
    {
        struct stat st;
        if (lstat(s_path, &st) == 0)
        {
            if (S_ISREG(st.st_mode))
            {
                copy_file(s_path, d_path);
            }
            else if (S_ISLNK(st.st_mode))
            {
                unlink(d_path);
                char link_target[PATH_MAX];
                ssize_t len = readlink(s_path, link_target, sizeof(link_target) - 1);
                if (len != -1)
                {
                    link_target[len] = '\0';
                    symlink(link_target, d_path);
                }
            }
            if (S_ISDIR(st.st_mode))
            {
                 chmod(d_path, st.st_mode);
            }
        }
    }
}

void childwork_inotify(int fd, const char* src_root, const char* dest_root)
{
    int root_wd = add_watch_recur(fd, src_root);
    if (root_wd == -1)
    {
        fprintf(stderr, "Failed to add watch for %s\n", src_root);
        exit(EXIT_FAILURE);
    }

    char buf[EVENT_BUF_LEN];
    while (1)
    {
        ssize_t len = read(fd, buf, EVENT_BUF_LEN);
        if (len < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        for (char* ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + ((struct inotify_event*)ptr)->len)
        {
            struct inotify_event* event = (struct inotify_event*)ptr;

            if (event->mask & IN_IGNORED)
            {
                if (event->wd == root_wd)
                {
                    exit(EXIT_SUCCESS);
                }
                remove_watch_by_wd(fd, event->wd);
                continue;
            }

            process_single_event(fd, event, src_root, dest_root);
        }
    }
}