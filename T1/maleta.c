// Plantilla para maleta.c

#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "maleta.h"

#include <pthread.h> // necesario para lanzar threads


typedef struct {
  double *w;
  double *v;
  int *z;
  int n;
  double maxW;
  int k;
  double best;
} Args;  // estructura para almacenar argumentos y respuestas

void *thread_llenarMaletaSec(void *ptr) {  // funcion para thread
  Args *a = (Args*) ptr;
  a->best = llenarMaletaSec(a->w,a->v,a->z,a->n,a->maxW,a->k);
  return NULL;
}

double llenarMaletaPar(double w[], double v[], int z[], int n,
                       double maxW, int k) {
  int P = 8;
  pthread_t pid[P];
  Args args[P];
  for(int i=0; i<P; i++) {
    // Inicialización de parametros
    args[i].w = w;
    args[i].v = v;
    args[i].z = (int*) malloc(n*sizeof(int)); // arreglos independientes
    args[i].n = n;
    args[i].maxW = maxW;
    args[i].k = k/P;
    pthread_create(&pid[i], NULL, thread_llenarMaletaSec, &args[i]);
  }
  double best = -1;
  int *z_candidate = NULL;
  for(int i=0; i<P; i++) {
    pthread_join(pid[i], NULL);
    if (args[i].best>best) { // actualizamos optimo
      best = args[i].best;
      z_candidate = args[i].z;
    }
  }
  for(int i=0; i<n; i++)
    z[i] = z_candidate[i]; // copiamos el mejor resultado encontrado por los threads
  for(int i=0; i<P; i++)
    free(args[i].z);  // liberamos los arreglos de cada thread
  return best;
}