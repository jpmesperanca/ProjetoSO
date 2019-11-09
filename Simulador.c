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
#include <sys/msg.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include "LinkedList.h" 


#define CINQ 50
#define CEM 100
#define PIPE_NAME "input_pipe"
#define ARRIVAL_PATTERN  "ARRIVAL TP[0-9]+ init: [0-9]+ eta: [0-9]+ fuel: [0-9]+"
#define DEPARTURE_PATTERN "DEPARTURE TP[0-9]+ init: [0-9]+ takeoff: [0-9]+"
#define LIMVOO 200

typedef struct sharedMemStruct{

	int maluck;
	// Insert code here lmao

} memStruct;



typedef struct messageQueue* messageQueuePtr;
typedef struct messageQueue{

  long messageType;

  char* message;
  int shmSlot; 

} messageStruct;

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


void *timerCount(void*);
void *timeComparator(void*);
void *ArrivalFlight(void* );
void *DepartureFlight(void* );


messageQueuePtr criaMQStruct();
void testMQ();


//CONFIGVALUES
valuesStruct* valuesPtr; 

//MESSAGE QUEUE
int messageQueueID;

//SHARED MEMORY
int shmid;
memStruct* sharedMemPtr;

//NAMED PIPE
int fdNamedPipe;

//PTHREADS
pthread_mutex_t timeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t timeThread;
int timer = 0;
pthread_t comparatorThread;

//PTHREADS ARRIVALS
pthread_mutex_t arrivalMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t arrivalThread[LIMVOO];
int sizeArrivals;

//PTHREADS DEPARTURES
pthread_mutex_t departureMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t departureThread[LIMVOO];
int sizeDepartures;

//Exit Condition
int condition = 1;

//LISTA LIGADAS
arrivalPtr arrivalHead;
departurePtr departureHead;

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

	char comando[CINQ];

	arrivalHead = criaArrivals();
	departureHead = criaDepartures();

	
	pthread_create(&timeThread,NULL,timerCount,NULL);
	pthread_create(&comparatorThread,NULL,timeComparator,NULL);
	
	criaSharedMemory();
	criaMessageQueue();
	testMQ();
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

			processaDeparture(comando, departureHead);
			printDepartures(departureHead);
		}

		else printf("*to be completed*\n");//escreve no log mal
	}

	freeArrivals(arrivalHead);
	freeDepartures(departureHead);
	msgctl(messageQueueID, IPC_RMID, 0);
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

	messageQueueID = msgget(IPC_PRIVATE, IPC_CREAT | 0777);
}

void testMQ(){
	
	messageQueuePtr enviar = criaMQStruct();
	messageQueuePtr msgrecebida = criaMQStruct();

	enviar->message = "Test Message";
	enviar->messageType = 1;

	msgsnd(messageQueueID, enviar, sizeof(messageStruct), 0);
	sleep(1);
	msgrcv(messageQueueID, msgrecebida, sizeof(messageStruct), 1, 0);

	printf("MENSAGEM RECEBIDA: %s\n", msgrecebida->message);
}

messageQueuePtr criaMQStruct(){

	messageQueuePtr new = malloc(sizeof(messageStruct));

	if (new != NULL){
		new->messageType = -1;
		new->message = malloc(CINQ*sizeof(char));
		new->shmSlot = -1;
	}
	return new;
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

void *timerCount(void* unused){
 	/*
	pthread_mutex_t timeMutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t timeThread;
	int idTime = 0;
	int timer;
	*/
 	while(condition){
 		usleep((valuesPtr->unidadeTempo)*1000);
 		pthread_mutex_lock(&timeMutex);
 		timer++;
 		pthread_mutex_unlock(&timeMutex);
 		printf("Unidade de Tempo %d\n",timer);
 	}
 	pthread_exit(0);
 }

 void *timeComparator(void* unused){
 	int i = 0;
 	int j = 0;
	
	while(condition){
	   	arrivalPtr arrivalAux = arrivalHead->nextNodePtr;
		departurePtr departureAux = departureHead->nextNodePtr; 
	   	while(arrivalAux!= NULL){
	   		pthread_mutex_lock(&timeMutex);
	   		if (arrivalAux->init == timer && arrivalAux->created == 0){
	   			pthread_mutex_unlock(&timeMutex);
	   			pthread_create(&arrivalThread[i++],NULL,ArrivalFlight,NULL);
	   			arrivalAux->created = 1;
	   		}
	   		else{
	   			pthread_mutex_unlock(&timeMutex);
	   			if(arrivalAux->init > timer)break;
	   		}
	   		arrivalAux=arrivalAux->nextNodePtr;
	    }

	    while(departureAux!= NULL){
	   		pthread_mutex_lock(&timeMutex);
	   		if (departureAux->init == timer && departureAux->created == 0){
	   			pthread_mutex_unlock(&timeMutex);
	   			pthread_create(&departureThread[j++],NULL,DepartureFlight,NULL);
	   			departureAux->created = 1;
	   		}
	   		else {
	   			pthread_mutex_unlock(&timeMutex);
	   			if(departureAux->init > timer)break;
	   		}
	   		departureAux=departureAux->nextNodePtr;
	    }

	}
	sizeArrivals = i;
	sizeDepartures = j;
	pthread_exit(0);
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

	sscanf(comando, "ARRIVAL %s init: %d eta: %d fuel: %d", nome, &init, &eta, &fuel);

	if ((fuel > eta && timer<=init) /*&& (fuel > init)*/) insereArrival(aux,nome,init,eta,fuel);
}

void processaDeparture(char* comando, departurePtr departureHead){

	char nome[10];
	int init;
	int takeoff;
	departurePtr aux = departureHead;

	sscanf(comando, "DEPARTURE %s init: %d takeoff: %d", nome, &init, &takeoff);

	if (timer<=init) insereDeparture(aux,nome,init,takeoff);
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


void *ArrivalFlight(void* unused){

	printf("wooshy!A Airplane wants to stop here\n");
	pthread_exit(NULL);
}


void *DepartureFlight(void* unused){

	printf("woosh!A Airplane wants to get the f*** out of here\n");
	pthread_exit(NULL);
}


void terminate(){

	printf("Tutto finisce..\n");

	pthread_join(timeThread,NULL);
	pthread_join(comparatorThread,NULL);
	
	for(int i=0;i<sizeArrivals;i++){
		pthread_join(arrivalThread[i],NULL);
	}

	for(int i=0;i<sizeDepartures;i++){
		pthread_join(departureThread[i],NULL);
	}

	unlink(PIPE_NAME);
	remove(PIPE_NAME);

	shmdt(sharedMemPtr);
	shmctl(shmid,IPC_RMID,NULL);

	printf("Dappertutto!\n");
}
