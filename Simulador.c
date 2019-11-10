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
#define LIMITEVOOS 1000

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
void insertLogfile(char *command, char *status);
void startLog();
void endLog();

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
pthread_mutex_t arrivalMutex = PTHREAD_MUTEX_INITIALIZER;	/* ŃOT USED YET*/

pthread_t arrivalThreads[LIMITEVOOS];
int sizeArrivals = 0;

//PTHREADS DEPARTURES
pthread_mutex_t departureMutex = PTHREAD_MUTEX_INITIALIZER;	/* ŃOT USED YET*/
pthread_t departureThreads[LIMITEVOOS];
int sizeDepartures = 0;

pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;

//Exit Condition
int isActive= 1;

//LISTA LIGADAS
arrivalPtr arrivalHead;
departurePtr departureHead;

int main() {

	pid_t childPid;

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

	readConfig();
	
	pthread_create(&timeThread,NULL,timerCount,NULL);
	pthread_create(&comparatorThread,NULL,timeComparator,NULL);
	
	criaSharedMemory();
	criaMessageQueue();
	//testMQ();
	criaPipe();
	startLog();

	while(isActive == 1){

		read(fdNamedPipe,&comando,CINQ);
		strtok(comando, "\n");

		if (strcmp(comando,"exit") == 0) isActive = 0;
			
		else if ((comando[0] == 'A') && (confirmaSintaxe(comando, ARRIVAL_PATTERN) == 1)){

			processaArrival(comando, arrivalHead);
			//printArrivals(arrivalHead);
			insertLogfile(comando,"NEW");
		}

		else if ((comando[0] == 'D') && (confirmaSintaxe(comando, DEPARTURE_PATTERN) == 1)){
			printf("to be completed\n");

			processaDeparture(comando, departureHead);
			//printDepartures(departureHead);
			insertLogfile(comando,"NEW");
		}

		else {
		//printf("*to be completed*\n");//escreve no log mal
		insertLogfile(comando,"WRONG");
		}
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

 	while(isActive == 1){

 		usleep((valuesPtr->unidadeTempo)*1000);
 		pthread_mutex_lock(&timeMutex);
 		timer++;
 		pthread_mutex_unlock(&timeMutex);

 		printf("Unidade de Tempo %d\n",timer);

 	}pthread_exit(0);
 }

 void *timeComparator(void* unused){

 	int i = 0;
 	int j = 0;
	

	arrivalPtr arrivalAux = arrivalHead;
	departurePtr departureAux = departureHead;

	while(isActive == 1){


		pthread_mutex_lock(&timeMutex);

	   	if ((arrivalAux->nextNodePtr != NULL) && (arrivalAux->nextNodePtr->init == timer)){

	   		pthread_create(&arrivalThreads[i++],NULL,ArrivalFlight,(void *)arrivalAux->nextNodePtr);
	   		arrivalAux = arrivalAux->nextNodePtr;
	   		sizeArrivals++;
	    }

	    if ((departureAux->nextNodePtr != NULL) && (departureAux->nextNodePtr->init == timer)){

	   		pthread_create(&departureThreads[j++],NULL,DepartureFlight,(void *)arrivalAux->nextNodePtr);
	   		departureAux = departureAux->nextNodePtr;
	   		sizeDepartures++;
	    }
	    pthread_mutex_unlock(&timeMutex);
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
	else insertLogfile(comando,"WRONG");
}

void processaDeparture(char* comando, departurePtr departureHead){

	char nome[10];
	int init;
	int takeoff;
	departurePtr aux = departureHead;

	sscanf(comando, "DEPARTURE %s init: %d takeoff: %d", nome, &init, &takeoff);

	if (timer<=init) insereDeparture(aux,nome,init,takeoff);
	else insertLogfile(comando,"WRONG");

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


void *ArrivalFlight(void *flight){
	printf("wooshy!The Airplane %s wants to stop here\n",((arrivalPtr)flight)->nome);
	pthread_exit(0);
}


void *DepartureFlight(void *flight){

	printf("woosh!A Airplane %s wants to get the f*** out of here\n",((departurePtr)flight)->nome);
	pthread_exit(0);
}


void terminate(){

	int i;

	printf("Tutto finisce..\n");

	pthread_join(timeThread,NULL);
	pthread_join(comparatorThread,NULL);
	
	for(int i=0;i<sizeArrivals;i++){
		pthread_join(arrivalThreads[i],NULL);
	}

	for(int i=0;i<sizeDepartures;i++){
		pthread_join(departureThreads[i],NULL);
	}

	unlink(PIPE_NAME);
	remove(PIPE_NAME);

	shmdt(sharedMemPtr);
	shmctl(shmid,IPC_RMID,NULL);

	endLog();

	printf("Dappertutto!\n");
}


void insertLogfile(char *command, char *status){
	FILE *f;
	f=fopen("Logfile.txt","a");
	pthread_mutex_lock(&logMutex);
	fprintf(f,"xx:xx:xx %s COMMAND => %s\n",status,command);
	printf("xx:xx:xx %s COMMAND => %s\n",status,command);
	pthread_mutex_unlock(&logMutex);
	fclose(f);

}

void startLog(){
	FILE *f;
	f=fopen("Logfile.txt","w");
	pthread_mutex_lock(&logMutex);
	fprintf(f,"Day XX, xx:xx:xx SIMULATION START\n");
	printf("Day XX, xx:xx:xx SIMULATION START\n");
	pthread_mutex_unlock(&logMutex);
	fclose(f);
}

void endLog(){
	FILE *f;
	f=fopen("Logfile.txt","a");
	pthread_mutex_lock(&logMutex);
	fprintf(f, "DAY XX, xx:xx:xx SIMULATION END\n");
	printf("DAY XX, xx:xx:xx SIMULATION END\n");
	pthread_mutex_unlock(&logMutex);
	fclose(f);
}
