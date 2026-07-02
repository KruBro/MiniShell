#include "minishell.h"

void signal_handler(int sig, siginfo_t *info, void *data) {
    (void)info; // unused - kept because SA_SIGINFO requires this signature
    (void)data; // unused

    if (sig == SIGINT) {
        write(STDOUT_FILENO, "\n", 1);
    }
    else if (sig == SIGTSTP) {
        write(STDOUT_FILENO, "\n", 1);
    }
    else if (sig == SIGCHLD) {
        pid_t pid;
        int wstatus;
        
        while ((pid = waitpid(-1, &wstatus, WNOHANG | WUNTRACED)) > 0) {
            
            // 1. Process finished normally OR was killed by Ctrl+C (SIGINT)
            if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
                if (WIFEXITED(wstatus)) {
                    last_exit_status = WEXITSTATUS(wstatus);
                }
                
                // If it was killed by a signal like Ctrl+C, update the status
                if (WIFSIGNALED(wstatus)) {
                    last_exit_status = 128 + WTERMSIG(wstatus); 
                }

                // This safely removes the job if it was in the background/list
                remove_job(pid); 
            }
            
            // 2. Process was stopped by Ctrl+Z (SIGTSTP)
            else if (WIFSTOPPED(wstatus)) {
                // Only add it to the list if it isn't already there
                // (e.g. it may already be tracked if this is a job that
                // was fg'd and got Ctrl+Z'd a second time).
                if (!job_exists(pid)) {
                    // current_fg_cmd now always holds the FULL command
                    // line of whatever is currently in the foreground
                    // (set by handle_redirection_and_piping / call_n_pipe
                    // / bring_job_to_foreground), not just argv[0].
                    add_job(pid, current_fg_cmd);
                }
                
                // Async-signal-safe way to print a notification to the user
                char msg[] = "\n" COLOR_YELLOW "[Suspended]" COLOR_RESET "\n";
                write(STDOUT_FILENO, msg, sizeof(msg) - 1);
            }

            // SYNCHRONIZATION: Wake up the main shell loop / fg's wait loop
            if (pid == foreground_pid) {
                foreground_pid = 0; 
            }
        }
    }
}

void init_signal_handlers() {
    // Initialize signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_sigaction = signal_handler;
    // BUGFIX: SA_SIGINFO was missing. Without it, the kernel treats
    // sa_handler (which shares a union with sa_sigaction) as a plain
    // one-argument handler instead of the three-argument form actually
    // implemented here - the handler was never correctly installed.
    // SA_RESTART additionally keeps interruptible syscalls (like the
    // fgets() in main_loop) from returning EINTR every time a
    // background job changes state.
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGINT, &sa, NULL) < 0)
    {
        perror("minishell: sigaction(SIGINT)");
        return;
    }
    if(sigaction(SIGTSTP, &sa, NULL) < 0)
    {
        perror("minishell: sigaction(SIGTSTP)");
        return;
    }
    if(sigaction(SIGCHLD, &sa, NULL) < 0)
    {
        perror("minishell: sigaction(SIGCHLD)");
        return;
    }
}