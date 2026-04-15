#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include "lpaTPGEngine.hpp"
#include <dirent.h>
#include <ctime>

int textseed = 0;

std::vector<std::string> getSaveFiles() {
    std::vector<std::string> files;
    DIR *dir = opendir(".");
    if (!dir) return files;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".sav")
            files.push_back(name);
    }
    closedir(dir);
    return files;
}

#define CX 8
#define CZ 8
#define CY 96
#define WORLD_X 96
#define WORLD_Z 96

#define RENDER_DIST 6  // in chunks

#define PLAYER_W 0.6f
#define PLAYER_H 1.8f
#define PLAYER_EYE 0.4f
float GRAVITY = -20.0f;
#define JUMP_VEL -8.0f     // usually -8
#define PLAYER_SPEED 6.0f // usually 6

#define SEA_LEVEL 999 // useless

int BLOCK_COUNT = 0;

/* Original ID system
#define BLOCK_AIR  0
#define BLOCK_GRASS  1
#define BLOCK_DIRT   2
#define BLOCK_STONE  3
#define BLOCK_PLANKS 4
#define BLOCK_LOGS   5
#define BLOCK_SAND   6
#define BLOCK_LEAVES 7
*/

// New ID system

class BlockID {
public:
    short numid; // Numerical ID, used internally
    std::string name; // String ID, used for commands and high level internals
    lpa::Color color; // Colour, used for rendering
    BlockID() : numid(-32768), name("empty_block"), color(lpa::Color(0,0,0)) {}
    BlockID(int num, std::string nm, lpa::Color col) : numid(num), name(nm), color(col) {}
    const bool isBlock() {
        return numid >= 0;
    }
};

std::vector<BlockID> blocks;

int searchForBlockID(int id) {
    for (int i = 0; i < blocks.size(); i++) {
        if (blocks[i].numid == id)
            return i;
    }
    return -1;
}

int registerID(short num, std::string nm, lpa::Color color) {
    BlockID newblock(num, nm, color);
    blocks.push_back(newblock);
    BLOCK_COUNT++;
    return num;
}

lpa::Color blockColor(int type) {
    int idx = searchForBlockID(type);
    if (idx != -1) return blocks[idx].color;
    return lpa::Color(0,0,0);
}

int searchForBlockName(const std::string name) {
    for (int i = 0; i < blocks.size(); i++) {
        if (blocks[i].name == name)
            return i;
    }
    return -1;
}

namespace mycrap {
    int BLOCK_AIR = registerID(0, "air", lpa::Color(0, 0, 0));
    int BLOCK_GRASS = registerID(1, "grass_block", lpa::Color(0, 125, 25));
    int BLOCK_DIRT = registerID(2, "dirt_block", lpa::Color(33, 18, 1));
    int BLOCK_STONE = registerID(3, "stone", lpa::Color(60, 60, 60));
    int BLOCK_PLANKS = registerID(4, "wooden_planks", lpa::Color(217, 215, 163));
    int BLOCK_LOGS = registerID(5, "wooden_logs", lpa::Color(99, 89, 74));
    int BLOCK_SAND = registerID(6, "sand", lpa::Color(220, 230, 94));
    int BLOCK_LEAVES = registerID(7, "leaves", lpa::Color(230, 94, 210));
    int BLOCK_EMPTY = registerID(-32768, "empty_block", lpa::Color(0,0,0));
}

uint8_t selected = 0;

short chunk[WORLD_X][WORLD_Z][CX][CZ][CY] = {};
lpa::Model3D chunkMesh[WORLD_X][WORLD_Z];
bool chunkDirty[WORLD_X][WORLD_Z];

float velY = 0;
bool onGround = false;

// player                   feet
float px, py, pz;

