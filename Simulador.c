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
	int duration;
	int inUse;

} shmSlotsStruct;

typedef struct estatisticasStruct* estatisticasPtr;
typedef struct estatisticasStruct{

	int totalVoos;
	int totalArrivals;
	int totalDepartures;
	int numeroVoosRedirecionados;
	int numeroRejeitados;
	float tempoMedioEsperaA;
	float tempoMedioEsperaD;
	float mediaManobrasHolding;
	float mediaManobrasHoldingPrio;

} statsStruct;


typedef struct sharedMemStruct* memoryPtr ;
typedef struct sharedMemStruct{

	struct tm * structHoras;
	struct timespec Time;
	statsStruct estatisticas;

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
void Aterragem(struct timespec tempo);
void Descolagem(struct timespec tempo);
struct timespec ValorAbsoluto(struct timespec now,int tempo);

//Exit Condition
int isActive= 1;

//MESSAGE QUEUE
int messageQueueID;
sem_t* msgsem;

//SHARED MEMORY
memoryPtr sharedMemPtr;
int shmid, shmidDepartures, shmidArrivals;

//PTHREADS
pthread_mutex_t timeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condTime = PTHREAD_COND_INITIALIZER;
pthread_cond_t creator = PTHREAD_COND_INITIALIZER;
pthread_t timeThread;

//PTHREADS ARRIVALS
pthread_mutex_t arrivalMutex = PTHREAD_MUTEX_INITIALIZER;	/* ŃOT USED YET*/
pthread_cond_t condArrival = PTHREAD_COND_INITIALIZER;
pthread_t arrivalThreads[LIMITEVOOS];
int sizeArrivals = 0;

//PTHREADS DEPARTURES
pthread_mutex_t departureMutex = PTHREAD_MUTEX_INITIALIZER;	/* ŃOT USED YET*/
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

int PISTAS[4];

arrivalPtr arrivalHead;
departurePtr departureHead;

valuesStructPtr valuesPtr; 
shmSlotsPtr arrivals;
shmSlotsPtr departures;

queuePtr arrivalQueue;
queuePtr departureQueue;

int main() {

	pid_t childPid;

	signal(SIGINT,terminate);
	
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

	int isUpdaterCreated = 0;
	int isDecisionCreated = 0;
	int arrivalsHelper = 0; 
	int departuresHelper = 0;

	arrivalQueue = criaQueue();
	departureQueue = criaQueue();

	messageQueuePtr mensagem = criaMQStruct();

	while(isActive == 1){
		
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

/*
	int unidadeTempo;
	int duracaoDescolagem;
	int intervaloDescolagens;
	int duracaoAterragem;
	int intervaloAterragens;
	int minHolding;
	int maxHolding;
	int maxPartidas;
	int maxChegadas;
	arrivals[reply->id].ordem;



void *flightPlanner(){
	int tempo=0, tempo_sec=0, tempo_nsec=0;
	int selection = 0;
	struct timespec timetoWait = {0};
	struct timespec check = {0};
	int result;
	replyQueuePtr reply = criaReplyStruct();
	printf("Escolha feita\n");
	while(isActive){

		pthread_mutex_lock(&decisionMutex);

		if (arrivalQueue->nextNodePtr == NULL && departureQueue == NULL)
			pthread_cond_wait(&condGeral,&decisionMutex);
		
		//OPCAO 1- ARRIVALS TD < DEPARTURE TD  -- ARRIVALS COM APENAS 1 FLIGHT
		//OPCAO 2- ARRIVALS TD < DEPARTURE TD  OU ARRIVALS MAIOR MAS 2 VOOS EM ESPERA E DEPARTURE APENAS 1 -- ARRIVALS COM 2 FLIGHTS
		// OPCAO 3 - DEPARTURE TD <ARRIVALS -- DEPARTURE 1 FLIGHT
		//OPCAO 4 DEPARTURE TD < ARRIVALS TD OU DEPARTURE MAIOR MAS 2 VOOS EM ESPERA E ARRIVALS SO 1 -- DEPARTURE 2 FLIGHTS
		if (departureQueue->nextNodePtr ==  NULL){
			tempo = arrivalQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo;
			selection = 1;
		}
		else if (arrivalQueue->nextNodePtr == NULL){
			tempo = departureQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo;
			selection = 3;
		}

		else if(arrivalQueue->nextNodePtr->tempoDesejado <= departureQueue->nextNodePtr->tempoDesejado){
			if (arrivalQueue->nextNodePtr->nextNodePtr == NULL && departureQueue->nextNodePtr->nextNodePtr != NULL){
				tempo = departureQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo;
				selection = 4;
			}
			else{
				tempo = arrivalQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo;
				if (arrivalQueue->nextNodePtr->nextNodePtr != NULL){
					selection = 2;
				}
				else selection =1;
			}
		}

		else if(departureQueue->nextNodePtr->tempoDesejado < arrivalQueue->nextNodePtr->tempoDesejado ){
			if (departureQueue->nextNodePtr->nextNodePtr == NULL && arrivalQueue->nextNodePtr->nextNodePtr != NULL){
				tempo = arrivalQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo;
				selection = 2;
			}
			else{
				tempo = departureQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo;
				if (departureQueue->nextNodePtr->nextNodePtr != NULL){
					selection = 4;
				}
				else selection = 2;
			}
		}
		printf("HERE1!\n");
		tempo_sec = tempo/1000;
        tempo_nsec = (tempo%1000)*1000000;
        printf("HERE2!\n");
        timetoWait.tv_sec = sharedMemPtr->Time.tv_sec + tempo_sec + (tempo_nsec + sharedMemPtr->Time.tv_nsec)/1000000000;
        timetoWait.tv_nsec = (tempo_nsec + sharedMemPtr->Time.tv_nsec)%1000000000;

		result = pthread_cond_timedwait(&condGeral,&decisionMutex,&timetoWait);
	    if (result !=0  && result == ETIMEDOUT) {
	        fprintf(stderr, "%s\n", strerror(result));
	        exit(EXIT_FAILURE);
	    }
	    printf("Escolha feita");

	    result = clock_gettime(CLOCK_REALTIME, &check);
		    if (result == -1) {
	        perror("clock_gettime");
	        exit(EXIT_FAILURE);
	    }

	    printf("%s-%s\n",ctime(&timetoWait.tv_sec),ctime(&check.tv_sec));

	    if (difftime(timetoWait.tv_sec,check.tv_sec) < 0){
	    	printf("WELL FUCK\n");
	    	continue;
	    }

		printf("STARTING ORDEM\n");
	    if (selection == 1 || selection == 2){
	    	strcpy(arrivals[(arrivalQueue->nextNodePtr->slot)].ordem,"ATERRAR");
	    	if (selection == 2)
	    		strcpy(arrivals[(arrivalQueue->nextNodePtr->nextNodePtr->slot)].ordem,"ATERRAR");

	    	tempo_sec = (valuesPtr->duracaoAterragem + valuesPtr->intervaloAterragens) * valuesPtr->unidadeTempo /1000;
	    	tempo_nsec = ((valuesPtr->duracaoAterragem + valuesPtr->intervaloAterragens) * valuesPtr->unidadeTempo %1000) *1000000;

	    	timetoWait.tv_sec = sharedMemPtr->Time.tv_sec + tempo_sec + (tempo_nsec + sharedMemPtr->Time.tv_nsec) /1000000000;
        	timetoWait.tv_nsec = (tempo_nsec + sharedMemPtr->Time.tv_nsec) %1000000000;

	    	result = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&timetoWait,NULL);
			if (result !=0 && result !=EINVAL){
				fprintf(stderr, "%s\n", strerror(result));
				exit(EXIT_FAILURE);
			}
	    }
	    if (selection == 3 || selection == 4){
	    	strcpy(departures[(departureQueue->nextNodePtr->slot)].ordem,"PARTIR");
	    	if (selection == 4)
	    		strcpy(departures[(departureQueue->nextNodePtr->nextNodePtr->slot)].ordem,"PARTIR");

	    	tempo_sec = (valuesPtr->duracaoDescolagem + valuesPtr->intervaloDescolagens) * valuesPtr->unidadeTempo /1000;
	    	tempo_nsec = ((valuesPtr->duracaoDescolagem + valuesPtr->intervaloDescolagens) * valuesPtr->unidadeTempo %1000) *1000000;

	    	timetoWait.tv_sec = sharedMemPtr->Time.tv_sec + tempo_sec + (tempo_nsec + sharedMemPtr->Time.tv_nsec) /1000000000;
        	timetoWait.tv_nsec = (tempo_nsec + sharedMemPtr->Time.tv_nsec) %1000000000;

	    	result = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&timetoWait,NULL);
			if (result !=0 && result !=EINVAL){
				fprintf(stderr, "%s\n", strerror(result));
				exit(EXIT_FAILURE);
			}
	    }
	    arrivals[reply->id].ordem;
		pthread_mutex_unlock(&decisionMutex);

	}
}
*/


void *flightPlanner(){

	int utAtual, arrivalsReady, departuresReady, i,count = 0;
	int tempo, tempo_sec, tempo_nsec;
	queuePtr arrivalAux = arrivalQueue;
	queuePtr departureAux = arrivalQueue;
	struct timespec now = {0};
	struct timespec timetoWait = {0};
	while(isActive){

		pthread_mutex_lock(&decisionMutex);

	    if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
	        perror("clock_gettime");
	        exit(EXIT_FAILURE);
	    }
		utAtual = ((1000* (now.tv_sec - sharedMemPtr->Time.tv_sec) + abs(now.tv_nsec - sharedMemPtr->Time.tv_nsec)/1000000) / valuesPtr->unidadeTempo);
		departuresReady = contaQueue(departureQueue, utAtual);
		arrivalsReady = contaQueue(arrivalQueue, utAtual);
		printf("%d\n",arrivalsReady);
		if (arrivalsReady >= departuresReady && arrivalsReady >= 1){

			if (arrivalsReady >= 1){
				for (i = 0; i < arrivalsReady && i < 2; i++){
					strcpy(arrivals[arrivalQueue->nextNodePtr->slot].ordem,"ATERRAR");
					removeQueue(arrivalQueue);
				}
				arrivalAux = arrivalQueue;
				while(arrivalAux->nextNodePtr != NULL){
					if (count<2){
						strcpy(arrivals[arrivalAux->nextNodePtr->slot].ordem,"WAIT");
						count++;
					}
					else{
						strcpy(arrivals[arrivalAux->nextNodePtr->slot].ordem,"HOLDING");
						arrivals[arrivalAux->nextNodePtr->slot].duration = 20;
						insereQueue(arrivalQueue, arrivalQueue->tempoDesejado + 20, arrivalQueue->fuel, arrivalQueue->prio, arrivalQueue->slot);
						//REMOVER DO MEIO
						removeQueue(arrivalAux); //DOESNT WORK
					}
					if(arrivalAux->nextNodePtr != NULL) arrivalAux= arrivalAux->nextNodePtr;
				}

				pthread_cond_broadcast(&condArrival);
				pthread_mutex_unlock(&decisionMutex);
				//sleep(10);
				pthread_mutex_lock(&decisionMutex);
			}

			else{
				//printf("Broken Arrivals\n");
			}
		}

		else {
			
			if (departuresReady >= 1){

				for (i = 0; i < departuresReady && i < 2; i++){
					strcpy(departures[departureQueue->nextNodePtr->slot].ordem,"LEVANTAR");
					//FAZER FUNCOES PARA REMOVER A CABECA
					removeQueue(departureQueue);
				}
				pthread_cond_broadcast(&condDeparture);

				pthread_mutex_unlock(&decisionMutex);
				sleep(10);
				pthread_mutex_lock(&decisionMutex);
			}

			else{
				//printf("Broken Departures\n");
			}
		}


		count=0;
		usleep(valuesPtr->unidadeTempo*1000);

		/*  ÑAO FUNCIONA PARA DEPARTURES OU ARRIVALS NULOS E EU QUERO ME MATAR
		if (departuresReady ==0  && arrivalsReady == 0){
			if (departureQueue->nextNodePtr != NULL  || departureQueue->nextNodePtr->tempoDesejado <= arrivalQueue->nextNodePtr->tempoDesejado){
				tempo_sec = (departureQueue->nextNodePtr->tempoDesejado) * valuesPtr->unidadeTempo /1000;
		    	tempo_nsec = (departureQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo %1000) *1000000;

		    	timetoWait.tv_sec = sharedMemPtr->Time.tv_sec + tempo_sec + (tempo_nsec + sharedMemPtr->Time.tv_nsec) /1000000000;
	        	timetoWait.tv_nsec = (tempo_nsec + sharedMemPtr->Time.tv_nsec) %1000000000;

	        	pthread_cond_timedwait(&condGeral,&decisionMutex,&timetoWait);
			}
			else{
				tempo_sec = (arrivalQueue->nextNodePtr->tempoDesejado) * valuesPtr->unidadeTempo /1000;
		    	tempo_nsec = (arrivalQueue->nextNodePtr->tempoDesejado * valuesPtr->unidadeTempo %1000) *1000000;

		    	timetoWait.tv_sec = sharedMemPtr->Time.tv_sec + tempo_sec + (tempo_nsec + sharedMemPtr->Time.tv_nsec) /1000000000;
	        	timetoWait.tv_nsec = (tempo_nsec + sharedMemPtr->Time.tv_nsec) %1000000000;

	        	pthread_cond_timedwait(&condGeral,&decisionMutex,&timetoWait);
			}
		}
		*/
		pthread_mutex_unlock(&decisionMutex);
	}
	
}



void *fuelUpdater(){

	int result;
	struct timespec tempo = {0};
	queuePtr arrivalAux = arrivalQueue;

	result = clock_gettime(CLOCK_REALTIME, &tempo);
    if (result == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    tempo.tv_nsec = (sharedMemPtr->Time.tv_nsec + valuesPtr->unidadeTempo*1000000) % 1000000000;
    tempo.tv_sec = tempo.tv_sec +(sharedMemPtr->Time.tv_nsec + valuesPtr->unidadeTempo*1000000) / 1000000000;

	while(isActive == 1){

		result = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&tempo,NULL);
		if (result !=0 && result !=EINVAL){
			fprintf(stderr, "%s\n", strerror(result));
			exit(EXIT_FAILURE);
		}

		while(arrivalAux->nextNodePtr !=NULL){

			pthread_mutex_lock(&fuelMutex);

			if (arrivalAux >0)
				arrivalAux->nextNodePtr->fuel--;

			pthread_mutex_unlock(&fuelMutex);

			arrivalAux =arrivalAux->nextNodePtr;
		}

		arrivalAux= arrivalQueue;

    	tempo.tv_sec = tempo.tv_sec +(tempo.tv_nsec + valuesPtr->unidadeTempo*1000000) / 1000000000;
    	tempo.tv_nsec = (tempo.tv_nsec + valuesPtr->unidadeTempo*1000000) % 1000000000;
	}
}


int newDeparture(messageQueuePtr mensagem, int departuresHelper){

	int aux = 1;

	replyQueuePtr reply = criaReplyStruct();

	insereQueue(departureQueue,mensagem->tempoDesejado,mensagem->fuel,1,departuresHelper);
	pthread_cond_signal(&condGeral);

	printf("NEW DEPARTURE -- td: %d\n", mensagem->tempoDesejado);

	reply->messageType = 3;
	reply->id = departuresHelper;

	strcpy(departures[departuresHelper++].ordem, "HOLDING420");
	msgsnd(messageQueueID, reply, sizeof(replyStruct), 0);

	sharedMemPtr->totalDepartures++;
	sharedMemPtr->estatisticas.totalVoos++;

	while(aux){
		if (departuresHelper == valuesPtr->maxPartidas) departuresHelper = 0;
			
		if (arrivals[departuresHelper].inUse == 1) departuresHelper++;
			
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

	char* comando = malloc(CINQ*sizeof(char));
	char letra;
	int fdNamedPipe, i , result;

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

     while(isActive == 1){

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
	sharedMemPtr->estatisticas.tempoMedioEsperaA = 0;
	sharedMemPtr->estatisticas.tempoMedioEsperaD = 0;
	sharedMemPtr->estatisticas.mediaManobrasHolding = 0;
	sharedMemPtr->estatisticas.mediaManobrasHoldingPrio = 0;
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
	int isWorking = 1,result;
	struct timespec check = {0};
	struct timespec tempo = {0};

	insertLogfile("ARRIVAL STARTED =>",((arrivalPtr)flight)->nome);

	enviar->fuel = ((arrivalPtr)flight)->fuel;
	enviar->tempoDesejado = ((arrivalPtr)flight)->init + ((arrivalPtr)flight)->eta;
	
	if (4 + enviar->tempoDesejado + valuesPtr->duracaoAterragem >= enviar->fuel)
		enviar->messageType = 2;
	
	else
		enviar->messageType = 1;

	msgsnd(messageQueueID, enviar, sizeof(messageStruct), 0);
	pthread_mutex_lock(&arrivalMutex);
	msgrcv(messageQueueID, reply, sizeof(replyStruct), 3, 0);
	calculaHora();	
	printf("%02d:%02d:%02d VOO SLOT[%d] => Tenho a ordem: %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, reply->id, arrivals[reply->id].ordem);
	clock_gettime(CLOCK_REALTIME, &tempo);

    tempo = ValorAbsoluto(sharedMemPtr->Time,enviar->tempoDesejado);
	pthread_cond_timedwait(&condArrival,&arrivalMutex,&tempo);
	while (strcmp(arrivals[reply->id].ordem,"ATERRAR")!=0){
		clock_gettime(CLOCK_REALTIME, &tempo);

		if (strcmp(arrivals[reply->id].ordem,"WAIT")==0){
    		tempo = ValorAbsoluto(tempo, arrivals[reply->id].duration);
			pthread_cond_timedwait(&condArrival,&arrivalMutex,&tempo);
		}
		else if (strcmp(arrivals[reply->id].ordem,"HOLDING")==0){
			calculaHora();
    		printf("%02d:%02d:%02d VOO SLOT[%d] => Tenho a ordem: %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, reply->id, arrivals[reply->id].ordem);
    		tempo = ValorAbsoluto(tempo, arrivals[reply->id].duration);
    		pthread_mutex_unlock(&arrivalMutex);
			clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,&tempo,NULL);
			pthread_mutex_lock(&arrivalMutex);
		}

	}
	pthread_mutex_unlock(&arrivalMutex);
	calculaHora();	
	printf("%02d:%02d:%02d VOO SLOT[%d] => Tenho a ordem: %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, reply->id, arrivals[reply->id].ordem);
	usleep((valuesPtr->duracaoAterragem) * (valuesPtr->unidadeTempo) * 1000);
	insertLogfile("ARRIVAL CONCLUDED =>",((arrivalPtr)flight)->nome);

	sharedMemPtr->totalArrivals--;
	pthread_exit(0);
}


void *DepartureFlight(void *flight){

	messageQueuePtr enviar = criaMQStruct();
	replyQueuePtr reply = criaReplyStruct();
	int isWorking = 1;

	insertLogfile("DEPARTURE STARTED =>",((departurePtr)flight)->nome);

	enviar->messageType = 2;
	enviar->tempoDesejado = ((departurePtr)flight)->init + ((departurePtr)flight)->takeoff;

	msgsnd(messageQueueID, enviar, sizeof(messageStruct), 0);
	while (isWorking){
		calculaHora();	
		msgrcv(messageQueueID, reply, sizeof(replyStruct), 3, 0);
		printf("%02d:%02d:%02d VOO SLOT[%d] => Tenho a ordem: %s\n", sharedMemPtr->structHoras->tm_hour, sharedMemPtr->structHoras->tm_min, sharedMemPtr->structHoras->tm_sec, reply->id, arrivals[reply->id].ordem);

		if (strcmp(arrivals[reply->id].ordem,"ATERRAR")==0) isWorking = 0;
	}
	usleep((valuesPtr->duracaoDescolagem) * (valuesPtr->unidadeTempo) * 1000);
	insertLogfile("DEPARTURE CONCLUDED =>",((departurePtr)flight)->nome);
	
	sharedMemPtr->totalDepartures--;
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

	for(int i=0;i<sizeArrivals;i++){
		pthread_join(arrivalThreads[i],NULL);
	}
	for(int i=0;i<sizeDepartures;i++){
		pthread_join(departureThreads[i],NULL);
	}

	pthread_join(fuelThread,NULL);


	freeArrivals(arrivalHead);
	freeDepartures(departureHead);
	msgctl(messageQueueID, IPC_RMID, 0);

	unlink(PIPE_NAME);
	remove(PIPE_NAME);

	endLog();

	shmdt(sharedMemPtr);
	shmctl(shmid,IPC_RMID,NULL);
	shmdt(departures);
	shmctl(shmidDepartures,IPC_RMID,NULL);
	shmdt(arrivals);
	shmctl(shmidArrivals,IPC_RMID,NULL);


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



struct timespec ValorAbsoluto(struct timespec now,int tempo){
	struct timespec timetoWait = {0};
	int tempo_sec, tempo_nsec;
	tempo_sec = tempo * valuesPtr->unidadeTempo /1000;
	tempo_nsec = (tempo * valuesPtr->unidadeTempo %1000) *1000000;

	timetoWait.tv_sec = now.tv_sec + tempo_sec + (tempo_nsec + now.tv_nsec) /1000000000;
	timetoWait.tv_nsec = (tempo_nsec + now.tv_nsec) %1000000000;

	return timetoWait;
}