/* This is the only file you should update and submit. */

/* Fill in your Name and GNumber in the following two comment fields
 * Name: Piyush Budhathoki
 * GNumber: G01187180
 */

#include <sys/wait.h>
#include "taskctl.h"
#include "parse.h"
#include "util.h"   

/* Constants */
#define DEBUG 0

//static const char *task_path[] = { "./", "/usr/bin/", NULL };
static const char *instructions[] = { "quit", "help", "list", "purge", "exec", "bg", "kill", "suspend", "resume", "pipe", NULL};

/* Node struct for linked list structure. */
typedef struct Process_Node{
    pid_t pid;  //pid of process
    int exitCode;   //Exit code of process
    int backGround; // If back ground process
    int status; // Status of process
    char *command; // Full command line string
    Instruction *inst; // Instruction
    struct Process_Node *next; // Next node

}Process_Node;

/* Pointer to head of linked list
 * to store instructions. */
Process_Node *head;

int currentTaskNum;

struct sigaction actChild;
struct sigaction actKey;

/* Finds the node with specified task number.
 * Returns node with instruciton with correct task num, or NULL on failure. */
Process_Node* getTaskNode(pid_t pid);

/* Finds the node with specified pid.
 * Returns node with instruciton with correct pid, or NULL on failure. */
Process_Node* getPidNode(pid_t pid);

/* Sends specified signal with logs related to specified node.
 * Also handles sending signals with keybaord inputs. */
void sendSig(Process_Node *node, int sig, int kB);

/* Child handler primarily for background processes.
 * Uses pid of reaped child to find the node of the process.
 * Also handles SIGINT, SIGTSTP, and SIGCONT */
void bg_handler(int sig){
    pid_t pid;  //Pid of process
    int child_status;  //Child exit status information

    //Reaps all current background processes
    //Also decects process status change, suspend or resume.
    while((pid = waitpid(-1, &child_status, WNOHANG | WUNTRACED | WCONTINUED)) > 0){
        Process_Node* node = getPidNode(pid);  //Get the node from pid

        //Error from waitpid results in -1 pid
        if(node == NULL){
            return;
        }
        
        //Checks for normal termination and stores exit code
        if(WIFEXITED(child_status)){
            node -> exitCode = WEXITSTATUS(child_status);
        }
        
        //Checks if child terminated by signal.
        //If so, updates the node and displays status change to terminated by signal
        if(WIFSIGNALED(child_status)){
            if(WTERMSIG(child_status) == SIGINT){
                node -> status = LOG_STATE_KILLED;
                log_kitc_status_change(node -> inst -> num, pid, node -> backGround, node -> command, LOG_TERM_SIG);
                return;
            }
        }

        //Checks if child stopped by signal.
        //If so, updates the node and displays status change to stopped
        else if(WIFSTOPPED(child_status)){
            if(WSTOPSIG(child_status) == SIGTSTP){
                node -> status = LOG_STATE_SUSPENDED;
                log_kitc_status_change(node -> inst -> num, node -> pid, node -> backGround, node -> command, LOG_SUSPEND);
                return;
            }
        }

        //Checks if child is being resumed by signal.
        //If so, updates the node and displays status change to running
        else if(WIFCONTINUED(child_status)){
            node -> status = LOG_STATE_RUNNING;
            log_kitc_status_change(node -> inst -> num, node -> pid, node -> backGround, node -> command, LOG_RESUME);
            return;
        }
        node -> status = LOG_STATE_FINISHED;
        log_kitc_status_change(node -> inst -> num, pid, LOG_BG, node -> command, LOG_TERM);
    }
    
}

/* Child handler primarily for foreground processes.
 * Uses pid of reaped child to find the node of the process.
 * Also handles child status change from signals. */