// player                   inventory
class ItemStack {
public:
    BlockID type;
    bool increment(int amt) {
        count += amt;
        return true;
    }
    bool decrement(int amt) {
        if ((count - amt) < 0) return false;
        if ((count - amt) == 0) {
            count = 0;
            type = BlockID();
            return true;
        }
        count -= amt;
        return true;
    }
    ItemStack() : type(), count(0) {}
    ItemStack(short id, int count) : count(count) {
        int idx = searchForBlockID(id);
        if (idx != -1) type = blocks[idx];
        else type = BlockID(); // fallback
    }
    short count;
};

std::vector<ItemStack> inventory(16);

int inventorySearchName(std::string name) {
    for (int i = 0; i < inventory.size(); i++) {
        if (inventory[i].type.name == name)
            return i;
    }
    return -1;
}

int inventorySearchID(int id) {
    for (int i = 0; i < inventory.size(); i++) {
        if (inventory[i].type.numid == id)
            return i;
    }
    return -1;
}

bool addItem(ItemStack items) {
    if (items.count < 1) return false;
    short itemexists = inventorySearchName(items.type.name);
    if (itemexists != -1) {
        inventory[itemexists].increment(items.count);
        return true;
    }
    short next = inventorySearchName("empty_block");
    if (next == -1) return false;
    inventory[next] = items;
    return true;
}

// this gives us 16 inventory entries to choose from
void getSelected(lpa::Window& win) {
    if (win.keyPressed(SDLK_e)) selected++;
    if (win.keyPressed(SDLK_q)) selected--;

    if (selected > 15) selected = 0;
    if (selected < 0) selected = 15;
}

void generateWorld(int seed) {
    textseed = seed;
    for (int wx = 0; wx < WORLD_X; wx++)
    for (int wz = 0; wz < WORLD_Z; wz++) {

        for (int x = 0; x < CX; x++)
        for (int z = 0; z < CZ; z++) {

            float worldX = (wx * CX + x);
            float worldZ = (wz * CZ + z);
            // Noise helpers
            auto hash = [](int x, int z, int seed) {
                int h = x * 374761393 + z * 668265263;
                h = (h ^ (h >> 13)) * seed;
                return (h ^ (h >> 16)) & 0x7fffffff;
            };

            auto grad = [&](int x, int z, float dx, float dz, int seed) {
                int h = hash(x, z, seed) & 3;
                float gx = (h & 1) ? 1.0f : -1.0f;
                float gz = (h & 2) ? 1.0f : -1.0f;
                return gx * dx + gz * dz;
            };

            auto noise2D = [&](float fx, float fz, int seed) {
                int x0 = (int)floor(fx), z0 = (int)floor(fz);
                int x1 = x0 + 1,         z1 = z0 + 1;

                float sx = fx - x0, sz = fz - z0;
                float u = sx * sx * (3.0f - 2.0f * sx);
                float v = sz * sz * (3.0f - 2.0f * sz);

                float n00 = grad(x0, z0, sx,      sz, seed);
                float n10 = grad(x1, z0, sx-1.0f, sz, seed);
                float n01 = grad(x0, z1, sx,      sz-1.0f, seed);
                float n11 = grad(x1, z1, sx-1.0f, sz-1.0f, seed);

                float nx0 = n00 + u * (n10 - n00);
                float nx1 = n01 + u * (n11 - n01);
                return nx0 + v * (nx1 - nx0);
            };
            // Biomes
            float biomeNoise = noise2D(worldX * 0.01f, worldZ * 0.01f, seed);

            enum { BIOME_PLAINS, BIOME_DESERT, BIOME_MOUNTAINS, BIOME_OCEAN };
            int biome;

            if (biomeNoise < -0.5f) biome = BIOME_OCEAN;
            else if (biomeNoise < -0.2f) biome = BIOME_DESERT;
            else if (biomeNoise > 0.7f) biome = BIOME_MOUNTAINS;
            else biome = BIOME_PLAINS;
            // ⛰️ Height
            float base = noise2D(worldX * 0.05f, worldZ * 0.05f, seed);
            float mountain = noise2D(worldX * 0.02f, worldZ * 0.02f, seed);
            mountain *= mountain;

            float heightF = 0;

            if (biome == BIOME_MOUNTAINS)
                heightF = base * 1.7f + mountain * 1.5f;
            else if (biome == BIOME_DESERT)
                heightF = base * 0.3f;
            else if (biome == BIOME_OCEAN)
                heightF = base * 0.05f / 5.0f;
            else
                heightF = base * 0.6f;

            int height = (int)((heightF * 0.5f + 0.5f) * (CY / 4)) + 28;

            for (int y = 0; y < CY; y++) {

                if (y < height) {
                    // sky / water (above ground)
                        chunk[wx][wz][x][z][y] = mycrap::BLOCK_AIR;
                }

                else if (y == height) {
                    // surface
                    if (biome == BIOME_DESERT) {
                        chunk[wx][wz][x][z][y] = mycrap::BLOCK_SAND;
                    }
                    else if (biome == BIOME_MOUNTAINS) {
                        if (height > SEA_LEVEL + 10)
                            chunk[wx][wz][x][z][y] = mycrap::BLOCK_STONE;
                        else
                            chunk[wx][wz][x][z][y] = mycrap::BLOCK_GRASS;
                    }
                    else {
                        if (height >= SEA_LEVEL - 1)
                            chunk[wx][wz][x][z][y] = mycrap::BLOCK_SAND;
                        else
                            chunk[wx][wz][x][z][y] = mycrap::BLOCK_GRASS;
                    }
                }

                else if (y <= height + 3) {
                    // subsurface (below ground)
                    if (biome == BIOME_DESERT || height >= SEA_LEVEL - 1)
                        chunk[wx][wz][x][z][y] = mycrap::BLOCK_SAND;
                    else
                        chunk[wx][wz][x][z][y] = mycrap::BLOCK_DIRT;
                }

                else {
                    // deep underground
                    chunk[wx][wz][x][z][y] = mycrap::BLOCK_STONE;
                }
            }
        }

        chunkDirty[wx][wz] = true;
    }
}

