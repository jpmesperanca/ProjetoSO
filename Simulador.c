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
#define ARRIVAL_PATTERN  "ARRIVAL TP[0-9]+ init: [0-9]+ eta: [0-9]+ fuel: [0-9]+$"
#define DEPARTURE_PATTERN "DEPARTURE TP[0-9]+ init: [0-9]+ takeoff: [0-9]+$"
#define LIMITEVOOS 1000
#define BUFSIZE 10000


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
	int duration;
	int pista;
	int check;
	int inUse;

} shmSlotsStruct;

typedef struct estatisticasStruct* estatisticasPtr;
typedef struct estatisticasStruct{

	int totalVoos;
	int totalArrivals;
	int totalDepartures;
	int numeroVoosRedirecionados;
	int numeroRejeitados;
	int numeroHoldings;
	int numeroHoldingsPrio;
	int tempoEsperadoA;
	int tempoEsperadoD;
	int totalPrio;

} statsStruct;


typedef struct sharedMemStruct* memoryPtr ;
typedef struct sharedMemStruct{

	struct tm * structHoras;
	struct timespec Time;
	statsStruct estatisticas;
	pid_t towerPid;
	int running;
	int isActive;
	int totalArrivals;
	int totalDepartures;

} memStruct;

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


void inicializaStats();
void controlTower();
void flightManager();
void processaComando(char* comando);
void readConfig();
void terminator();
void calculaHora();
void criaSharedMemory();
int criaPipe();
int confirmaSintaxe(char* comando, char* padrao);
void insertLogfile(char *status,char *command);
void startLog();
void endLog();


void *timerCount();
void timeComparator();
void showStats();
void initializeSlots();
void *ArrivalFlight(void* );
void *DepartureFlight(void* );
int newDeparture(messageQueuePtr mensagem, int departuresHelper);
int newArrival(messageQueuePtr mensagem, int arrivalsHelper);
messageQueuePtr criaMQStruct();
replyQueuePtr criaReplyStruct();
void criaMessageQueue();
void testMQ();

void *fuelUpdater();
void *flightPlanner();
void clearTower();
void arrivalOrders(queuePtr arrivalQueue, int num);
void Aterragem(struct timespec tempo);
void Descolagem(struct timespec tempo);
struct timespec ValorAbsoluto(struct timespec now,int tempo);

//SIGNALS 
sigset_t blocker;

//sem_t* exitsem;

//MESSAGE QUEUE
int messageQueueID;

//SHARED MEMORY
memoryPtr sharedMemPtr;
int shmid, shmidDepartures, shmidArrivals;

//PTHREADS
pthread_mutex_t timeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condTime = PTHREAD_COND_INITIALIZER;
pthread_cond_t creator = PTHREAD_COND_INITIALIZER;
pthread_t timeThread;

//PTHREADS ARRIVALS
pthread_mutex_t arrivalMutex = PTHREAD_MUTEX_INITIALIZER;	
pthread_cond_t condArrival = PTHREAD_COND_INITIALIZER;
pthread_t arrivalThreads[LIMITEVOOS];
int sizeArrivals = 0;

//PTHREADS DEPARTURES
pthread_mutex_t departureMutex = PTHREAD_MUTEX_INITIALIZER;	
pthread_cond_t condDeparture = PTHREAD_COND_INITIALIZER;
pthread_t departureThreads[LIMITEVOOS];
int sizeDepartures = 0;

pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
FILE *logFile;

//PTHREADS CONTROL TOWER
pthread_t fuelThread, decisionThread;
pthread_mutex_t fuelMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t decisionMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condGeral = PTHREAD_COND_INITIALIZER;

//STATS
pthread_mutex_t statsMutex = PTHREAD_MUTEX_INITIALIZER;

int started = 0;

arrivalPtr arrivalHead;
departurePtr departureHead;

valuesStructPtr valuesPtr; 
shmSlotsPtr arrivals;
shmSlotsPtr departures;

queuePtr arrivalQueue;
queuePtr departureQueue;

int main() {
	
	pid_t childPid;

	printf("PID para STATS / EXIT -- %d\n", getpid());
	srand(time(NULL));
	sigfillset(&blocker);
	sigdelset(&blocker, SIGINT);
	sigdelset(&blocker, SIGUSR1);
	sigdelset(&blocker, SIGUSR2);
	sigprocmask(SIG_SETMASK, &blocker, NULL);

	signal(SIGINT, terminator);
	signal(SIGUSR1, showStats);

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

	return 0;
}


