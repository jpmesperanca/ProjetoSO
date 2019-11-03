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
#define CEM 100

typedef struct sharedMemStruct{

	int maluck;
	// Insert code here lmao

} memStruct;

void controlTower();
void flightManager();
void readConfig();

int shmid;
memStruct* sharedMemPtr;
int inputPipe[2];

int unidadeTempo;
int duracaoDescolagem;
int intervaloDescolagens;
int duracaoAterragem;
int intervaloAterragens;
int minHolding;
int maxHolding;
int maxPartidas;
int maxChegadas;


int main() {
	
	pipe(inputPipe);

	
	if ((shmid = shmget(IPC_PRIVATE, sizeof(memStruct), IPC_CREAT | 0700)) < 1) {
		perror("Error creating shared memory\n");
		exit(1);
	}
	
	if ((sharedMemPtr = (memStruct*)shmat(shmid, NULL, 0)) < (memStruct*)1) {
		perror("Error in shmat\n");
		exit(1);
	}

	readConfig();

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
	
}


void flightManager() {

}