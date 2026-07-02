#include "minishell.h"

int call_n_pipe(int no_of_args, int command_count, char **args, char *raw_cmd) {
    // cmds is an array of string arrays, so it requires a char ***
    char ***cmds = malloc(command_count * sizeof(char **));
    if (cmds == NULL) {
        perror("malloc");
        return 1;
    }

    // Parsing the commands
    int cmd_index = 0;
    cmds[cmd_index++] = &args[0];

    //Iterate through all arguments, not just the command count
    for (int i = 0; i < no_of_args; i++) {
        
        // Safety check to ensure we don't pass NULL to strcmp
        if (args[i] != NULL && strcmp(args[i], "|") == 0) {
            
            //Actually assign NULL to split the array
            args[i] = NULL;

            //Safely check for missing trailing command without segfaulting
            if (args[i + 1] == NULL || strcmp(args[i + 1], "|") == 0) {
                fprintf(stderr, COLOR_RED "minishell: syntax error near unexpected token `|'\n" COLOR_RESET);
                free(cmds);
                return 1;
            }
            cmds[cmd_index++] = &args[i + 1];
        }
    }

    // If the very first argument was a PIPE
    if (cmds[0][0] == NULL) {
        fprintf(stderr, COLOR_RED "minishell: syntax error near unexpected token `|'\n" COLOR_RESET);
        free(cmds);
        return 1;
    }

    int stdin_backup = dup(0);
    int stdout_backup = dup(1);

    int prev_fd = -1;
    int fd[2];
    pid_t pgid = 0; // process group shared by every stage of this pipeline

    for (int i = 0; i < command_count; i++) {
        if (i < command_count - 1) {
            if (pipe(fd) < 0) {
                perror("minishell: pipe");
                free(cmds);
                return 1;
            }
        }

        pid_t ret = fork();
        if (ret < 0) {
            perror("minishell: fork");
            free(cmds);
            return 1;
        } 
        else if (ret == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            // Put every stage of the pipeline into ONE process group so
            // the terminal (and Ctrl+C/Ctrl+Z) can be handed to it as a
            // single unit.
            setpgid(0, (i == 0) ? 0 : pgid);

            // Child logic (Flawless)
            if (i > 0) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }

            if (i < command_count - 1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
            }

            execvp(cmds[i][0], cmds[i]);
            perror("minishell");
            exit(127);
        } 
        else {
            // Parent logic
            if (i == 0) {
                pgid = ret;
            }
            // Both parent and child call setpgid on the same pid; this
            // is the standard race-free idiom (whichever call happens
            // first "wins", and both end up agreeing on the group).
            setpgid(ret, pgid);

            if (i > 0) {
                close(prev_fd);
            }

            if (i < command_count - 1) {
                prev_fd = fd[0];
                close(fd[1]);
            }

            // BUGFIX: this was previously nested one brace too deep,
            // inside `if (i < command_count - 1)`, which made the
            // condition `i == command_count - 1` impossible to satisfy
            // at the same time — foreground_pid was NEVER set, so the
            // shell never waited for the pipeline to finish.
            if (i == command_count - 1) {
                foreground_pid = ret;
            }
        }
    }

    dup2(stdin_backup, STDIN_FILENO);
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdin_backup);
    close(stdout_backup);

    strncpy(current_fg_cmd, raw_cmd, MAX_INPUT_SIZE - 1);
    current_fg_cmd[MAX_INPUT_SIZE - 1] = '\0';

    // Hand the terminal to the pipeline's process group while it runs.
    tcsetpgrp(STDIN_FILENO, pgid);
    wait_for_foreground(foreground_pid);

    // Take the terminal back.
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    free(cmds);
    return 0;
}

void handle_redirection_and_piping(char **args, char *raw_cmd, int background) {
    // Detecting the pipes
    int pipe_count = 0;
    int no_of_args = 0;
    
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_count++;    
        }
        no_of_args++;
    }

    int command_count = pipe_count + 1;

    if (pipe_count == 0) {
        pid_t pid = fork(); 
        
        if (pid < 0) {
            perror("minishell: fork");
            return;
        } 
        else if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            // Give this child its own process group so job control /
            // terminal handoff (tcsetpgrp) works correctly.
            setpgid(0, 0);

            execvp(args[0], args);
            perror("minishell"); 
            exit(127);
        } 
        else { 
            setpgid(pid, pid);

            // Store the FULL command line (not just argv[0]) so `jobs`
            // and the "[Suspended]" notice show something meaningful.
            strncpy(current_fg_cmd, raw_cmd, MAX_INPUT_SIZE - 1);
            current_fg_cmd[MAX_INPUT_SIZE - 1] = '\0';

            if (background) {
                add_job(pid, raw_cmd, JOB_RUNNING);
                printf(COLOR_YELLOW "[%d] %d\n" COLOR_RESET, next_job_id - 1, pid);
            } else {
                tcsetpgrp(STDIN_FILENO, pid);
                wait_for_foreground(pid);
                tcsetpgrp(STDIN_FILENO, shell_pgid);
            }
        }
    }
    else {
        // Background pipelines aren't supported by this shell's job
        // tracking (a Job only stores a single pid), so fall back to
        // running it in the foreground rather than silently misbehaving.
        if (background) {
            fprintf(stderr, COLOR_YELLOW "minishell: background execution of pipelines is not supported; running in foreground\n" COLOR_RESET);
        }

        // Multi-pipe execution
        if (call_n_pipe(no_of_args, command_count, args, raw_cmd) == 1) {
            // Error already printed by call_n_pipe
        }
    }
}