void fg_handler(int sig){
    
    int child_status;   //Child exit status information
    //Reaps dead child and detects process change 
    pid_t pid = waitpid(-1, &child_status, WUNTRACED | WNOHANG);  
    Process_Node *node = getPidNode(pid);

    if(node == NULL){
        return;
    }

    //Checks if child terminated by signal.
    //If so, updates the node and displays status change to terminated by signal
    if(WIFSIGNALED(child_status)){
        if(WTERMSIG(child_status) == SIGINT){
            node -> status = LOG_STATE_KILLED;
            log_kitc_status_change(node -> inst -> num, pid, node -> backGround, node -> command, LOG_TERM_SIG);
            return;
        }
    }

    //Checks if child stopped by signal.
    //If so, updates the node and displays status change to stopped
    else if(WIFSTOPPED(child_status)){
        if(WSTOPSIG(child_status) == SIGTSTP){
            node -> status = LOG_STATE_SUSPENDED;
            log_kitc_status_change(node -> inst -> num, node -> pid, node -> backGround, node -> command, LOG_SUSPEND);
            return;
        }
    }
    if(WIFEXITED(child_status)){
        node -> exitCode = WEXITSTATUS(child_status);
        node -> status = LOG_STATE_FINISHED;
        log_kitc_status_change(node -> inst -> num, node -> pid, node -> backGround, node -> command, LOG_TERM);
        return;
    }
    log_kitc_status_change(node -> inst -> num, node -> pid, node -> backGround, node -> command, LOG_TERM);
    node -> status = LOG_STATE_FINISHED;
}

void pipe_handler(int sig){
    pid_t pid;  //Pid of process
    int child_status;  //Child exit status information

    while((pid = waitpid(-1, &child_status, WNOHANG | WUNTRACED | WCONTINUED)) > 0){
        Process_Node* node = getPidNode(pid);  //Get the node from pid

        //Error from waitpid results in -1 pid
        if(node == NULL){
            return;
        }

        node -> status = LOG_STATE_FINISHED;
        log_kitc_status_change(node -> inst -> num, pid, LOG_BG, node -> command, LOG_TERM);
    }
}

/* Handles SIGINT and SIGTSTP from keyboard inputs(^C, ^Z).
 * Limited to foreground process.
 * Uses global variable currentTaskNum to get the node to send signals to. */
void key_handler(int sig){
    //Gets the node from global var
    Process_Node *node = getTaskNode(currentTaskNum);
    //No current foreground process
    if(node == NULL){
        return;
    }
    //Only sends signals if the process is already running
    if(node -> status != LOG_STATE_RUNNING){
        return;
    }
    
    //Sends the signal to specified node
    sendSig(node, sig, 1);
}

/* Blocks SIGCHLD.
 * unblock as true indicates unblocking. */
void blockSig(int unblock){
    //Initialize sig set
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);

    //If unblock is true, unblock SIGCHLD
    if(unblock){
        sigprocmask(SIG_UNBLOCK, &block, NULL);
        return;
    }
    sigprocmask(SIG_SETMASK, &block, NULL);
}

/* Copy an instruction to another instruction pointer.
 * Frees memory for command and inst but not for next.
 * Returns 0 on success and -1 otherwise. */
int cpyInst(Instruction inst, Instruction *pInst){
    //Free existing command string
    free(pInst -> instruct);
    //Store new copy of command string
    pInst -> instruct = string_copy(inst.instruct);
    pInst -> num = inst.num;  //Task number 1
    pInst -> num2 = inst.num2;  //Task number 2

    //if existing in or out file
    if(inst.infile != NULL){
        *pInst -> infile = *string_copy(inst.infile);
    }
    if(inst.outfile != NULL){
        *pInst -> outfile = *string_copy(inst.outfile);
    }
    return 0;
}

/* Frees a node and the pointers within the node. */
void freeNode(Process_Node *node){
    free_instruction(node -> inst);
    free(node -> command);
    free(node);
}

/* Initializes new node. */
int newNode(Process_Node *new, Instruction *instruction, char *cmd){
    if(new == NULL){
        return -1;
    }

    // Default values
    new -> pid = 0;
    new -> exitCode = 0;
    new -> backGround = LOG_FG;
    new -> status = LOG_STATE_READY;
    new -> next = NULL;

    // Instruction
    new -> inst = instruction;

    // Command line
    new -> command = cmd;

    //
    return 0;
}

/* Size of list. */
int listSize(){
    if(head == NULL){
        return 0;
    }
    Process_Node *current = head;
    int count = 0;
    while(current != NULL){
        if(current -> inst == NULL){
            count--;
        }
        current = current -> next;
        count++;
    }

    return count;
}

/* Insert new Instruction. Places node in the next sequential task number spot.
 * Returns 0 on success and -1 otherwise.
 */
