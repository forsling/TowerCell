#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_image.h>
#include <GL/glu.h>

#include "engine.h"
#include "world.h"
#include "vector.h"
#include "utils.h"
#include "render.h"
#include "audio.h"
#include "settings.h"

const bool DEBUG_LOG = true;

#define MAX_CELLS 16

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_GLContext gl_context = NULL;
World world;
Player player;

bool have_audio = false;
static bool quit = false;

int sound_jump;

bool init_engine() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int SCREEN_WIDTH = get_setting_int("screen_width");
    int SCREEN_HEIGHT = get_setting_int("screen_height");
    window = SDL_CreateWindow("Game Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (window == NULL) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    have_audio = audio_init();
    sound_jump = audio_load_sound("assets/jump1.wav");

    gl_context = SDL_GL_CreateContext(window);
    if (gl_context == NULL) {
        printf("OpenGL context could not be created! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    // Set swap interval for Vsync
    SDL_GL_SetSwapInterval(1);

    int imgFlags = IMG_INIT_PNG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        printf("SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError());
        return false;
    }

    if (!load_engine_assets()) {
        return false;
    }

    srand(time(NULL));

    // Initialize player object
    player = (struct Player) {0};
    player.position.x = get_setting_float("player_pos_x");
    player.position.y = get_setting_float("player_pos_y");
    player.position.z = get_setting_float("player_pos_z");
    player.height = CELL_Z_SCALE / 2;
    player.speed = 10.0f;
    player.jump_velocity = -8.0f;
    player.size = 0.3f * CELL_XY_SCALE;

    init_opengl(player);

    return true;
}

void main_loop() {
    SDL_Event event;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    Uint32 lastFrameTime = 0;
    const Uint32 targetFrameRate = 120;
    const Uint32 targetFrameTime = 1000 / targetFrameRate; // 1000ms / target FPS

    while (!quit) {
        Uint32 currentFrameTime = SDL_GetTicks();
        float deltaTime = fmin(((currentFrameTime - lastFrameTime) / 1000.0f), 0.1f);

        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }

            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.scancode == SDL_SCANCODE_F) {
                    bool free_mode = get_setting_bool("free_mode");
                    char * new_free_mode_val = free_mode ? "false" : "true";
                    set_setting("free_mode", SETTING_TYPE_BOOL, new_free_mode_val);
                    printf("Free mode set to %d\n", new_free_mode_val);
                }
            }
        }

        process_input(&world, deltaTime);
        process_mouse();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        render_world(&world, &player);

        SDL_GL_SwapWindow(window);

        // Cap the framerate
        Uint32 elapsedTime = SDL_GetTicks() - currentFrameTime;
        if (elapsedTime < targetFrameTime) {
            SDL_Delay(targetFrameTime - elapsedTime);
        }

        lastFrameTime = currentFrameTime;
    }

    SDL_SetRelativeMouseMode(SDL_FALSE);
}

void process_input(World *world, float deltaTime) {
    const Uint8 *state = SDL_GetKeyboardState(NULL);

    float dx = 0.0f;
    float dy = 0.0f;
    bool isJumping = false;

    if (state[SDL_SCANCODE_ESCAPE]) {
        quit = true;
    }
    if (state[SDL_SCANCODE_W]) {
        dx += cosf(player.yaw);
        dy += sinf(player.yaw);
    }
    if (state[SDL_SCANCODE_S]) {
        dx -= cosf(player.yaw);
        dy -= sinf(player.yaw);
    }
    if (state[SDL_SCANCODE_D]) {
        dx -= sinf(player.yaw);
        dy += cosf(player.yaw);
    }
    if (state[SDL_SCANCODE_A]) {
        dx += sinf(player.yaw);
        dy -= cosf(player.yaw);
    }

    if (state[SDL_SCANCODE_SPACE]) {
        if (get_setting_bool("free_mode")) {
            player.position.z -= player.speed * deltaTime;
        } else if (player.velocity_z == 0.0f) { // Jump only when the player is on the ground
            player.velocity_z = player.jump_velocity;
            isJumping = true;
            audio_play_sound(sound_jump, 0.2f);
        } 
    }
    if (state[SDL_SCANCODE_LSHIFT]) {
        if (get_setting_bool("free_mode")) {
            player.position.z += player.speed * deltaTime;        }
    }

    update_player_position(&player, world, dx, dy, deltaTime);
}

void update_player_position(Player *player, World *world,
                            float dx, float dy, float deltaTime) {

    // Handle free mode unrestricted movement
    if (get_setting_bool("free_mode")) {
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
    Layer *layer = &world->layers[z_layer];

    // Calculate the destination position
    vec2 source = {player->position.x, player->position.y};
    vec2 destination = {target_x, target_y};

    // Update the player's position based on the furthest legal position
    if (z_layer >= 0) {
        vec2 furthest_legal_position = get_furthest_legal_position(layer, source, destination, player->size);
        target_x = furthest_legal_position.x;
        target_y = furthest_legal_position.y;
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

void process_mouse() {
    int mouseX, mouseY;
    SDL_GetRelativeMouseState(&mouseX, &mouseY);

    const float MOUSE_SENSITIVITY = 0.001f;
    player.yaw += mouseX * MOUSE_SENSITIVITY;
    player.pitch -= mouseY * MOUSE_SENSITIVITY;

    if (player.pitch < -M_PI / 2)
        player.pitch = -M_PI / 2;
    if (player.pitch > M_PI / 2)
        player.pitch = M_PI / 2;
}

void cleanup_engine() {
    free_engine_assets();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    window = NULL;
    renderer = NULL;

    IMG_Quit();
    SDL_Quit();
}

bool load_engine_assets() {
    // Load world from a bitmap file
    
    if (!load_world(&world, get_setting_string("current_level"))) {
        printf("Failed to load world.\n");
        return false;
    }
    return true;
}

void free_engine_assets() {
    free_world(&world);
    audio_quit();
}
