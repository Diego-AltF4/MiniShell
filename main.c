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
#define DEFAULT_BUFFER_SIZE 1024
#include <sys/stat.h>
#include <fcntl.h>

void printPrompt(){

    fflush(stdout);
    uid_t user_uid;
    user_uid = getuid();

    struct passwd *passwd_struct;
    passwd_struct = getpwuid(user_uid);

    char hostname[HOST_NAME_MAX];
    int erc = gethostname(hostname, HOST_NAME_MAX);

    if (erc == -1){
        fprintf(stderr, "Ha habido un problema al obtener el hostname\n");
        exit(-1);
    }
    char *pwd;
    pwd = get_current_dir_name(); //free this

    char userSymbol;

    if (passwd_struct->pw_uid == 0){
        userSymbol='#';
    }else{
        userSymbol='$';
    }

    printf("%s@%s:%s%c ", passwd_struct->pw_name, hostname, pwd, userSymbol);

    free(pwd);
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
        int err_fd = open(redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        dup2(err_fd,2);
        close(err_fd);
    }
}

void processAndExec(char * buf){
    tline * line;
    line = tokenize(buf);

    int pipefd[2], io_act[2], in_next = 0, i;
    pid_t pid;

    //si es input open, in_next=open
    if (line->redirect_input)
        in_next = open(line->redirect_input, O_RDONLY);

    for (i = 0; i < line->ncommands; i++) {
        int isLast = (i==line->ncommands - 1);
        preparePipe(&io_act, &in_next, &pipefd, isLast);
        
        //si es output open, act[1]=open
        if (isLast && line->redirect_output)
            io_act[1] = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);

        pid = fork();
        if (pid == -1){
            fprintf(stderr,"Problema al ejecutar fork\n");
            exit (-1);
        }

        if (!pid) {
            /*char tmp[1024];  //for debugging
            sprintf(tmp, "ls -la /proc/%d/fd >&2",getpid());
            system(tmp);*/
            setIO(io_act, in_next, isLast, line->redirect_error);
            /*sprintf(tmp, "ls -la /proc/%d/fd >&2",getpid());
            system(tmp);*/
            execvp(line->commands[i].filename, line->commands[i].argv);
            fprintf(stderr,"Error al ejecutar el comando");
        }else{
            wait(NULL);
            if(io_act[0] != 0){
                close(io_act[0]);
            }
            if(io_act[1] != 1) {
                close(io_act[1]);
            }
        }
    }
}


int main() {

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    while (1) {
        printPrompt();
        //printf("msh > ");
        char *command_buffer;
        command_buffer = calloc(DEFAULT_BUFFER_SIZE, sizeof(char)); // free this
        fgets(command_buffer, DEFAULT_BUFFER_SIZE, stdin);

        processAndExec(command_buffer);

        free(command_buffer);
    }
    return 0;
}
