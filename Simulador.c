#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/fcntl.h>
#include <semaphore.h> 
#include <pthread.h>

typedef struct sharedMemStruct{

	// Insert code here lmao

} sharedMem;

void controlTower();
void flightManager();

int inputPipe[2];
int
int main() {
	
	pipe(inputPipe);

	if ((shmid = shmget(IPC_PRIVATE, sizeof(sharedMem), IPC_CREAT | 0700)) == -1) {
		perror("Error creating shared memory\n");
		exit(1);
	}

	if ((shared_var = shmat(shmid, NULL, 0)) <= 0) {
		perror("Error in shmat\n");
		exit(1);
	}


	if (fork() == 0){
		controlTower();
		exit(0);
	}
	
	flightManager();
	
	wait(NULL);

	//closing
	shmctl(shmid,IPC_RMID,NULL);
	
	return 0;
}


void controlTower() {

}


void flightManager() {

}