#define _GNU_SOURCE
#define __USE_POSIX

#include "ArchivosDados/parser.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define DEFAULT_BUFFER_SIZE 1024
#define MAX_JOBS 1024


#define TERMINATED 0
#define RUNNING 1

typedef struct{
    char * commands;
    pid_t lastPid;
    short state;
} job;

job jobs[MAX_JOBS];
int executing = 0;

void printPrompt(){
    char hostname[HOST_NAME_MAX];
    struct passwd *passwd_struct;
    char userSymbol;
    char *pwd;
    int erc;

    passwd_struct = getpwuid(getuid());

    erc = gethostname(hostname, HOST_NAME_MAX);

    if (erc == -1){
        fprintf(stderr, "Ha habido un problema al obtener el hostname\n");
        exit(-1);
    }

    pwd = get_current_dir_name(); //free this


    if (passwd_struct->pw_uid == 0){
        userSymbol='#';
    }else{
        userSymbol='$';
    }

    printf("%s@%s:%s%c ", passwd_struct->pw_name, hostname, pwd, userSymbol);

    free(pwd);
}

void handleSignals(){
    puts("");
    if (!executing)
        printPrompt();
}

void addJob(char * commands, pid_t pid){
    int num = -1;
    int commands_len = 0;
    for (int i = 0; i < MAX_JOBS; ++i){
        if (jobs[i].state == TERMINATED){
            num = i;
            break;
        }
    }
    if (num == -1){
        fprintf(stderr, "msh: No hay espacio para crear más jobs\n");
        return;
    }
    jobs[num].lastPid = pid;
    commands_len = strlen(commands);
    jobs[num].commands = malloc(commands_len);
    strcpy(jobs[num].commands, commands);
    jobs[num].commands[commands_len-1] = 0;
    jobs[num].state = RUNNING;
}

void removeJob(int toRemoveId){
    jobs[toRemoveId].state = TERMINATED;
    jobs[toRemoveId].lastPid = 0;
    free(jobs[toRemoveId].commands);
    jobs[toRemoveId].commands = NULL;
}

void getJobs(){
    for (int i = 0; i < MAX_JOBS; ++i){
        if (jobs[i].state != TERMINATED)
            printf("[%d] Ejecutando\t%s\n", i, jobs[i].commands);
    }
}

void job2Foreground(tline *line){
    int jobId = -1;
    if (line->commands[0].argc == 1){

        for (int i = 0; i < MAX_JOBS; ++i){
            if (jobs[i].state != TERMINATED)
                jobId = i;
        }
        if (jobId == -1){
            fprintf(stderr, "msh: fg: No hay ningún job actualmente\n");
            return;
        }

    }else{

        jobId = atoi(line->commands[0].argv[1]);
        if (jobId < 0 || jobId >= MAX_JOBS || jobs[jobId].state == TERMINATED){
            fprintf(stderr, "msh: fg: %d: No existe ese job\n", jobId);
            return;
        }
    
    }
    waitpid(jobs[jobId].lastPid, NULL, 0);
    removeJob(jobId);
}

void childHandler() {
    pid_t child_pid;
    int found = 0;
    while ((child_pid = waitpid(-1, NULL, WNOHANG | WUNTRACED)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].lastPid == child_pid) {
                found = 1;
                printf("\n[%d] Hecho\t%s\n", i, jobs[i].commands);
                removeJob(i);
            }
        }
    }
    if (!executing && found)
        printPrompt();
}

// stdout de act -> pipefd[0]
// stdin de next -> pipefd[1]
void preparePipe(int (*io_current)[2], int *input_next, int (*pipefd)[2] , int isLast){
    (*io_current)[0] = *input_next;
    (*io_current)[1] = 1;
    *input_next = 0;
    if (!isLast){
        pipe(*pipefd);
        (*io_current)[1] = (*pipefd)[1];
        (*input_next) = (*pipefd)[0];
    }
}