void saveToFile(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(chunk, sizeof(chunk), 1, f);
    fclose(f);
}

void loadFromFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fread(chunk, sizeof(chunk), 1, f);
    fclose(f);
    for (int wx = 0; wx < WORLD_X; wx++)
        for (int wz = 0; wz < WORLD_Z; wz++)
            chunkDirty[wx][wz] = true;
}

int getBlock(int wx, int wy, int wz) {
    int cx = wx / CX, cz = wz / CZ;
    int lx = wx % CX, lz = wz % CZ;
    if (cx < 0 || cx >= WORLD_X || cz < 0 || cz >= WORLD_Z) return 0;
    if (wy < 0 || wy >= CY) return 0;
    return chunk[cx][cz][lx][lz][wy];
}

void setBlock(int wx, int wy, int wz, int val) {
    int cx = wx / CX, cz = wz / CZ;
    int lx = wx % CX, lz = wz % CZ;
    if (cx < 0 || cx >= WORLD_X || cz < 0 || cz >= WORLD_Z) return;
    if (wy < 0 || wy >= CY) return;
    chunk[cx][cz][lx][lz][wy] = val;
    chunkDirty[cx][cz] = true;
    if (lx == 0    && cx > 0)          chunkDirty[cx-1][cz] = true;
    if (lx == CX-1 && cx < WORLD_X-1) chunkDirty[cx+1][cz] = true;
    if (lz == 0    && cz > 0)          chunkDirty[cx][cz-1] = true;
    if (lz == CZ-1 && cz < WORLD_Z-1) chunkDirty[cx][cz+1] = true;
}

