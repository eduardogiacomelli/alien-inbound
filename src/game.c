#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "game.h"
#include "threads.h"

/* Default metrics until renderer measures terminal */
#define DEF_W 120
#define DEF_H 32
#define HUD_H 3
#define CTRL_H 2

/* Difficulty table:
   Easy:   30 ships, spawn 2-3s, 4 launchers, 2500ms reload, ships slower
   Medium: 40 ships, spawn 2s,   7 launchers, 1500ms reload, medium speed
   Hard:   60 ships, spawn 1-2s, 12 launchers, 800ms reload, faster ships
*/
static const DifficultyConfig DIFFS[3] = {
    {0, "Easy",   4, 2500, 30, 800, 2000, 3000},
    {1, "Medium", 7, 1500, 40, 600, 2000, 2000},
    {2, "Hard",  12,  800, 60, 450, 1000, 2000},
};

void game_init(GameState* game, int dificuldade) {
    memset(game, 0, sizeof(GameState));

    /* Initial dynamic metrics (renderer will overwrite on first frame) */
    game->screen_width  = DEF_W;
    game->screen_height = DEF_H;
    game->hud_height = HUD_H;
    game->controls_height = CTRL_H;

    game->dificuldade = (dificuldade < 0 || dificuldade > 2) ? 1 : dificuldade;
    game->cfg = DIFFS[game->dificuldade];

    game->num_lancadores = game->cfg.launchers;
    game->tempo_recarga  = game->cfg.reload_ms;
    game->naves_total    = game->cfg.ships_total;

    for (int i = 0; i < game->num_lancadores; i++) {
        game->lancadores[i].tem_foguete = false;
        game->lancadores[i].direcao = DIR_VERTICAL;
    }

    pthread_mutex_init(&game->mutex_naves, NULL);
    pthread_mutex_init(&game->mutex_foguetes, NULL);
    pthread_mutex_init(&game->mutex_estado, NULL);
    pthread_mutex_init(&game->mutex_lancadores, NULL);
    pthread_mutex_init(&game->mutex_render, NULL);

    pthread_cond_init(&game->cond_lancador_vazio, NULL);
    pthread_cond_init(&game->cond_game_over, NULL);

    game->pontuacao = 0;
    game->naves_destruidas = 0;
    game->naves_chegaram = 0;
    game->naves_spawned = 0;
    game->num_naves_ativas = 0;
    game->num_foguetes_ativos = 0;

    game->shots_fired = 0;
    game->shots_hit = 0;
    game->current_streak = 0;
    game->best_streak = 0;
    game->start_time = time(NULL);
    game->elapsed_sec = 0;

    atomic_init(&game->game_over, false);

    game->bateria_x = game->screen_width / 2;
    game->direcao_atual = DIR_VERTICAL;
}

void game_cleanup(GameState* game) {
    pthread_mutex_destroy(&game->mutex_naves);
    pthread_mutex_destroy(&game->mutex_foguetes);
    pthread_mutex_destroy(&game->mutex_estado);
    pthread_mutex_destroy(&game->mutex_lancadores);
    pthread_mutex_destroy(&game->mutex_render);

    pthread_cond_destroy(&game->cond_lancador_vazio);
    pthread_cond_destroy(&game->cond_game_over);
}

void criar_nave(GameState* game) {
    pthread_mutex_lock(&game->mutex_estado);
    if (game->naves_spawned >= game->naves_total) {
        pthread_mutex_unlock(&game->mutex_estado);
        return;
    }
    pthread_mutex_unlock(&game->mutex_estado);

    pthread_mutex_lock(&game->mutex_naves);
    if (game->num_naves_ativas >= MAX_NAVES) {
        pthread_mutex_unlock(&game->mutex_naves);
        return;
    }
    int idx = -1;
    for (int i = 0; i < MAX_NAVES; i++) {
        if (!game->naves[i].ativa) { idx = i; break; }
    }
    if (idx == -1) { pthread_mutex_unlock(&game->mutex_naves); return; }

    int w, hud;
    pthread_mutex_lock(&game->mutex_estado);
    w = game->screen_width; hud = game->hud_height;
    pthread_mutex_unlock(&game->mutex_estado);

    game->naves[idx].id = idx;
    game->naves[idx].x  = (w > 0) ? rand() % w : 0;
    game->naves[idx].y  = hud;     /* spawn right below HUD */
    game->naves[idx].ativa = true;
    game->naves[idx].destruida = false;
    game->num_naves_ativas++;
    pthread_mutex_unlock(&game->mutex_naves);

    pthread_mutex_lock(&game->mutex_estado);
    game->naves_spawned++;
    pthread_mutex_unlock(&game->mutex_estado);

    ThreadArgs* args = (ThreadArgs*)malloc(sizeof(ThreadArgs));
    if (!args) return;
    args->entity = &game->naves[idx];
    args->game = game;
    if (pthread_create(&game->naves[idx].thread_id, NULL, thread_nave, args) != 0) {
        free(args);
        pthread_mutex_lock(&game->mutex_naves);
        game->naves[idx].ativa = false;
        game->num_naves_ativas--;
        pthread_mutex_unlock(&game->mutex_naves);
    }
}

