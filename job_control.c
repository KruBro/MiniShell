#include "minishell.h"

int job_exists(pid_t pid) {
    Job *temp = job_list;
    while (temp != NULL) {
        if (temp->pid == pid) {
            return 1;
        }
        temp = temp->next;
    }
    return 0;
}

int get_last_job_id() {
    // If there are no background jobs, return -1 as an error code
    if (job_list == NULL) {
        return -1;
    }
    
    // Traverse to the very end of the list
    Job *temp = job_list;
    while (temp->next != NULL) {
        temp = temp->next;
    }
    
    // Return the ID of the final node
    return temp->job_id;
}

void add_job(pid_t pid, char *command) {
    Job *new_job = malloc(sizeof(Job));
    if (new_job == NULL) {
        perror("minishell: malloc failed for new job");
        return;
    }
  
    // to prevent buffer overflows
    strncpy(new_job->command, command, MAX_INPUT_SIZE - 1);
    new_job->command[MAX_INPUT_SIZE - 1] = '\0'; // Guarantee null termination
    
    new_job->pid = pid;
    new_job->job_id = next_job_id++;
    new_job->next = NULL;

    // Case 1: The list is empty
    if (job_list == NULL) {
        job_list = new_job;
        return;
    }

    // Case 2: Append to the end of the list
    Job *temp = job_list;
    while (temp->next != NULL) {
        temp = temp->next;
    }
    
    // Link the new node to the tail of the list
    temp->next = new_job;
}

void remove_job(pid_t pid) {
    if (job_list == NULL) {
        return;
    }
    
    // State 2: Target is the head node
    if (job_list->pid == pid) {
        Job *curr_job = job_list;
        job_list = job_list->next;
        free(curr_job);
        return;
    }

    // State 3: Traversal for middle/tail nodes
    Job *curr_job = job_list->next;
    Job *prev = job_list;

    while (curr_job != NULL && curr_job->pid != pid) {
        prev = curr_job;
        curr_job = curr_job->next;
    }

    // If we reached the end of the list and found nothing
    if (curr_job == NULL) {
        return; 
    }

    // Safely bypass and deallocate
    prev->next = curr_job->next;
    free(curr_job);
}

void list_jobs() {
    if (job_list == NULL) {
        return;
    }
    
    Job *temp = job_list;
    while (temp != NULL) {
        printf(COLOR_YELLOW "[%d]" COLOR_RESET " %d  " COLOR_CYAN "%s\n" COLOR_RESET,
               temp->job_id, temp->pid, temp->command);
        temp = temp->next;
    }
}

void bring_job_to_foreground(int job_id) {
    Job *temp = job_list;
    while (temp != NULL && temp->job_id != job_id) {
        temp = temp->next;
    }

    // UX: Report if the job isn't found
    if (temp == NULL) {
        fprintf(stderr, COLOR_RED "minishell: fg: %d: no such job\n" COLOR_RESET, job_id);
        return;
    }
    
    pid_t target_pid = temp->pid;

    // Track the full command so the SIGCHLD handler can label the job
    // correctly if it gets suspended again with Ctrl+Z.
    strncpy(current_fg_cmd, temp->command, MAX_INPUT_SIZE - 1);
    current_fg_cmd[MAX_INPUT_SIZE - 1] = '\0';

    // UX: Print the command that is taking over the screen
    printf(COLOR_CYAN "%s\n" COLOR_RESET, temp->command);

    // 1. Give terminal to the job's process group (each background job
    // is its own process group leader, set up when it was launched).
    tcsetpgrp(STDIN_FILENO, target_pid);

    // 2. Wake child up, then wait for the SIGCHLD handler to reap it
    // (exited) or re-suspend it (stopped again).
    //
    // BUGFIX: this used to call waitpid() directly in this function too,
    // which raced with the async SIGCHLD handler doing the same
    // waitpid(-1, ...) — whichever one reaped the child first would
    // leave the other with ECHILD, silently corrupting job/exit-status
    // state. wait_for_foreground() relies solely on the signal handler
    // (like every other wait point in this shell) and blocks SIGCHLD
    // around the check/sigsuspend so a fast-finishing child can't be
    // missed either.
    kill(target_pid, SIGCONT);
    wait_for_foreground(target_pid);

    // 3. Shell takes terminal back.
    // BUGFIX: the old code also called signal(SIGTTIN/SIGTTOU, SIG_DFL)
    // here, which permanently undid the shell's own protection against
    // being stopped by the kernel — after the FIRST "fg", any later
    // tcsetpgrp() call while the shell wasn't the foreground group could
    // stop the shell itself. The shell must ignore these forever.
    tcsetpgrp(STDIN_FILENO, shell_pgid);
}

void send_job_to_background(int job_id) {
    Job *temp = job_list;
    while (temp != NULL && temp->job_id != job_id) {
        temp = temp->next;
    }

    // UX: Report if the job isn't found
    // BUGFIX: this used to always say "fg:" even when called from bg.
    if (temp == NULL) {
        fprintf(stderr, COLOR_RED "minishell: bg: %d: no such job\n" COLOR_RESET, job_id);
        return;
    }
    
    pid_t target_pid = temp->pid;

    printf(COLOR_GREEN "[%d]  %s &\n" COLOR_RESET, job_id, temp->command);
    kill(target_pid, SIGCONT);
}