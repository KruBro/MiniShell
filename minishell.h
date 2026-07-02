#ifndef MINISHELL_H
#define MINISHELL_H

#define _XOPEN_SOURCE 700 

// 2. Standard C libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

// 3. System libraries for process handling (CRITICAL FIX)
#include <sys/types.h>  // Defines pid_t
#include <sys/wait.h>   // Defines waitpid and macros

// Define constants
#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_JOBS 64

// ---------------------------------------------------------------------
// UI colors (ANSI escape codes) - used only for shell "chrome": prompt,
// error messages, and job-control notifications. Program output itself
// is never colorized.
// ---------------------------------------------------------------------
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"

// Job control structures
typedef struct Job {
    int job_id;
    pid_t pid;
    char command[MAX_INPUT_SIZE];
    struct Job *next;
} Job;

// Simple redirection descriptor extracted from a single command's argv
typedef struct {
    char *infile;   // set if "<" was found
    char *outfile;  // set if ">" or ">>" was found
    int append;     // 1 if ">>" was used, 0 for ">"
} Redirection;

extern Job *job_list;
extern int next_job_id;
extern int last_exit_status;
extern volatile sig_atomic_t foreground_pid;
extern char current_fg_cmd[MAX_INPUT_SIZE];
extern pid_t shell_pgid;

// Function declarations
void init_shell();
void main_loop();
void parse_command(char *input, char **args);
int strip_background(char **args);
void trim_trailing_amp(char *cmd);
void wait_for_foreground(pid_t pid);
int execute_command(char **args, char *raw_cmd, int background);
int is_builtin_command(char **args);
void handle_builtin_command(char **args);
void handle_redirection_and_piping(char **args, char *raw_cmd, int background);
void signal_handler(int sig, siginfo_t *info, void *data);
void init_signal_handlers();
void add_job(pid_t pid, char *command);
void remove_job(pid_t pid);
void list_jobs();
void bring_job_to_foreground(int job_id);
void send_job_to_background(int job_id);
int call_n_pipe(int no_of_args, int command_count, char **args, char *raw_cmd);
int get_last_job_id();
int job_exists(pid_t pid);

// Redirection helpers
// int extract_redirection(char **argv, Redirection *redir);
// int apply_redirection(Redirection *redir);

#endif // MINISHELL_H