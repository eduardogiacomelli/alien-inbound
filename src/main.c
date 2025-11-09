/**
 * main.c - Entry point
 */
 #include "game.h"
 #include "threads.h"
 #include "render.h"
 #include <stdio.h>
 #include <stdlib.h>
 #include <time.h>
 #include <unistd.h>
 
 static void print_usage(const char* program_name) {
     printf("Usage: %s [difficulty]\n\n", program_name);
     printf("  0 - Easy   (30 ships, 2–3s spawn, 4 launchers, 2500ms reload)\n");
     printf("  1 - Medium (40 ships, 2s spawn,    7 launchers, 1500ms reload)\n");
     printf("  2 - Hard   (60 ships, 1–2s spawn, 12 launchers,  800ms reload)\n\n");
     printf("Rules:\n");
     printf("  • Game ends when all ships are handled (destroyed or reached ground),\n");
     printf("    OR immediately if more than half the total ships reach the ground.\n");
     printf("  • Victory requires destroying at least half of the total ships.\n\n");
     printf("Controls:\n");
     printf("  A/D  Move | W/Q/E/Z/C Direction | SPACE Fire | X/ESC Quit\n\n");
 }
 
 int main(int argc, char* argv[]) {
     int dificuldade = 1;
     if (argc > 1) {
         if (argv[1][0] == '-' && argv[1][1] == 'h') { print_usage(argv[0]); return 0; }
         dificuldade = atoi(argv[1]);
         if (dificuldade < 0 || dificuldade > 2) { fprintf(stderr,"Invalid difficulty.\n"); return 1; }
     }
 
     srand((unsigned)time(NULL));
 
     GameState game;
     game_init(&game, dificuldade);
 
     render_init();
 
     if (pthread_create(&game.thread_input, NULL, thread_input, &game) != 0) {
         fprintf(stderr, "Failed to create input thread\n");
         render_cleanup(); game_cleanup(&game); return 1;
     }
     if (pthread_create(&game.thread_artilheiro, NULL, thread_artilheiro, &game) != 0) {
         fprintf(stderr, "Failed to create loader thread\n");
         render_cleanup(); game_cleanup(&game); return 1;
     }
 
     thread_principal(&game);
     finalizar_threads(&game);
 
     render_cleanup();
     game_cleanup(&game);
 
     printf("\n========================================\n");
     printf("               GAME OVER\n");
     printf("========================================\n");
     printf("Final Score: %d\n", game.pontuacao);
     printf("Ships Destroyed: %d / %d\n", game.naves_destruidas, game.naves_total);
     printf("Ships Reached Ground: %d\n", game.naves_chegaram);
     int shots = game.shots_fired;
     int hits  = game.shots_hit;
     double acc = (shots > 0) ? (100.0 * hits / shots) : 0.0;
     printf("Shots: %d | Hits: %d | Accuracy: %.1f%%\n", shots, hits, acc);
     printf("Best Streak: %d\n", game.best_streak);
     printf("Time: %ds\n", game.elapsed_sec);
     if (game.naves_chegaram > game.naves_total / 2) {
         printf("*** DEFEAT! (too many reached ground) ***\n");
     } else if (game.naves_destruidas >= game.naves_total / 2) {
         printf("*** VICTORY! ***\n");
     } else {
         printf("*** DEFEAT! (destroyed less than half) ***\n");
     }
     printf("========================================\n\n");
 
     return 0;
 }
 