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
	int timer;

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


void *timerCount(void*);
void *timeComparator(void*);
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

arrivalPtr arrivalHead;
departurePtr departureHead;

int main() {

	pid_t childPid;



	//signal(SIGINT,forceclose);
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
}


void flightManager() {

	char* comando;
	char letra;
	int fdNamedPipe;
	int i;

	criaSharedMemory();
	criaMessageQueue();

	arrivalHead = criaArrivals();
	departureHead = criaDepartures();

	readConfig();
	
	pthread_create(&timeThread,NULL,timerCount,NULL);
	pthread_create(&comparatorThread,NULL,timeComparator,NULL);
	
	//testMQ();

	fdNamedPipe = criaPipe();
	calculaHora();
	startLog();


	while(isActive == 1){

		i = 0;

		comando = malloc(CINQ*sizeof(char));
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

		else insertLogfile("WRONG COMMAND =>",comando);
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

void *timerCount(void* unused){

	struct timeb tempoStruct, tempoStruct2;

	sharedMemPtr->timer = 0;

	ftime(&tempoStruct);

	while(isActive == 1){

 		pthread_mutex_lock(&timeMutex);
 		ftime(&tempoStruct2);
 		sharedMemPtr->timer = (int)(1000*(tempoStruct2.time - tempoStruct.time)+(tempoStruct2.millitm - tempoStruct.millitm))/sharedMemPtr->valuesPtr->unidadeTempo;
 		calculaHora();
 		pthread_mutex_unlock(&timeMutex);
 		//usleep(20000);
 		//printf("Unidade de Tempo %d\n",sharedMemPtr->timer);	/*PRINT TO CHECK SYNCRONIZATION*/
 	}
 }

 void *timeComparator(void* unused){

 	int i = 0;
 	int j = 0;
	

	arrivalPtr arrivalAux = arrivalHead;
	departurePtr departureAux = departureHead;

	while(isActive == 1){


		pthread_mutex_lock(&timeMutex);

	   	if ((arrivalAux->nextNodePtr != NULL) && (arrivalAux->nextNodePtr->init == sharedMemPtr->timer)){

	   		pthread_create(&arrivalThreads[i++],NULL,ArrivalFlight,(void *)arrivalAux->nextNodePtr);
	   		arrivalAux = arrivalAux->nextNodePtr;
	   		sizeArrivals++;
	    }

	    if ((departureAux->nextNodePtr != NULL) && (departureAux->nextNodePtr->init == sharedMemPtr->timer)){

	   		pthread_create(&departureThreads[j++],NULL,DepartureFlight,(void *)arrivalAux->nextNodePtr);
	   		departureAux = departureAux->nextNodePtr;
	   		sizeDepartures++;
	    }
	    pthread_mutex_unlock(&timeMutex);
	}
	sizeArrivals = i;
	sizeDepartures = j;
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
	arrivalPtr aux = arrivalHead;

	sscanf(comando, "ARRIVAL %s init: %d eta: %d fuel: %d", nome, &init, &eta, &fuel);

	if ((fuel >= eta) && (sharedMemPtr->timer<=init)){

		insertLogfile("NEW COMMAND =>",comando);
		insereArrival(aux,nome,init,eta,fuel);
	} 

	else insertLogfile("WRONG COMMAND =>",comando);
}

void processaDeparture(char* comando){

	char nome[10];
	int init;
	int takeoff;
	departurePtr aux = departureHead;

	sscanf(comando, "DEPARTURE %s init: %d takeoff: %d", nome, &init, &takeoff);

	if (sharedMemPtr->timer<=init){

		insertLogfile("NEW COMMAND =>",comando);
		insereDeparture(aux,nome,init,takeoff);
	}
	else insertLogfile("WRONG COMMAND =>",comando);

}

void readConfig() {

	FILE *f;

	sharedMemPtr->valuesPtr = malloc(sizeof(valuesStruct));

	if (!(f = fopen("config.txt", "r"))){
		perror("Error opening file");
		exit(1);
	}

	fscanf(f, "%d\n", &sharedMemPtr->valuesPtr->unidadeTempo);
	fscanf(f, "%d, %d\n", &sharedMemPtr->valuesPtr->duracaoDescolagem, &sharedMemPtr->valuesPtr->intervaloDescolagens);
	fscanf(f, "%d, %d\n", &sharedMemPtr->valuesPtr->duracaoAterragem, &sharedMemPtr->valuesPtr->intervaloAterragens);
	fscanf(f, "%d, %d\n", &sharedMemPtr->valuesPtr->minHolding, &sharedMemPtr->valuesPtr->maxHolding);
	fscanf(f, "%d\n", &sharedMemPtr->valuesPtr->maxPartidas);
	fscanf(f, "%d\n", &sharedMemPtr->valuesPtr->maxChegadas);

	fclose(f);
}


void *ArrivalFlight(void *flight){


	insertLogfile("ARRIVAL STARTED =>",((departurePtr)flight)->nome);
	usleep((sharedMemPtr->valuesPtr->duracaoAterragem) * (sharedMemPtr->valuesPtr->unidadeTempo) * 1000);
	insertLogfile("ARRIVAL CONCLUDED =>",((departurePtr)flight)->nome);

	pthread_exit(0);
}


void *DepartureFlight(void *flight){
	
	insertLogfile("DEPARTURE STARTED =>",((departurePtr)flight)->nome);
	usleep((sharedMemPtr->valuesPtr->duracaoDescolagem) * (sharedMemPtr->valuesPtr->unidadeTempo) * 1000);
	insertLogfile("DEPARTURE CONCLUDED =>",((departurePtr)flight)->nome);
	
	pthread_exit(0);
}


void terminate(){

	int i;
	isActive = 0;
	printf("Tutto finisce..\n");

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

	printf("Dappertutto!\n");
	exit(0);
}


void insertLogfile(char *status, char *command){
	FILE *f;
	f=fopen("Logfile.txt","a");
	
	pthread_mutex_lock(&logMutex);
	fprintf(f,"%02d:%02d:%02d %s %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, status, command);
	printf("%02d:%02d:%02d %s %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, status, command);
	pthread_mutex_unlock(&logMutex);
	
	fclose(f);
}

void startLog(){
	FILE *f;
	f=fopen("Logfile.txt","w");

	pthread_mutex_lock(&logMutex);
	fprintf(f,"DAY %d, %02d:%02d:%02d SIMULATION START\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	printf("DAY %d, %02d:%02d:%02d SIMULATION START\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	pthread_mutex_unlock(&logMutex);

	fclose(f);
}

void endLog(){
	FILE *f;
	f=fopen("Logfile.txt","a");

	pthread_mutex_lock(&logMutex);
	fprintf(f,"DAY %d, %02d:%02d:%02d SIMULATION END\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	printf("DAY %d, %02d:%02d:%02d SIMULATION END\n", sharedMemPtr->structHoras->tm_mday, sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec);
	pthread_mutex_unlock(&logMutex);
	
	fclose(f);
}

void calculaHora(){

	time_t tempo;

	time(&tempo);
 	sharedMemPtr->structHoras = localtime(&tempo);
}