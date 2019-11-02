#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/fcntl.h>
#include <semaphore.h> // include POSIX semaphores
#include <pthread.h>


int main(){
	printf("Hello World");
	pid_t torreControlo=fork();
	if(torreControlo==0){
		smth();
	}
	else{
		smthelse();
	}
}