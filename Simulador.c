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
#include <regex.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#define CINQ 50
#define CEM 100
#define PIPE_NAME "input_pipe"
#define ARRIVAL_PATTERN  "ARRIVAL TP[0-9]+ init:[0-9]+ eta:[0-9]+ fuel:[0-9]+"
#define DEPARTURE_PATTERN "DEPARTURE TP[0-9]+ init:[0-9]+ takeoff:[0-9]+"

void controlTower();
void flightManager();
void readConfig();
void terminate();
void criaPipe();
void criaMessageQueue();
void criaSharedMemory();
int validaComando(char* comando, char* padrao);
void criarVoo();

typedef struct sharedMemStruct{

	int maluck;
	// Insert code here lmao

} memStruct;

typedef struct arrivalFlight{

	char* nome;
	int init; 
	int eta;
	int fuel;

	arrivalStruct* nextPtr;

} arrivalStruct;

typedef struct departureFlight{

	char* nome;
	int init; 
	int takeoff;

	departureStruct* nextPtr;

} departureStruct;

typedef struct baseValuesStruct{

	int unidadeTempo;
	int duracaoDescolagem;
	int intervaloDescolagens;
	int duracaoAterragem;
	int intervaloAterragens;
	int minHolding;
	int maxHolding;
	int maxPartidas;
	int maxChegadas;

} valuesStruct;

//CONFIGVALUES
valuesStruct* valuesPtr; 

//SHARED MEMORY
int shmid;
memStruct* sharedMemPtr;

//NAMED PIPE
int fd_named_pipe;


int main() {

	pid_t childPid;
	
	readConfig();

	childPid = fork();

	if (childPid == 0){
		controlTower();
		exit(0);
	}
	
	printf("Entering Flight Manager\n");
	flightManager();

	wait(NULL);
	terminate();
	
	return 0;
}


void controlTower() {
	printf("CT - Doing work!\n");
	sleep(2);
	printf("CT - Well, there's no work, going home\n");
}


void flightManager() {

	int condition = 1;
	int init;
	int eta;
	int fuel;
	int takeoff;
	char comando[CINQ];
	char nome[10];
	arrivalStruct arrivalFlights;
	departureStruct departureFlights;

	criaSharedMemory();
	criaMessageQueue();
	criaPipe();

	while(condition == 1){

		read(fd_named_pipe,&comando,CINQ);
		comando[strlen(comando)-1] = '\0';

		if (strcmp(comando,"exit") == 0) condition = 0;

		else if ((comando[0] == 'A') and (validaComando(comando, ARRIVAL_PATTERN) == 1)){

			sscanf(comando, "ARRIVAL, %s init:%d eta:%d fuel:%d", )
		}

		else if ((comando[0] == 'D') and (validaComando(comando, DEPARTURE_PATTERN) == 1)){

		}

		else printf("ye");//escreve no log mal
	}
}


void criaSharedMemory(){

	if ((shmid = shmget(IPC_PRIVATE, sizeof(memStruct), IPC_CREAT | 0700)) < 1) {
		perror("Error creating shared memory\n");
		exit(1);
	}
	
	if ((sharedMemPtr = (memStruct*)shmat(shmid, NULL, 0)) < (memStruct*)1) {
		perror("Error in shmat\n");
		exit(1);
	}
}


void criaMessageQueue(){

	printf("oi\n");
}


void criaPipe(){
	
	unlink(PIPE_NAME);

	if((mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST)){
		perror("Cannot open named pipe");
		exit(0);
	}

	if ((fd_named_pipe = open(PIPE_NAME, O_RDWR)) < 0) {
		perror("Cannot open pipe for read/write: ");
		exit(0);
	}
}


int validaComando(char* comando, char* padrao){

	regex_t expressaoRegular;
	int returnValue = 0;

	printf("A processar comando\n");

    if (regcomp(&expressaoRegular, padrao, REG_EXTENDED) != 0)
        printf("erro a criar a expressao regular");
    
    if (regexec(&expressaoRegular, comando, (size_t) 0, NULL, 0) == 0)
    	returnValue = 1
    
    regfree(&expressaoRegular);

    return returnValue;
}


void readConfig() {

	FILE *f;
	valuesPtr = malloc(sizeof(valuesStruct));

	if (!(f = fopen("config.txt", "r"))){
		perror("Error opening file");
		exit(1);
	}

	fscanf(f, "%d\n", &valuesPtr->unidadeTempo);
	fscanf(f, "%d, %d\n", &valuesPtr->duracaoDescolagem, &valuesPtr->intervaloDescolagens);
	fscanf(f, "%d, %d\n", &valuesPtr->duracaoAterragem, &valuesPtr->intervaloAterragens);
	fscanf(f, "%d, %d\n", &valuesPtr->minHolding, &valuesPtr->maxHolding);
	fscanf(f, "%d\n", &valuesPtr->maxPartidas);
	fscanf(f, "%d\n", &valuesPtr->maxChegadas);

	fclose(f);
}


void criarVoo(){

	printf("woosh!\n");
}


void* voos(void* agr){

	printf("wooshy!\n");
}


void terminate(){

	printf("\ntutto finisce..\n");
	unlink(PIPE_NAME);
	remove(PIPE_NAME);
	shmdt(sharedMemPtr);
	shmctl(shmid,IPC_RMID,NULL);
	printf("Dappertutto!\n");
}