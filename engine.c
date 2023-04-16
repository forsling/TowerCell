#include <stdio.h>
#include <math.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_image.h>
#include <GL/glu.h>
#include "engine.h"
#include "world.h"

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const float SCALE_FACTOR = 2.0f;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_GLContext gl_context = NULL;

World world;
Player player;

static bool quit = false;

bool init_engine() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    window = SDL_CreateWindow("Game Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (window == NULL) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

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

    // Initialize player object
    player.position.x = world.width / 2;
    player.position.y = world.height / 2;
    player.position.z = 1.0f; // Adjust this value to set the initial height
    player.pitch = 0.0f;
    player.yaw = 0.0f;
    player.speed = 0.1f; // Adjust this value to set the movement speed

    // Initialize OpenGL
    glClearColor(0.17f, 0.2f, 0.26f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT, 0.1f, 500.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    return true;
}

void main_loop() {
    SDL_Event event;

    SDL_SetRelativeMouseMode(SDL_TRUE);

    while (!quit) {
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }
        }

        process_input(&world);
        process_mouse();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        render_world(&world);

        SDL_GL_SwapWindow(window);
    }

    SDL_SetRelativeMouseMode(SDL_FALSE);
}

void update_player_position(Player *player, World *world, float dx, float dy, float dz) {
    const float COLLISION_BUFFER = 0.3f * SCALE_FACTOR;

    float newX = player->position.x + dx * player->speed;
    float newY = player->position.y + dy * player->speed;
    float newZ = player->position.z + dz * player->speed;

    int gridX = (int)(newX / SCALE_FACTOR);
    int gridY = (int)(newY / SCALE_FACTOR);

    bool canMoveX = !is_solid_cell(world, gridX, gridY);
    bool canMoveY = !is_solid_cell(world, gridX, gridY);

    if (canMoveX || canMoveY) {
        float cellCenterX = gridX * SCALE_FACTOR + SCALE_FACTOR / 2.0f;
        float cellCenterY = gridY * SCALE_FACTOR + SCALE_FACTOR / 2.0f;

        if (canMoveX && fabs(newX - cellCenterX) <= (SCALE_FACTOR / 2.0f + COLLISION_BUFFER)) {
            canMoveX = false;
        }

        if (canMoveY && fabs(newY - cellCenterY) <= (SCALE_FACTOR / 2.0f + COLLISION_BUFFER)) {
            canMoveY = false;
        }
    }

    // Handle corner cases to avoid getting stuck
    if (!canMoveX && !canMoveY) {
        float oldX = player->position.x;
        float oldY = player->position.y;

        player->position.x = newX;
        canMoveX = !is_solid_cell(world, (int)(player->position.x / SCALE_FACTOR), (int)(player->position.y / SCALE_FACTOR));

        player->position.x = oldX;
        player->position.y = newY;
        canMoveY = !is_solid_cell(world, (int)(player->position.x / SCALE_FACTOR), (int)(player->position.y / SCALE_FACTOR));

        player->position.y = oldY;
    }

    if (canMoveX) {
        player->position.x = newX;
    }
    if (canMoveY) {
        player->position.y = newY;
    }
    player->position.z = newZ;
}

void process_input(World *world) {
    const Uint8 *state = SDL_GetKeyboardState(NULL);

    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;

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
    if (state[SDL_SCANCODE_A]) {
        dx -= sinf(player.yaw);
        dy += cosf(player.yaw);
    }
    if (state[SDL_SCANCODE_D]) {
        dx += sinf(player.yaw);
        dy -= cosf(player.yaw);
    }
    if (state[SDL_SCANCODE_SPACE]) {
        dz += 1.0f;
    }
    if (state[SDL_SCANCODE_LSHIFT]) {
        dz -= 1.0f;
    }

    update_player_position(&player, world, dx, dy, dz);
}

void process_mouse() {
    int mouseX, mouseY;
    SDL_GetRelativeMouseState(&mouseX, &mouseY);

    const float MOUSE_SENSITIVITY = 0.001f;
    player.yaw -= mouseX * MOUSE_SENSITIVITY;   // negate the value here
    player.pitch += mouseY * MOUSE_SENSITIVITY; // and here

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
    if (!load_world("assets/world1.bmp", &world, renderer)) {
        printf("Failed to load world.\n");
        return false;
    }
    return true;
}

GLuint load_texture(const char *filename) {
    SDL_Surface *surface = IMG_Load(filename);
    if (!surface) {
        printf("Error loading texture: %s\n", IMG_GetError());
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);

    SDL_FreeSurface(surface);

    return texture;
}

void free_engine_assets() {
    free_world(&world);
}

void render_textured_quad(GLuint texture, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    Vec3 scaled_a = {a.x * SCALE_FACTOR, a.y * SCALE_FACTOR, a.z * SCALE_FACTOR};
    Vec3 scaled_b = {b.x * SCALE_FACTOR, b.y * SCALE_FACTOR, b.z * SCALE_FACTOR};
    Vec3 scaled_c = {c.x * SCALE_FACTOR, c.y * SCALE_FACTOR, c.z * SCALE_FACTOR};
    Vec3 scaled_d = {d.x * SCALE_FACTOR, d.y * SCALE_FACTOR, d.z * SCALE_FACTOR};

    glBindTexture(GL_TEXTURE_2D, texture);

    glBegin(GL_QUADS);
    {
        glTexCoord2f(0, 0);
        glVertex3f(scaled_a.x, scaled_a.y, scaled_a.z);

        glTexCoord2f(1, 0);
        glVertex3f(scaled_b.x, scaled_b.y, scaled_b.z);

        glTexCoord2f(1, 1);
        glVertex3f(scaled_c.x, scaled_c.y, scaled_c.z);

        glTexCoord2f(0, 1);
        glVertex3f(scaled_d.x, scaled_d.y, scaled_d.z);
    }
    glEnd();
}