bool tentar_disparar(GameState* game) {
    bool fired = false;

    pthread_mutex_lock(&game->mutex_lancadores);
    int lancador_idx = -1;
    for (int i = 0; i < game->num_lancadores; i++) {
        if (game->lancadores[i].tem_foguete) { lancador_idx = i; break; }
    }
    if (lancador_idx == -1) {
        pthread_mutex_unlock(&game->mutex_lancadores);
        return false;
    }

    pthread_mutex_lock(&game->mutex_foguetes);
    int foguete_idx = -1;
    for (int i = 0; i < MAX_FOGUETES; i++) {
        if (!game->foguetes[i].ativa) { foguete_idx = i; break; }
    }
    if (foguete_idx == -1) {
        pthread_mutex_unlock(&game->mutex_foguetes);
        pthread_mutex_unlock(&game->mutex_lancadores);
        return false;
    }

    int bx, sw, sh, ch;
    DirecaoDisparo dir;
    pthread_mutex_lock(&game->mutex_estado);
    bx  = game->bateria_x;
    dir = game->direcao_atual;
    sw  = game->screen_width;
    sh  = game->screen_height;
    ch  = game->controls_height;
    pthread_mutex_unlock(&game->mutex_estado);

    game->foguetes[foguete_idx].id = foguete_idx;
    game->foguetes[foguete_idx].x  = (bx < 0) ? 0 : ((bx >= sw) ? sw-1 : bx);
    game->foguetes[foguete_idx].y  = (sh - ch - 1); /* ground line */
    game->foguetes[foguete_idx].direcao = dir;
    game->foguetes[foguete_idx].lancador_id = lancador_idx;
    game->foguetes[foguete_idx].ativa = true;
    game->num_foguetes_ativos++;

    game->lancadores[lancador_idx].tem_foguete = false;
    pthread_cond_signal(&game->cond_lancador_vazio);

    /* Count as a shot only if we actually launched */
    pthread_mutex_lock(&game->mutex_estado);
    game->shots_fired++;
    pthread_mutex_unlock(&game->mutex_estado);

    fired = true;

    pthread_mutex_unlock(&game->mutex_foguetes);
    pthread_mutex_unlock(&game->mutex_lancadores);

    ThreadArgs* args = (ThreadArgs*)malloc(sizeof(ThreadArgs));
    if (!args) return true;
    args->entity = &game->foguetes[foguete_idx];
    args->game = game;
    if (pthread_create(&game->foguetes[foguete_idx].thread_id, NULL, thread_foguete, args) != 0) {
        free(args);
        pthread_mutex_lock(&game->mutex_foguetes);
        game->foguetes[foguete_idx].ativa = false;
        game->num_foguetes_ativos--;
        pthread_mutex_unlock(&game->mutex_foguetes);
        fired = false;
    }
    return fired;
}

void finalizar_threads(GameState* game) {
    pthread_mutex_lock(&game->mutex_estado);
    atomic_store(&game->game_over, true);
    pthread_cond_broadcast(&game->cond_game_over);
    pthread_mutex_unlock(&game->mutex_estado);

    /* Wake reloader if waiting */
    pthread_mutex_lock(&game->mutex_lancadores);
    pthread_cond_broadcast(&game->cond_lancador_vazio);
    pthread_mutex_unlock(&game->mutex_lancadores);

    pthread_t nave_tids[MAX_NAVES];
    int nave_count = 0;
    pthread_mutex_lock(&game->mutex_naves);
    for (int i = 0; i < MAX_NAVES; i++) {
        if (game->naves[i].ativa) {
            nave_tids[nave_count++] = game->naves[i].thread_id;
        }
    }
    pthread_mutex_unlock(&game->mutex_naves);

    pthread_t foguete_tids[MAX_FOGUETES];
    int foguete_count = 0;
    pthread_mutex_lock(&game->mutex_foguetes);
    for (int i = 0; i < MAX_FOGUETES; i++) {
        if (game->foguetes[i].ativa) {
            foguete_tids[foguete_count++] = game->foguetes[i].thread_id;
        }
    }
    pthread_mutex_unlock(&game->mutex_foguetes);

    for (int i = 0; i < nave_count; i++) pthread_join(nave_tids[i], NULL);
    for (int i = 0; i < foguete_count; i++) pthread_join(foguete_tids[i], NULL);

    pthread_join(game->thread_input, NULL);
    pthread_join(game->thread_artilheiro, NULL);
}
