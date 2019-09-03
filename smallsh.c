/************************************************************
*
* smallsh.c
*
* -----------------
* EvVikare
* evvikare@protonmail.com
* CS344 -- Summer 2017
* Program 3 Assignment
* -----------------
*
* A lightweight shell resembling bash 
*
*************************************************************/

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_SIZE_COMMAND_LINE 2048
#define MAX_NUM_ARGS 512

typedef struct sigaction Sigact;
typedef struct Command{
    char* shell_pid;
    char* user_input;
    char* tokens[MAX_NUM_ARGS];
    short count;
}Command;

void configure_signal(int, Sigact*, int, void (*sig_func)(int));
void smallsh_SIGTSTP(int);
void configure_command_struct(Command*);
void receive_command(Command*);
void tokenize_command(Command*);
void parse_command(Command*);
void clear_command(Command*);
void fork_off(Command*);
void check_for_finished_bg_job();
void find_and_replace_dsvar(Command*, short);
short set_cl_opts(Command*, short*, short);
void process_cl_opts(Command*, short*, short*);
char* get_filename(Command*, short, short);
void set_fd(Command*, short, short);
void get_fd(Command*, char*, short*);
short is_builtin(Command*);
void smallsh_cd(Command*);
void smallsh_status();
void smallsh_exit(Command*);
void shell_fail(char*);
void child_fail(Command*);
void report(short, short);
void cleanup(Command*);
short count_tokens(Command*);

size_t buffersize = MAX_SIZE_COMMAND_LINE + 1;
static volatile sig_atomic_t fg_only = 0;
int last_ex = 0, last_sig = 0, bg_jobs = 0;

Sigact INT_action = {0}, TSTP_action = {0}, QUIT_action = {0};

int main(int argc, char** argv){

    Command cmd = {0};
    configure_command_struct(&cmd);

    configure_signal(SIGINT, &INT_action, SA_RESTART, SIG_IGN);
    configure_signal(SIGQUIT, &QUIT_action, SA_RESTART, SIG_IGN);
    configure_signal(SIGTSTP, &TSTP_action, SA_RESTART, smallsh_SIGTSTP);

    while(1){
        receive_command(&cmd);
        tokenize_command(&cmd);
        parse_command(&cmd);
        clear_command(&cmd);
    }
    
    return 0;
}

/* ---- smallsh_SIGTSTP

  Signal handler for TSTP
 
  rtn:  none
  prm:  signo - signal number 
  pre:  none
  pst:  fg_only global is toggled

*/
void smallsh_SIGTSTP(int signo){
    char* on_msg = "\nEntering foreground-only mode (& is now ignored)\n: ";
    char* off_msg = "\nExiting foreground-only mode\n: ";
    fg_only ? write(1, off_msg, 32) : write(1, on_msg, 52);
    fg_only ^= 1;
}

/* ---- configure_signal

  Wrapper for sigaction
 
  rtn:  none
  prm:  sig - signal number
        sa_p - pointer to signal action struct
        flag - sigaction flag
        sig_func - function pointer to signal handler 
  pre:  none
  pst:  a signal action is set

*/
void configure_signal(int sig, Sigact* sa_p, int flag, void (*sig_func)(int)){
    sigemptyset(&(sa_p->sa_mask));
    sa_p->sa_flags = flag;
    sa_p->sa_handler = sig_func;
    sigaction(sig, sa_p, NULL);
}

/* ---- configure_command_struct

  Allocate memory for command struct members
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  user input and shell pid have allocated memory
        token count is initialized to zero

*/
void configure_command_struct(Command* cmd){
    cmd->user_input = (char*) malloc(buffersize);
    if(cmd->user_input == NULL){ shell_fail("User Input malloc failed.\n"); }
    
    cmd->shell_pid = (char*) malloc(16);
    if(cmd->shell_pid == NULL){ shell_fail("PID location malloc failed.\n"); }
    sprintf(cmd->shell_pid, "%d", getpid());

    cmd->count = 0;
}

