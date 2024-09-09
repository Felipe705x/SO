#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include "disk.h"
#include "pss.h"

/*****************************************************
 * Agregue aca los tipos, variables globales u otras
 * funciones que necesite
 *****************************************************/

typedef struct {
  int ready;
  pthread_cond_t w;
} Request;

pthread_mutex_t m;  // Mutex
PriQueue *pq[2]; // Dos colas de prioridad: Se van intercambiando cada vez que se hace una bajada en disco con busy=1
int current_track;  // El track en el cual se encuentra actualmente el cabezal
int busy; // Indica verdadero (1) ssi hay requests en espera o se esta ocupando un track
int pq_switch;  // Indica cual es la cola de prioridad (según indice de pq) para track>=current_track


void iniDisk(void) {
  pq[0] = makePriQueue();
  pq[1] = makePriQueue();
  pthread_mutex_init(&m, NULL);
  pq_switch = busy = current_track = 0;
}

void cleanDisk(void) {
  destroyPriQueue(pq[0]);
  destroyPriQueue(pq[1]);
  pthread_mutex_destroy(&m);
}

void requestDisk(int track) {
  pthread_mutex_lock(&m);
  if (!busy)        // Si nadie esta esperando o ocupando un track
    busy = 1;       // Cedo cabezal instanteamente y marco que se esta ocupando el disco
  else {
    // Si llegamos a este punto, el thread tendra que esperar
    Request req = {0, PTHREAD_COND_INITIALIZER};
    int placement = pq_switch;    // A que cola ira el request
    if (track<current_track)
      placement = !pq_switch;
    priPut(pq[placement], &req, track);
    while (!req.ready)
      pthread_cond_wait(&req.w, &m);
  }
  current_track = track;    // Actualizo el track del cabezal
  pthread_mutex_unlock(&m);
}

void releaseDisk() {
  pthread_mutex_lock(&m);
  Request *pr = NULL;
  if (!emptyPriQueue(pq[pq_switch])) {
    pr = priGet(pq[pq_switch]);
    pr->ready = 1;
    pthread_cond_signal(&pr->w);
  } 
  else {
    if (!emptyPriQueue(pq[!pq_switch])) {
      pq_switch = !pq_switch; // Cambiamos de cola y preparamos bajada
      pr = priGet(pq[pq_switch]);
      pr->ready = 1;
      pthread_cond_signal(&pr->w);
    } 
    else
      busy = 0;   // Si al liberar no queda nadie esperando, entonces el disco esta desocupado
  }
  pthread_mutex_unlock(&m);
  return;
}