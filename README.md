# ops-backup

A robust, multi-process Linux backup system written in C. It provides an interactive command shell to manage live, unidirectional directory mirroring. Each backup destination operates as a fully independent background worker process that utilizes `inotify` to replicate file system changes (creations, modifications, deletions, attribute changes) in real-time.

## Quick Start

Build the project using the provided Makefile[cite: 9]:
```bash
make            # Compiles the executable ./ops-backup
make clean      # Cleans up object files and the binary

Run the daemon interactively:
```
```bash
./ops-backup
```

## Commands

The system features an interactive shell with quote-aware path parsing (handling spaces in filenames).   

| Command | Description |
| :--- | :--- |
| `add <source> <target> [targets...]` | Forks a separate watcher process for each provided target directory. Copies the current state recursively, then begins live monitoring. |
| `list` | Displays all active backup sessions, listing their PIDs, source paths, and target paths. |
| `end <source> <target> [targets...]` | Terminates the specific background workers handling the given source-target pairs. |
| `restore <source> <backup>` | Blocks the shell to perform a differential restore from the backup to the source directory. |
| `exit` | Gracefully shuts down all active workers via `SIGTERM`, reaps them, and exits the shell. |

## Architecture

### Process Management
- **Parent Shell:** The main process sits in a continuous `fgets` loop. Instead of relying on `SIGCHLD`, it performs a non-blocking `waitpid(-1, &status, WNOHANG)` sweep (`children_cleanup`) before every prompt to clear out terminated workers and update the active registry.
- **State Tracking:** Active jobs are tracked within a fixed-size array (`BackupChild children[256]`).
- **Graceful Termination:** Sending `SIGINT` or `SIGTERM` to the parent causes it to broadcast `SIGTERM` to all running children and `wait()` for them to exit cleanly before shutting down.

### Worker Logic (Live Mirroring)
- **Initialization:** Upon forking, a worker recursively copies the source directory using `opendir`/`readdir`, creating missing destinations and preventing nested recursion (source inside target).
- **Event Loop & Watch List:** The worker initializes an `inotify` instance and maintains a custom linked list (`WatchNode`) to map inotify Watch Descriptors (WDs) back to absolute paths.
- **Subtree Tracking:** Directory creation (`IN_CREATE | IN_ISDIR`) triggers a recursive copy of the new subdirectory and dynamically adds new watches (`add_watch_recur`).
- **Auto-exit:** If the root source directory is deleted (`IN_IGNORED` on the root watch), the worker recognizes it and exits securely without hanging.

### File System Operations
- **Resilient I/O:** File transfers are handled via chunked `bulk_read` and `bulk_write` wrappers. These utilize `TEMP_FAILURE_RETRY` to guarantee that operations interrupted by `EINTR` are resumed rather than failing prematurely.
- **Fidelity:** File copies accurately restore metadata (timestamps and permissions) using `fstat`, `futimens`, and `fchmod`. Absolute symlinks pointing into the source tree are intelligently rewritten to point directly into the target tree.
- **Differential Restore:** The `restore` command executes a two-pass synchronization. It first sweeps the destination to delete elements no longer present in the backup, then selectively copies files back based on a rigorous check of file sizes, modification times, and modes (`needs_update`).

## Project Layout

- `main.c` — The command parser, signal handlers, array state management, and main prompt loop.
- `child_backup.c` — Worker initialization, manual recursive tree copying, and differential restoration logic.
- `child_inotify.c` — `inotify` event parsing (`IN_MOVED_TO`, `IN_DELETE`, etc.), linked-list WD management, and live patching of the backup tree.
- `string_proc.c` — Path validation (`realpath`), `malloc`-free command tokenizer for quotes, and uninterrupted bulk I/O wrappers.
- `common.h` — Global constants, limits (`MAX_BACKUP_CHILDREN`), and shared struct definitions.