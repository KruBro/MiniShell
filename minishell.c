#include "minishell.h"

// Global job list
Job *job_list = NULL;
int next_job_id = 1;
static int last_exit_status = 0;

void init_shell() {
    // Initialize the shell (e.g., set up environment, signal handlers)
    init_signal_handlers();
}

void main_loop() {
    char input[MAX_INPUT_SIZE];
    char *args[MAX_ARGS];

    while (1) {
        // 1. Fetch the prompt dynamically from the OS environment
        char *prompt = getenv("PS1");
        if (prompt == NULL) {
            prompt = "minishell$ "; // Fallback if PS1 is not set
        }
        
        printf("%s", prompt);
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
                    fprintf(stderr, "minishell: syntax error: unclosed quote\n");
                }
            } 
            // Branch 2: Invalid space immediately after equals
            else if (input[4] == ' ') {
                fprintf(stderr, "minishell: syntax error near unexpected token ' '\n");
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

        parse_command(input, args);
        if (args[0] != NULL) {
            execute_command(args);
        }
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

int execute_command(char **args) {
    if (is_builtin_command(args)) {
        handle_builtin_command(args);
    } else {
        handle_redirection_and_piping(args);
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
                fprintf(stderr, "minishell: cd: HOME not set\n");
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
}

// Assume this is declared globally at the top of minishell.c
// int last_exit_status = 0;

int call_n_pipe(int no_of_args, int command_count, char **args) {
    // cmds is an array of string arrays, so it requires a char ***
    char ***cmds = malloc(command_count * sizeof(char **));
    if (cmds == NULL) {
        perror("malloc");
        return 1;
    }

    // Parsing the commands
    int cmd_index = 0;
    cmds[cmd_index++] = &args[0];

    // FIX 2: Iterate through all arguments, not just the command count
    for (int i = 0; i < no_of_args; i++) {
        
        // Safety check to ensure we don't pass NULL to strcmp
        if (args[i] != NULL && strcmp(args[i], "|") == 0) {
            
            //Actually assign NULL to split the array
            args[i] = NULL;

            //Safely check for missing trailing command without segfaulting
            if (args[i + 1] == NULL || strcmp(args[i + 1], "|") == 0) {
                fprintf(stderr, "minishell: syntax error near unexpected token `|'\n");
                free(cmds);
                return 1;
            }
            cmds[cmd_index++] = &args[i + 1];
        }
    }

    // If the very first argument was a PIPE
    if (cmds[0][0] == NULL) {
        fprintf(stderr, "minishell: syntax error near unexpected token `|'\n");
        free(cmds);
        return 1;
    }

    int stdin_backup = dup(0);
    int stdout_backup = dup(1);

    int prev_fd = -1;
    int fd[2];

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
            // Parent logic (Flawless)
            if (i > 0) {
                close(prev_fd);
            }

            if (i < command_count - 1) {
                prev_fd = fd[0];
                close(fd[1]);
            }
        }
    }

    dup2(stdin_backup, STDIN_FILENO);
    dup2(stdout_backup, STDOUT_FILENO);
    close(stdin_backup);
    close(stdout_backup);

    // reap all children in the pipeline
    for (int i = 0; i < command_count; i++) {
        int wstatus;
        wait(&wstatus);

        if (WIFEXITED(wstatus)) {
            last_exit_status = WEXITSTATUS(wstatus);
        }
    }

    free(cmds);
    return 0;
}

void handle_redirection_and_piping(char **args) {
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
        // ... (Your existing single command execution block) ...
        pid_t pid = fork(); 
        
        if (pid < 0) {
            perror("minishell: fork");
            return;
        } 
        else if (pid == 0) {
            execvp(args[0], args);
            perror("minishell"); 
            exit(1);
        } 
        else {
            int wstatus;
            waitpid(pid, &wstatus, WUNTRACED); 
            if (WIFEXITED(wstatus)) {
                last_exit_status = WEXITSTATUS(wstatus);
            }
        }
    }
    else {
        // Multi-pipe execution
        if (call_n_pipe(no_of_args, command_count, args) == 1) {
            // Error already printed by call_n_pipe
        }
    }
}
