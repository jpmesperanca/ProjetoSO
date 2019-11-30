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

	int fuel;
	int tempoDesejado;
  
} messageStruct;

typedef struct replyQueue* replyQueuePtr;
typedef struct replyQueue{

	long messageType;

	int id;

} replyStruct;

typedef struct shmSlots* shmSlotsPtr;
typedef struct shmSlots{

	char ordem[15];
	int inUse;

} shmSlotsStruct;

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


typedef struct sharedMemStruct* memoryPtr ;
typedef struct sharedMemStruct{

	struct tm * structHoras;
	struct timeb startTime;

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
void initializeSlots();
void *timeComparator();
void *ArrivalFlight(void* );
void *DepartureFlight(void* );
void newDeparture(messageQueuePtr mensagem);
void newArrival(messageQueuePtr mensagem);
messageQueuePtr criaMQStruct();
replyQueuePtr criaReplyStruct();
void criaMessageQueue();
void testMQ();


//Exit Condition
int isActive= 1;

//MESSAGE QUEUE
int messageQueueID;
sem_t* msgsem;

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

valuesStructPtr valuesPtr; 
shmSlotsPtr arrivals;
shmSlotsPtr departures;


int main() {

	pid_t childPid;

	//signal(SIGINT,terminate);
	
	readConfig();
	criaSharedMemory();
	criaMessageQueue();
	
	childPid = fork();

	if (childPid == 0){
		controlTower();
		printf("Exiting ct\n");
		exit(0);
	}
	
	printf("Entering Flight Manager\n");
	flightManager();

	wait(NULL);
	terminate();
	
	return 0;
}


void controlTower() {

	int totalVoos = 0;

	queuePtr arrivalQueue = criaQueue();
	queuePtr departureQueue = criaQueue();

	messageQueuePtr mensagem = criaMQStruct();

	while(isActive == 1){

		msgrcv(messageQueueID, mensagem, sizeof(messageStruct), -2, 0);

		if (mensagem->fuel == -1)
			newDeparture(mensagem);
		
		else
			newArrival(mensagem);
	}
}


void newDeparture(messageQueuePtr mensagem){

	replyQueuePtr reply = criaReplyStruct();

	printf("NEW DEPARTURE -- td: %d\n", mensagem->tempoDesejado);

	reply->messageType = 1;
	reply->id = 0;
	strcpy(departures[0].ordem, "HOLDING420");
	msgsnd(messageQueueID, reply, sizeof(replyStruct), 0);
}


void newArrival(messageQueuePtr mensagem){

	replyQueuePtr reply = criaReplyStruct();

	printf("NEW ARRIVAL -- fuel: %d, td: %d\n", mensagem->fuel, mensagem->tempoDesejado);

	reply->messageType = 3;
	reply->id = 0;
	/*
	strcpy(arrivals[0].ordem,"HOLDING420");
	printf("%s\n", arrivals[0].ordem);*/
	msgsnd(messageQueueID, reply, sizeof(replyStruct), 0);
}


void initializeSlots(){

	int i;

	for (i = 0; i < valuesPtr->maxChegadas; i++){
		arrivals[i].inUse = 2;}
	for (i = 0; i < valuesPtr->maxPartidas; i++)
		departures[i].inUse = 2;
}


void flightManager() {

	char* comando = malloc(CINQ*sizeof(char));
	char letra;
	int fdNamedPipe;
	int i;
	
	ftime(&(sharedMemPtr->startTime));

	logFile = fopen("Logfile.txt", "w");

	arrivalHead = criaArrivals();
	departureHead = criaDepartures();
	
	pthread_cond_init(&condTime,NULL);
	pthread_cond_init(&creator,NULL);

	pthread_create(&timeThread,NULL,timerCount,NULL);
	pthread_create(&comparatorThread,NULL,timeComparator,NULL);

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
			//printArrivals(arrivalHead);
		}

		else if ((comando[0] == 'D') && (confirmaSintaxe(comando, DEPARTURE_PATTERN) == 1)){

			processaDeparture(comando);
			//printDepartures(sharedMemPtr->departureHead);
		}

		else insertLogfile("WRONG COMMAND =>",comando);
	}

}