bool aabbSolid(float x, float y, float z) {
    float half = PLAYER_W / 2.0f;

    int x0 = (int)floor(x - half), x1 = (int)floor(x + half);
    int y0 = (int)floor(y),        y1 = (int)floor(y + PLAYER_H - 0.001f);
    int z0 = (int)floor(z - half), z1 = (int)floor(z + half);

    for (int bx = x0; bx <= x1; bx++) {
        for (int by = y0; by <= y1; by++) {
            for (int bz = z0; bz <= z1; bz++) {

                // 🔥 CRITICAL: bounds check
                if (bx < 0 || by < 0 || bz < 0 ||
                    bx >= WORLD_X * CX ||
                    by >= CY ||
                    bz >= WORLD_Z * CZ)
                    continue;

                int b = getBlock(bx, by, bz);

                if (b != mycrap::BLOCK_AIR)
                    return true;
            }
        }
    }
    return false;
}

void movePlayer(float dx, float dy, float dz) {

    // X
    if (!aabbSolid(px + dx, py, pz))
        px += dx;

    // Z
    if (!aabbSolid(px, py, pz + dz))
        pz += dz;

    // Y
    if (!aabbSolid(px, py + dy, pz)) {
        py += dy;
    } else {
        if (velY > 0) onGround = true;
        velY = 0;
    }
}

void rebuildChunk(int wx, int wz) {
    chunkMesh[wx][wz] = lpa::Model3D();
    float offX = wx * CX, offZ = wz * CZ;

    auto isSolid = [&](int x, int y, int z) {
        return getBlock((int)offX + x, y, (int)offZ + z) != 0;
    };

    for (int x = 0; x < CX; x++) {
        for (int z = 0; z < CZ; z++) {
            for (int y = 0; y < CY; y++) {
                if (!chunk[wx][wz][x][z][y]) continue;
                float fx = offX + x, fy = y, fz = offZ + z;
                lpa::Color c = blockColor(chunk[wx][wz][x][z][y]);
                if (!isSolid(x, y+1, z)) {
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy+1,fz), lpa::Vec3(fx+1,fy+1,fz+1), lpa::Vec3(fx+1,fy+1,fz), c));
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy+1,fz), lpa::Vec3(fx,fy+1,fz+1), lpa::Vec3(fx+1,fy+1,fz+1), c));
                }
                if (!isSolid(x, y-1, z)) {
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy,fz+1), lpa::Vec3(fx+1,fy,fz), lpa::Vec3(fx+1,fy,fz+1), c));
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy,fz+1), lpa::Vec3(fx,fy,fz), lpa::Vec3(fx+1,fy,fz), c));
                }
                if (!isSolid(x, y, z+1)) {
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy,fz+1), lpa::Vec3(fx+1,fy+1,fz+1), lpa::Vec3(fx,fy+1,fz+1), c));
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy,fz+1), lpa::Vec3(fx+1,fy,fz+1), lpa::Vec3(fx+1,fy+1,fz+1), c));
                }
                if (!isSolid(x, y, z-1)) {
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx+1,fy,fz), lpa::Vec3(fx,fy+1,fz), lpa::Vec3(fx+1,fy+1,fz), c));
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx+1,fy,fz), lpa::Vec3(fx,fy,fz), lpa::Vec3(fx,fy+1,fz), c));
                }
                if (!isSolid(x+1, y, z)) {
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx+1,fy,fz+1), lpa::Vec3(fx+1,fy+1,fz), lpa::Vec3(fx+1,fy+1,fz+1), c));
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx+1,fy,fz+1), lpa::Vec3(fx+1,fy,fz), lpa::Vec3(fx+1,fy+1,fz), c));
                }
                if (!isSolid(x-1, y, z)) {
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy,fz), lpa::Vec3(fx,fy+1,fz+1), lpa::Vec3(fx,fy+1,fz), c));
                    chunkMesh[wx][wz].addTri(lpa::triangle3d(lpa::Vec3(fx,fy,fz), lpa::Vec3(fx,fy,fz+1), lpa::Vec3(fx,fy+1,fz+1), c));
                }
            }
        }
    }
    chunkDirty[wx][wz] = false;
}

