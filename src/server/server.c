#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "game_logic.h"
#include "../shared/game.h"
#include "../shared/utils.h"
#include "../shared/settings.h"
#include "../shared/vector.h"

#define BUFFER_SIZE 8192

typedef struct {
    TCPsocket socket;
    Player* player;
    GameState* game_state;
    World* world;
    SDL_mutex* game_state_mutex;
    float* delta_time;
} ClientData;

World world;
float delta_time;

Player* add_new_player(GameState* game_state, World* world) {
    int player_index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!game_state->players[i].connected) {
            player_index = i;
            break;
        }
    }
    if (player_index < 0) {
        return NULL;
    }

    int player_x = rand() % (world->layers[0].width + 1);
    int player_y = rand() % (world->layers[0].height + 1);
    float player_height = CELL_Z_SCALE / 2;
    game_state->players[player_index] = (Player) {
        .id = player_index,
        .position.x = player_x,
        .position.y = player_y,
        .position.z = 4 - player_height,
        .height = player_height,
        .speed = 10.0f,
        .jump_velocity = -8.0f,
        .size = 0.3 * CELL_XY_SCALE,
        .death_timer = 0.0f,
        .connected = true,
        .health = PLAYER_HEALTH
    };
    return &game_state->players[player_index];
}

int handle_client(void* data) {
    ClientData* client_data = (ClientData*)data;
    GameState* game_state = client_data->game_state;
    World* world = client_data->world;
    Player* player = client_data->player;
    TCPsocket client_socket = client_data->socket;
    float delta_time = *client_data->delta_time;

    // Send initial game state to the client
    InitialGameState initial_game_state = {
        .world = *world,
        .player_id = player->id
    };
    int sent_initial = SDLNet_TCP_Send(client_socket, &initial_game_state, sizeof(initial_game_state));
    if (sent_initial < sizeof(initial_game_state)) {
        printf("Error sending initial game state to the client.\n");
        SDLNet_TCP_Close(client_socket);
        free(client_data);
        return 1;
    }

    // Loop until the client disconnects
    while (1) {
        InputState input_state;
        int received = SDLNet_TCP_Recv(client_socket, &input_state, sizeof(input_state));
        if (received <= 0) {
            break; // Client disconnected or an error occurred
        }

        // Process the received input state and update the game state
        SDL_LockMutex(client_data->game_state_mutex);
        update(game_state, world, &input_state, player->id, delta_time);
        SDL_UnlockMutex(client_data->game_state_mutex);

        // Send the updated game state back to the client
        int sent = SDLNet_TCP_Send(client_socket, game_state, sizeof(*game_state));
        if (sent < sizeof(*game_state)) {
            break; // An error occurred while sending the data or the client disconnected
        }
    }

    player->connected = false;
    SDLNet_TCP_Close(client_socket);
    printf("Client disconnected.\n");
    free(client_data);
    return 0;
}

int main(int argc, char *argv[]) {
    initialize_default_server_settings();
    int server_port = get_setting_int("server_port");
    srand(time(NULL));

    if (SDL_Init(0) == -1 || SDLNet_Init() == -1) {
        printf("Error initializing SDL or SDL_net: %s\n", SDL_GetError());
        return 1;
    }

    if (!load_settings("server.txt", true)) {
        fprintf(stderr, "Failed to load settings\n");
        return 1;
    }

    IPaddress server_ip;
    if (SDLNet_ResolveHost(&server_ip, NULL, server_port) == -1) {
        printf("Error resolving server IP: %s\n", SDLNet_GetError());
        return 1;
    }
    
    TCPsocket server_socket = SDLNet_TCP_Open(&server_ip);
    if (!server_socket) {
        printf("Error opening server socket: %s\n", SDLNet_GetError());
        return 1;
    }

    printf("Server listening on port %d...\n", server_port);

    // Load level data
    const char* level_name = "darkchasm";
    if (!load_world(&world, level_name)) {
        printf("Failed to load world.\n");
        return false;
    }

    // Initialize game state
    GameState game_state;
    game_state.players_count = 0;
    memset(game_state.projectiles, 0, sizeof(game_state.projectiles));
    memset(game_state.players, 0, sizeof(game_state.players));

    // Prepare for multi-client handling
    SDL_Thread* client_threads[MAX_CLIENTS];

    // Create a mutex for synchronizing access to the game state
    SDL_mutex* game_state_mutex = SDL_CreateMutex();

    const Uint32 targetTickRate = 60;
    const Uint32 targetTickTime = 1000 / targetTickRate; // 1000ms / target TPS
    Uint32 lastTickTime = 0;

    while (1) {
        Uint32 currentTickTime = SDL_GetTicks();
        delta_time = fmin(((currentTickTime - lastTickTime) / 1000.0f), 0.1f);

        TCPsocket client_socket = SDLNet_TCP_Accept(server_socket);
        if (client_socket) {
            Player* player = add_new_player(&game_state, &world);
            if (player) {
                printf("Client connected!\n");

                // Create a new thread to handle the client connection
                ClientData* client_data = (ClientData*)malloc(sizeof(ClientData));
                client_data->socket = client_socket;
                client_data->player = player;
                client_data->game_state = &game_state;
                client_data->world = &world;
                client_data->game_state_mutex = game_state_mutex;
                client_data->delta_time = &delta_time;
                client_threads[player->id] = SDL_CreateThread(handle_client, "ClientThread", (void*)client_data);
            } else {
                printf("Server is full. Client connection rejected.\n");
                SDLNet_TCP_Close(client_socket);
            }
        }

        // Cap the tick rate
        Uint32 elapsedTime = SDL_GetTicks() - currentTickTime;
        if (elapsedTime < targetTickTime) {
            SDL_Delay(targetTickTime - elapsedTime);
        }

        lastTickTime = currentTickTime;
    }

    SDLNet_TCP_Close(server_socket);
    SDLNet_Quit();
    SDL_Quit();

    // Clean up threads and mutex
    for (int i = 0; i < game_state.players_count; i++) {
        SDL_WaitThread(client_threads[i], NULL);
    }
    SDL_DestroyMutex(game_state_mutex);

    return 0;
}
