#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "pss.h"
#include "bolsa.h"
#include "spinlocks.h"

// Problema análogo a P1, P2 del C2

typedef enum {EN_ESPERA, RECHAZADO, ADJUDICADO} Status;

int sl = OPEN;              // spin-lock global para exclusión mutua
int lowest_price;           // variable global para guardar el precio más economico ofertado 
int *seller_psl = NULL;     // spin-lock en donde el vendedor activo espera
char **seller_name = NULL;  // dirección del string con el nombre del vendedor
char **buyer_name = NULL;   // dirección en dode el comprador debe copiar su nombre
Status *status = NULL;      // variable global para mantener el estado de la oferta del vendedor activo


int vendo(int precio, char *vendedor, char *comprador) {
  spinLock(&sl);

  Status s = EN_ESPERA;  // variable local "VL"
  // Si ya hay otro vendedor activo ...
  if (status != NULL && *status == EN_ESPERA) {
    // Si mi precio ofertado no es menor al precio actual ...
    if (precio >= lowest_price) {
      s = RECHAZADO;     // ... entonces mi oferta es rechazada (esta línea no es necesaria, pero ilustra la lógica)
      spinUnlock(&sl);
      return 0;
    }
    // ... de lo contrario, mi oferta es menor que la del vendedor actual, por lo que la oferta de dicho vendedor se rechaza
    *status = RECHAZADO;
    spinUnlock(seller_psl);    // despertamos al vendedor actual para avisarle que su oferta fue rechazada
  }
  // Actualizo variables globales para indicar que ahora soy el vendedor activo
  seller_name = &vendedor;
  buyer_name = &comprador;
  lowest_price = precio;
  int m = CLOSED;   // spin-lock para esperar
  seller_psl = &m;
  status = &s;
  
  spinUnlock(&sl);
  spinLock(&m);  // wait
  return s == ADJUDICADO;
}

int compro(char *comprador, char *vendedor) {
  spinLock(&sl);
  
  // Si no hay ningún vendedor activo ...
  if (status == NULL || *status != EN_ESPERA) {
    spinUnlock(&sl);
    return 0;   // ...retorno de inmediato
  }
  // ... la compra fue exitosa
  int price_paid = lowest_price;    // precio pagado por el comprador
  strcpy(vendedor, *seller_name);
  strcpy(*buyer_name, comprador);
  *status = ADJUDICADO;             // marco estado como adjudicado
  spinUnlock(seller_psl);           // despierto al vendedor para avisar que su oferta fue aceptada

  // Reestablecemos variables globales, ya que una compra deja momentaneamente sin vendedores
  seller_psl = NULL;
  seller_name = NULL;
  buyer_name = NULL;
  status = NULL;
  
  spinUnlock(&sl);
  return price_paid;
}