/* ---- receive_command

  Prompt user for a command and store the result
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  user input is stored as a string in command struct
        an insufficient input is replaced with a comment by default

*/
void receive_command(Command* cmd){
    check_for_finished_bg_job();
    printf(": "); 
    fflush(stdout);

    short rv = getline(&cmd->user_input, &buffersize, stdin);
    if(rv < 2){
        cmd->user_input[0] = '#';
        rv = 2;
    }
    cmd->user_input[rv - 1] = '\0'; // Replace getline's trailing newline
}

/* ---- tokenize_command

  Store user input string as an array of tokens
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  command's token array is populated

*/
void tokenize_command(Command* cmd){
    char* tok_p;
    short i = 0;
    char* copy = strdup(cmd->user_input);

    for(tok_p = strtok(copy, " "); tok_p; tok_p = strtok(NULL, " "), i++){
        cmd->tokens[i] = strdup(tok_p);
    }
    free(copy);
}

/* ---- parse_command

  Parse a command to determine if it is a comment, built-in, or needs a fork()
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  any instance of $$ is expanded into the shell's PID

*/
void parse_command(Command* cmd){
    if(cmd->tokens[0][0] == '#'){ return; } // Newline or comment

    short i;
    cmd->count = count_tokens(cmd);

    for(i = 0; i < cmd->count; i++){
        if(strstr(cmd->tokens[i], "$$")){
            find_and_replace_dsvar(cmd, i); // Expand any "$$" into shell's PID
        }
    }

    if(is_builtin(cmd)){ return; }          // Built-in commands
    fork_off(cmd);                          // All other commands
}

/* ---- clear_command

  Clear the data associated with a command
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  all command tokens are cleared and freed
        the user input string is set to all null terminators

*/
void clear_command(Command* cmd){
    int k = 0;
    for(k = 0; k < MAX_NUM_ARGS; k++){
        if(cmd->tokens[k]){
            free(cmd->tokens[k]);
            cmd->tokens[k] = NULL;
        }
    }
    memset(cmd->user_input, '\0', buffersize);
}

/* ---- fork_off

  Create child process for all non-builtin tasks
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  a child process is created
        the last_sig/last_ex globals are updated

*/
void fork_off(Command* cmd){
    short cl_opts[] = {0, 0, 0};
    short fds[] = {0, 1};
    short opts_count = sizeof(cl_opts)/sizeof(short);
    short end_of_args = set_cl_opts(cmd, cl_opts, opts_count);
    short is_bg_command = strcmp(cmd->tokens[cmd->count - 1], "&") == 0;
    short run_in_bg = is_bg_command && ( ! fg_only);

    pid_t fg_pid = fork();
    int fg_stat = -1;

    switch(fg_pid){
        case -1:
            shell_fail("Call to fork failed.\n");
            
        case 0:
            process_cl_opts(cmd, cl_opts, fds);
            cmd->tokens[end_of_args] = '\0';
            
            if( ! run_in_bg){
                configure_signal(SIGINT, &INT_action, 0, SIG_DFL);
            }
            configure_signal(SIGQUIT, &QUIT_action, SA_RESTART, SIG_DFL);
            
            if(execvp(cmd->tokens[0], cmd->tokens) < 0){
                printf("%s: no such file or directory\n", cmd->tokens[0]);
                child_fail(cmd);
            }
            
        default:
            if(run_in_bg){
                bg_jobs++;
                printf("background pid is %d\n", fg_pid);
                fflush(stdout);
            }else{
                waitpid(fg_pid, &fg_stat, 0);

                last_sig = WIFEXITED(fg_stat) ? 0 : WTERMSIG(fg_stat);
                last_ex = WIFSIGNALED(fg_stat) ? 0 : WEXITSTATUS(fg_stat);

                if(last_sig){ report(0, last_sig); }
            }
    }
}