void controlTower() {

	int isUpdaterCreated = 0;
	int isDecisionCreated = 0;
	int arrivalsHelper = 0; 
	int departuresHelper = 0;

	sharedMemPtr->towerPid = getpid();
	
	signal(SIGUSR2, clearTower);
	signal(SIGINT, SIG_IGN);

	arrivalQueue = criaQueue();
	departureQueue = criaQueue();

	messageQueuePtr mensagem = criaMQStruct();

	while(sharedMemPtr->running){
		
		msgrcv(messageQueueID, mensagem, sizeof(messageStruct), -2, 0);

		if (mensagem->fuel == -1 && sharedMemPtr->totalDepartures < valuesPtr->maxChegadas){
			departuresHelper = newDeparture(mensagem, departuresHelper);

			if (isDecisionCreated == 0){
				pthread_create(&decisionThread,NULL,flightPlanner,NULL);
				isDecisionCreated = 1;
			}
		}
			
	
		else if (sharedMemPtr->totalArrivals < valuesPtr->maxPartidas){
			arrivalsHelper = newArrival(mensagem, arrivalsHelper);

			if (isUpdaterCreated == 0){
				pthread_create(&fuelThread,NULL,fuelUpdater,NULL);
				isUpdaterCreated = 1;
			}

			if (isDecisionCreated == 0){
				pthread_create(&decisionThread,NULL,flightPlanner,NULL);
				isDecisionCreated = 1;
			}
		}
		else sharedMemPtr->estatisticas.numeroRejeitados++;
	}
}

void clearTower(){
	printf("here|\n");
	sharedMemPtr->running = 0;
	printf("here|\n");
	pthread_join(timeThread,NULL);
	printf("here|\n");
	pthread_join(decisionThread,NULL);
	printf("here|\n");
	pthread_join(fuelThread,NULL);
	printf("here|\n");
	pthread_cond_destroy(&condTime);
	pthread_cond_destroy(&creator);
	pthread_cond_destroy(&condGeral);
	pthread_cond_destroy(&condDeparture);
	pthread_cond_destroy(&condArrival);
	printf("here|\n");
	pthread_mutex_destroy(&statsMutex);
	pthread_mutex_destroy(&timeMutex);
	pthread_mutex_destroy(&arrivalMutex);
	pthread_mutex_destroy(&departureMutex);
	pthread_mutex_destroy(&fuelMutex);
	pthread_mutex_destroy(&decisionMutex);
	printf("Cleantower\n");
	exit(0);
}


