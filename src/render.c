/**
 * render.c - Flicker-free rendering with ncurses PAD + doupdate()
 */
 #include "render.h"
 #include "game.h"
 #include <ncurses.h>
 #include <string.h>
 #include <stdlib.h>
 #include <pthread.h>
 #include <stdatomic.h>
 
 /* Colors */
 #define CP_SHIP       1
 #define CP_ROCKET     2
 #define CP_BATTERY    3
 #define CP_HUD        4
 #define CP_EXPLOSION  5
 #define CP_GROUND     6
 #define CP_DIRECTION  7
 #define CP_TRAIL      8
 
 /* Explosions */
 typedef struct { int x, y, frames_left; } Explosion;
 #define MAX_EXPLOSIONS 32
 static Explosion s_expl[MAX_EXPLOSIONS];
 static int s_expl_count = 0;
 static pthread_mutex_t s_expl_mtx = PTHREAD_MUTEX_INITIALIZER;
 
 /* Off-screen pad */
 static WINDOW* s_pad = NULL;
 static int s_pad_w = 0, s_pad_h = 0;
 
 static void expl_add(int x, int y) {
     pthread_mutex_lock(&s_expl_mtx);
     if (s_expl_count < MAX_EXPLOSIONS) {
         s_expl[s_expl_count].x = x;
         s_expl[s_expl_count].y = y;
         s_expl[s_expl_count].frames_left = 5;
         s_expl_count++;
     }
     pthread_mutex_unlock(&s_expl_mtx);
 }
 static void expl_update(void) {
     pthread_mutex_lock(&s_expl_mtx);
     for (int i = 0; i < s_expl_count; ++i) {
         s_expl[i].frames_left--;
         if (s_expl[i].frames_left <= 0) {
             for (int j = i; j < s_expl_count - 1; ++j) s_expl[j] = s_expl[j+1];
             s_expl_count--;
             i--;
         }
     }
     pthread_mutex_unlock(&s_expl_mtx);
 }
 
 void render_add_explosion(int x, int y) { expl_add(x, y); }
 
 void render_init(void) {
     initscr();
     set_escdelay(25);
     if (has_colors()) {
         start_color();
         init_pair(CP_SHIP,      COLOR_RED,    COLOR_BLACK);
         init_pair(CP_ROCKET,    COLOR_YELLOW, COLOR_BLACK);
         init_pair(CP_BATTERY,   COLOR_CYAN,   COLOR_BLACK);
         init_pair(CP_HUD,       COLOR_WHITE,  COLOR_BLACK);
         init_pair(CP_EXPLOSION, COLOR_MAGENTA,COLOR_BLACK);
         init_pair(CP_GROUND,    COLOR_GREEN,  COLOR_BLACK);
         init_pair(CP_DIRECTION, COLOR_BLUE,   COLOR_BLACK);
         init_pair(CP_TRAIL,     COLOR_YELLOW, COLOR_BLACK);
     }
     cbreak();
     noecho();
     keypad(stdscr, TRUE);
     curs_set(0);
     nodelay(stdscr, TRUE);
     leaveok(stdscr, TRUE);
 
     s_pad = NULL;
     s_pad_w = s_pad_h = 0;
 
     s_expl_count = 0;
 }
 
 static void ensure_pad(int h, int w) {
     if (!s_pad || h != s_pad_h || w != s_pad_w) {
         if (s_pad) { delwin(s_pad); s_pad = NULL; }
         s_pad = newpad(h, w);
         s_pad_h = h; s_pad_w = w;
     }
 }
 
 void render_game(GameState* game) {
     /* Snapshot world under entity locks BEFORE touching ncurses */
     /* Ships snapshot */
     int ship_count = 0;
     int ship_x[MAX_NAVES], ship_y[MAX_NAVES];
 
     pthread_mutex_lock(&game->mutex_naves);
     for (int i = 0; i < MAX_NAVES; i++) {
         if (game->naves[i].ativa) {
             ship_x[ship_count] = game->naves[i].x;
             ship_y[ship_count] = game->naves[i].y;
             ship_count++;
         }
     }
     pthread_mutex_unlock(&game->mutex_naves);
 
     /* Rockets snapshot */
     int rocket_count = 0;
     int rocket_x[MAX_FOGUETES], rocket_y[MAX_FOGUETES];
     DirecaoDisparo rocket_dir[MAX_FOGUETES];
 
     pthread_mutex_lock(&game->mutex_foguetes);
     for (int i = 0; i < MAX_FOGUETES; i++) {
         if (game->foguetes[i].ativa) {
             rocket_x[rocket_count] = game->foguetes[i].x;
             rocket_y[rocket_count] = game->foguetes[i].y;
             rocket_dir[rocket_count] = game->foguetes[i].direcao;
             rocket_count++;
         }
     }
     pthread_mutex_unlock(&game->mutex_foguetes);
 
     /* HUD/game metrics snapshot */
     int sw, sh, hud, ch, bx;
     DirecaoDisparo dir;
 
     pthread_mutex_lock(&game->mutex_estado);
     sw = game->screen_width;
     sh = game->screen_height;
     hud = game->hud_height;
     ch  = game->controls_height;
     bx  = game->bateria_x;
     dir = game->direcao_atual;
     pthread_mutex_unlock(&game->mutex_estado);
 
     /* ----- Serialize all ncurses access ----- */
     pthread_mutex_lock(&game->mutex_render);
 
     int real_h, real_w;
     getmaxyx(stdscr, real_h, real_w);
     if (real_h < 8) real_h = 8;
     if (real_w < 40) real_w = 40;
 
     ensure_pad(real_h, real_w);
     werase(s_pad);
 
     /* If terminal changed, publish to game state */
     if (real_w != sw || real_h != sh) {
         pthread_mutex_lock(&game->mutex_estado);
         game->screen_width  = real_w;
         game->screen_height = real_h;
         sw = real_w; sh = real_h; hud = game->hud_height; ch = game->controls_height;
         if (game->bateria_x >= sw) game->bateria_x = sw - 1;
         bx = game->bateria_x;
         pthread_mutex_unlock(&game->mutex_estado);
     }
 
     const int game_start_y = hud;
     const int game_end_y   = sh - ch;
     const int ground_y     = game_end_y - 1;
 
     /* Ground line */
     wattron(s_pad, COLOR_PAIR(CP_GROUND));
     for (int x = 0; x < sw; x++) mvwaddch(s_pad, ground_y, x, '_');
     wattroff(s_pad, COLOR_PAIR(CP_GROUND));
 
     /* Ships */
     wattron(s_pad, COLOR_PAIR(CP_SHIP));
     for (int i = 0; i < ship_count; i++) {
         int x = ship_x[i], y = ship_y[i];
         if (x >= 0 && x < sw && y >= game_start_y && y < game_end_y)
             mvwaddch(s_pad, y, x, 'V');
     }
     wattroff(s_pad, COLOR_PAIR(CP_SHIP));
 
     /* Rockets */
     wattron(s_pad, COLOR_PAIR(CP_ROCKET));
     for (int i = 0; i < rocket_count; i++) {
         int x = rocket_x[i], y = rocket_y[i];
         char sym = '|';
         switch (rocket_dir[i]) {
             case DIR_VERTICAL: sym = '|'; break;
             case DIR_DIAGONAL_ESQ: sym = '\\'; break;
             case DIR_DIAGONAL_DIR: sym = '/'; break;
             case DIR_HORIZONTAL_ESQ: sym = '<'; break;
             case DIR_HORIZONTAL_DIR: sym = '>'; break;
         }
         if (x >= 0 && x < sw && y >= game_start_y && y < game_end_y)
             mvwaddch(s_pad, y, x, sym);
     }
     wattroff(s_pad, COLOR_PAIR(CP_ROCKET));
 
     /* Battery */
     wattron(s_pad, COLOR_PAIR(CP_BATTERY));
     if (bx >= 0 && bx < sw) {
         if (bx > 0 && bx < sw - 1) {
             mvwaddch(s_pad, ground_y, bx - 1, '/');
             mvwaddch(s_pad, ground_y, bx,     '^');
             mvwaddch(s_pad, ground_y, bx + 1, '\\');
         } else {
             mvwaddch(s_pad, ground_y, bx, '^');
         }
     }
     wattroff(s_pad, COLOR_PAIR(CP_BATTERY));
 
     /* Direction indicator */
     int aim_dx = 0, aim_dy = -1; char aim_ch = '|';
     switch (dir) {
         case DIR_VERTICAL:        aim_dx=0;  aim_dy=-1; aim_ch='|'; break;
         case DIR_DIAGONAL_ESQ:    aim_dx=-1; aim_dy=-1; aim_ch='\\'; break;
         case DIR_DIAGONAL_DIR:    aim_dx=1;  aim_dy=-1; aim_ch='/'; break;
         case DIR_HORIZONTAL_ESQ:  aim_dx=-1; aim_dy=0;  aim_ch='<'; break;
         case DIR_HORIZONTAL_DIR:  aim_dx=1;  aim_dy=0;  aim_ch='>'; break;
     }
     wattron(s_pad, COLOR_PAIR(CP_DIRECTION));
     int px = bx, py = ground_y;
     for (int i = 0; i < 4; i++) {
         px += aim_dx; py += aim_dy;
         if (px >= 0 && px < sw && py >= game_start_y && py < game_end_y)
             mvwaddch(s_pad, py, px, aim_ch);
     }
     wattroff(s_pad, COLOR_PAIR(CP_DIRECTION));
 
     /* Explosions */
     wattron(s_pad, COLOR_PAIR(CP_TRAIL));
     pthread_mutex_lock(&s_expl_mtx);
     for (int i = 0; i < s_expl_count; i++) {
         int ex = s_expl[i].x, ey = s_expl[i].y;
         if (ex >= 0 && ex < sw && ey >= game_start_y && ey < game_end_y) {
             mvwaddch(s_pad, ey, ex, '*');
             if (ex > 0)               mvwaddch(s_pad, ey, ex - 1, '*');
             if (ex < sw - 1)          mvwaddch(s_pad, ey, ex + 1, '*');
             if (ey > game_start_y)    mvwaddch(s_pad, ey - 1, ex, '*');
             if (ey < game_end_y - 1)  mvwaddch(s_pad, ey + 1, ex, '*');
         }
     }
     pthread_mutex_unlock(&s_expl_mtx);
     wattroff(s_pad, COLOR_PAIR(CP_TRAIL));
 
     /* HUD snapshot */
     int score, destroyed, total, reached, elapsed, shots, hits, streak, spawned;
     const char* diff_name = game->cfg.name;
     pthread_mutex_lock(&game->mutex_estado);
     score = game->pontuacao;
     destroyed = game->naves_destruidas;
     total = game->naves_total;
     reached = game->naves_chegaram;
     spawned = game->naves_spawned;
     elapsed = game->elapsed_sec;
     shots = game->shots_fired;
     hits  = game->shots_hit;
     streak = game->current_streak;
     pthread_mutex_unlock(&game->mutex_estado);
 
     /* HUD */
     wattron(s_pad, COLOR_PAIR(CP_HUD));
     int handled = destroyed + reached;
     int remaining = total - handled;
     if (remaining < 0) remaining = 0;
     mvwprintw(s_pad, 0, 0, "Score:%-6d  Diff:%-6s  Time:%3ds  Ships Rem:%-3d (spawned:%d/%d)",
               score, diff_name, elapsed, remaining, spawned, total);
 
     /* Rockets bar */
     int loaded = 0, launchers = 0;
     pthread_mutex_lock(&game->mutex_lancadores);
     launchers = game->num_lancadores;
     for (int i = 0; i < launchers; i++)
         if (game->lancadores[i].tem_foguete) loaded++;
     pthread_mutex_unlock(&game->mutex_lancadores);
 
     int bar_x = 58;
     int bar_w = (sw > 60) ? (sw / 5) : 12;
     if (bar_x + 10 < sw) {
         if (bar_x + 2 + bar_w + 8 < sw) {
             mvwprintw(s_pad, 0, bar_x, "Rockets:[");
             int filled = (launchers > 0) ? (loaded * bar_w) / launchers : 0;
             for (int i = 0; i < bar_w; i++) waddch(s_pad, (i < filled) ? '=' : ' ');
             wprintw(s_pad, "] %d/%d", loaded, launchers);
         }
     }
 
     /* Line 2: Accuracy + Streak + Summary */
     double acc = (shots > 0) ? (100.0 * hits / shots) : 0.0;
     int acc_bar_x = 0;
     int acc_bar_w = (sw > 40) ? (sw / 4) : 18;
     mvwprintw(s_pad, 1, acc_bar_x, "Acc:[");
     int acc_filled = (int)((acc / 100.0) * acc_bar_w);
     if (acc_filled < 0) acc_filled = 0;
     if (acc_filled > acc_bar_w) acc_filled = acc_bar_w;
     for (int i = 0; i < acc_bar_w; i++) waddch(s_pad, (i < acc_filled) ? '=' : ' ');
     wprintw(s_pad, "] %3.0f%%", acc);
 
     int info_x = acc_bar_x + 10 + acc_bar_w;
     if (info_x + 30 < sw) {
         mvwprintw(s_pad, 1, info_x, "Hits:%d Shots:%d  Streak:%d  Kills:%d Ground:%d",
                   hits, shots, streak, destroyed, reached);
     } else {
         mvwprintw(s_pad, 1, info_x, "H:%d S:%d Stk:%d", hits, shots, streak);
     }
 
     mvwprintw(s_pad, sh - 1, 0,
               "A/D=Move | W/Q/E/Z/C=Dir | SPACE=Fire | X=Quit");
     wattroff(s_pad, COLOR_PAIR(CP_HUD));
 
     /* Explosions decay */
     expl_update();
 
     /* Present frame without flicker */
     pnoutrefresh(s_pad, 0, 0, 0, 0, sh - 1, sw - 1);
     doupdate();
 
     pthread_mutex_unlock(&game->mutex_render);
 }
 
 void render_cleanup(void) {
     if (s_pad) { delwin(s_pad); s_pad = NULL; }
     endwin();
 }
 