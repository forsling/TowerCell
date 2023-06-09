
#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include "../shared/vector.h"
#include "world.h"

typedef struct {
    Cell* cell;
    vec3 position;
} CellInfo;

bool start_level(GameState* gamestate, const char* level);
void update(GameState* game_state, World* world, InputState* input_state, int player_index, float delta_time);

#endif // GAME_LOGIC_H