void criaSharedMemory(){

	int maxVoos = valuesPtr->maxPartidas + valuesPtr->maxChegadas;

	if ((shmid = shmget(IPC_PRIVATE, sizeof(memStruct)+(maxVoos*sizeof(shmSlotsStruct)), IPC_CREAT | 0777)) < 1) {
		perror("Error creating shared memory\n");
		exit(1);
	}
	
	if ((sharedMemPtr = (memStruct*)shmat(shmid, NULL, 0)) < (memStruct*)1) {
		perror("Error in shmat\n");
		exit(1);
	}

	departures = (shmSlotsPtr)malloc(sizeof(shmSlotsStruct)*(valuesPtr->maxPartidas));
	arrivals = (shmSlotsPtr)malloc(sizeof(shmSlotsStruct)*(valuesPtr->maxChegadas));

	initializeSlots(arrivals, departures);

	departures = (shmSlotsPtr)shmat(shmid, NULL, 0);
	arrivals = (shmSlotsPtr)shmat(shmid, NULL, 0);
}


void criaMessageQueue(){

	messageQueueID = msgget(IPC_PRIVATE, IPC_CREAT | 0777);
}


messageQueuePtr criaMQStruct(){

	messageQueuePtr new = malloc(sizeof(messageStruct));

	if (new != NULL){
		new->messageType = -1;
		new->fuel = -1;
		new->tempoDesejado = -1;
	}
	return new;
}