void *flightPlanner(){

	int utAtual, arrivalsReady, departuresReady, i,count = 0, pistaD = 0,pistaA = 0, lastflight = 2;
	int tempo, tempo_sec, tempo_nsec, result;
	queuePtr arrivalAux = arrivalQueue;
	queuePtr departureAux = arrivalQueue;
	struct timespec now = {0};
	struct timespec timetoWait = {0};

	while(sharedMemPtr->running){

		pthread_mutex_lock(&decisionMutex);

	    if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
	        perror("clock_gettime");
	        exit(EXIT_FAILURE);
	    }

	    utAtual = ((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + abs(now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / valuesPtr->unidadeTempo);
		departuresReady = contaQueue(departureQueue, utAtual); printf("%d\n",departuresReady);
		arrivalsReady = contaQueue(arrivalQueue, utAtual); printf("a%d\n",arrivalsReady);
		
		if ((arrivalsReady>= departuresReady && arrivalsReady >0) || (departureAux->nextNodePtr != NULL && arrivalAux->nextNodePtr !=NULL && departureAux->nextNodePtr->tempoDesejado + valuesPtr->duracaoDescolagem + valuesPtr->intervaloDescolagens > arrivalAux->nextNodePtr->fuel)){
			
			if (arrivalsReady == 0){
				timetoWait = ValorAbsoluto(sharedMemPtr->Time,arrivalQueue->nextNodePtr->tempoDesejado);
	        	pthread_cond_timedwait(&condGeral,&decisionMutex,&timetoWait);
			}
			if (arrivalsReady > 0){
				for (i = 0; i < arrivalsReady && i < 2; i++){
					if (arrivals[arrivalAux->nextNodePtr->slot].check == 0){
						strcpy(arrivals[arrivalAux->nextNodePtr->slot].ordem,"ATERRAR");
						arrivals[arrivalAux->nextNodePtr->slot].pista = pistaA++ % 2;
						removeQueue(arrivalAux);
						count++;

					}
					else {
						arrivalAux = arrivalAux ->nextNodePtr;
						i--;
					}
				}
				arrivalOrders(arrivalQueue, 5 - count);

				pthread_mutex_unlock(&decisionMutex);
				if(lastflight == 1) // LAST ARRIVALS
					timetoWait = ValorAbsoluto(now, valuesPtr->duracaoAterragem + valuesPtr->intervaloAterragens);
				else if(lastflight == 0) // LAST DEPARTURE
					timetoWait = ValorAbsoluto(now, valuesPtr->duracaoAterragem);
				else timetoWait = ValorAbsoluto(now, valuesPtr->duracaoAterragem);
				lastflight = 1;
				clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&timetoWait,NULL);
				pthread_mutex_lock(&decisionMutex);
			}
		}

		else if (departuresReady > arrivalsReady && departuresReady >= 1){
			

			for (i = 0; i < departuresReady && i < 2; i++){
				strcpy(departures[departureQueue->nextNodePtr->slot].ordem,"LEVANTAR");
				departures[departureQueue->nextNodePtr->slot].pista = pistaD++ % 2;
				removeQueue(departureQueue);
			}

			departureAux = departureQueue;

			while(departureAux->nextNodePtr != NULL){

				strcpy(departures[departureAux->nextNodePtr->slot].ordem,"WAIT");
				departures[departureAux->nextNodePtr->slot].duration = 1;
				departureAux = departureAux->nextNodePtr;
			}


			arrivalOrders(arrivalQueue, 5);

			pthread_mutex_unlock(&decisionMutex);

			if(lastflight == 1) // LAST ARRIVALS
					timetoWait = ValorAbsoluto(now, valuesPtr->duracaoDescolagem);
			else if(lastflight == 0) // LAST DEPARTURE
				timetoWait = ValorAbsoluto(now, valuesPtr->duracaoDescolagem + valuesPtr->intervaloDescolagens);
			else timetoWait = ValorAbsoluto(now, valuesPtr->duracaoDescolagem);
			lastflight = 0;
			timetoWait = ValorAbsoluto(now, valuesPtr->duracaoDescolagem + valuesPtr->intervaloDescolagens);
			clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&timetoWait,NULL);
			pthread_mutex_lock(&decisionMutex);
		
		}


		count=0;

		if (departuresReady ==0  || arrivalsReady == 0){

			if (departureQueue->nextNodePtr != NULL &&( arrivalQueue->nextNodePtr == NULL || departureQueue->nextNodePtr->tempoDesejado <= arrivalQueue->nextNodePtr->tempoDesejado)){
	        	timetoWait = ValorAbsoluto(sharedMemPtr->Time,departureQueue->nextNodePtr->tempoDesejado);
	        	result = pthread_cond_timedwait(&condGeral,&decisionMutex,&timetoWait);
		       	if (result !=0  && result != ETIMEDOUT) {
		            fprintf(stderr, "%s\n", strerror(result));
		            exit(EXIT_FAILURE);

		        }
			}

			else if (arrivalQueue->nextNodePtr != NULL &&( departureQueue->nextNodePtr == NULL || arrivalQueue->nextNodePtr->tempoDesejado <= departureQueue->nextNodePtr->tempoDesejado)){
	        	timetoWait = ValorAbsoluto(sharedMemPtr->Time,arrivalQueue->nextNodePtr->tempoDesejado);
	        	result = pthread_cond_timedwait(&condGeral,&decisionMutex,&timetoWait);
		       	if (result !=0  && result != ETIMEDOUT) {
		            fprintf(stderr, "%s\n", strerror(result));
		            exit(EXIT_FAILURE);

		        }
			}

			else{
				printf("waiting\n"); 
				pthread_cond_wait(&condGeral,&decisionMutex);
				printf("waiting\n");
			}
		}
		pthread_mutex_unlock(&decisionMutex);
	}
	
}

void arrivalOrders(queuePtr arrivalQueue, int num){

	queuePtr arrivalAux = arrivalQueue;
	int count = 0;

	while(arrivalAux->nextNodePtr != NULL){
		//printf("%d\n",arrivals[arrivalAux->nextNodePtr->slot].check);
		if (count< num){
			strcpy(arrivals[arrivalAux->nextNodePtr->slot].ordem,"WAIT");
			arrivals[arrivalAux->nextNodePtr->slot].duration = 1;
			count++;
			arrivalAux= arrivalAux->nextNodePtr;
		}
		else if (arrivalAux->nextNodePtr->prio == 1){
			strcpy(arrivals[arrivalAux->nextNodePtr->slot].ordem,"IMPOSSIBLE");
			sharedMemPtr->estatisticas.numeroHoldingsPrio++;
			removeQueue(arrivalAux);
		}
		else if (arrivals[arrivalAux->nextNodePtr->slot].check++ == 0){
			strcpy(arrivals[arrivalAux->nextNodePtr->slot].ordem,"HOLDING");
			sharedMemPtr->estatisticas.numeroHoldings++;
			arrivals[arrivalAux->nextNodePtr->slot].duration = valuesPtr->minHolding + rand() % (valuesPtr->maxHolding - valuesPtr->minHolding);
			insereQueue(arrivalQueue, arrivalAux->nextNodePtr->tempoDesejado + arrivals[arrivalAux->nextNodePtr->slot].duration, arrivalAux->nextNodePtr->fuel, arrivalAux->nextNodePtr->prio, arrivalAux->nextNodePtr->slot);
			removeQueue(arrivalAux);
		}
		else arrivalAux = arrivalAux->nextNodePtr;
	}
}

