#include "minishell.h"

// Global job list
Job *job_list = NULL;
int next_job_id = 1;


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
    // Check if the command is a built-in command (e.g., cd, exit, fg, bg)
    if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "exit") == 0 ||
        strcmp(args[0], "fg") == 0 || strcmp(args[0], "bg") == 0 ||
        strcmp(args[0], "jobs") == 0) {
        return 1;
    }
    return 0;
}

void handle_builtin_command(char **args) {
    // Handle built-in commands
    // cd, exit, fg, bg, jobs etc
}

void handle_redirection_and_piping(char **args) {
    // Handle redirection and piping
    // This is a placeholder for actual implementation
}

void signal_handler(int sig) {
    // Handle signals
}

void init_signal_handlers() {
    // Initialize signal handlers

}