replyQueuePtr criaReplyStruct(){

	replyQueuePtr new = malloc(sizeof(replyStruct));

	if (new != NULL){
		new->messageType = -1;
		new->id = -1;
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


/*
void *timerCount(){

	struct timeb tempoStruct, tempoStruct2;

	sharedMemPtr->timer = 0;

	ftime(&tempoStruct);

	while(isActive == 1){

 		pthread_mutex_lock(&timeMutex);
 		ftime(&tempoStruct2);
 		sharedMemPtr->timer = (int)(1000*(tempoStruct2.time - tempoStruct.time)+(tempoStruct2.millitm - tempoStruct.millitm))/valuesPtr->unidadeTempo;
 		calculaHora();
 		pthread_mutex_unlock(&timeMutex);
 		//usleep(20000);
 		//printf("Unidade de Tempo %d\n",sharedMemPtr->timer);	PRINT TO CHECK SYNCRONIZATION
 	}
 }
*/



void *timeComparator(){

 	int i = 0;
 	int j = 0;
	int timer;
	struct timeb now;

	arrivalPtr arrivalAux = arrivalHead;
	departurePtr departureAux = departureHead;

	while(isActive == 1){

		pthread_mutex_lock(&timeMutex);
		pthread_cond_wait(&creator,&timeMutex);
		ftime(&now);
		timer = ((1000* (now.time - sharedMemPtr->startTime.time) + (now.millitm - sharedMemPtr->startTime.millitm)) / valuesPtr->unidadeTempo);
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
}


void *timerCount(){ 
 	int timer=0,aux=0;
 	struct timeb tempo;
 	struct timespec timetoWait;

	pthread_mutex_lock(&timeMutex);
 	pthread_cond_wait(&condTime,&timeMutex);
	pthread_mutex_unlock(&timeMutex);

 	while(isActive == 1){
 	
 		ftime(&tempo);
 		aux=0;

 		if(departureHead->nextNodePtr == NULL && arrivalHead->nextNodePtr != NULL)
 			timer = (arrivalHead->nextNodePtr->init * valuesPtr->unidadeTempo); 

 		else if (arrivalHead->nextNodePtr == NULL && departureHead->nextNodePtr != NULL)
 			timer = (departureHead->nextNodePtr->init * valuesPtr->unidadeTempo);

 		else if (arrivalHead->nextNodePtr->init <= departureHead->nextNodePtr->init)
 			timer = (arrivalHead->nextNodePtr->init * valuesPtr->unidadeTempo);

 		else if (arrivalHead->nextNodePtr->init > departureHead->nextNodePtr->init)
 			timer = (departureHead->nextNodePtr->init * valuesPtr->unidadeTempo);

 		else{

 			pthread_cond_wait(&condTime,&timeMutex);
 			aux=1;
 		}
 		pthread_cond_signal(&creator);

    	timetoWait.tv_sec = timer/1000;
    	timetoWait.tv_nsec = timer %1000 * 1000000; 

    	if (aux == 0){
			pthread_mutex_lock(&timeMutex);
			pthread_cond_timedwait(&condTime,&timeMutex,&timetoWait);
	 		pthread_mutex_unlock(&timeMutex);
		}
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

void processaArrival(char* comando){

	char nome[10];
	int init;
	int eta;
	int fuel;
	messageQueuePtr enviar;
	arrivalPtr aux = arrivalHead;
	struct timeb now;

	ftime(&now);

	sscanf(comando, "ARRIVAL %s init: %d eta: %d fuel: %d", nome, &init, &eta, &fuel);
	printf("TIME -- %d\n MILLITM -- %d\n", (int)sharedMemPtr->startTime.time, (int)sharedMemPtr->startTime.millitm);
	if ((fuel >= eta) && (((1000* (now.time - sharedMemPtr->startTime.time) + (now.millitm - sharedMemPtr->startTime.millitm)) / valuesPtr->unidadeTempo) <= init)){
		pthread_cond_signal(&condTime);
		insertLogfile("NEW COMMAND =>",comando);
		insereArrival(aux,nome,init,eta,fuel);
	} 

	else{
		insertLogfile("WRONG COMMAND =>",comando);	
	} 
}

void processaDeparture(char* comando){

	char nome[10];
	int init;
	int takeoff;
	departurePtr aux = departureHead;
	struct timeb now;
	ftime(&now);

	sscanf(comando, "DEPARTURE %s init: %d takeoff: %d", nome, &init, &takeoff);

	if (((1000* (now.time - sharedMemPtr->startTime.time) + (now.millitm - sharedMemPtr->startTime.millitm)) / valuesPtr->unidadeTempo) <= init){
		pthread_cond_signal(&condTime);
		insertLogfile("NEW COMMAND =>",comando);
		insereDeparture(aux,nome,init,takeoff);
	}
	else insertLogfile("WRONG COMMAND =>",comando);

}

void readConfig() {

	FILE *configFile;

	valuesPtr = malloc(sizeof(valuesStruct));

	if (!(configFile = fopen("config.txt", "r"))){
		perror("Error opening file");
		exit(1);
	}

	fscanf(configFile, "%d\n", &valuesPtr->unidadeTempo);
	fscanf(configFile, "%d, %d\n", &valuesPtr->duracaoDescolagem, &valuesPtr->intervaloDescolagens);
	fscanf(configFile, "%d, %d\n", &valuesPtr->duracaoAterragem, &valuesPtr->intervaloAterragens);
	fscanf(configFile, "%d, %d\n", &valuesPtr->minHolding, &valuesPtr->maxHolding);
	fscanf(configFile, "%d\n", &valuesPtr->maxPartidas);
	fscanf(configFile, "%d\n", &valuesPtr->maxChegadas);

	fclose(configFile);
}


void *ArrivalFlight(void *flight){

	messageQueuePtr enviar = criaMQStruct();
	replyQueuePtr reply = criaReplyStruct();

	insertLogfile("ARRIVAL STARTED =>",((arrivalPtr)flight)->nome);

	enviar->messageType = 2;
	enviar->fuel = ((arrivalPtr)flight)->fuel;
	enviar->tempoDesejado = ((arrivalPtr)flight)->init + ((arrivalPtr)flight)->eta;

	msgsnd(messageQueueID, enviar, sizeof(messageStruct), 0);
	msgrcv(messageQueueID, reply, sizeof(replyStruct), 3, 0);

	printf("O meu slot favorito é o %d!!\n", reply->id);
	//printf("Tenho a ordem: %s\n", arrivals[reply->id].ordem);

	usleep((valuesPtr->duracaoAterragem) * (valuesPtr->unidadeTempo) * 1000);
	insertLogfile("ARRIVAL CONCLUDED =>",((arrivalPtr)flight)->nome);

	pthread_exit(0);
}


void *DepartureFlight(void *flight){
	
	messageQueuePtr enviar = criaMQStruct();
	replyQueuePtr reply = criaReplyStruct();

	insertLogfile("DEPARTURE STARTED =>",((departurePtr)flight)->nome);

	enviar->messageType = 2;
	enviar->tempoDesejado = ((arrivalPtr)flight)->init + ((arrivalPtr)flight)->eta;

	msgsnd(messageQueueID, enviar, sizeof(messageStruct), 0);
	msgrcv(messageQueueID, reply, sizeof(replyStruct), 3, 0);

	printf("O meu slot favorito é o %d!!\n", reply->id);

	usleep((valuesPtr->duracaoDescolagem) * (valuesPtr->unidadeTempo) * 1000);
	insertLogfile("DEPARTURE CONCLUDED =>",((departurePtr)flight)->nome);
	
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