void *fuelUpdater(){

	int result;
	int ut = 1;
	struct timespec tempo = {0};
	queuePtr arrivalAux = arrivalQueue;

	result = clock_gettime(CLOCK_REALTIME, &tempo);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    tempo = ValorAbsoluto(sharedMemPtr->Time,ut++);

	while(sharedMemPtr->running){

		result = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&tempo,NULL);
		if (result !=0 && result !=EINVAL){
			fprintf(stderr, "%s\n", strerror(result));
			exit(EXIT_FAILURE);
		}

		while(arrivalAux->nextNodePtr !=NULL){

			pthread_mutex_lock(&fuelMutex);

			if (strcmp(arrivals[arrivalAux->nextNodePtr->slot].ordem,"ATERRAR")!= 0 && arrivalAux->nextNodePtr->fuel < valuesPtr->duracaoAterragem){
				strcpy(arrivals[arrivalAux->nextNodePtr->slot].ordem,"IMPOSSIBLE");
				sharedMemPtr->estatisticas.numeroHoldingsPrio++;
				removeQueue(arrivalAux);
			}
			else if (arrivalAux->nextNodePtr->fuel>0)
				arrivalAux->nextNodePtr->fuel--;
			
			if (arrivalAux->nextNodePtr->fuel < valuesPtr->minHolding && arrivalAux->nextNodePtr->prio != 1){
				arrivalAux->nextNodePtr->prio = 1;
				insereQueue(arrivalQueue, arrivalAux->nextNodePtr->tempoDesejado, arrivalAux->nextNodePtr->fuel, arrivalAux->nextNodePtr->prio, arrivalAux->nextNodePtr->slot);
				removeQueue(arrivalAux);
			}

			pthread_mutex_unlock(&fuelMutex);

			arrivalAux =arrivalAux->nextNodePtr;
		}

		arrivalAux= arrivalQueue;

    	tempo = ValorAbsoluto(sharedMemPtr->Time,ut++);
	}
}


int newDeparture(messageQueuePtr mensagem, int departuresHelper){

	int aux = 1;

	replyQueuePtr reply = criaReplyStruct();
	insereQueue(departureQueue,mensagem->tempoDesejado,mensagem->fuel,0,departuresHelper);
	pthread_cond_signal(&condGeral);

	reply->messageType = 4;
	reply->id = departuresHelper;

	strcpy(departures[departuresHelper++].ordem, "WAIT");
	msgsnd(messageQueueID, reply, sizeof(replyStruct), 0);

	sharedMemPtr->totalDepartures++;
	sharedMemPtr->estatisticas.totalVoos++;

	while(aux){
		if (departuresHelper == valuesPtr->maxPartidas) departuresHelper = 0;
			
		if (departures[departuresHelper].inUse == 1) departuresHelper++;
			
		else aux = 0;	
	}

	return departuresHelper;
}


int newArrival(messageQueuePtr mensagem, int arrivalsHelper){

	int aux = 1;
	replyQueuePtr reply = criaReplyStruct();

	if (4 + mensagem->tempoDesejado + valuesPtr->duracaoAterragem >= mensagem->fuel)
		insereQueue(arrivalQueue,mensagem->tempoDesejado,mensagem->fuel,1,arrivalsHelper);
	
	else insereQueue(arrivalQueue,mensagem->tempoDesejado,mensagem->fuel,0,arrivalsHelper);
	pthread_cond_signal(&condGeral);
	reply->messageType = 3;
	reply->id = arrivalsHelper;

	arrivals[arrivalsHelper].check = 0;
	strcpy(arrivals[arrivalsHelper++].ordem,"WAIT");
	msgsnd(messageQueueID, reply, sizeof(replyStruct), 0);

	sharedMemPtr->totalArrivals++;
	sharedMemPtr->estatisticas.totalVoos++;

	while(aux){
		if (arrivalsHelper == valuesPtr->maxChegadas) arrivalsHelper = 0;
			
		if (arrivals[arrivalsHelper].inUse == 1) arrivalsHelper++;
			
		else aux = 0;
	}

	return arrivalsHelper;
}