int addNode(Instruction *instruction, char *cmd){
    //Test for NULL head or instruction
    if(instruction == NULL){
        return -1;
    }

    //Empty head case
    if(head == NULL){
        head = malloc(sizeof(Process_Node));
        instruction -> num = 0;
        newNode(head, instruction, cmd);
        return 0;
    }    

    Process_Node *current = head;
    Process_Node *new = malloc(sizeof(Process_Node));

    if(newNode(new, instruction, cmd)){  //Create new node
        return -1;
    }

    //Next avalible task number at start of list
    if(head -> inst -> num != 0){
        new -> next = head;
        head = new;
        return 0;
    }

    // Iterate through list
    while(current -> next != NULL){

        // Check for spot without sequential task number
        if(current -> inst -> num + 1 != current -> next -> inst -> num){
            Process_Node *temp = current -> next;
            new -> inst -> num = (current -> inst -> num) + 1; // Assigns insert node the next sequential task number
            current -> next = new; // Inserts new node
            new -> next = temp;
            return 0;
        }

        current = current -> next;
    }

    // End of list case
    current -> next = new;
    new -> inst -> num = (current -> inst -> num) +1;

    return 0;
}

/* Removes node from list by task number.
 * Returns the status on success, -1 on failure, and the status number if
 * process is running or suspended.
 */
int purgeNode(int taskNum){
    if(head == NULL || taskNum < 0){
        return -1;
    }

    //Head is the node to be purged
    if(head -> inst -> num == taskNum){
        if(head -> status != LOG_STATE_RUNNING && head -> status != LOG_STATE_SUSPENDED){
            Process_Node *temp = head;
            head = head -> next;
            freeNode(temp);
        }
        return head -> status;
    }

    Process_Node *current = head;
    while(current -> next != NULL){
        if(current -> next -> inst -> num == taskNum){  //Check for matching taskNum
            int retStatus = current -> next -> status;
            if(retStatus != LOG_STATE_RUNNING && retStatus != LOG_STATE_SUSPENDED){ //Check for running or suspended
                Process_Node *temp = current -> next;
                current -> next = current -> next -> next;
                //Free the removed node
                freeNode(temp);
            }
            return retStatus;
        }

        current = current -> next;
    }

    return -1;
}

/* Gets node at position index.
 * Returns the node at index on success or NULL on failure. */
Process_Node* getNode(int index){
    // NULL head
    if(head == NULL){
        return NULL;
    }

    Process_Node *current = head;
    while(current != NULL){
        if(index <= 0){
            return current;
        }
        index--;
        current = current -> next;
    }
    return NULL;
}

/* Finds the node with specified task number.
 * Returns node with instruciton with correct task num, or NULL on failure. */
Process_Node* getTaskNode(int taskNum){
    // NULL head
    if(head == NULL){
        return NULL;
    }

    Process_Node *current = head;
    while(current != NULL){
        if(current -> inst -> num == taskNum){
            return current;
        }
        current = current -> next;
    }
    return NULL;
}

/* Finds the node with specified pid.
 * Returns node with instruciton with correct pid, or NULL on failure. */
Process_Node* getPidNode(pid_t pid){
    // NULL head
    if(head == NULL){
        return NULL;
    }

    Process_Node *current = head;
    while(current != NULL){
        if(current -> pid == pid){
            return current;
        }
        current = current -> next;
    }
    return NULL;
}

/* Frees the entire list. */
void freeList(){
    Process_Node *temp;
    Process_Node *current = head;
    while(current != NULL){
        temp = current;
        current = current -> next;
        freeNode(temp);
    }
}

/* Splits string by provided delimiter and stores each token into a specified
 * array. Adds NULL terminator. 
 * Returns 0 on success and -1 otherwise*/
int stringSplit(char *list[], char* string, char* delim){
    int i = 0;
    char* ptr = strtok(string, delim); //Generate token
    while(ptr != NULL){
        list[i] = ptr; //Add each token to list
        ptr = strtok(NULL, delim); //Generate new token
        i++;
    }
    list[i] = NULL; //NULL terminator
    return 0;
}

/* Finds the correct path to the file */
void findPath(char *path, char *file){
    //Tests "./" location first
    strcpy(path, "./");
    strncat(path, file, strlen(file));

    //Checks if valid file location
    //0 if it exists
    if(access(path, F_OK)){
        //Checks "/usr/bin/" location next
        path[0] = '\0';
        strcpy(path, "/usr/bin/");
        strncat(path, file, strlen(file));

        //Check for valid location
        //0 if it exists
        if(access(path, F_OK)){
            path = NULL;
            return;
        }
        return;
    }
    return;
}

