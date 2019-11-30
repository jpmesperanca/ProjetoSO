#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
#include <sys/timeb.h>
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


typedef struct messageQueue* messageQueuePtr;
typedef struct messageQueue{

  long messageType;

  char* message;
  int shmSlot; 

} messageStruct;

typedef struct baseValuesStruct* valuesStructPtr;
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

typedef struct sharedMemStruct* memoryPtr;
typedef struct sharedMemStruct{

	valuesStructPtr valuesPtr; 
	struct tm * structHoras;
	struct timespec Time;

} memStruct;

void controlTower();
void flightManager();
void readConfig();
void terminate();
void calculaHora();
void criaSharedMemory();
int criaPipe();
int confirmaSintaxe(char* comando, char* padrao);
void insertLogfile(char *status,char *command);
void startLog();
void endLog();


void *timerCount();
void timeComparator();
void *ArrivalFlight(void* );
void *DepartureFlight(void* );

messageQueuePtr criaMQStruct();
void criaMessageQueue();
void testMQ();

//Exit Condition
int isActive= 1;

//MESSAGE QUEUE
int messageQueueID;

//SHARED MEMORY
memoryPtr sharedMemPtr;
int shmid;

//PTHREADS
pthread_mutex_t timeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condTime = PTHREAD_COND_INITIALIZER;
pthread_cond_t creator = PTHREAD_COND_INITIALIZER;
pthread_t timeThread;
pthread_t comparatorThread;

//PTHREADS ARRIVALS
//pthread_mutex_t arrivalMutex = PTHREAD_MUTEX_INITIALIZER;	/* ŃOT USED YET*/
pthread_t arrivalThreads[LIMITEVOOS];
int sizeArrivals = 0;

//PTHREADS DEPARTURES
//pthread_mutex_t departureMutex = PTHREAD_MUTEX_INITIALIZER;	/* ŃOT USED YET*/
pthread_t departureThreads[LIMITEVOOS];
int sizeDepartures = 0;

pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
FILE *logFile;

arrivalPtr arrivalHead;
departurePtr departureHead;

int main() {

	pid_t childPid;

	signal(SIGINT,terminate);
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
	exit(0);
}


