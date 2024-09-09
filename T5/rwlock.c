#define _XOPEN_SOURCE 500

#include "nthread-impl.h"

#include "rwlock.h"

struct rwlock {
  NthQueue *readerQueue;
  NthQueue *writerQueue;
  int reading;
  int writing;
};

nRWLock *nMakeRWLock() {
  nRWLock *controller = (nRWLock*) malloc(sizeof(nRWLock));
  controller->readerQueue = nth_makeQueue();
  controller->writerQueue = nth_makeQueue();
  controller->reading = 0;
  controller->writing = 0;
  return controller;
}

void nDestroyRWLock(nRWLock *rwl) {
  nth_destroyQueue(rwl->readerQueue);
  nth_destroyQueue(rwl->writerQueue);
  free(rwl);
}

void f(nThread th) {
  nth_delQueue((NthQueue*) th->ptr, th);
  th->ptr = NULL;
}

int nEnterRead(nRWLock *rwl, int timeout) {
  START_CRITICAL

  nThread thisTh = nSelf();
  // Si hay escritores trabajando o esperando ...
  if (rwl->writing || !nth_emptyQueue(rwl->writerQueue)) {
    nth_putBack(rwl->readerQueue, thisTh);
    suspend(WAIT_RWLOCK);
    schedule();
  }
  else
    rwl->reading++;

  END_CRITICAL
  return 1;
}

int nEnterWrite(nRWLock *rwl, int timeout) {
  START_CRITICAL


  // Asumimos que se logra obtener el recurso, lo actualizamos en caso de que no
  int success = 1;
  // Si hay escritores o lectores trabajando ...
  if (rwl->reading || rwl->writing) {
    nThread thisTh = nSelf();
    nth_putBack(rwl->writerQueue, thisTh);
    if (timeout>0) {
      thisTh->ptr = rwl->writerQueue;
      suspend(WAIT_RWLOCK_TIMEOUT);
      nth_programTimer(timeout*1000000LL, &f);
    }
    else
      suspend(WAIT_RWLOCK);

    schedule();
    
    if (timeout>0 && thisTh->ptr == NULL)
      success = 0;  // No se obtuvo el recurso
  }
  else
    rwl->writing = 1;


  END_CRITICAL
  return success;
}

void nExitRead(nRWLock *rwl) {
  START_CRITICAL

  rwl->reading--;     // Marcamos que este lector ya no esta leyendo

  // Si no quedan otros lectores trabajando, pero hay solicitudes de escritores pendientes ...
  if (!rwl->reading && !nth_emptyQueue(rwl->writerQueue)) {
    nThread w = nth_getFront(rwl->writerQueue);    // El escritor que lleva mas tiempo esperando es el primero en la cola
    if (w->status == WAIT_RWLOCK_TIMEOUT)          // cancelamos el timeout si este estaba configurado
      nth_cancelThread(w);
    rwl->writing = 1;       // Marcamos que ahora se esta escribiendo
    setReady(w);            // Preparamos al escritor
    schedule();
  }

  END_CRITICAL
}

void nExitWrite(nRWLock *rwl) {
  START_CRITICAL

  rwl->writing = 0;   // Marcamos que se dejo de escribir

  // Si quedan solicitudes de lectores pendientes ...
  if (!nth_emptyQueue(rwl->readerQueue)) {
    while (!nth_emptyQueue(rwl->readerQueue)) {     // Aceptamos la solicitud de todos los lectores pendientes
      nThread w = nth_getFront(rwl->readerQueue);
      rwl->reading++;       // Marcamos incorporacion escritor
      setReady(w);
    }
    schedule();
  }
  // Por el contrario, si quedan solicitudes de escritores pendientes ...
  else if (!nth_emptyQueue(rwl->writerQueue)) {
    nThread w = nth_getFront(rwl->writerQueue);    // El escritor que lleva mas tiempo esperando es el primero en la cola
    if (w->status == WAIT_RWLOCK_TIMEOUT)           // cancelamos el timeout si este estaba configurado
        nth_cancelThread(w);
    rwl->writing = 1;       // Marcamos que se esta escribiendo
    setReady(w);
    schedule();
  }

  END_CRITICAL
}