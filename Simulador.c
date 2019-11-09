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

typedef struct sharedMemStruct{

	int maluck;
	// Insert code here lmao

} memStruct;


typedef struct arrivalNode* arrivalPtr;
typedef struct arrivalNode{

	char* nome;
	int init; 
	int eta;
	int fuel;

	arrivalPtr nextNodePtr;

} arrivalStruct;

typedef struct departureNode* departurePtr;
typedef struct departureFlight{

	char* nome;
	int init; 
	int takeoff;

	departurePtr nextNodePtr;

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

void controlTower();
void flightManager();
void readConfig();
void terminate();
void criaPipe();
void criaMessageQueue();
void criaSharedMemory();
int confirmaSintaxe(char* comando, char* padrao);
void processaArrival(char* comando, arrivalPtr arrivalHead);
void printArrivals(arrivalPtr arrivalHead);
void freeArrivals(arrivalPtr arrivalHead);
void insereArrival(arrivalPtr arrivalHead, char* nome, int init, int eta, int fuel);
void criarVoo();
arrivalPtr criaArrivals();


//CONFIGVALUES
valuesStruct* valuesPtr; 

//SHARED MEMORY
int shmid;
memStruct* sharedMemPtr;

//NAMED PIPE
int fdNamedPipe;


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
	char comando[CINQ];

	arrivalPtr arrivalHead = criaArrivals();

	criaSharedMemory();
	criaMessageQueue();
	criaPipe();

	while(condition == 1){

		read(fdNamedPipe,&comando,CINQ);
		strtok(comando, "\n");

		if (strcmp(comando,"exit") == 0) condition = 0;
			
		else if ((comando[0] == 'A') && (confirmaSintaxe(comando, ARRIVAL_PATTERN) == 1)){

			processaArrival(comando, arrivalHead);
			printArrivals(arrivalHead);
		}

		else if ((comando[0] == 'D') && (confirmaSintaxe(comando, DEPARTURE_PATTERN) == 1)){
			printf("to be completed\n");
		}

		else printf("*to be completed*\n");//escreve no log mal
	}

	freeArrivals(arrivalHead);
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

	printf("Creating Message Queue\n");
}


void criaPipe(){
	
	unlink(PIPE_NAME);

	if((mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST)){
		perror("Cannot open named pipe");
		exit(0);
	}

	if ((fdNamedPipe = open(PIPE_NAME, O_RDWR)) < 0) {
		perror("Cannot open pipe for read/write: ");
		exit(0);
	}
}


int confirmaSintaxe(char* comando, char* padrao){

	regex_t expressaoRegular;
	int returnValue = 0;

    if (regcomp(&expressaoRegular, padrao, REG_EXTENDED) != 0)
        printf("erro a criar a expressao regular");
    
    if (regexec(&expressaoRegular, comando, (size_t) 0, NULL, 0) == 0)
    	returnValue = 1;
    
    regfree(&expressaoRegular);

    return returnValue;
}

void processaArrival(char* comando, arrivalPtr arrivalHead){

	char nome[10];
	int init;
	int eta;
	int fuel;
	arrivalPtr aux = arrivalHead;

	sscanf(comando, "ARRIVAL %s init:%d eta:%d fuel:%d", nome, &init, &eta, &fuel);

	if ((fuel > eta) && (fuel > init)) insereArrival(aux,nome,init,eta,fuel);
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

	printf("Tutto finisce..\n");

	unlink(PIPE_NAME);
	remove(PIPE_NAME);

	shmdt(sharedMemPtr);
	shmctl(shmid,IPC_RMID,NULL);

	printf("Dappertutto!\n");
}

arrivalPtr criaArrivals(){

    arrivalPtr aux;
    aux = malloc(sizeof(arrivalStruct));

    if (aux!=NULL){

        aux->nome = malloc(10*sizeof(char));
        aux->init = -1;
        aux->eta = -1;
        aux->fuel = -1;
        aux->nextNodePtr = NULL;
    }

    return aux;
}

void insereArrival(arrivalPtr arrivalHead, char* nome, int init, int eta, int fuel){

    arrivalPtr novo = criaArrivals();
    arrivalPtr aux = arrivalHead;

    while((aux->nextNodePtr != NULL) && (aux->nextNodePtr->init < init))
        aux = aux->nextNodePtr;

    novo->nextNodePtr = aux->nextNodePtr;
    aux->nextNodePtr = novo;

    strcpy(novo->nome, nome);
    novo->init = init;
    novo->eta = eta;
    novo->fuel = fuel;
}

void freeArrivals(arrivalPtr arrivalHead){

    arrivalPtr aux = arrivalHead->nextNodePtr;

        while(arrivalHead != NULL){

            free(arrivalHead->nome);
            aux = arrivalHead->nextNodePtr;
            free(arrivalHead);
            arrivalHead = aux;
        }
}

void printArrivals(arrivalPtr arrivalHead){

	arrivalPtr aux = arrivalHead->nextNodePtr;

        while(aux != NULL){

        	printf("Voo:%s, init:%d, eta:%d, fuel:%d\n", aux->nome, aux->init, aux->eta, aux->fuel);
        	aux = aux->nextNodePtr;
        } 
    printf("------\n");
}