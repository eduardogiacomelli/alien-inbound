#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <ncurses.h>
#include "threads.h"
#include "render.h"
#include "input.h"
#include "game.h"

/* Utility: randomized spawn interval within [min,max] ms; fixed if equal */
static inline int next_spawn_ms(const DifficultyConfig* cfg) {
    if (cfg->spawn_min_ms >= cfg->spawn_max_ms) return cfg->spawn_min_ms;
    int span = cfg->spawn_max_ms - cfg->spawn_min_ms;
    return cfg->spawn_min_ms + (rand() % (span + 1));
}

void* thread_principal(void* arg) {
    GameState* game = (GameState*)arg;
    struct timespec last_spawn_ts;
    clock_gettime(CLOCK_MONOTONIC, &last_spawn_ts);
    int wait_ms = next_spawn_ms(&game->cfg);

    while (!atomic_load(&game->game_over)) {
        /* Update elapsed & check end conditions */
        pthread_mutex_lock(&game->mutex_estado);
        game->elapsed_sec = (int)difftime(time(NULL), game->start_time);

        int total = game->naves_total;
        int destroyed = game->naves_destruidas;
        int reached   = game->naves_chegaram;
        int spawned   = game->naves_spawned;

        bool lose_now = (reached > (total / 2)); /* immediate defeat if > half reached */

        /* Finish only when all ships handled OR defeat now */
        bool all_handled = (destroyed + reached) >= total;

        pthread_mutex_unlock(&game->mutex_estado);

        if (lose_now || all_handled) {
            pthread_mutex_lock(&game->mutex_estado);
            atomic_store(&game->game_over, true);
            pthread_cond_broadcast(&game->cond_game_over);
            pthread_mutex_unlock(&game->mutex_estado);
            break;
        }

        /* Spawn logic: until total ships are spawned */
        if (spawned < total) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);

            long diff_ms = (now.tv_sec - last_spawn_ts.tv_sec) * 1000L
                         + (now.tv_nsec - last_spawn_ts.tv_nsec) / 1000000L;

            if (diff_ms >= wait_ms) {
                criar_nave(game);
                clock_gettime(CLOCK_MONOTONIC, &last_spawn_ts);
                wait_ms = next_spawn_ms(&game->cfg);
            }
        }

        render_game(game);
        usleep(33000); /* ~30 FPS */
    }
    return NULL;
}

void* thread_input(void* arg) {
    GameState* game = (GameState*)arg;
    while (!atomic_load(&game->game_over)) {
        pthread_mutex_lock(&game->mutex_render);
        int ch = getch();
        pthread_mutex_unlock(&game->mutex_render);

        if (ch != ERR) process_input(game, ch);
        usleep(2000);
    }
    return NULL;
}