void initializeSlots(){

	int i;

	for (i = 0; i < valuesPtr->maxChegadas; i++){
		arrivals[i].inUse = 2;}
	for (i = 0; i < valuesPtr->maxPartidas; i++)
		departures[i].inUse = 2;
}


void flightManager() {

	char buffer[BUFSIZE];
	char* comando = malloc(60*sizeof(char));
	int fdNamedPipe, i , result;
	
	signal(SIGUSR2, terminator);

	result = clock_gettime(CLOCK_REALTIME, &sharedMemPtr->Time);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	logFile = fopen("Logfile.txt", "w");

	arrivalHead = criaArrivals();
	departureHead = criaDepartures();
	
	pthread_create(&timeThread,NULL,timerCount,NULL);

	fdNamedPipe = criaPipe();
	calculaHora();
	startLog();


	while (sharedMemPtr->isActive){

		i = 0;

		read(fdNamedPipe,&buffer,BUFSIZE);
		comando = strtok(buffer,"\n");

		if (comando != NULL){
			processaComando(comando);
		
			while((comando = strtok(NULL,"\n")) != NULL)
				processaComando(comando);
		}
		else insertLogfile("WRONG COMMAND =>",comando);
	}

	//PRINTAR NO LOG O RESTO DO BUFFER
}

void processaComando(char* comando){

	if ((comando[0] == 'A') && (confirmaSintaxe(comando, ARRIVAL_PATTERN) == 1)){

			processaArrival(comando);
			//printArrivals(arrivalHead);
		}

		else if ((comando[0] == 'D') && (confirmaSintaxe(comando, DEPARTURE_PATTERN) == 1)){

			processaDeparture(comando);
			//printDepartures(sharedMemPtr->departureHead);
		}

		else insertLogfile("WRONG COMMAND =>",comando);
}

