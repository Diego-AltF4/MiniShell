#define _GNU_SOURCE
#define __USE_POSIX


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>

#include <limits.h>


void printPrompt(){

    uid_t user_uid;
    user_uid = getuid();

    struct passwd *passwd_struct;
    passwd_struct = getpwuid(user_uid);

    char hostname[HOST_NAME_MAX];
    int erc = gethostname(hostname, HOST_NAME_MAX);

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

int main(int argc, char *argv[], char *envp[]){

    printPrompt();


    return 0;
}

