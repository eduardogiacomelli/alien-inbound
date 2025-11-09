/**
 * game.h - Game State Structures and Definitions
 */

 #ifndef GAME_H
 #define GAME_H
 
 #include <pthread.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <stdatomic.h>
 #include <time.h>
 
 /* Upper bounds (storage only; rendering adapts to terminal size) */
 #define MAX_NAVES        80
 #define MAX_FOGUETES     150
 #define MAX_LANCADORES   15
 
 /* Directions */
 typedef enum {
     DIR_VERTICAL = 0,
     DIR_DIAGONAL_ESQ = 1,
     DIR_DIAGONAL_DIR = 2,
     DIR_HORIZONTAL_ESQ = 3,
     DIR_HORIZONTAL_DIR = 4
 } DirecaoDisparo;
 
 typedef struct {
     bool tem_foguete;
     DirecaoDisparo direcao;
 } Lancador;
 
 typedef struct {
     int id;
     int x, y;
     bool ativa;
     bool destruida;
     pthread_t thread_id;
 } Nave;
 
 typedef struct {
     int id;
     int x, y;
     int dx, dy;
     DirecaoDisparo direcao;
     int lancador_id;
     bool ativa;
     pthread_t thread_id;
 } Foguete;
 
 typedef struct {
     int id;               /* 0=Easy,1=Medium,2=Hard */
     const char* name;     /* "Easy"/"Medium"/"Hard" */
     int launchers;        /* number of launchers */
     int reload_ms;        /* per launcher reload time */
     int ships_total;      /* total ships to spawn in the level */
     int ship_speed_ms;    /* vertical step time for ships */
     int spawn_min_ms;     /* spawn interval minimum (ms) */
     int spawn_max_ms;     /* spawn interval maximum (ms); if equal to min => fixed */
 } DifficultyConfig;
 
 typedef struct {
     /* ========= Dynamic screen metrics (updated by renderer) ========= */
     int screen_width;           // guarded by mutex_estado
     int screen_height;          // guarded by mutex_estado
     int hud_height;             // guarded by mutex_estado (constant 3)
     int controls_height;        // guarded by mutex_estado (constant 2)
 
     /* ========= Game state (mutex_estado) ========= */
     int pontuacao;
     int naves_total;            /* target total for the level */
     int naves_destruidas;
     int naves_chegaram;
     int naves_spawned;          /* how many have been spawned so far */
     atomic_bool game_over;
     int dificuldade;
     DifficultyConfig cfg;
     time_t start_time;
     int elapsed_sec;
 
     /* Player performance stats (mutex_estado) */
     int shots_fired;
     int shots_hit;
     int current_streak;
     int best_streak;
 
     /* ========= Battery (mutex_estado) ========= */
     int bateria_x;
     DirecaoDisparo direcao_atual;
 
     /* ========= Launchers (mutex_lancadores) ========= */
     Lancador lancadores[MAX_LANCADORES];
     int num_lancadores;
     int tempo_recarga; // ms
 
     /* ========= Entities ========= */
     Nave naves[MAX_NAVES];          // (mutex_naves)
     int num_naves_ativas;
     Foguete foguetes[MAX_FOGUETES]; // (mutex_foguetes)
     int num_foguetes_ativos;
 
     /* ========= Sync primitives ========= */
     pthread_mutex_t mutex_naves;
     pthread_mutex_t mutex_foguetes;
     pthread_mutex_t mutex_estado;
     pthread_mutex_t mutex_lancadores;
     pthread_mutex_t mutex_render;   // single ncurses mutex
 
     pthread_cond_t cond_lancador_vazio;
     pthread_cond_t cond_game_over;
 
     /* ========= Thread handles ========= */
     pthread_t thread_input;
     pthread_t thread_artilheiro;
 } GameState;
 
 /* ========= API ========= */
 void game_init(GameState* game, int dificuldade);
 void game_cleanup(GameState* game);
 
 void criar_nave(GameState* game);
 bool tentar_disparar(GameState* game); /* returns true if a rocket was actually fired */
 void finalizar_threads(GameState* game);
 
 #endif /* GAME_H */
 