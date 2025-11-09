#ifndef RENDER_H
#define RENDER_H

#include "game.h"

void render_init(void);
void render_game(GameState* game);
void render_cleanup(void);

/* thread-safe, internal sync */
void render_add_explosion(int x, int y);

#endif /* RENDER_H */
