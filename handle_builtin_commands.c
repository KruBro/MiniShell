#include "minishell.h"

void handle_builtin_command(char **args) {
    char cwd[1024];
    if (strcmp(args[0], "exit") == 0) {
        exit(0);
    } 
    else if (strcmp(args[0], "cd") == 0) {
        // Case A: No arguments provided, default to HOME
        if (args[1] == NULL) {
            char *home = getenv("HOME");
            if (home == NULL) {
                fprintf(stderr, COLOR_RED "minishell: cd: HOME not set\n" COLOR_RESET);
            } 
            else if (chdir(home) == -1) {
                perror("minishell");
            }
        } 
        // Case B: A specific path was provided
        else {
            if (chdir(args[1]) == -1) {
                perror("minishell");
            }
        }
    }
    else if (strcmp(args[0], "pwd") == 0) {
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd");
        }
    }
    else if (strcmp(args[0], "echo") == 0) {
        int i = 1;
        while (args[i] != NULL) {
            if (strcmp(args[i], "$$") == 0) {
                printf("%d", getpid());
            } 
            else if (strcmp(args[i], "$?") == 0) {
                printf("%d\n", last_exit_status); // Placeholder until waitpid status tracking is wired
            } 
            else if (strcmp(args[i], "$SHELL") == 0) {
                char *executable_shell = getenv("SHELL");
                if (executable_shell != NULL) {
                    printf("%s", executable_shell);
                }
            } 
            else {
                printf("%s", args[i]);
            }

            if (args[i + 1] != NULL) {
                printf(" ");
            }
            i++;
        }
        printf("\n");
    }
    else if (strcmp(args[0], "clear") == 0) {
        printf("\033[2J\033[H");
        fflush(stdout);
    }
    else if(strcmp(args[0], "jobs") == 0)
    {
        list_jobs();
    }
    else if(strcmp(args[0], "fg") == 0)
    {
        // BUGFIX: this used to call send_job_to_background() for BOTH
        // "fg" and "bg", so `fg` could never actually foreground a job.
        if(args[1] == NULL)
        {
            int last_id = get_last_job_id();
            if(last_id == -1)
                fprintf(stderr, COLOR_RED "minishell: fg: current: no such job\n" COLOR_RESET);
            else
                bring_job_to_foreground(last_id);
        }
        else
        {
            bring_job_to_foreground(atoi(args[1]));
        }
    }
    else if(strcmp(args[0], "bg") == 0)
    {
        if(args[1] == NULL)
        {
            int last_id = get_last_job_id();
            if(last_id == -1)
                // BUGFIX: this used to say "fg:" even for the bg command
                fprintf(stderr, COLOR_RED "minishell: bg: current: no such job\n" COLOR_RESET);
            else
                send_job_to_background(last_id);
        }
        else
            send_job_to_background(atoi(args[1]));
    }
}