void flightManager() {

	char* comando = malloc(CINQ*sizeof(char));
	char letra;
	int fdNamedPipe, i , result;

	criaSharedMemory();
	criaMessageQueue();

	result = clock_gettime(CLOCK_REALTIME, &sharedMemPtr->Time);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	logFile = fopen("Logfile.txt", "w");

	arrivalHead = criaArrivals();
	departureHead = criaDepartures();


	readConfig();
	
	pthread_create(&timeThread,NULL,timerCount,NULL);
	
	//testMQ();

	fdNamedPipe = criaPipe();
	calculaHora();
	startLog();


	while(isActive == 1){

		i = 0;


		read(fdNamedPipe,&letra,1);
		while ( letra != '\n') {
			comando[i++] = letra;
			read(fdNamedPipe,&letra,1);	
		}
		
		comando[i] = '\0';

		if (strcmp(comando,"exit") == 0) isActive = 0;
			
		else if ((comando[0] == 'A') && (confirmaSintaxe(comando, ARRIVAL_PATTERN) == 1)){

			processaArrival(comando);
			//printArrivals(sharedMemPtr->arrivalHead);
		}

		else if ((comando[0] == 'D') && (confirmaSintaxe(comando, DEPARTURE_PATTERN) == 1)){

			processaDeparture(comando);
			//printDepartures(sharedMemPtr->departureHead);
		}

		else{
			insertLogfile("WRONG COMMAND =>",comando);
		}
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

int criaPipe(){
	
	int fdNamedPipe;
	unlink(PIPE_NAME);

	if((mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST)){
		perror("Cannot open named pipe");
		exit(0);
	}

	if ((fdNamedPipe = open(PIPE_NAME, O_RDWR)) < 0) {
		perror("Cannot open pipe for read/write: ");
		exit(0);
	}
	return fdNamedPipe;
}


void *timerCount(){
 	int aux=0,result;
 	int tempo=0, tempo_sec=0, tempo_nsec=0;
 	struct timespec timetoWait = { 0 };

 	while(isActive == 1){
 		
		aux=0;

		timeComparator();

	    result=pthread_mutex_lock(&timeMutex);
		if (result != 0) {
        	fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        	exit(EXIT_FAILURE);
   		} 

   		if(arrivalHead->nextNodePtr == NULL && departureHead->nextNodePtr == NULL){
 			pthread_cond_wait(&condTime,&timeMutex);
 			aux=1;
   		}

   		else if(departureHead->nextNodePtr == NULL && arrivalHead->nextNodePtr != NULL)
 			tempo = arrivalHead->nextNodePtr->init * sharedMemPtr->valuesPtr->unidadeTempo;

 		else if (arrivalHead->nextNodePtr == NULL && departureHead->nextNodePtr != NULL)
 			tempo = departureHead->nextNodePtr->init * sharedMemPtr->valuesPtr->unidadeTempo;

 		else if (arrivalHead->nextNodePtr->init <= departureHead->nextNodePtr->init)
 			tempo = arrivalHead->nextNodePtr->init * sharedMemPtr->valuesPtr->unidadeTempo;

 		else if (arrivalHead->nextNodePtr->init > departureHead->nextNodePtr->init)
 			tempo = departureHead->nextNodePtr->init * sharedMemPtr->valuesPtr->unidadeTempo;


 		tempo_sec = tempo/1000;
 		tempo_nsec = (tempo%1000)*1000000;


 		timetoWait.tv_sec =sharedMemPtr->Time.tv_sec + tempo_sec + (tempo_nsec + sharedMemPtr->Time.tv_nsec)/1000000000;
 		timetoWait.tv_nsec = (tempo_nsec + sharedMemPtr->Time.tv_nsec)%1000000000;


    	if (aux ==0){

			result = pthread_cond_timedwait(&condTime,&timeMutex,&timetoWait);
			if (result !=0  && result != ETIMEDOUT) {
        		fprintf(stderr, "%s\n", strerror(result));
        		exit(EXIT_FAILURE);
    		}
	 	}

	 	pthread_mutex_unlock(&timeMutex);
	}
}


void timeComparator(){

 	int i = 0;
 	int j = 0;
	int timer;
	int result;
	struct timespec now;

	result = clock_gettime(CLOCK_REALTIME, &now);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	arrivalPtr arrivalAux = arrivalHead;
	departurePtr departureAux = departureHead;

	timer = ((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + (now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / sharedMemPtr->valuesPtr->unidadeTempo);
   	while ((arrivalAux->nextNodePtr != NULL) && (arrivalAux->nextNodePtr->init == timer)){

   		pthread_create(&arrivalThreads[i++],NULL,ArrivalFlight,(void *)arrivalAux->nextNodePtr);
   		arrivalAux = arrivalAux->nextNodePtr;
   		sizeArrivals++;
   		
    }

    while ((departureAux->nextNodePtr != NULL) && (departureAux->nextNodePtr->init == timer)){

   		pthread_create(&departureThreads[j++],NULL,DepartureFlight,(void *)arrivalAux->nextNodePtr);
   		departureAux = departureAux->nextNodePtr;
   		sizeDepartures++;
   		removeDeparture(departureHead);
    }

	sizeArrivals += i;
	sizeDepartures += j;
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

void processaArrival(char* comando){

	char nome[10];
	int init;
	int eta;
	int fuel;
	int result;
	arrivalPtr aux = arrivalHead;
	struct timespec now;
	
	result = clock_gettime(CLOCK_REALTIME, &now);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }


	sscanf(comando, "ARRIVAL %s init: %d eta: %d fuel: %d", nome, &init, &eta, &fuel);

	if ((fuel >= eta) && (((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + (now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / sharedMemPtr->valuesPtr->unidadeTempo) <= init)){
		pthread_cond_signal(&condTime);
		insertLogfile("NEW COMMAND =>",comando);
		insereArrival(aux,nome,init,eta,fuel);
	} 

	else insertLogfile("WRONG COMMAND =>",comando);
}

void processaDeparture(char* comando){

	char nome[10];
	int init;
	int takeoff;
	int result;
	departurePtr aux = departureHead;
	struct timespec now;
	
	result = clock_gettime(CLOCK_REALTIME, &now);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	sscanf(comando, "DEPARTURE %s init: %d takeoff: %d", nome, &init, &takeoff);

	if (((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + (now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / sharedMemPtr->valuesPtr->unidadeTempo) <= init){
		pthread_cond_signal(&condTime);
		insertLogfile("NEW COMMAND =>",comando);
		insereDeparture(aux,nome,init,takeoff);
	}
	else insertLogfile("WRONG COMMAND =>",comando);

}

void readConfig() {

	FILE *configFile;

	sharedMemPtr->valuesPtr = malloc(sizeof(valuesStruct));

	if (!(configFile = fopen("config.txt", "r"))){
		perror("Error opening file");
		exit(1);
	}

	fscanf(configFile, "%d\n", &sharedMemPtr->valuesPtr->unidadeTempo);
	fscanf(configFile, "%d, %d\n", &sharedMemPtr->valuesPtr->duracaoDescolagem, &sharedMemPtr->valuesPtr->intervaloDescolagens);
	fscanf(configFile, "%d, %d\n", &sharedMemPtr->valuesPtr->duracaoAterragem, &sharedMemPtr->valuesPtr->intervaloAterragens);
	fscanf(configFile, "%d, %d\n", &sharedMemPtr->valuesPtr->minHolding, &sharedMemPtr->valuesPtr->maxHolding);
	fscanf(configFile, "%d\n", &sharedMemPtr->valuesPtr->maxPartidas);
	fscanf(configFile, "%d\n", &sharedMemPtr->valuesPtr->maxChegadas);

	fclose(configFile);
}


void *ArrivalFlight(void *flight){

	struct arrivalNode voo;
	voo = arrivalCopy((arrivalPtr)flight);
	removeArrival(arrivalHead);
	insertLogfile("ARRIVAL STARTED =>",voo.nome);
	usleep((sharedMemPtr->valuesPtr->duracaoAterragem) * (sharedMemPtr->valuesPtr->unidadeTempo) * 1000);
	insertLogfile("ARRIVAL CONCLUDED =>",voo.nome);

	pthread_exit(0);
}


void *DepartureFlight(void *flight){
	
	struct departureNode voo;
	voo = departureCopy((departurePtr)flight);
	removeDeparture(departureHead);
	insertLogfile("DEPARTURE STARTED =>",voo.nome);
	usleep((sharedMemPtr->valuesPtr->duracaoDescolagem) * (sharedMemPtr->valuesPtr->unidadeTempo) * 1000);
	insertLogfile("DEPARTURE CONCLUDED =>",voo.nome);
	
	pthread_exit(0);
}

void terminate(){

	int i;
	isActive = 0;
	printf("Tutto finisce..\n");

	//Just in case
	pthread_cond_signal(&condTime);
	pthread_cond_signal(&creator);

	pthread_cond_destroy(&condTime);
	pthread_cond_destroy(&creator);

	pthread_join(timeThread,NULL);
	pthread_join(comparatorThread,NULL);
	
	for(int i=0;i<sizeArrivals;i++){
		pthread_join(arrivalThreads[i],NULL);
	}
	for(int i=0;i<sizeDepartures;i++){
		pthread_join(departureThreads[i],NULL);
	}

	freeArrivals(arrivalHead);
	freeDepartures(departureHead);
	msgctl(messageQueueID, IPC_RMID, 0);

	unlink(PIPE_NAME);
	remove(PIPE_NAME);

	endLog();

	shmdt(sharedMemPtr);
	shmctl(shmid,IPC_RMID,NULL);

	fclose(logFile);
	printf("Dappertutto!\n");
	exit(0);
}


void insertLogfile(char *status, char *command){

	pthread_mutex_lock(&logMutex);
	calculaHora();
	fprintf(logFile,"%02d:%02d:%02d %s %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, status, command);
	printf("%02d:%02d:%02d %s %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, status, command);
	pthread_mutex_unlock(&logMutex);
}

void startLog(){

	pthread_mutex_lock(&logMutex);
	calculaHora();
	fprintf(logFile,"DAY %d, %02d:%02d:%02d SIMULATION START\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	printf("DAY %d, %02d:%02d:%02d SIMULATION START\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	pthread_mutex_unlock(&logMutex);
}

void endLog(){

	pthread_mutex_lock(&logMutex);
	calculaHora();
	fprintf(logFile,"DAY %d, %02d:%02d:%02d SIMULATION END\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	printf("DAY %d, %02d:%02d:%02d SIMULATION END\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	pthread_mutex_unlock(&logMutex);
}

void calculaHora(){

	time_t tempo;

	time(&tempo);
 	sharedMemPtr->structHoras = localtime(&tempo);
}
