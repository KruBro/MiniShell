#include "minishell.h"

// Note: Changed back to the standard 1-arg signature for sa_handler compatibility
void signal_handler(int sig, siginfo_t *info, void *data) {
    if (sig == SIGINT) {
        write(STDOUT_FILENO, "\n", 1);
    }
    else if (sig == SIGTSTP) {
        write(STDOUT_FILENO, "\n", 1);
    }
    else if (sig == SIGCHLD) {
        pid_t pid;
        int wstatus;
        
        // CRITICAL: Parentheses around the assignment to ensure pid gets the actual ID, not a boolean
        while ((pid = waitpid(-1, &wstatus, WNOHANG | WUNTRACED)) > 0) {

            if(pid == foreground_pid)
                foreground_pid = 0;
            
            if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
                if (WIFEXITED(wstatus)) {
                    last_exit_status = WEXITSTATUS(wstatus);
                }
                
                // UX: Standard shells notify the user when a background job finishes
                // (Optional: You can add a check here to ensure it's a background job before printing)
                
                // Remove the job using the correctly captured pid
                remove_job(pid);
            }
        }
    }
}

void init_signal_handlers() {
    // Initialize signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_sigaction = signal_handler;
    sa.sa_flags = 0;

    if(sigaction(SIGINT, (const struct sigaction *)&sa, NULL) < 0)
    {
        perror("sigaction");
        return;
    }
    if(sigaction(SIGTSTP, (const struct sigaction *)&sa, NULL) < 0)
    {
        perror("sigaction");
        return;
    }
    if(sigaction(SIGCHLD, (const struct sigaction *)&sa, NULL) < 0)
    {
        perror("sigaction");
        return;
    }
}