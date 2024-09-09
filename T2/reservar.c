#define _XOPEN_SOURCE 500

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "reservar.h"

// Defina aca las variables globales y funciones auxiliares que necesite
pthread_mutex_t m;
pthread_cond_t c;
int ocupado[10];
int ticket_dist, display;

int espacio_disponible(int k, int *pos_libre) {  // Determina si es que hay k espacios contiguos disponibles
  int libres_contiguos = 0;
  int j = 0, disponible = 0;
  for (int i = 0; i<10; i++) {                    
    if (ocupado[i]) {
      libres_contiguos = 0;
      j = i+1;
    } else {
      libres_contiguos++;
    }
    if (libres_contiguos==k) {
      disponible = 1;
      break;
    }
  }
  if (disponible) {
    *pos_libre = j; // Almacenamos la primera posicion libre
    return 1;
  }
  return 0;
}

void initReservar() {
  for (int i=0;i<10;i++)
    ocupado[i] = 0;
  ticket_dist = 0;
  display = 0;
  pthread_mutex_init(&m, NULL);
  pthread_cond_init(&c, NULL);
}

void cleanReservar() {
  pthread_mutex_destroy(&m);
  pthread_cond_destroy(&c);
}

int reservar(int k) {
  pthread_mutex_lock(&m);
  int my_num = ticket_dist++;
  int pos_libre = -1; // Almacena la posicion en la que podria estacionarse
  while (my_num!=display || !espacio_disponible(k, &pos_libre))
    pthread_cond_wait(&c, &m);
  int ult_pos_libre = pos_libre + k;
  for (int i = pos_libre; i<ult_pos_libre; i++)
    ocupado[i] = 1;
  display++; // Es el turno del siguiente vehiculo si el actual logro estacionarse
  pthread_cond_broadcast(&c);
  pthread_mutex_unlock(&m);
  return pos_libre;
}

void liberar(int e, int k) {
  pthread_mutex_lock(&m);
  int ult_pos_libre = e+k;
  for (int i = e; i<ult_pos_libre; i++)
    ocupado[i] = 0;
  pthread_cond_broadcast(&c);
  pthread_mutex_unlock(&m);
}