/* Handles errors with a node before executing instruction. */
int handleExeErr(Process_Node *node, int taskNum){
    //Task number not found in list
    if(node == NULL){
        log_kitc_task_num_error(taskNum);
        return -1;
    }

    //Check if task is in running or suspended state
    if(node -> status == LOG_STATE_RUNNING || node -> status == LOG_STATE_SUSPENDED){
        log_kitc_status_error(taskNum, node -> status);
        return -1;
    }
    return 0;
}

/* Executes a command. 
 * Returns 0 on success, -1*/
int execCmd(Process_Node *eNode, int BG, int pipefd[]){

    Instruction *eInst = eNode -> inst;

    //Splits the command attached to the instruction by " "
    char *command[MAXARGS+1];
    stringSplit(command, string_copy(eNode -> command), " ");
    
    actKey.sa_handler = key_handler;

    //Set up background specific elements of the process
    //If background tast, use the bg_handler
    if(BG){
        actChild.sa_handler = bg_handler;
        eNode -> backGround = LOG_BG;
        
    }
    //Set up foreground specific elements of the process
    //If foreground task and not using pipes, use fg_handler
    else{
        actChild.sa_handler = fg_handler;
        currentTaskNum = eInst-> num;
        eNode -> backGround = LOG_FG;
    }

    //Handler for if pipe
    if(pipefd != NULL){
        actChild.sa_handler = pipe_handler;
    }
    
    //Assign handlers for signals
    sigaction(SIGINT, &actKey, NULL);
    sigaction(SIGTSTP, &actKey, NULL);
    sigaction(SIGCHLD, &actChild, NULL);


    //Display the current process as running 
    log_kitc_status_change(eInst -> num, eNode -> pid, eNode -> backGround, eNode -> command, LOG_START);
    eNode -> status = LOG_STATE_RUNNING;

    //Fork process and store the child pid
    pid_t child_pid = fork();
    eNode -> pid = child_pid;

    if(!child_pid){
        if(BG){
            setpgid(0,0);
        }

        //Check if pipes used and setup the pipe redirection
        if(pipefd != NULL){
            //Background process is the writing side
            if(eNode -> backGround){
                //Close read end
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO); 
            }
            //Foreground process is the reading side
            else{
                dup2(pipefd[0], STDIN_FILENO);
            }
        }

        //If infile points to something
        if(eInst -> infile){
            int fdIn = open(eInst -> infile, O_RDONLY);     //Open file with read only
            dup2(fdIn, STDIN_FILENO);   //Set the process's input to the file
        }

        //if outfile points to something
        if(eInst -> outfile){
            //Opens the file with write only, create is file does not exist,
            //and truncate if file already exists
            int fdOut = open(eInst -> outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fdOut, STDOUT_FILENO);     //Set the process's output to the file
        }
        //Create the proper path
        char path[MAXLINE];
        findPath(path, command[0]);
        //Run the command
        execv(path, command);
        exit(0);
    }

    //Waits to reap process if it is a foreground process
    if (!BG){
        sleep(5);
        if(waitpid(eNode -> pid, NULL, 0) != -1){
            //Display process as terminated
            log_kitc_status_change(eInst -> num, eNode -> pid, eNode -> backGround, eNode -> command, LOG_TERM);
            eNode -> status = LOG_STATE_FINISHED;
        }
        currentTaskNum = -1;
    }

    return 0;
}

/* Handles the execution of commands with pipes. */
void execPipe(int taskNum1, int taskNum2){

    //Sets up node1 and node2
    Process_Node *node1 = getTaskNode(taskNum1);
    if(handleExeErr(node1, taskNum1)){
        return;
    }
    Process_Node *node2 = getTaskNode(taskNum2);
    if(handleExeErr(node2, taskNum2)){
        return;
    }

    //Set up the pipe
    int pipefd[2];
    if(pipe(pipefd)){
        log_kitc_file_error(taskNum1, LOG_FILE_PIPE);
        return;
    }

    //Display message indicating pipe
    log_kitc_pipe(taskNum1, taskNum2);

    //Prepare node1 as a background task
    node1 -> backGround = LOG_BG;
    //Use the execCmd to execute node1 with pipefd
    execCmd(node1, LOG_BG, pipefd);
    close(pipefd[1]);  //Close write end

    //prepare node2 and run it as foreground task
    node2 -> backGround = LOG_FG;
    execCmd(node2, LOG_FG, pipefd);

    close(pipefd[0]);  //Close read end
    return;

}