void render_world(World *world) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(90.0, (double)SCREEN_WIDTH / (double)SCREEN_HEIGHT, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(player.position.x, player.position.y, player.position.z,
            player.position.x + cosf(player.yaw), player.position.y + sinf(player.yaw), player.position.z - sinf(player.pitch),
            0.0f, 0.0f, 1.0f);

    const int DX[] = {1, 0, -1, 0};
    const int DY[] = {0, 1, 0, -1};
    Vec3 WALL_CORNERS[] = {
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };

    for (int y = 0; y < world->height; ++y) {
        for (int x = 0; x < world->width; ++x) {
            CellDefinition* cell = &world->cells[y][x];

            // Get cell neighbors
            CellDefinition *neighbors[4];
            for (int i = 0; i < 4; ++i) {
                int nx = x + DX[i];
                int ny = y + DY[i];

                if (is_within_bounds(world, nx, ny)) {
                    neighbors[i] = &world->cells[ny][nx];
                } else {
                    neighbors[i] = NULL;
                }
            }

            // Render floors
            if (cell->floor_texture != 0) {
                Vec3 floor_vertices[4] = {
                    {x, y, 0.0f},
                    {x + 1, y, 0.0f},
                    {x + 1, y + 1, 0.0f},
                    {x, y + 1, 0.0f}
                };
                render_textured_quad(cell->floor_texture, floor_vertices[0], floor_vertices[1], floor_vertices[2], floor_vertices[3]);
            }

            // Render ceilings
            if (cell->ceiling_texture != 0) {
                Vec3 ceiling_vertices[4] = {
                    {x, y, 1.0f},
                    {x + 1, y, 1.0f},
                    {x + 1, y + 1, 1.0f},
                    {x, y + 1, 1.0f}
                };
                render_textured_quad(cell->ceiling_texture, ceiling_vertices[0], ceiling_vertices[1], ceiling_vertices[2], ceiling_vertices[3]);
            }

            if (cell->type == CELL_OPEN) {
                for (int i = 0; i < 4; ++i) {
                    CellDefinition *neighbor = neighbors[i];
                    if (neighbor != NULL && neighbor->type == CELL_SOLID) {                        
                        if (neighbor->type == CELL_SOLID) {
                            if (neighbor->wall_texture != 0) {
                                // Render walls
                                Vec3 a = {x + WALL_CORNERS[i].x, y + WALL_CORNERS[i].y, 0.0f};
                                Vec3 b = {x + WALL_CORNERS[(i + 1) % 4].x, y + WALL_CORNERS[(i + 1) % 4].y, 0.0f};
                                Vec3 c = {x + WALL_CORNERS[(i + 1) % 4].x, y + WALL_CORNERS[(i + 1) % 4].y, 1.0f};
                                Vec3 d = {x + WALL_CORNERS[i].x, y + WALL_CORNERS[i].y, 1.0f};
                                render_textured_quad(neighbor->wall_texture, a, b, c, d);
                            }
                        }
                    }
                }
            }
            else if (cell->type == CELL_SOLID) {
                for (int i = 0; i < 4; ++i) {
                    CellDefinition *neighbor = neighbors[i];
                    bool isSolidEdgeBlock = (cell->wall_texture != 0 && neighbor == NULL);
                    bool isTransparentSolidWithSolidNeighbor = (cell->wall_texture == 0 && neighbor != NULL && neighbor->type == CELL_SOLID && neighbor->wall_texture != 0);
                    if (isSolidEdgeBlock || isTransparentSolidWithSolidNeighbor) {
                        Vec3 a = {x + WALL_CORNERS[i].x, y + WALL_CORNERS[i].y, 0.0f};
                        Vec3 b = {x + WALL_CORNERS[(i + 1) % 4].x, y + WALL_CORNERS[(i + 1) % 4].y, 0.0f};
                        Vec3 c = {x + WALL_CORNERS[(i + 1) % 4].x, y + WALL_CORNERS[(i + 1) % 4].y, 1.0f};
                        Vec3 d = {x + WALL_CORNERS[i].x, y + WALL_CORNERS[i].y, 1.0f};
                        GLuint wall_texture = isSolidEdgeBlock ? cell->wall_texture : neighbor->wall_texture;
                        render_textured_quad(wall_texture, a, b, c, d); // Using the appropriate wall texture
                    }
                }
            }
        }
    }
}

bool is_solid_cell(World *world, int x, int y) {
    if (is_within_bounds(world, x, y)) {
        CellDefinition *cell = &world->cells[y][x];
        return cell->type == CELL_SOLID;
    }
    return false;
}

bool is_out_of_bounds(World *world, int x, int y) {
    return x < 0 || x >= world->width || y < 0 || y >= world->height;
}

bool is_within_bounds(World *world, int x, int y) {
    return x >= 0 && x < world->width && y >= 0 && y < world->height;
}
