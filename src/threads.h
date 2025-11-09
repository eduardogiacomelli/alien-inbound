#ifndef THREADS_H
#define THREADS_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "game.h"

void* thread_principal(void* arg);
void* thread_input(void* arg);
void* thread_nave(void* arg);
void* thread_foguete(void* arg);
void* thread_artilheiro(void* arg);

typedef struct {
    void* entity;     /* Nave* or Foguete* */
    GameState* game;
} ThreadArgs;

#endif /* THREADS_H */
