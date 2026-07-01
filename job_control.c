#include "minishell.h"


// Job *job_list = NULL;
// int next_job_id = 1;

// Add this near your other job control functions (add_job, remove_job, etc.)
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
        printf("[%d] %d %s\n", temp->job_id, temp->pid, temp->command);
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
        fprintf(stderr, "minishell: fg: %d: no such job\n", job_id);
        return;
    }
    
    pid_t target_pid = temp->pid;
    foreground_pid = target_pid;

    // UX: Print the command that is taking over the screen
    printf("%s\n", temp->command);

    // SYSTEM SAFETY: Ignore terminal signals while we shift terminal control
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    // 1. Give terminal to child
    tcsetpgrp(STDIN_FILENO, target_pid);
    
    // 2. Wake child up
    kill(target_pid, SIGCONT);

    // 3. Wait for child to exit or pause
    int wstatus;
    waitpid(target_pid, &wstatus, WUNTRACED);

    // 4. Shell takes terminal back
    tcsetpgrp(STDIN_FILENO, getpid());

    // SYSTEM SAFETY: Restore default signal behavior
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);

    // 5. Cleanup and State update
    if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
        if (WIFEXITED(wstatus)) {
            last_exit_status = WEXITSTATUS(wstatus);
        }
        remove_job(target_pid);
    }
}

void send_job_to_background(int job_id) {
      Job *temp = job_list;
    while (temp != NULL && temp->job_id != job_id) {
        temp = temp->next;
    }

    // UX: Report if the job isn't found
    if (temp == NULL) {
        fprintf(stderr, "minishell: bg: %d: no such job\n", job_id);
        return;
    }
    
    pid_t target_pid = temp->pid;

    printf("[%d]  %s &\n", job_id, temp->command);
    kill(target_pid, SIGCONT);
}
