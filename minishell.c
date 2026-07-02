#include "minishell.h"

// Global job list
Job *job_list = NULL;
int next_job_id = 1;
int last_exit_status = 0;
volatile sig_atomic_t foreground_pid = 0;
char current_fg_cmd[MAX_INPUT_SIZE] = "unknown command";

// The shell's own process group. Kept globally so job-control code can
// hand the terminal back to the shell after a foreground job finishes.
pid_t shell_pgid;

void init_shell() {
    // Make the shell the leader of its own process group and take
    // control of the terminal. Without this, tcsetpgrp() calls in
    // job_control.c fail silently and "fg" can never actually give the
    // terminal to a child.
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0 && errno != EPERM) {
        perror("minishell: setpgid");
    }
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    // The shell must permanently ignore SIGTTOU/SIGTTIN. If these ever
    // get reset to SIG_DFL, the shell can be stopped by the kernel the
    // next time it touches the terminal while a child (temporarily) owns
    // the foreground process group.
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    init_signal_handlers();
}

void main_loop() {
    char input[MAX_INPUT_SIZE];
    char raw_cmd[MAX_INPUT_SIZE];
    char *args[MAX_ARGS];

    while (1) {
        // 1. Fetch the prompt dynamically from the OS environment
        char *prompt = getenv("PS1");
        if (prompt == NULL) {
            printf(COLOR_GREEN COLOR_BOLD "minishell$ " COLOR_RESET); // Fallback if PS1 is not set
        } else {
            printf("%s", prompt);
        }
        fflush(stdout); 

        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) {
                printf("\n"); 
                exit(0); 
            }
            continue;
        }

        // 2. Strip the trailing newline
        input[strcspn(input, "\n")] = '\0';

        // 3. Intercept PS1= and store it in the environment block
        if (strncmp(input, "PS1=", 4) == 0) {
            
            // Branch 1: Quoted string
            if (input[4] == '"') {
                char *value = input + 5;
                int i = 5;
                
                // SAFETY: Scan until we hit the closing quote OR the end of the string
                while (input[i] != '"' && input[i] != '\0') {
                    i++;
                }
                
                if (input[i] == '"') {
                    input[i] = '\0'; // Isolate the string
                    setenv("PS1", value, 1);
                } else {
                    // They forgot the closing quote
                    fprintf(stderr, COLOR_RED "minishell: syntax error: unclosed quote\n" COLOR_RESET);
                }
            } 
            // Branch 2: Invalid space immediately after equals
            else if (input[4] == ' ') {
                fprintf(stderr, COLOR_RED "minishell: syntax error near unexpected token ' '\n" COLOR_RESET);
            } 
            // Branch 3: Unquoted string
            else {
                char *value = input + 4;
                int i = 0;
                
                // Scan until we hit a space OR the end of the string
                while (value[i] != ' ' && value[i] != '\0') {
                    i++;
                }
                
                value[i] = '\0'; // Truncate at the first space (if one exists)
                setenv("PS1", value, 1);
            }
            continue; // Skip execution 
        }

        // Keep a copy of the raw line BEFORE parse_command() tokenizes it
        // in place (strtok replaces whitespace with '\0'). This copy is
        // what gets shown by `jobs`/`fg` and stored in the job list, so
        // users see the full command instead of just argv[0].
        strncpy(raw_cmd, input, sizeof(raw_cmd) - 1);
        raw_cmd[sizeof(raw_cmd) - 1] = '\0';

        parse_command(input, args);
        if (args[0] == NULL) {
            continue;
        }

        int background = strip_background(args);
        if (background) {
            trim_trailing_amp(raw_cmd);
        }

        execute_command(args, raw_cmd, background);
    }
}

void parse_command(char *input, char **args) {
    // Tokenize the input string into arguments and  store in args
    int count = 0;

    char *token = strtok(input, " \t\n");
    
    while(token != NULL)
    {
        args[count] = token;
        count++;

        token = strtok(NULL, " \t\n");
    }

    args[count] = NULL;
}

// If the last token is a lone "&", remove it from args and report that
// the command should run in the background.
int strip_background(char **args) {
    int count = 0;
    while (args[count] != NULL) {
        count++;
    }

    if (count > 0 && strcmp(args[count - 1], "&") == 0) {
        args[count - 1] = NULL;
        return 1;
    }
    return 0;
}

// Strips a trailing "&" (and any whitespace before it) from a raw command
// string, so job listings show the clean command instead of "cmd &".
void trim_trailing_amp(char *cmd) {
    int len = (int)strlen(cmd);
    int i = len - 1;

    while (i >= 0 && (cmd[i] == ' ' || cmd[i] == '\t')) {
        i--;
    }
    if (i >= 0 && cmd[i] == '&') {
        i--;
        while (i >= 0 && (cmd[i] == ' ' || cmd[i] == '\t')) {
            i--;
        }
    }
    cmd[i + 1] = '\0';
}

// Blocks until the SIGCHLD handler reports that `pid` is no longer the
// foreground process (it exited or was suspended).
//
// BUGFIX: a naive `while (foreground_pid != 0) pause();` loop has a
// classic race: if SIGCHLD arrives (and the handler sets foreground_pid
// to 0) after the check but before pause() actually blocks, that signal
// is "lost" and pause() then blocks forever waiting for a SIGCHLD that
// already happened. This showed up in testing as commands hanging.
// Blocking SIGCHLD, checking the flag, then atomically unblocking +
// waiting via sigsuspend() closes that window.
void wait_for_foreground(pid_t pid) {
    sigset_t block_mask, orig_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_mask, &orig_mask);

    foreground_pid = pid;

    while (foreground_pid != 0) {
        sigsuspend(&orig_mask);
    }

    sigprocmask(SIG_SETMASK, &orig_mask, NULL);
}

int execute_command(char **args, char *raw_cmd, int background) {
    int has_pipe = 0;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            has_pipe = 1;
            break;
        }
    }

    if (!has_pipe && is_builtin_command(args)) {
        handle_builtin_command(args);
    } else {
        handle_redirection_and_piping(args, raw_cmd, background);
    }
    return 0;
}

int is_builtin_command(char **args) {
    // Check if the command matches any of our implemented internal commands
    if (strcmp(args[0], "cd") == 0 || 
        strcmp(args[0], "exit") == 0 ||
        strcmp(args[0], "pwd") == 0 || 
        strcmp(args[0], "echo") == 0 || 
        strcmp(args[0], "clear") == 0 ||
        strcmp(args[0], "fg") == 0 || 
        strcmp(args[0], "bg") == 0 ||
        strcmp(args[0], "jobs") == 0) {
        return 1;
    }
    return 0;
}


