# minishell

A small Unix shell written in C, with builtins, pipes, I/O redirection,
background jobs, and job control (`fg`/`bg`/`jobs`, Ctrl+C, Ctrl+Z).

## Build

```
make
```

This builds `./minishell` from `main.c`, `minishell.c`, `job_control.c`,
`signals.c`, and `redirection.c`. `make clean` removes build artifacts.

## Run

```
./minishell
```

You'll get a colored `minishell$ ` prompt. Set a custom prompt with:

```
PS1="myshell> "
```

## Features

- **Builtins:** `cd`, `pwd`, `echo` (supports `$$`, `$?`, `$SHELL`),
  `clear`, `exit`, `jobs`, `fg [job_id]`, `bg [job_id]`.
- **External commands** via `execvp` / `PATH` lookup.
- **Pipelines:** `cmd1 | cmd2 | cmd3 | ...` (any number of stages).
- **I/O redirection:** `<`, `>`, `>>`, usable on any external command and
  on any stage of a pipeline (e.g. `sort < in.txt | uniq > out.txt`).
- **Background jobs:** append `&` to run a command without blocking the
  shell, e.g. `sleep 30 &`. The shell prints `[job_id] pid`.
- **Job control:**
  - `jobs` — lists background/suspended jobs with id, state
    (`Running`/`Stopped`), and full command line, bash-style:
    ```
    [1]+  Stopped                 sleep 30
    [2]+  Running                 sleep 60 &
    ```
  - `fg [job_id]` — brings a job into the foreground and gives it the
    terminal. Omit the id to use the most recently added job.
  - `bg [job_id]` — resumes a stopped job in the background, printing
    `[id]+ command &`.
  - **Ctrl+C** (`SIGINT`) interrupts whatever currently owns the
    terminal.
  - **Ctrl+Z** (`SIGTSTP`) suspends the foreground job, adds it to the
    job list, and prints a bash-style notice:
    ```
    [1]+  Stopped                 sleep 30
    ```
  - **Asynchronous completion notices.** When a *background* job (one
    started with `&` or resumed with `bg`) finishes on its own — even
    while you've since moved on to other commands — the shell announces
    it right before the next prompt, exactly like bash:
    ```
    [1]+  Done                    sleep 5
    [2]+  Exit 1                  false
    [3]+  Terminated              some_long_job
    ```
    The job you're actively watching in the foreground is never
    announced this way (you can see it finish yourself); only jobs
    running unattended in the background get this notice.
- **Colored UI:** the prompt, error messages, and job-control
  notifications (`[Suspended]`, background job start, `jobs` output,
  etc.) are colorized with ANSI escape codes. Program output itself is
  never colorized.

## Known limitations

- Background execution of a *pipeline* (`cmd1 | cmd2 &`) isn't
  supported by the job-tracking data structure (a job only records one
  pid); the shell prints a notice and runs it in the foreground instead.
- No argument quoting/escaping (`"a b"`, `'a b'`) outside of the special
  `PS1="..."` syntax — arguments are split on whitespace only.
- Builtins (`cd`, `echo`, etc.) run in the shell's own process, so they
  only get their special in-process handling when they're the *entire*
  command. If they appear inside a pipeline (`echo hi | grep h`), they
  run as the real `/bin/echo`-style external program instead, same as
  most shells do for coreutils-backed builtins.
- Fixed-size buffers: input lines and stored job command strings are
  capped at `MAX_INPUT_SIZE` (1024 bytes), and pipelines/argument lists
  are capped at `MAX_ARGS` (64) tokens.

## Bugs found and fixed

This codebase had several bugs beyond styling; the notable ones:

1. **`fg` never foregrounded anything.** Both the `fg` and `bg` builtins
   called `send_job_to_background()`. `fg` now calls
   `bring_job_to_foreground()`.
2. **`sigaction` never actually installed the signal handler.**
   `sa_sigaction` was set, but `SA_SIGINFO` was missing from
   `sa_flags`. Without it, the kernel treats the handler as the plain
   one-argument form (`sa_handler`, which shares a union with
   `sa_sigaction`) — undefined behavior. Fixed by adding
   `SA_SIGINFO | SA_RESTART`.
3. **Pipelines were never waited on.** In `call_n_pipe`, the line that
   sets `foreground_pid = ret` for the last pipeline stage was nested
   one brace too deep, inside a condition that could never be true at
   the same time (`i == command_count - 1` nested inside
   `i < command_count - 1`). The shell's wait loop saw
   `foreground_pid == 0` immediately and never actually waited for the
   pipeline.
4. **No process groups → job control couldn't work.** `tcsetpgrp()`
   requires the target to already be its own process group leader.
   Children were never given their own group, so every `tcsetpgrp()`
   call in `fg` was silently failing. Every fork site now calls
   `setpgid()` (both in the child and the parent, the standard
   race-free idiom), and the shell claims its own process group and the
   terminal on startup in `init_shell()`.
5. **`fg` permanently broke terminal safety after first use.**
   `bring_job_to_foreground()` used to reset `SIGTTIN`/`SIGTTOU` back to
   `SIG_DFL` at the end. The shell must ignore those signals *forever*,
   or a later `tcsetpgrp()` call can stop the shell itself. Removed the
   reset; ignoring is now set once, permanently, in `init_shell()`.
6. **Race between `fg`'s direct `waitpid()` and the async `SIGCHLD`
   handler.** `bring_job_to_foreground()` used to call `waitpid()`
   directly while a `SIGCHLD` handler was *also* calling
   `waitpid(-1, ...)` asynchronously — whichever reaped the child first
   left the other with `ECHILD`. All waiting now goes through the
   `SIGCHLD` handler exclusively.
7. **`pause()`-based wait loops had a lost-wakeup race.**
   `while (foreground_pid != 0) pause();` can hang forever if the
   `SIGCHLD` that would clear the flag arrives *before* `pause()` is
   called. Replaced with a `sigprocmask()` + `sigsuspend()` pattern
   (`wait_for_foreground()`) that closes this window.
8. **Jobs only ever showed `argv[0]`, not the full command.** `jobs`
   and the `[Suspended]` notice used to show just the program name
   (e.g. `ls`) instead of the full line (e.g. `ls -la /tmp`). The raw
   input line is now captured before tokenization and threaded through
   to the job list and `current_fg_cmd`.
9. **No way to actually background a job.** Nothing parsed a trailing
   `&`; `add_job()` was only ever called from the Ctrl+Z path. Added
   `strip_background()` in `minishell.c` to detect and strip a trailing
   `&` and launch the command without waiting.
10. **`handle_redirection_and_piping` implemented no redirection.**
    Despite the name, there was no `<`/`>`/`>>` handling anywhere.
    Added `redirection.c` (`extract_redirection` / `apply_redirection`),
    wired into both the single-command and each pipeline-stage exec
    paths.
11. **`bg`'s "no such job" message said `fg:`.** Cosmetic but
    misleading; fixed to say `bg:`.
12. **Pipelines whose first word matched a builtin name never actually
    piped.** `is_builtin_command()` only checked `argv[0]`, so
    `echo hi | grep h` was dispatched straight to the `echo` builtin,
    which printed `hi | grep h` literally instead of running a
    pipeline. `execute_command()` now checks for a `|` anywhere on the
    line first.