/* Uses kill() to send signals. 
 * Has flag kB to indicate if the keyboard commands were called (^C, ^Z).
 * Only sends signals to foreground processes when keyboard commands are used. */
void sendSig(Process_Node *node, int sig, int kB){
    
    switch(sig){

        //Terminate Process
        case SIGINT:
            //If sent from keyboard
            if(kB){
                log_kitc_ctrl_c();
                //Sends only to foreground processes
                kill(-LOG_FG+1, sig);
            }
            else{
                log_kitc_sig_sent(LOG_CMD_KILL, node -> inst -> num, node -> pid);
                kill(node -> pid, sig);
            }
            
            break;
        case SIGTSTP:
            if(kB){
                log_kitc_ctrl_z();
                //Sends only to foreground processes
                kill(-LOG_FG+1, sig);
            }
            else{
                log_kitc_sig_sent(LOG_CMD_SUSPEND, node -> inst -> num, node -> pid);
                kill(node -> pid, sig);
            }
            break;
        case SIGCONT:
            log_kitc_sig_sent(LOG_CMD_RESUME, node -> inst -> num, node -> pid);
            kill(node -> pid, sig);
            break;
    }

    return;
}

void contLoop(char *cmd, char *argv[], Instruction *inst){
    free(cmd);
    cmd = NULL;
    free_command(inst, argv);
}