lpa::Vec2 getChunkPos(int px, int pz) {
    int cx = px / CX;
    int cz = pz / CZ;

    if (px < 0 && px % CX != 0) cx--;
    if (pz < 0 && pz % CZ != 0) cz--;

    return lpa::Vec2((float)cx, (float)cz);
}

void renderWorld(lpa::Window &win, lpa::Camera &cam, int ppx, int ppz) {
    lpa::Vec2 cpos = getChunkPos(ppx, ppz);
    for (int wx = 0; wx < WORLD_X; wx++) {
        for (int wz = 0; wz < WORLD_Z; wz++) {
            int dx = wx - (int)cpos.x;
            int dz = wz - (int)cpos.y;
            if (dx*dx + dz*dz > RENDER_DIST*RENDER_DIST) continue;

            if (chunkDirty[wx][wz]
                && wx >= cpos.x - 1 && wx <= cpos.x + 1
                && wz >= cpos.y - 1 && wz <= cpos.y + 1)
                rebuildChunk(wx, wz);

            chunkMesh[wx][wz].render(win, lpa::color::WHITE, cam);
        }
    }
}

void rebuildWorld() {
    for (int wx = 0; wx < WORLD_X; wx++)
        for (int wz = 0; wz < WORLD_Z; wz++)
            rebuildChunk(wx, wz);
}

bool raycast(lpa::Camera &cam, float maxDist,
             int &hitX, int &hitY, int &hitZ,
             int &normX, int &normY, int &normZ) {
    float dx = -sin(cam.yaw) * cos(cam.pitch);
    float dy =  sin(cam.pitch);
    float dz =  cos(cam.yaw) * cos(cam.pitch);

    float x = cam.x, y = cam.y, z = cam.z;
    int bx = (int)floor(x), by = (int)floor(y), bz = (int)floor(z);

    int stepX = dx > 0 ? 1 : -1;
    int stepY = dy > 0 ? 1 : -1;
    int stepZ = dz > 0 ? 1 : -1;

    float tDeltaX = dx != 0 ? fabs(1.0f / dx) : 1e30f;
    float tDeltaY = dy != 0 ? fabs(1.0f / dy) : 1e30f;
    float tDeltaZ = dz != 0 ? fabs(1.0f / dz) : 1e30f;

    float tMaxX = dx != 0 ? ((dx > 0 ? (floor(x)+1) - x : x - floor(x)) / fabs(dx)) : 1e30f;
    float tMaxY = dy != 0 ? ((dy > 0 ? (floor(y)+1) - y : y - floor(y)) / fabs(dy)) : 1e30f;
    float tMaxZ = dz != 0 ? ((dz > 0 ? (floor(z)+1) - z : z - floor(z)) / fabs(dz)) : 1e30f;

    normX = normY = normZ = 0;
    float dist = 0;

    while (dist < maxDist) {
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            bx += stepX; dist = tMaxX; tMaxX += tDeltaX;
            normX = -stepX; normY = 0; normZ = 0;
        } else if (tMaxY < tMaxZ) {
            by += stepY; dist = tMaxY; tMaxY += tDeltaY;
            normX = 0; normY = -stepY; normZ = 0;
        } else {
            bz += stepZ; dist = tMaxZ; tMaxZ += tDeltaZ;
            normX = 0; normY = 0; normZ = -stepZ;
        }
        if (getBlock(bx, by, bz)) {
            hitX = bx; hitY = by; hitZ = bz;
            return true;
        }
    }
    return false;
}

bool paused = false;

