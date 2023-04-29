#include <stdio.h>

#include "game_logic.h"
#include "vector.h"
#include "world.h"
#include "utils.h"
#include "settings.h"
#include "math.h"

const float MOUSE_SENSITIVITY = 0.001f;

static void calculate_projectile_direction(Player* player, vec3* direction) {
    float forward_x = cosf(player->yaw) * cosf(player->pitch);
    float forward_y = sinf(player->yaw) * cosf(player->pitch);
    float forward_z = -sinf(player->pitch);

    direction->x = forward_x;
    direction->y = forward_y;
    direction->z = forward_z;
}

static void create_projectile(Projectile* projectiles, Player* player) {
    // Create a new projectile
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].ttl < 1) {
            Projectile* proj = &projectiles[i];
            proj->position = player->position;
            proj->speed = 20.0f;
            proj->size = 1.0f;
            proj->ttl = 1000;
            proj->active = true;
            calculate_projectile_direction(player, &proj->direction);
            break;
        }
    }
}

static void update_projectile(World* world, Projectile* projectile, float deltaTime) {
    if (projectile->ttl < 1) {
        return;
    }
    projectile->ttl--;
    if (!projectile->active) {
        return;
    }

    vec3 old_pos = {
        .x = projectile->position.x, 
        .y = projectile->position.y, 
        .z = projectile->position.z
    };
    vec3 new_pos = {
        .x = projectile->position.x + projectile->direction.x * projectile->speed * deltaTime,
        .y = projectile->position.y + projectile->direction.y * projectile->speed * deltaTime,
        .z = projectile->position.z + projectile->direction.z * projectile->speed * deltaTime
    };

    int num_cells;
    CellInfo3D* cell_infos = get_cells_for_vector_3d(world, old_pos, new_pos, &num_cells);
    for (int i = 0; i < num_cells; i++) {
        CellInfo3D cell_info = cell_infos[i];
        Cell* cell = cell_info.cell;
        vec3 cell_position = cell_info.position;

        if (cell != NULL && cell->type == CELL_SOLID) {
            projectile->active = false;
            projectile->ttl = 100;
        }
    }
    projectile->position = new_pos;
}

static void update_player_position(Player* player, World* world, float dx, float dy, float deltaTime) {
    // Handle free mode unrestricted movement
    if (player->free_mode) {
        player->position.x += dx * player->speed * deltaTime;
        player->position.y += dy * player->speed * deltaTime;
        player->velocity_z = 0;
        debuglog(4, "x %f, y %f, z %f \n", player->position.x, player->position.y, player->position.z);
        return;
    }

    // Apply gravity
    float gravity = get_setting_float("gravity");
    player->velocity_z += gravity * deltaTime;

    // New player position (to be evaluated)
    float target_x = player->position.x + dx * player->speed * deltaTime;
    float target_y = player->position.y + dy * player->speed * deltaTime;
    float target_z = player->position.z + (player->velocity_z * deltaTime);
    ivec3 target_grid_pos = get_grid_pos3(target_x, target_y, target_z);

    int z_layer = (int)floor(player->position.z / CELL_Z_SCALE);
    Layer* layer = &world->layers[z_layer];

    // Calculate the destination position
    vec3 source = {player->position.x, player->position.y, player->position.z};
    vec3 destination = {target_x, target_y, target_z};

    // Update the player's position based on the furthest legal position
    if (z_layer >= 0) {
        vec3 furthest_legal_position = get_furthest_legal_position_3d(world, source, destination, player->size);
        target_x = furthest_legal_position.x;
        target_y = furthest_legal_position.y;
        target_z = furthest_legal_position.z;
    }

    // z-axis handling
    float next_z_obstacle;
    bool has_obstacle_down = get_next_z_obstacle(world, target_grid_pos.x, target_grid_pos.y, target_z, &next_z_obstacle);
    if (has_obstacle_down) {
        float highest_valid_z = next_z_obstacle - player->height;
        if (target_z > highest_valid_z) {
            // Player z movement is obstructed
            if (player->velocity_z >= 0) {
                target_z = highest_valid_z;
                player->velocity_z = 0.0f;
            } else {
                target_z = next_z_obstacle + 0.01f;
                player->velocity_z = 0.01f;
            }
        }
    }

    // Update player position if the target cell is not solid
    ivec3 newpos = get_grid_pos3(target_x, target_y, target_z);
    Cell* cell_candidate;
    bool got_cell = get_world_cell(world, newpos, &cell_candidate);
    if (!got_cell || cell_candidate->type != CELL_SOLID) {
        if (!(player->position.x == target_x && player->position.y == target_y && player->position.z == target_z)) {
            debuglog(1, "Player: %d,%d (%f, %f, %d) -> %d,%d (%f, %f, %d) \n", (int)(player->position.x / CELL_XY_SCALE), (int)(player->position.y / CELL_XY_SCALE), player->position.x, player->position.y, z_layer, target_grid_pos.x, target_grid_pos.y, target_x, target_y, (int)floor(target_z / CELL_Z_SCALE));
        }
        player->position.x = target_x;
        player->position.y = target_y;
        player->position.z = target_z;
    } else {
        debuglog(1, "Player: rejected: %d,%d (%f, %f, %d) -> %d,%d (%f, %f, %d) \n", (int)(player->position.x / CELL_XY_SCALE), (int)(player->position.y / CELL_XY_SCALE), player->position.x, player->position.y, z_layer, target_grid_pos.x, target_grid_pos.y, target_x, target_y, (int)floor(target_z / CELL_Z_SCALE));
        ivec3 old_grid_pos = get_grid_pos3(player->position.x, player->position.y, player->position.z);
        Cell* cell_candidate;
        bool got_cell = get_world_cell(world, old_grid_pos, &cell_candidate);
        if (got_cell && cell_candidate->type == CELL_SOLID) {
            player->position.z -= CELL_Z_SCALE;
        }
    }
}

