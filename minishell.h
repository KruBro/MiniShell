#ifndef MINISHELL_H
#define MINISHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

// Define constants
#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_JOBS 64

// Job control structures
typedef struct Job {
    int job_id;
    pid_t pid;
    char command[MAX_INPUT_SIZE];
    struct Job *next;
} Job;

// Function declarations
void init_shell();
void main_loop();
void parse_command(char *input, char **args);
int execute_command(char **args);
int is_builtin_command(char **args);
void handle_builtin_command(char **args);
void handle_redirection_and_piping(char **args);
void signal_handler(int sig);
void init_signal_handlers();
void add_job(pid_t pid, char *command);
void remove_job(pid_t pid);
void list_jobs();
void bring_job_to_foreground(int job_id);
void send_job_to_background(int job_id);

#endif // MINISHELL_H