void* thread_nave(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    Nave* nave = (Nave*)args->entity;
    GameState* game = args->game;

    int velocidade_ms = game->cfg.ship_speed_ms;

    while (!atomic_load(&game->game_over)) {
        pthread_mutex_lock(&game->mutex_naves);
        if (!nave->ativa) { pthread_mutex_unlock(&game->mutex_naves); break; }
        nave->y++;
        int nx = nave->x, ny = nave->y;
        pthread_mutex_unlock(&game->mutex_naves);

        /* ground metrics */
        int sh, ch;
        pthread_mutex_lock(&game->mutex_estado);
        sh = game->screen_height; ch = game->controls_height;
        pthread_mutex_unlock(&game->mutex_estado);

        if (ny >= (sh - ch - 1)) {
            bool first = false;
            pthread_mutex_lock(&game->mutex_naves);
            if (nave->ativa) {         /* transition gate: ground reached once */
                nave->ativa = false;
                first = true;
            }
            pthread_mutex_unlock(&game->mutex_naves);

            if (first) {
                pthread_mutex_lock(&game->mutex_estado);
                game->naves_chegaram++;
                game->current_streak = 0; /* break combo */
                pthread_mutex_unlock(&game->mutex_estado);
            }
            break;
        }

        /* collision against rockets (forgiving box) */
        pthread_mutex_lock(&game->mutex_foguetes);
        bool colidiu = false;
        for (int i = 0; i < MAX_FOGUETES; i++) {
            if (game->foguetes[i].ativa) {
                int dx = abs(game->foguetes[i].x - nx);
                int dy = abs(game->foguetes[i].y - ny);
                if (dx <= 2 && dy <= 2) {
                    game->foguetes[i].ativa = false;
                    colidiu = true;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&game->mutex_foguetes);

        if (colidiu) {
            bool first = false;
            pthread_mutex_lock(&game->mutex_naves);
            if (nave->ativa) {           /* transition gate: only one thread counts kill */
                nave->ativa = false;
                nave->destruida = true;
                first = true;
            }
            pthread_mutex_unlock(&game->mutex_naves);

            if (first) {
                render_add_explosion(nx, ny);
                pthread_mutex_lock(&game->mutex_estado);
                game->naves_destruidas++;
                game->pontuacao += 10;
                game->shots_hit++;
                game->current_streak++;
                if (game->current_streak > game->best_streak) game->best_streak = game->current_streak;
                pthread_mutex_unlock(&game->mutex_estado);
            }
            break;
        }

        usleep(velocidade_ms * 1000);
    }

    pthread_mutex_lock(&game->mutex_naves);
    game->num_naves_ativas--;
    pthread_mutex_unlock(&game->mutex_naves);

    free(args);
    return NULL;
}

void* thread_foguete(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    Foguete* f = (Foguete*)args->entity;
    GameState* game = args->game;

    switch (f->direcao) {
        case DIR_VERTICAL:        f->dx = 0;  f->dy = -1; break;
        case DIR_DIAGONAL_ESQ:    f->dx = -1; f->dy = -1; break;
        case DIR_DIAGONAL_DIR:    f->dx = 1;  f->dy = -1; break;
        case DIR_HORIZONTAL_ESQ:  f->dx = -1; f->dy = 0;  break;
        case DIR_HORIZONTAL_DIR:  f->dx = 1;  f->dy = 0;  break;
    }

    while (!atomic_load(&game->game_over)) {
        pthread_mutex_lock(&game->mutex_foguetes);
        if (!f->ativa) { pthread_mutex_unlock(&game->mutex_foguetes); break; }
        f->x += f->dx;
        f->y += f->dy;
        pthread_mutex_unlock(&game->mutex_foguetes);

        int sw, sh, hud, ch;
        pthread_mutex_lock(&game->mutex_estado);
        sw = game->screen_width; sh = game->screen_height;
        hud = game->hud_height;  ch = game->controls_height;
        pthread_mutex_unlock(&game->mutex_estado);

        if (f->x < 0 || f->x >= sw || f->y < hud || f->y >= (sh - ch)) {
            pthread_mutex_lock(&game->mutex_foguetes);
            f->ativa = false;
            pthread_mutex_unlock(&game->mutex_foguetes);
            break;
        }

        /* rocket-side collision (same forgiving box) */
        int fx = f->x, fy = f->y;
        bool hit = false; int hit_ship = -1; bool first = false;

        pthread_mutex_lock(&game->mutex_naves);
        for (int i = 0; i < MAX_NAVES; i++) {
            if (game->naves[i].ativa) {
                int dx = abs(game->naves[i].x - fx);
                int dy = abs(game->naves[i].y - fy);
                if (dx <= 2 && dy <= 2) {
                    /* transition gate: only count if we flip ativa->false */
                    game->naves[i].ativa = false;
                    game->naves[i].destruida = true;
                    first = true;
                    hit_ship = i;
                    hit = true;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&game->mutex_naves);

        if (hit) {
            pthread_mutex_lock(&game->mutex_foguetes);
            f->ativa = false;
            pthread_mutex_unlock(&game->mutex_foguetes);

            if (hit_ship >= 0 && first) {
                int ex, ey;
                pthread_mutex_lock(&game->mutex_naves);
                ex = game->naves[hit_ship].x;
                ey = game->naves[hit_ship].y;
                pthread_mutex_unlock(&game->mutex_naves);
                render_add_explosion(ex, ey);
            }
            if (first) {
                pthread_mutex_lock(&game->mutex_estado);
                game->naves_destruidas++;
                game->pontuacao += 10;
                game->shots_hit++;
                game->current_streak++;
                if (game->current_streak > game->best_streak) game->best_streak = game->current_streak;
                pthread_mutex_unlock(&game->mutex_estado);
            }
            break;
        }

        usleep(35000);
    }

    pthread_mutex_lock(&game->mutex_foguetes);
    game->num_foguetes_ativos--;
    pthread_mutex_unlock(&game->mutex_foguetes);

    free(args);
    return NULL;
}

void* thread_artilheiro(void* arg) {
    GameState* game = (GameState*)arg;

    while (!atomic_load(&game->game_over)) {
        bool todos_cheios = true;

        pthread_mutex_lock(&game->mutex_lancadores);
        for (int i = 0; i < game->num_lancadores; i++) {
            if (!game->lancadores[i].tem_foguete) {
                todos_cheios = false;

                pthread_mutex_lock(&game->mutex_estado);
                DirecaoDisparo dir_atual = game->direcao_atual;
                pthread_mutex_unlock(&game->mutex_estado);

                pthread_mutex_unlock(&game->mutex_lancadores);
                usleep(game->tempo_recarga * 1000);
                pthread_mutex_lock(&game->mutex_lancadores);

                if (!atomic_load(&game->game_over) && !game->lancadores[i].tem_foguete) {
                    game->lancadores[i].tem_foguete = true;
                    game->lancadores[i].direcao = dir_atual;
                }
                break;
            }
        }

        if (todos_cheios) {
            while (!atomic_load(&game->game_over)) {
                pthread_cond_wait(&game->cond_lancador_vazio, &game->mutex_lancadores);
                bool any_empty = false;
                for (int i = 0; i < game->num_lancadores; i++)
                    if (!game->lancadores[i].tem_foguete) { any_empty = true; break; }
                if (any_empty) break;
            }
        }

        pthread_mutex_unlock(&game->mutex_lancadores);
    }
    return NULL;
}