void setIO(int io_current[], int input_next, int isLast, char *redirect_error){
    int err_fd;
    if (io_current[0] != 0){ //no es el primer mandato
        dup2(io_current[0],0);
        close(io_current[0]);
    }
    if (io_current[1] != 1){ //no es el último mandato (menos si hay '> file')
        if (!isLast)
            close(input_next);
        dup2(io_current[1],1);
        close(io_current[1]);
    }
    if(isLast && redirect_error){
        err_fd = open(redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        dup2(err_fd,2);
        close(err_fd);
    }
}

int isBuiltin(tline *line){
    int err;
    if (!strcmp("cd",line->commands[0].argv[0])){
        if (line->ncommands > 1){
            fprintf(stderr,"msh: cd: No se debe usar con pipes\n");
            return 1;
        }

        if (line->commands[0].argc > 2){
            fprintf(stderr,"msh: cd: Demasiados argumentos\n");
            return 1;
        }

        err = chdir(line->commands[0].argc == 1? getenv("HOME") : line->commands[0].argv[1]);
        if (err == -1)
            perror("msh: cd");
        
        return 1;
    }else if(!strcmp("jobs",line->commands[0].argv[0])){
        getJobs();
        return 1;
    }else if(!strcmp("fg",line->commands[0].argv[0])){
        job2Foreground(line);
        return 1;
    }
    return 0;
}

void processAndExec(char * buf){
    tline * line;
    int pipefd[2], io_act[2], in_next = 0, i, isLast;
    pid_t pid;

    line = tokenize(buf);

    if (!line->ncommands)
        return;

    if (isBuiltin(line))
        return;

    //si es input open, in_next=open
    if (line->redirect_input)
        in_next = open(line->redirect_input, O_RDONLY);
    /*else if (line->background)
        in_next = open("/dev/null", O_RDONLY);*/


    for (i = 0; i < line->ncommands; i++) {
        isLast = (i==line->ncommands - 1);
        preparePipe(&io_act, &in_next, &pipefd, isLast);
            
        //si es output open, act[1]=open
        if (isLast && line->redirect_output)
           io_act[1] = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);

        pid = fork();
        if (pid == -1){
            perror("msh: Error al crear proceso hijo");
            exit(-1);
        }

        if (!pid) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            if (line->background)
                setpgid(0,0);

            setIO(io_act, in_next, isLast, line->redirect_error);
                /*char tmp[1024];  //for debugging
                sprintf(tmp, "ls -la /proc/%d/fd >&2",getpid());
                system(tmp);*/
                /*sprintf(tmp, "ls -la /proc/%d/fd >&2",getpid());
                system(tmp);*/
            if (!line->commands[i].filename)
                fprintf(stderr,"msh: %s: Archivo no encontrado\n",line->commands[i].argv[0]);
            execvp(line->commands[i].filename, line->commands[i].argv);
            perror("msh: Error al ejecutar commando");
        }else{
            if (io_act[0] != 0){
                close(io_act[0]);
            }
            if (io_act[1] != 1){
                close(io_act[1]);
            }

            if (line->background){
                waitpid(pid, NULL, WNOHANG | WUNTRACED);
            }else{
                waitpid(pid, NULL, 0);
            }
        }
    }
    if (line->background)
        addJob(buf, pid);
}


int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGINT, handleSignals);
    signal(SIGQUIT, handleSignals);
    signal(SIGCHLD, childHandler);

    for (int i = 0; i < MAX_JOBS; ++i){
        jobs[i].state = TERMINATED;
    }

    char *command_buffer = calloc(DEFAULT_BUFFER_SIZE, sizeof(char)); // free this

    while (1) {
        printPrompt();
        //printf("msh > ");
        if(!fgets(command_buffer, DEFAULT_BUFFER_SIZE, stdin))
            break;
        executing = 1;
        processAndExec(command_buffer);
        executing = 0;
    }
    free(command_buffer);
    puts("");
    return 0;
}