/* ---- check_for_finished_bg_job

  Poll for finished background jobs and report any that have finished
 
  rtn:  none
  prm:  none
  pre:  none
  pst:  bg_jobs count is decremented for each finished job

*/
void check_for_finished_bg_job(){

    if(bg_jobs < 1){ return; }

    pid_t ch_pid;
    int ch_status, exited, term_val;
    
    while((ch_pid = waitpid(-1, &ch_status, WNOHANG)) > 0){
        bg_jobs--;
        exited = WIFEXITED(ch_status);
        term_val = (exited) ? WEXITSTATUS(ch_status) : WTERMSIG(ch_status);
        printf("background pid %d is done: ", ch_pid);
        report(exited, term_val);
    }
}

/* ---- find_and_replace_dsvar

  Find and replace instances of $$ in command with the shell's PID
 
  rtn:  none
  prm:  cmd - command struct
        idx - the index of the token containing $$ 
  pre:  none
  pst:  any token with $$ in its string will be freed and replaced

*/
void find_and_replace_dsvar(Command* cmd, short idx){ 
    short src_i, dst_i, pid_i;
    char buf[buffersize];
    memset(buf, '\0', buffersize);

    char* copy = strdup(cmd->tokens[idx]);
    free(cmd->tokens[idx]);
   
    for(src_i = 1, dst_i = 0; src_i - 1 < strlen(copy); src_i++, dst_i++){
        if((copy[src_i] == '$') && (copy[src_i - 1] == '$')){
            memcpy(&buf[dst_i], cmd->shell_pid, strlen(cmd->shell_pid)); 
            dst_i += strlen(cmd->shell_pid) - 1;
            src_i++;
        }else{
            memcpy(&buf[dst_i], &copy[src_i - 1], 1);
        }
    }    
    
    cmd->tokens[idx] = strdup(buf);
    free(copy);    
}

/* ---- set_cl_opts

  Redirection and background process options are set
 
  rtn:  the index of the first non-special token, OR the number of tokens
  prm:  cmd - command struct,
        options - array of options 
  pre:  none
  pst:  options array is populated with indices of redirection targets
        background option is either zero or index of & char (true or false)

*/
short set_cl_opts(Command* cmd, short* options, short opts_sz){
    short k, m; 
    short earliest = cmd->count;
    char* cl_chars[] = {"<", ">", "&"};
       
    for(k = 0; k < cmd->count; k++){
        for(m = 0; m < opts_sz; m++){
            if(strcmp(cmd->tokens[k], cl_chars[m]) == 0){
                earliest = (k < earliest) ? k : earliest;
                options[m] = k + 1;
            }
        }
    }

    return earliest;
}

/* ---- process_cl_opts

  Process the command line options: redirection and background
 
  rtn:  none
  prm:  cmd - command struct
        cl_opts - the command options (cleared or set to their respective index)
        fds - the I/O file descriptors of the current process
  pre:  none
  pst:  I/O redirection is set,
        any files are opened as requested in the command
  
*/
void process_cl_opts(Command* cmd, short* cl_opts, short* fds){
    short i;
    char *filename;

    for(i = 0; i < 2; i++){
        if( ! cl_opts[i] ){ continue; }
       
        filename = get_filename(cmd, cl_opts[2], cl_opts[i]);
        get_fd(cmd, filename, &fds[i]);
        set_fd(cmd, fds[i], i);
    }
}

/* ---- get_filename

  Get the name of a file to be used in redirection
 
  rtn:  a filename or "/dev/null"
  prm:  cmd - command struct
        hasAmp - indicates whether the last token is an ampersand
        redir_sym - the index of the file to be the subject of redirection
  pre:  none
  pst:  none
  
*/
char* get_filename(Command* cmd, short hasAmp, short redir_sym){
    return (fg_only || (!hasAmp) ) ? cmd->tokens[ redir_sym ] : "/dev/null";
}

/* ---- set_fd

  Redirect a custom file descriptor to match a standard counterpart
 
  rtn:  none
  prm:  cmd - command struct
        redir_fd- file descriptor to redirect
        std_fd - standard file descriptor
  pre:  none
  pst:  redir is pointed to the same location as std_fd
  
*/
void set_fd(Command* cmd, short redir_fd, short std_fd){
    if(dup2(redir_fd, std_fd) < 0){
        printf("Dup2 call failed.\n");
        child_fail(cmd);
    }
}