/* The entry of your task controller program */
int main() {

    //Initialized global variable used for signal handling
    currentTaskNum = -1;

    //create sigaction to handle SIGCHLD
    memset(&actChild, 0, sizeof(actChild));
    actChild.sa_handler = bg_handler;

    memset(&actKey, 0, sizeof(actKey));

    char cmdline[MAXLINE];        /* Command line */
    char *cmd = NULL;

    /* Inital Prompt and Welcome */
    log_kitc_intro();
    log_kitc_help();


    /* Shell looping here to accept user command and execute */
    while(1) {
        char *argv[MAXARGS+1];        /* Argument list */
        Instruction inst;           /* Instruction structure: check parse.h */

        /* Print prompt */
        log_kitc_prompt();

        /* Read a line */
        // note: fgets will keep the ending '\n'
	errno = 0;
        if (fgets(cmdline, MAXLINE, stdin) == NULL) {
            if (errno == EINTR) {
                continue;
            }
            exit(-1);
        }

        if (feof(stdin)) {  /* ctrl-d will exit text processor */
          exit(0);
        }

        /* Parse command line */
        if (strlen(cmdline)==1)   /* empty cmd line will be ignored */
          continue;     

        cmdline[strlen(cmdline) - 1] = '\0';        /* remove trailing '\n' */

        cmd = malloc(strlen(cmdline) + 1);          /* duplicate the command line */
        snprintf(cmd, strlen(cmdline) + 1, "%s", cmdline);

        /* Bail if command is only whitespace */
        if(!is_whitespace(cmd)) {
            initialize_command(&inst, argv);    /* initialize arg lists and instruction */
            parse(cmd, &inst, argv);            /* call provided parse() */

            if (DEBUG) {  /* display parse result, redefine DEBUG to turn it off */
                debug_print_parse(cmd, &inst, argv, "main (after parse)");
	        }

            /* After parsing: your code to continue from here */
            /*================================================*/
            
            if(!strcmp(inst.instruct, instructions[0])){ /* quit */
                log_kitc_quit();  /* Display quit information */
                freeList(head);
                exit(0);  /* Exit the process */
            }

            else if(!strcmp(inst.instruct, instructions[1])){ /* help */
                //Call help
                log_kitc_help();
            }

            else if(!strcmp(inst.instruct, instructions[2])){ /* list */

                //Display size of list
                log_kitc_num_tasks(listSize(head));
        
                Process_Node *current;
                for(int i = 0; i < listSize(head); i++){
                    current = getNode(i); // Gets node at position i
                    //Displays the retrieved node
                    log_kitc_task_info(current -> inst -> num, current -> status, 0, current -> pid, current -> command);
                    
                }
            }
            
            /* Remove operation from list. */
            else if(!strcmp(inst.instruct, instructions[3])){ /* purge */
                //Blocks any SIGCHLD during sensitive operations
                blockSig(0);
                int taskNum = inst.num;
                //Purges node and displays based on return value
                int retStatus = purgeNode(taskNum);
                switch(retStatus){
                    //No process with entered task number
                    case -1:
                        log_kitc_task_num_error(taskNum);
                        break;
                    //
                    case 1:
                        log_kitc_status_error(taskNum, retStatus);
                        break;
                    case 2:
                        log_kitc_status_error(taskNum, retStatus);
                        break;
                    default:
                        log_kitc_purge(taskNum);
                        break;
                }
                //Unblock
                blockSig(1);
            }

            /* Run process as foreground process (exec) or as a background process (bg). */
            else if(!strcmp(inst.instruct, instructions[4]) || !strcmp(inst.instruct, instructions[5])){ /* exec or bg */
                int bg;
                if(!strcmp(inst.instruct, instructions[5])){bg = LOG_BG;}
                else{bg = LOG_FG;}

                char *eCommand[MAXARGS+1];  //Command line that called exec
                char *cmdCopy = string_copy(cmd);
                stringSplit(eCommand, cmdCopy, " ");
                int taskNum = inst.num;    //Task Number called with exec

                //Node with instruction to be executed
                Process_Node *eNode = getTaskNode(taskNum);  

                if(handleExeErr(eNode, taskNum)){
                    	contLoop(cmd, argv, &inst);
                        continue;
                }

                Instruction *eInst = eNode -> inst;     //Instruction within the node

                //Searches through command line args for "<" or ">" to indicate file redirection
                for(int i = 0; eCommand[i] != NULL; i++){

                    //Check for <
                    if(!strcmp(eCommand[i], "<")){
                        eInst -> infile = string_copy(eCommand[i+1]);
                        log_kitc_redir(taskNum, LOG_REDIR_IN, eInst -> infile);

                    }

                    //Check for >
                    if(!strcmp(eCommand[i], ">")){
                        eInst -> outfile = string_copy(eCommand[i+1]);
                        log_kitc_redir(taskNum, LOG_REDIR_OUT, eInst -> outfile);
                    }
                }

                if(execCmd(eNode, bg, NULL)){
                    log_kitc_exec_error(eNode -> command);
                }

                //Frees no longer need in and out files
                free(eNode -> inst -> infile);
                eNode -> inst -> infile = NULL;
                free(eNode -> inst -> outfile);
                eNode -> inst -> outfile = NULL;
                //free the string copy
                free(cmdCopy);
            }

            /* User command kill, suspend, and resume only affect background processes. */
            else if(!strcmp(inst.instruct, instructions[6]) ||
                    !strcmp(inst.instruct, instructions[7]) ||
                    !strcmp(inst.instruct, instructions[8])){ /* kill, suspend, and resume */

                //Check for which signal to send to process
                int sig;
                if(!strcmp(inst.instruct, instructions[6])){sig = SIGINT;}
                else if(!strcmp(inst.instruct, instructions[7])){sig = SIGTSTP;}
                else{sig = SIGCONT;}

                //Get the task number of process to send signal to
                int taskNum = inst.num;
                //Process
                Process_Node *kNode = getNode(taskNum);

                //No matching task num
                if(kNode == NULL){
                    log_kitc_task_num_error(taskNum);
                    contLoop(cmd, argv, &inst);
                    continue;
                }

                //Check if process is already idle
                switch(kNode -> status){
                    case LOG_STATE_READY:
                    case LOG_STATE_FINISHED:
                    case LOG_STATE_KILLED:
                        log_kitc_status_error(taskNum, kNode -> status);
                        contLoop(cmd, argv, &inst);
                        continue;
                }

                //Sends specified signal
                sendSig(kNode, sig, 0);
            }

            else if(!strcmp(inst.instruct, instructions[9])){ /* pipe */
                execPipe(inst.num, inst.num2);
            }

            else{ /* New user command */
                blockSig(0);
                
                //Allocate space for new instruction to be added to list
                Instruction *newInst = malloc(sizeof(Instruction));

                initialize_instruction(newInst); 

                //Copies the values of inst to newInst
                if(cpyInst(inst, newInst)){ 
                    contLoop(cmd, argv, &inst);
                    blockSig(1);
                    continue;
                }

                addNode(newInst, string_copy(cmd));
                log_kitc_task_init(newInst -> num, cmd);
                blockSig(1);
            }


        }  // end if(!is_whitespace(cmd))

	contLoop(cmd, argv, &inst);
    }  // end while(1)

    return 0;
}  // end main()