void renderInventory(lpa::Window& win, lpa::Font& font) {
    const int slotSize = 25;
    const int spacing = 5;
    const int invSize = 16;

    //int totalWidth = invSize * slotSize + (invSize - 1) * spacing;
    int startX = spacing;
    int startY = 600 - (slotSize + spacing);
    int y = startY;
        for (int i = 0; i < invSize; i++) {
            int x = startX + i * (slotSize + spacing);

            lpa::Color c = inventory[i].type.color;
            if (i == selected) {
                win.drawRect(x-1, y-1, slotSize + 1,slotSize + 1, lpa::color::WHITE);
            }
            win.fillRect(x, y, slotSize, slotSize, c);
            const char* text = std::to_string(inventory[i].count).c_str();
            win.drawText(text, x, y, font, lpa::color::bright::GREY);
        }
}

std::string TITLE = "MyCraps Alpha v1.2";

bool selectthing = false;

int main() {
    std::srand(static_cast<unsigned int>(time(0)));
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    lpa::Window win(TITLE.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600);
    SDL_RenderSetLogicalSize(win.renderer, win.screenW, win.screenH);
    lpa::Font font("./ASSETS/OpenSans-Regular.ttf", 24);
    lpa::Font sfont("./ASSETS/OpenSans-Regular.ttf", 16);
    if (!font.font) printf("Font failed to load: %s\n", TTF_GetError());
    IMG_Init(IMG_INIT_PNG);

    auto drawButton = [&](const char *text, int x, int y, int w, int h, bool hovered) {
        lpa::Color bg  = hovered ? lpa::color::WHITE : lpa::color::bright::GREY;
        lpa::Color txt = hovered ? lpa::color::BLACK : lpa::color::WHITE;
        win.fillRect(x, y, w, h, bg);
        win.drawRect(x, y, w, h, lpa::color::WHITE);
        win.drawText(text, x + 10, y + 8, font, txt);
    };

    bool appRunning = true;

    while (appRunning) {
        // --- MAIN MENU ---
        win.unlockMouse();
        bool menuRunning = true;
        int menuState = 0;
        std::vector<std::string> saveFiles;

        while (menuRunning) {
            int mx, my;
            win.getMousePos(mx, my);
            bool clicked = win.mousePressed(SDL_BUTTON_LEFT);

            SDL_Event me;
            while (SDL_PollEvent(&me)) {
                if (me.type == SDL_QUIT) { IMG_Quit(); return 0; }
            }

            win.clear(lpa::color::BLACK);

            if (menuState == 0) {
                bool hoverNew  = mx >= 250 && mx <= 550 && my >= 260 && my <= 310;
                bool hoverLoad = mx >= 250 && mx <= 550 && my >= 330 && my <= 380;
                bool hoverQuit = mx >= 250 && mx <= 550 && my >= 400 && my <= 450;

                if (clicked && hoverNew) {
                    win.clear(lpa::color::BLACK);
                    win.drawText("Loading...", 10, 10, font, lpa::color::WHITE);
                    win.present();
                    generateWorld(std::rand());
                    menuRunning = false;
                    SDL_Delay(150);
                }
                if (clicked && hoverLoad) { saveFiles = getSaveFiles(); menuState = 1; SDL_Delay(150); }
                if (clicked && hoverQuit) { IMG_Quit(); return 0; }

                win.drawText(TITLE.c_str(), 220, 180, font, lpa::color::WHITE);
                drawButton("New World",  250, 260, 300, 50, hoverNew);
                drawButton("Load World", 250, 330, 300, 50, hoverLoad);
                drawButton("Quit",       250, 400, 300, 50, hoverQuit);
            } else {
                win.drawText("Select a save file:", 250, 180, font, lpa::color::WHITE);

                if (saveFiles.empty()) {
                    win.drawText("No .sav files found.", 250, 240, font, lpa::color::bright::GREY);
                } else {
                    for (int i = 0; i < (int)saveFiles.size(); i++) {
                        int by = 240 + i * 60;
                        bool hovered = mx >= 250 && mx <= 550 && my >= by && my <= by + 50;
                        if (clicked && hovered) {
                            loadFromFile(saveFiles[i].c_str());
                            menuRunning = false;
                            SDL_Delay(150);
                        }
                        drawButton(saveFiles[i].c_str(), 250, by, 300, 50, hovered);
                    }
                }

                bool hoverBack = mx >= 250 && mx <= 550 && my >= 500 && my <= 550;
                if (clicked && hoverBack) { menuState = 0; SDL_Delay(150); }
                drawButton("Back", 250, 500, 300, 50, hoverBack);
            }

            win.present();
        }

        rebuildWorld();

        px = CX * WORLD_X / 2.0f;
        py = 16;
        pz = CZ * WORLD_Z / 2.0f;
        velY = 0;
        onGround = false;

        lpa::Camera cam(px, py + PLAYER_EYE, pz, 400.0f, 800, 600);
        win.lockMouse();

        float speed = PLAYER_SPEED;
        float sensitivity = 0.002f;
        bool running = true;
        bool paused = false;
        SDL_Event e;
        Uint32 lastTime = SDL_GetTicks();
        bool prevLeft = false, prevRight = false;
        bool prevPauseLeft = false;

        // --- GAME LOOP ---
        while (running) {
            Uint32 now = SDL_GetTicks();
            float dt = (now - lastTime) / 1000.0f;
            if (dt > 0.05f) dt = 0.05f;
            lastTime = now;

            int mdx, mdy;
            win.getMouseDelta(mdx, mdy);

            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { appRunning = false; running = false; }
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                    paused = !paused;
                    if (paused) win.unlockMouse();
                    else        win.lockMouse();
                    prevPauseLeft = true; // prevent immediate click-through
                }
            }

            if (!paused) {
                cam.yaw   -= mdx * sensitivity;
                cam.pitch += mdy * sensitivity;
                if (cam.pitch >  1.5f) cam.pitch =  1.5f;
                if (cam.pitch < -1.5f) cam.pitch = -1.5f;

                float mx = 0, mz = 0;
                if (win.keyPressed(SDLK_w)) { mx -= sin(cam.yaw); mz += cos(cam.yaw); }
                if (win.keyPressed(SDLK_s)) { mx += sin(cam.yaw); mz -= cos(cam.yaw); }
                if (win.keyPressed(SDLK_a)) { mx -= cos(cam.yaw); mz -= sin(cam.yaw); }
                if (win.keyPressed(SDLK_d)) { mx += cos(cam.yaw); mz += sin(cam.yaw); }
                if (win.keyPressed(SDLK_r)) {
                    px = CX * WORLD_X / 2.0f;
                    py = 16; pz = CZ * WORLD_Z / 2.0f;
                    velY = 0;
                }
                if ((win.keyPressed(SDLK_e) || win.keyPressed(SDLK_q)) && selectthing) {
                    getSelected(win);
                    selectthing = false;
                } else if (!(win.keyPressed(SDLK_e) || win.keyPressed(SDLK_q))) {
                    selectthing = true;
                }

                float mlen = sqrt(mx*mx + mz*mz);
                if (mlen > 0) { mx /= mlen; mz /= mlen; }

                velY -= GRAVITY * dt;
                if (win.keyPressed(SDLK_SPACE) && onGround) {
                    velY = JUMP_VEL;
                    onGround = false;
                }

                movePlayer(mx * speed * dt, velY * dt, mz * speed * dt);

                cam.x = px; cam.y = py + PLAYER_EYE; cam.z = pz;

                int hitX, hitY, hitZ, normX, normY, normZ;
                bool hit = raycast(cam, 6.0f, hitX, hitY, hitZ, normX, normY, normZ);

                bool leftDown  = win.mousePressed(SDL_BUTTON_LEFT);
                bool rightDown = win.mousePressed(SDL_BUTTON_RIGHT);

                if (hit) {
                    if (leftDown && !prevLeft) {
                        int block = getBlock(hitX, hitY, hitZ);
                        setBlock(hitX, hitY, hitZ, 0);
                        ItemStack item(block, 1);
                        addItem(item);
                    }
                    if (rightDown && !prevRight) {
                        if (inventory[selected].type.numid != mycrap::BLOCK_EMPTY) {
                            setBlock(hitX + normX, hitY + normY, hitZ + normZ, inventory[selected].type.numid);
                            inventory[selected].decrement(1);
                        }
                    }
                }

                prevLeft  = leftDown;
                prevRight = rightDown;
            }

            win.clear(lpa::color::BLACK);
            win.drawSkybox(lpa::color::CYAN, lpa::color::CYAN, cam);
            renderWorld(win, cam, (int)px, (int)pz);

            win.drawLine(395, 300, 405, 300, lpa::color::WHITE);
            win.drawLine(400, 295, 400, 305, lpa::color::WHITE);

            std::string fpsStr = "FPS: " + std::to_string((int)(1.0f / dt));
            std::string seedStr = "Seed: " + std::to_string(textseed);
            if (selected > 16) selected = 0;
            std::string blockstr = inventory[selected].type.name;

            win.drawText(fpsStr.c_str(), 10, 10,  font, lpa::color::BLACK);
            win.drawText(("X: " + std::to_string(px)).c_str(), 10, 35, font, lpa::color::BLACK);
            win.drawText(("Y: " + std::to_string(py)).c_str(), 10, 60, font, lpa::color::BLACK);
            win.drawText(("Z: " + std::to_string(pz)).c_str(), 10, 85, font, lpa::color::BLACK);

            win.drawText(("VY: " + std::to_string(velY)).c_str(), 10, 110, font, lpa::color::BLACK);
            if (blockstr != "empty_block") win.drawText(blockstr.c_str(), 5, 530, font, lpa::color::BLACK);
            win.drawText(seedStr.c_str(), 10, 135, font, lpa::color::BLACK);

            renderInventory(win, sfont);

            if (paused) {
                for (int y = 0; y < 600; y++) {
                    for (int x = 0; x < 800; x++) {
                        int idx = y * (win.pitch / 4) + x;
                        uint32_t p = win.pixels[idx];

                        uint8_t r = ((p >> 24) & 0xFF) / 2;
                        uint8_t g = ((p >> 16) & 0xFF) / 2;
                        uint8_t b = ((p >>  8) & 0xFF) / 2;

                        win.pixels[idx] =
                            ((uint32_t)r << 24) |
                            ((uint32_t)g << 16) |
                            ((uint32_t)b << 8) |
                            0xFF;
                    }
                }


                int mx, my;
                win.getMousePos(mx, my);
                bool clicked = win.mousePressed(SDL_BUTTON_LEFT) && !prevPauseLeft;

                bool hoverSave   = mx >= 250 && mx <= 550 && my >= 220 && my <= 270;
                bool hoverMenu   = mx >= 250 && mx <= 550 && my >= 290 && my <= 340;
                bool hoverResume = mx >= 250 && mx <= 550 && my >= 360 && my <= 410;

                if (clicked && hoverSave)   { saveToFile("world.sav"); SDL_Delay(150); }
                if (clicked && hoverMenu)   { running = false; SDL_Delay(1500); }
                if (clicked && hoverResume) { paused = false; win.lockMouse(); SDL_Delay(150); }

                prevPauseLeft = win.mousePressed(SDL_BUTTON_LEFT);

                win.drawText("Paused", 355, 160, font, lpa::color::WHITE);
                drawButton("Save World",   250, 220, 300, 50, hoverSave);
                drawButton("Exit to Menu", 250, 290, 300, 50, hoverMenu);
                drawButton("Resume",       250, 360, 300, 50, hoverResume);
            } else {
                prevPauseLeft = false;
            }

            win.present();
        }
    }

    IMG_Quit();
    return 0;
}