/* ---- get_fd

  Open a file for reading or writing by the shell
 
  rtn:  none
  prm:  cmd - command struct
        filename - file to be opened
        fd - file descriptor
  pre:  none
  pst:  a file is open and assigned a descriptor
  
*/
void get_fd(Command* cmd, char* filename, short* fd){
    char* direction = (*fd) ? "input" : "output";
    if(*fd){        
        *fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    }else{
        *fd = open(filename, O_RDONLY); 
    }
    
    if(*fd < 0){
        printf("cannot open %s for %s\n", filename, direction);
        child_fail(cmd);
    }
}

/* ---- is_builtin

  Determine if the first command token is a built-in of the shell
 
  rtn:  true, if the first command token is a built-in
  prm:  cmd - command struct
  pre:  none
  pst:  none
  
*/
short is_builtin(Command* cmd){

    short i;
    char* builtin_cmds[] = {"cd", "exit", "status"};
    void (*builtin_cmd_ptr[])(Command*) = {
        smallsh_cd, smallsh_exit, smallsh_status
    };

    // If first token is built-in, call respective function: exit, status, cd    
    for(i = 0; i < sizeof(builtin_cmds)/sizeof(char*); i++){
        if(strcmp(cmd->tokens[0], builtin_cmds[i]) == 0){
            (*builtin_cmd_ptr[i])(cmd);
            return 1;
        }
    }
   
    return 0;
}

/* ---- smallsh_cd

  Change the working directory
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  the working directory is set to either home or a given argument
  
*/
void smallsh_cd(Command* cmd){
    cmd->tokens[1] ? chdir(cmd->tokens[1]) : chdir(getenv("HOME"));
}

/* ---- smallsh_status

  Report the status of the last terminated foreground process
 
  rtn:  none
  prm:  none
  pre:  none
  pst:  none
  
*/
void smallsh_status(){
    short exited = (last_sig) ? 0 : 1;
    short term_val = (exited) ? last_ex : last_sig;
    report(exited, term_val);
}

/* ---- smallsh_exit

  Exit the shell
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  all background processes are terminated
        all associated memory is freed
        the shell process is terminated
  
*/
void smallsh_exit(Command* cmd){
    cleanup(cmd);
    kill(0, SIGQUIT);
    exit(0);
}

/* ---- shell_fail

  Print error message and exit if shell has a failure
 
  rtn:  none
  prm:  err_msg - an error message
  pre:  none
  pst:  the shell process is terminated
  
*/
void shell_fail(char* err_msg){
    perror(err_msg);
    exit(1);
}

/* ---- child_fail

  Perform clean-up and exit if child process has a failure
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  the child process is terminated
  
*/
void child_fail(Command* cmd){
    fflush(stdout);
    cleanup(cmd);
    exit(1);
}

/* ---- report

  Output the termination value of a process
 
  rtn:  none
  prm:  exited - whether the given process had an exit status
        term_value - the code associated with the process's termination
  pre:  none
  pst:  none
  
*/
void report(short exited, short term_value){
    char* msg = (exited) ? "exit value" : "terminated by signal";
    printf("%s %d\n", msg, term_value);
    fflush(stdout);
}

/* ---- cleanup

  Free memory associated with a command
 
  rtn:  none
  prm:  cmd - command struct
  pre:  none
  pst:  a command struct and its members are all freed
  
*/
void cleanup(Command* cmd){
    clear_command(cmd);
    free(cmd->user_input);
    free(cmd->shell_pid);
}

/* ---- count_tokens

  Count the number of tokens in a command
 
  rtn:  the number of counts in a command
  prm:  cmd - command struct
  pre:  none
  pst:  none
  
*/
short count_tokens(Command* cmd){
    short k = 0;
    while(cmd->tokens[k]){
        k++;
    }
    return k;
}