void criaSharedMemory(){

	int maxVoos = valuesPtr->maxPartidas + valuesPtr->maxChegadas;

	if ((shmid = shmget(IPC_PRIVATE, sizeof(memStruct), IPC_CREAT | 0777)) < 1) {
		perror("Error creating shared memory\n");
		exit(1);
	}
	
	if ((sharedMemPtr = (memStruct*)shmat(shmid, NULL, 0)) < (memStruct*)1) {
		perror("Error in shmat\n");
		exit(1);
	}

	sharedMemPtr->totalArrivals = 0;
	sharedMemPtr->totalDepartures = 0;
	sharedMemPtr->running = 1;
	sharedMemPtr->isActive = 1;

	inicializaStats();

	departures = (shmSlotsPtr)malloc(sizeof(shmSlotsStruct)*(valuesPtr->maxPartidas));
	arrivals = (shmSlotsPtr)malloc(sizeof(shmSlotsStruct)*(valuesPtr->maxChegadas));

	initializeSlots();

	if ((shmidDepartures = shmget(IPC_PRIVATE, valuesPtr->maxPartidas*sizeof(shmSlotsStruct), IPC_CREAT | 0777)) < 1) {
		perror("Error creating shared memory\n");
		exit(1);
	}

	if ((departures = (shmSlotsPtr)shmat(shmidDepartures, NULL, 0)) < (shmSlotsPtr)1) {
		perror("Error in departures shmat\n");
		exit(1);
	}

	if ((shmidArrivals = shmget(IPC_PRIVATE, valuesPtr->maxChegadas*sizeof(shmSlotsStruct), IPC_CREAT | 0777)) < 1) {
		perror("Error creating shared memory\n");
		exit(1);
	}

	if ((arrivals = (shmSlotsPtr)shmat(shmidArrivals, NULL, 0)) < (shmSlotsPtr)1){
		perror("Error in arrivals shmat\n");
		exit(1);
	}
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

void timeComparator(){

	int timer;
	int result;
	struct timespec now;
	arrivalPtr copyArrival;
	departurePtr copyDeparture;

	result = clock_gettime(CLOCK_REALTIME, &now);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	timer = ((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + (now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / valuesPtr->unidadeTempo);
   
   	while ((arrivalHead->nextNodePtr != NULL) && (arrivalHead->nextNodePtr->init == timer)){
		copyArrival = arrivalCopy(arrivalHead->nextNodePtr);
		removeArrival(arrivalHead);
		pthread_create(&arrivalThreads[sizeArrivals++],NULL,ArrivalFlight,(void *)copyArrival);
    }

    while ((departureHead->nextNodePtr != NULL) && (departureHead->nextNodePtr->init == timer)){
    	copyDeparture = departureCopy(departureHead->nextNodePtr);
		removeDeparture(departureHead);
   		pthread_create(&departureThreads[sizeDepartures++],NULL,DepartureFlight,(void *)copyDeparture);
    }
}


void *timerCount(){
     int aux=0,result;
     int tempo=0, tempo_sec=0, tempo_nsec=0;
     struct timespec timetoWait = { 0 };

     while(sharedMemPtr->running){

        aux=0;

        timeComparator();
        result = pthread_mutex_lock(&timeMutex);

        if (result != 0) {
            fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
            exit(EXIT_FAILURE);
           } 

            if(arrivalHead->nextNodePtr == NULL && departureHead->nextNodePtr == NULL){
	            pthread_cond_wait(&condTime,&timeMutex);
	            aux=1;
            }

        else if(departureHead->nextNodePtr == NULL && arrivalHead->nextNodePtr != NULL)
            tempo = arrivalHead->nextNodePtr->init;

        else if (arrivalHead->nextNodePtr == NULL && departureHead->nextNodePtr != NULL)
            tempo = departureHead->nextNodePtr->init;

        else if (arrivalHead->nextNodePtr->init <= departureHead->nextNodePtr->init)
            tempo = arrivalHead->nextNodePtr->init;

        else if (arrivalHead->nextNodePtr->init > departureHead->nextNodePtr->init)
            tempo = departureHead->nextNodePtr->init;

        timetoWait= ValorAbsoluto(sharedMemPtr->Time,tempo);


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


void inicializaStats(){

	sharedMemPtr->estatisticas.totalVoos = 0;
	sharedMemPtr->estatisticas.totalArrivals = 0;
	sharedMemPtr->estatisticas.totalDepartures = 0;
	sharedMemPtr->estatisticas.numeroVoosRedirecionados = 0;
	sharedMemPtr->estatisticas.numeroRejeitados = 0;
	sharedMemPtr->estatisticas.numeroHoldings = 0;
	sharedMemPtr->estatisticas.numeroHoldingsPrio = 0;
	sharedMemPtr->estatisticas.tempoEsperadoA = 0;
	sharedMemPtr->estatisticas.tempoEsperadoD = 0;
	sharedMemPtr->estatisticas.totalPrio = 0;
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


	if ((fuel >= eta) && (((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + (now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / valuesPtr->unidadeTempo) <= init)){
		pthread_cond_signal(&condTime);
		insertLogfile("NEW COMMAND =>",comando);
		if (started == 0)
			started = 1;
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
	int result;
	departurePtr aux = departureHead;
	struct timespec now;
	
	result = clock_gettime(CLOCK_REALTIME, &now);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	sscanf(comando, "DEPARTURE %s init: %d takeoff: %d", nome, &init, &takeoff);


	if (((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + (now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / valuesPtr->unidadeTempo) <= init){
		pthread_cond_signal(&condTime);
		insertLogfile("NEW COMMAND =>",comando);
		if (started == 0)
			started = 1;
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
	int isWorking = 1;
	int result, utAtual, tempoDesejado;
	char pista[4];
	struct timespec tempo = {0};
	struct timespec now;


	insertLogfile("ARRIVAL STARTED =>",((arrivalPtr)flight)->nome);

	tempoDesejado = ((arrivalPtr)flight)->init + ((arrivalPtr)flight)->eta;
	enviar->fuel = ((arrivalPtr)flight)->fuel;
	enviar->tempoDesejado = tempoDesejado;
	
	if (4 + enviar->tempoDesejado + valuesPtr->duracaoAterragem >= enviar->fuel)
		enviar->messageType = 2;
	
	else
		enviar->messageType = 1;

	msgsnd(messageQueueID, enviar, sizeof(messageStruct), 0);
	pthread_mutex_lock(&arrivalMutex);
	msgrcv(messageQueueID, reply, sizeof(replyStruct), 3, 0);
	calculaHora();	
	clock_gettime(CLOCK_REALTIME, &tempo);

    tempo = ValorAbsoluto(sharedMemPtr->Time,enviar->tempoDesejado);
	pthread_cond_timedwait(&condArrival,&arrivalMutex,&tempo);

	while (isWorking){

		clock_gettime(CLOCK_REALTIME, &tempo);

		if (strcmp(arrivals[reply->id].ordem,"ATERRAR") == 0){

			pthread_mutex_unlock(&arrivalMutex);
			if (arrivals[reply->id].pista == 0) strcpy(pista,"28L");
			else strcpy(pista,"28R");

			calculaHora();	
			printf("%02d:%02d:%02d VOO SLOT %s => Tenho a ordem: %s NA PISTA %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, ((arrivalPtr)flight)->nome, arrivals[reply->id].ordem, pista);
			usleep((valuesPtr->duracaoAterragem) * (valuesPtr->unidadeTempo) * 1000);
			insertLogfile("ARRIVAL CONCLUDED =>",((arrivalPtr)flight)->nome);
			sharedMemPtr->totalArrivals--;
			isWorking = 0; 
			pthread_mutex_lock(&statsMutex);
			sharedMemPtr->estatisticas.totalArrivals++;
			sharedMemPtr->estatisticas.tempoEsperadoA += utAtual - tempoDesejado - valuesPtr->duracaoAterragem;
			pthread_mutex_unlock(&statsMutex);
		}

		else if (strcmp(arrivals[reply->id].ordem,"WAIT") == 0){

    		tempo = ValorAbsoluto(tempo, arrivals[reply->id].duration);
    		pthread_cond_timedwait(&condArrival,&arrivalMutex,&tempo);
		}

		else if (strcmp(arrivals[reply->id].ordem,"HOLDING")==0){

			calculaHora();
    		printf("%02d:%02d:%02d %s => Tenho a ordem: %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec,((arrivalPtr)flight)->nome, arrivals[reply->id].ordem);
    		tempo = ValorAbsoluto(tempo, arrivals[reply->id].duration);
    		pthread_mutex_unlock(&arrivalMutex);
			clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&tempo,NULL);
			arrivals[reply->id].check--;
			arrivals[reply->id].duration = 1;
			strcpy(arrivals[reply->id].ordem,"WAIT");
			pthread_mutex_lock(&arrivalMutex);
		}

		else if (strcmp(arrivals[reply->id].ordem,"IMPOSSIBLE")==0){
			pthread_mutex_unlock(&arrivalMutex);
			calculaHora();
    		printf("%02d:%02d:%02d VOO %s => Tenho a ordem: %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, ((arrivalPtr)flight)->nome, arrivals[reply->id].ordem);
   			sharedMemPtr->totalArrivals--;
   			isWorking = 0;
   			pthread_mutex_lock(&statsMutex);
			sharedMemPtr->estatisticas.numeroVoosRedirecionados++;
			pthread_mutex_unlock(&statsMutex); 		
		}

	}
	arrivals[reply->id].inUse = 1;

	// PROBABLY NOT IN THE BEST PLACE, PQ MUDAMOS COISAS 

	if (sharedMemPtr->totalArrivals == 0 && sharedMemPtr->totalDepartures == 0 && sharedMemPtr->isActive == 0)
		kill(sharedMemPtr->towerPid, SIGUSR2);

	pthread_exit(0);
}


void *DepartureFlight(void *flight){

	messageQueuePtr enviar = criaMQStruct();
	replyQueuePtr reply = criaReplyStruct();
	int result, utAtual, tempoDesejado;
	char pista[4];
	struct timespec tempo = {0};
	struct timespec now;


	insertLogfile("DEPARTURE STARTED =>",((departurePtr)flight)->nome);

	tempoDesejado = ((departurePtr)flight)->init + ((departurePtr)flight)->takeoff;
	enviar->messageType = 2;
	enviar->tempoDesejado = tempoDesejado;

	msgsnd(messageQueueID, enviar, sizeof(messageStruct), 0);
	pthread_mutex_lock(&departureMutex);
	calculaHora();	
	msgrcv(messageQueueID, reply, sizeof(replyStruct), 4, 0);
	tempo = ValorAbsoluto(sharedMemPtr->Time,enviar->tempoDesejado);
	pthread_cond_timedwait(&condDeparture, &departureMutex,&tempo);
	while (strcmp(departures[reply->id].ordem,"LEVANTAR")!=0){
		clock_gettime(CLOCK_REALTIME, &tempo);
		if (strcmp(departures[reply->id].ordem,"WAIT") == 0){

    		tempo = ValorAbsoluto(tempo, departures[reply->id].duration);
			pthread_cond_timedwait(&condDeparture,&departureMutex,&tempo);
		}
	}

	if (departures[reply->id].pista == 0) strcpy(pista,"01L");
	else strcpy(pista,"01R");
	
	calculaHora();

	pthread_mutex_unlock(&departureMutex);
	printf("%02d:%02d:%02d VOO %s => Tenho a ordem: %s DA PISTA %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, ((departurePtr)flight)->nome, departures[reply->id].ordem, pista);

	usleep((valuesPtr->duracaoDescolagem) * (valuesPtr->unidadeTempo) * 1000);
	insertLogfile("DEPARTURE CONCLUDED =>",((departurePtr)flight)->nome);
	
	departures[reply->id].inUse = 1;

	if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
	        perror("clock_gettime");
	        exit(EXIT_FAILURE);
	    }

	utAtual = ((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + abs(now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / valuesPtr->unidadeTempo);
	
	pthread_mutex_lock(&statsMutex);
	sharedMemPtr->totalDepartures--;
	sharedMemPtr->estatisticas.totalDepartures++;
	sharedMemPtr->estatisticas.tempoEsperadoD += utAtual - tempoDesejado - valuesPtr->duracaoDescolagem;
	pthread_mutex_unlock(&statsMutex);
	
	if (sharedMemPtr->totalArrivals == 0 && sharedMemPtr->totalDepartures == 0 && sharedMemPtr->isActive == 0)
		kill(sharedMemPtr->towerPid, SIGUSR2);

	pthread_exit(0);
}

void showStats(){

	printf("\n PRINTING STATS \n");
	printf("Voos criados -- %d\n", sharedMemPtr->estatisticas.totalVoos);
	printf("Voos que aterraram -- %d\n", sharedMemPtr->estatisticas.totalArrivals);
	printf("Voos que descolaram -- %d\n", sharedMemPtr->estatisticas.totalDepartures);
	printf("Voos rejeitados pela Torre de Controlo -- %d\n", sharedMemPtr->estatisticas.numeroRejeitados);
	printf("Voos redirecionados -- %d\n", sharedMemPtr->estatisticas.numeroVoosRedirecionados);

	if (sharedMemPtr->estatisticas.totalArrivals == 0)
		printf("Holding por aterragem prioritária -- 0\n");
	else
		printf("Tempo médio de espera (A) -- %03lf\n", (float)sharedMemPtr->estatisticas.tempoEsperadoA / sharedMemPtr->estatisticas.totalArrivals);

	if (sharedMemPtr->estatisticas.totalDepartures == 0)
		printf("Holding por aterragem prioritária -- 0\n");
	else
		printf("Tempo médio de espera (D) -- %03lf\n", (float)sharedMemPtr->estatisticas.tempoEsperadoD / sharedMemPtr->estatisticas.totalDepartures);

	if (sharedMemPtr->estatisticas.totalArrivals == 0)
		printf("Holding por aterragem prioritária -- 0\n");
	else
		printf("Holding por aterragem -- %03lf\n", (float)sharedMemPtr->estatisticas.numeroHoldings / sharedMemPtr->estatisticas.totalArrivals);
	
	if (sharedMemPtr->estatisticas.totalPrio == 0)
		printf("Holding por aterragem prioritária -- 0\n");
	else
		printf("Holding por aterragem prioritária -- %03lf\n", (float)sharedMemPtr->estatisticas.numeroHoldingsPrio / sharedMemPtr->estatisticas.totalPrio);

	printf("\n-- END OF STATS --\n");
}

void terminator(){

	int i;
	
	printf("Tutto finisce..\n");

	sharedMemPtr->isActive = 0;

	if (started == 0)
		kill(sharedMemPtr->towerPid, SIGUSR2);

	wait(NULL);
	showStats();

	unlink(PIPE_NAME);
	remove(PIPE_NAME);

	freeArrivals(arrivalHead);
	freeDepartures(departureHead);

	msgctl(messageQueueID, IPC_RMID, 0);

	endLog();
	pthread_mutex_destroy(&logMutex);

	shmdt(sharedMemPtr);
	shmctl(shmid,IPC_RMID,NULL);
	shmdt(departures);
	shmctl(shmidDepartures,IPC_RMID,NULL);
	shmdt(arrivals);
	shmctl(shmidArrivals,IPC_RMID,NULL);
	
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
	fclose(logFile);
}

void calculaHora(){

	time_t tempo;

	time(&tempo);
 	sharedMemPtr->structHoras = localtime(&tempo);
}

struct timespec ValorAbsoluto(struct timespec now,int tempo){
	struct timespec timetoWait = {0};
	int tempo_sec, tempo_nsec;
	tempo_sec = tempo * valuesPtr->unidadeTempo /1000;
	tempo_nsec = (tempo * valuesPtr->unidadeTempo %1000) *1000000;

	timetoWait.tv_sec = now.tv_sec + tempo_sec + (tempo_nsec + now.tv_nsec) /1000000000;
	timetoWait.tv_nsec = (tempo_nsec + now.tv_nsec) %1000000000;

	return timetoWait;
}
