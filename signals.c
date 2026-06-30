#include "minishell.h"

void signal_handler(int sig, siginfo_t *info, void *data) {
    // Handle signals
    if(sig == SIGINT)
    {
        write(STDOUT_FILENO, "\n", 1);
    }
    else if(sig == SIGTSTP)
    {
        //Temp
        write(STDOUT_FILENO, "\n", 1);
    }
    else if(sig == SIGCHLD)
    {
        //
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
}