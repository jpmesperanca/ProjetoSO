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
#include "LinkedList.h" 

#define CINQ 50
#define CEM 100




//ARRIVALS
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

            aux = arrivalHead->nextNodePtr;
            freeArrivalNode(arrivalHead);
            arrivalHead = aux;
        }
}

void freeArrivalNode(arrivalPtr arrivalHead){

    free(arrivalHead->nome);
    free(arrivalHead);
}

void printArrivals(arrivalPtr arrivalHead){

	arrivalPtr aux = arrivalHead->nextNodePtr;

        while(aux != NULL){

        	printf("NEW COMMAND=>ARRIVAL %s, init: %d, eta: %d, fuel: %d\n", aux->nome, aux->init, aux->eta, aux->fuel);
        	aux = aux->nextNodePtr;
        } 
    printf("------\n");
}

void removeArrival(arrivalPtr arrivalHead){

    arrivalPtr aux = arrivalHead->nextNodePtr->nextNodePtr;

    freeArrivalNode(arrivalHead->nextNodePtr);

    arrivalHead->nextNodePtr = aux;
}

//DEPARTURES

departurePtr criaDepartures(){

    departurePtr aux;
    aux = malloc(sizeof(departureStruct));

    if (aux!=NULL){

        aux->nome = malloc(10*sizeof(char));
        aux->init = -1;
        aux->takeoff = -1;
        aux->nextNodePtr=NULL;

    }

    return aux;
}

void insereDeparture(departurePtr departureHead, char* nome, int init, int takeoff){

    departurePtr novo = criaDepartures();
    departurePtr aux = departureHead;

    while((aux->nextNodePtr != NULL) && (aux->nextNodePtr->init < init))
        aux = aux->nextNodePtr;

    novo->nextNodePtr = aux->nextNodePtr;
    aux->nextNodePtr = novo;

    strcpy(novo->nome, nome);
    novo->init = init;
    novo->takeoff = takeoff;
}

void freeDepartures(departurePtr departureHead){

    departurePtr aux = departureHead->nextNodePtr;

        while(departureHead != NULL){

            free(departureHead->nome);
            aux = departureHead->nextNodePtr;
            free(departureHead);
            departureHead = aux;
        }
}

void printDepartures(departurePtr departureHead){

	departurePtr aux = departureHead->nextNodePtr;

        while(aux != NULL){

        	printf("NEW COMMAND=>DEPARTURE %s, init: %d, takeoff: %d\n", aux->nome, aux->init, aux->takeoff);
        	aux = aux->nextNodePtr;
        } 
    printf("------\n");
}

//queue

queuePtr criaQueue(){

    queuePtr aux;
    aux = malloc(sizeof(queueStruct));

    if (aux!=NULL){

        aux->tempoDesejado = -1;
        aux->nextNodePtr=NULL;
        aux->fuel = -1;
    }

    return aux;
}

void insereQueue(queuePtr queueHead, int tempoDesejado, int fuel){

    queuePtr novo = criaQueue();
    queuePtr aux = queueHead;

    while((aux->nextNodePtr != NULL) && (aux->nextNodePtr->tempoDesejado <= tempoDesejado))
        aux = aux->nextNodePtr;

    novo->nextNodePtr = aux->nextNodePtr;
    aux->nextNodePtr = novo;

    novo->tempoDesejado = tempoDesejado;
    if (fuel != -1)
        novo->fuel = fuel;
}

void freeQueue(queuePtr queueHead){

    queuePtr aux = queueHead->nextNodePtr;

        while(queueHead != NULL){

            aux = queueHead->nextNodePtr;
            free(queueHead);
            queueHead = aux;
        }
}

void printDepartureQueue(queuePtr queueHead){

    queuePtr aux = queueHead->nextNodePtr;

        while(aux != NULL){

            printf("Voo: tempoDesejado: %d, \n", aux->tempoDesejado);
            aux = aux->nextNodePtr;
        } 
    printf("------\n");
}

void printArrivalQueue(queuePtr queueHead){

    queuePtr aux = queueHead->nextNodePtr;

        while(aux != NULL){

            printf("Voo: fuel - %d, tempoDesejado: %d\n", aux->fuel, aux->tempoDesejado);
            aux = aux->nextNodePtr;
        } 
    printf("------\n");
}
