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

#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#define CEM 100
#define PIPE_NAME   "input_pipe"

typedef struct sharedMemStruct{

	int maluck;
	// Insert code here lmao

} memStruct;

void controlTower();
void flightManager();
void readConfig();

//SHARED MEMORY
int shmid;
memStruct* sharedMemPtr;
int inputPipe[2];

//CONFIG.TXT
int unidadeTempo;
int duracaoDescolagem;
int intervaloDescolagens;
int duracaoAterragem;
int intervaloAterragens;
int minHolding;
int maxHolding;
int maxPartidas;
int maxChegadas;

//NAMED PIPE
int fd_named_pipe;


int main() {
	int id;
	pipe(inputPipe); /* unnamed pipe tho */

	
	if ((shmid = shmget(IPC_PRIVATE, sizeof(memStruct), IPC_CREAT | 0700)) < 1) {
		perror("Error creating shared memory\n");
		exit(1);
	}
	
	if ((sharedMemPtr = (memStruct*)shmat(shmid, NULL, 0)) < (memStruct*)1) {
		perror("Error in shmat\n");
		exit(1);
	}


	printf("check");
	
	// wtf is wrong with this?
	readConfig();
	if ((id = fork()) == 0){
		printf("is this right?");
		controlTower();
		exit(0);
	}
	else{	
		printf("Entering flightManager %d",id);
		flightManager();
	}
	wait(NULL);

	//closing
	shmctl(shmid,IPC_RMID,NULL);
	
	return 0;
}

void readConfig() {

	FILE *f;

	if (!(f = fopen("config.txt", "r"))){
		printf("Error opening file");
		exit(1);
	}

	fscanf(f, "%d\n", &unidadeTempo);
	fscanf(f, "%d, %d\n", &duracaoDescolagem, &intervaloDescolagens);
	fscanf(f, "%d, %d\n", &duracaoAterragem, &intervaloAterragens);
	fscanf(f, "%d, %d\n", &minHolding, &maxHolding);
	fscanf(f, "%d\n", &maxPartidas);
	fscanf(f, "%d\n", &maxChegadas);
}

void* voos(void* agr){

}

void controlTower() {
	printf("Doing work");
	sleep(2);
	exit(0);
}


void flightManager() {
	int Condition=1;
	char texto[10];

	// PIPE CREATION 
	unlink(PIPE_NAME);
	/*Not sure sobre isto*/
	remove(PIPE_NAME);
	if((mkfifo(PIPE_NAME,O_CREAT|O_EXCL|0600)<0) && (errno!=EEXIST)){
		perror("Cannot open named pipe");
		exit(0);
	}

	//PIPE OPENING
	if ((fd_named_pipe = open(PIPE_NAME, O_RDWR)) < 0) {
		perror("Cannot open pipe for reading: ");
		exit(0);
	}


	printf("CHeck");
	while(Condition){
		read(fd_named_pipe,&texto,10);
		if(strcmp(texto,"Arrival")==0){
			printf("FLight arriving this Airport!");
			Condition=0;
		}

	}
	
	unlink(PIPE_NAME);
	remove(PIPE_NAME);


}
