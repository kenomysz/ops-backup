#include "common.h"
#include "string_proc.h"
#include "child_backup.h"

BackupChild children[MAX_BACKUP_CHILDREN];
pid_t parent_pid;

void exit_program()
{
    if (getpid() == parent_pid)
    {
        for (int i = 0; i < MAX_BACKUP_CHILDREN; i++)
        {
            if (children[i].active)
            {
                kill(children[i].pid, SIGTERM);
                children[i].active = 0;
            }
        }
    }
    while (wait(NULL) > 0)
    {
    }
    exit(EXIT_SUCCESS);
}

void sig_handler_end(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
        exit_program();
}

void handle_all_signals()
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    for (int i = 1; i < NSIG; i++)
    {
        if (i != SIGKILL && i != SIGSTOP && i != SIGINT && i != SIGTERM)
        {
            sigaction(i, &sa, NULL);
        }
    }
    sa.sa_handler = sig_handler_end;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

bool is_backup_active(const char* source, const char* dest)
{
    for (int i = 0; i < MAX_BACKUP_CHILDREN; i++)
    {
        if (children[i].active)
        {
            char s1[PATH_MAX], s2[PATH_MAX];
            if (realpath(children[i].source, s1) && realpath(source, s2) && strcmp(s1, s2) == 0)
            {
                char d1[PATH_MAX], d2[PATH_MAX];
                if (realpath(children[i].dest, d1) && realpath(dest, d2) && strcmp(d1, d2) == 0)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void add_loop(char** args, int argc, char* source)
{
    for (int i = 2; i < argc; i++)
    {
        char dest[PATH_MAX];
        if (make_dest_path(args[i], dest) == -1)
        {
            fprintf(stderr, "Invalid destination path: %s\n", args[i]);
            continue;
        }

        if (is_backup_active(source, dest))
        {
            fprintf(stderr, "Backup already active: %s -> %s\n", source, dest);
            continue;
        }

        int slot = -1;
        for (int j = 0; j < MAX_BACKUP_CHILDREN; j++)
        {
            if (!children[j].active)
            {
                slot = j;
                break;
            }
        }

        if (slot == -1)
        {
            fprintf(stderr, "Too many active backups\n");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            childwork_backup(source, dest);
        }
        else if (pid > 0)
        {
            children[slot].pid = pid;
            strncpy(children[slot].source, source, PATH_MAX - 1);
            strncpy(children[slot].dest, dest, PATH_MAX - 1);
            children[slot].source[PATH_MAX - 1] = '\0';
            children[slot].dest[PATH_MAX - 1] = '\0';
            children[slot].active = 1;
            printf("Started backup process %d: %s -> %s\n", pid, source, dest);
        }
        else
        {
            perror("fork");
        }
    }
}

void children_cleanup()
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        for (int i = 0; i < MAX_BACKUP_CHILDREN; i++)
        {
            if (children[i].pid == pid)
            {
                children[i].active = 0;
                printf("Backup process %d terminated\n", pid);
                break;
            }
        }
    }
}

void end_loop(char** args, int argc)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: end <source> <dest1> [<dest2> ...]\n");
        return;
    }

    char source[PATH_MAX];
    if (!realpath(args[1], source))
    {
        fprintf(stderr, "Invalid source path: %s\n", args[1]);
        return;
    }

    for (int i = 2; i < argc; i++)
    {
        char dest[PATH_MAX];
        if (make_dest_path(args[i], dest) == -1)
        {
            fprintf(stderr, "Invalid destination path: %s\n", args[i]);
            continue;
        }

        for (int j = 0; j < MAX_BACKUP_CHILDREN; j++)
        {
            if (children[j].active)
            {
                char s[PATH_MAX], d[PATH_MAX];
                if (realpath(children[j].source, s) && realpath(source, s) && realpath(children[j].dest, d) &&
                    realpath(dest, d))
                {
                    if (strcmp(s, source) == 0 && strcmp(d, dest) == 0)
                    {
                        kill(children[j].pid, SIGTERM);
                        children[j].active = 0;
                        printf("Terminated backup: %s -> %s\n", source, dest);
                        break;
                    }
                }
            }
        }
    }
}

void list_loop()
{
    printf("Active backups:\n");
    bool any = false;
    for (int i = 0; i < MAX_BACKUP_CHILDREN; i++)
    {
        if (children[i].active)
        {
            printf("  %s -> %s (PID: %d)\n", children[i].source, children[i].dest, children[i].pid);
            any = true;
        }
    }
    if (!any)
    {
        printf("  No active backups\n");
    }
}

int main()
{
    handle_all_signals();
    parent_pid = getpid();

    char input[MAX_INPUT_LENGTH];
    char* args[MAX_ARGS];

    printf("Backup System - Available commands:\n");
    printf("  add <source> <dest1> [dest2 ...] - Start backup\n");
    printf("  end <source> <dest1> [dest2 ...] - Stop backup\n");
    printf("  restore <destination> <backup>   - Restore backup\n");
    printf("  list                              - List active backups\n");
    printf("  exit                              - Exit program\n\n");

    while (1)
    {
        if (fgets(input, MAX_INPUT_LENGTH, stdin) == NULL)
        {
            break;
        }

        children_cleanup();

        int argc = parse(input, args);
        if (argc == 0)
            continue;

        if (strcmp(args[0], "exit") == 0)
        {
            exit_program();
        }
        else if (strcmp(args[0], "add") == 0)
        {
            if (argc < 3)
            {
                fprintf(stderr, "Usage: add <source> <dest1> [dest2 ...]\n");
                continue;
            }
            char source[PATH_MAX];
            if (!realpath(args[1], source))
            {
                fprintf(stderr, "Invalid source path: %s\n", args[1]);
                continue;
            }
            add_loop(args, argc, source);
        }
        else if (strcmp(args[0], "end") == 0)
        {
            end_loop(args, argc);
        }
        else if (strcmp(args[0], "restore") == 0)
        {
            restore_loop(args, argc);
        }
        else if (strcmp(args[0], "list") == 0)
        {
            list_loop();
        }
        else
        {
            fprintf(stderr, "Unknown command: %s\n", args[0]);
        }
    }

    return EXIT_SUCCESS;
}