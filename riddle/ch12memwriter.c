#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char* argv[]){
    if ( argc != 4 ){
        printf("Three cmd args needed. PID memaddr char");
    }
    long int pid = atol(argv[1]);
    long int memaddr = strtol(argv[2],NULL,16);
    char to_write = argv[3][0];
    char file_name[64];
    sprintf(file_name, "/proc/%ld/mem", pid);

    int mem_file = open(file_name, O_RDWR);    
    
    ptrace(PTRACE_ATTACH,pid,0,0);
    waitpid(pid,NULL,0);
    if(pwrite(mem_file,&to_write,1,memaddr) == -1){
        perror("Failed To Write");
        return 1;
    }
    ptrace(PTRACE_DETACH,pid,0,0);

    close(mem_file);
    return 0;

}