static vec2 process_input(GameState* game_state, InputState* input_state) {
    vec2 movement = {0.0f, 0.0f};

    if (input_state->f.is_down && !input_state->f.was_down) {
        // Toggle free mode
        game_state->player.free_mode = !game_state->player.free_mode;
    }

    if (input_state->up.is_down) {
        movement.x += cosf(game_state->player.yaw);
        movement.y += sinf(game_state->player.yaw);
    }
    if (input_state->down.is_down) {
        movement.x -= cosf(game_state->player.yaw);
        movement.y -= sinf(game_state->player.yaw);
    }
    if (input_state->right.is_down) {
        movement.x -= sinf(game_state->player.yaw);
        movement.y += cosf(game_state->player.yaw);
    }
    if (input_state->left.is_down) {
        movement.x += sinf(game_state->player.yaw);
        movement.y -= cosf(game_state->player.yaw);
    }

    // Handle jumping and free mode
    if (input_state->space.is_down) {
        if (game_state->player.free_mode) {
            game_state->player.position.z -= game_state->player.speed * game_state->delta_time;
        } else if (game_state->player.velocity_z == 0.0f) { // Jump only when the player is on the ground
            game_state->player.velocity_z = game_state->player.jump_velocity;
            game_state->player.jumped = true;
        }
    }
    if (game_state->player.free_mode && input_state->shift.is_down) {
        game_state->player.position.z += game_state->player.speed * game_state->delta_time;
    }

    return movement;
}

static void process_mouse(GameState* game_state, InputState* input_state) {
    // Update player's yaw and pitch based on mouse input
    game_state->player.yaw += input_state->mouse_state.dx * MOUSE_SENSITIVITY;
    game_state->player.pitch -= input_state->mouse_state.dy * MOUSE_SENSITIVITY;

    if (game_state->player.pitch < -M_PI / 2) {
        game_state->player.pitch = -M_PI / 2;
    }
    if (game_state->player.pitch > M_PI / 2) {
        game_state->player.pitch = M_PI / 2;
    }
}

bool start_level(GameState* gamestate, const char* level) {
    // Initialize player object
    Player player = {0};
    player.position.x = get_setting_float("player_pos_x");
    player.position.y = get_setting_float("player_pos_y");
    player.position.z = get_setting_float("player_pos_z");
    player.height = CELL_Z_SCALE / 2;
    player.speed = 10.0f;
    player.jump_velocity = -8.0f;
    player.size = 0.3f * CELL_XY_SCALE;
    gamestate->player = player;

    gamestate->delta_time = 0.0f;

    memset(gamestate->projectiles, 0, sizeof(gamestate->projectiles));

    if (!load_world(&gamestate->world, level)) {
        printf("Failed to load world.\n");
        return false;
    }
    return true;
}

void update(GameState* game_state, InputState* input_state) {
    vec2 movement = process_input(game_state, input_state);
    process_mouse(game_state, input_state);
        
    update_player_position(&game_state->player, &game_state->world, movement.x, movement.y, game_state->delta_time);


    // Update projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        update_projectile(&game_state->world, &game_state->projectiles[i], game_state->delta_time);
    }

    if (input_state->mouse_button_1.is_down && !input_state->mouse_button_1.was_down) {
        create_projectile(game_state->projectiles, &game_state->player);
    }
}
