/**
 * input.c - Input Processing
 */
 #include "input.h"
 #include "game.h"
 #include <ncurses.h>
 #include <stdatomic.h>
 #include <pthread.h>
 
 void process_input(GameState* game, int key) {
     pthread_mutex_lock(&game->mutex_estado);
 
     int sw = game->screen_width;
 
     switch (key) {
         case 'a': case 'A': case KEY_LEFT:
             if (game->bateria_x > 0) game->bateria_x--;
             break;
         case 'd': case 'D': case KEY_RIGHT:
             if (game->bateria_x < sw - 1) game->bateria_x++;
             break;
         case 'w': case 'W': case KEY_UP:
             game->direcao_atual = DIR_VERTICAL; break;
         case 'q': case 'Q':
             game->direcao_atual = DIR_DIAGONAL_ESQ; break;
         case 'e': case 'E':
             game->direcao_atual = DIR_DIAGONAL_DIR; break;
         case 'z': case 'Z':
             game->direcao_atual = DIR_HORIZONTAL_ESQ; break;
         case 'c': case 'C':
             game->direcao_atual = DIR_HORIZONTAL_DIR; break;
         case ' ':
             pthread_mutex_unlock(&game->mutex_estado);
             /* Fire outside this lock (it takes others); shot counted in tentar_disparar */
             (void)tentar_disparar(game);
             return;
         case 'x': case 'X': case 27:
             atomic_store(&game->game_over, true);
             break;
     }
 
     pthread_mutex_unlock(&game->mutex_estado);
 }
 