/*
 * MineClone - Politechnika Warszawska
 * Prototyp gry voxelowej w stylu Minecrafta zbudowany na raylib.
 *
 * Funkcje tej wersji:
 *  - mapa: kampus Politechniki Warszawskiej z Gmachem Glownym
 *    (dziedziniec ze szklanym dachem, portyk z kolumnami, napis POLITECHNIKA,
 *     plac z fontanna, aleja drzew)
 *  - pasek narzedzi (hotbar) na 9 slotow, wybor klawiszami 1-9 i kolkiem myszy
 *  - ekwipunek [E] ze wszystkimi rodzajami blokow
 *  - zdrowie: serca, obrazenia od upadku, toniecie (paski powietrza),
 *    regeneracja, ekran smierci i respawn
 *  - kamera [F5]: pierwsza osoba / trzecia osoba zza plecow / z przodu
 *  - model gracza: lysy czlowiek w stylu Minecrafta z animacja chodzenia
 *  - dzwieki (generowane proceduralnie): niszczenie, stawianie, kroki,
 *    skok, ladowanie, powitanie
 *  - NPC: Jan Pawel II przy wejsciu do Gmachu Glownego - rusza glowa,
 *    patrzy na gracza, a po nacisnieciu [T] wita sie i macha reka
 *
 * Zbudowane na raylib (https://www.raylib.com)
 */

#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define WX 96              /* szerokosc swiata (X) */
#define WY 48              /* wysokosc swiata (Y) */
#define WZ 96              /* glebokosc swiata (Z) */
#define GROUND 10          /* poziom gruntu kampusu */
#define EYE_HEIGHT 1.62f
#define PLAYER_HW  0.30f   /* polowa szerokosci gracza */
#define PLAYER_H   1.80f   /* wysokosc gracza */
#define MAX_REACH  6.0f    /* zasieg reki */
#define MAX_HP     20      /* 10 serc po 2 HP */
#define MAX_BREATH 10.0f   /* sekundy powietrza pod woda */

enum { B_AIR = 0, B_GRASS, B_DIRT, B_STONE, B_SAND, B_WOOD, B_LEAVES, B_WATER,
       B_BRICK, B_PLASTER, B_ROOF, B_WINDOW, B_PAVING };

static unsigned char world[WX][WY][WZ];
static int heightMap[WX][WZ];
static unsigned int gSeed = 1337u;

/* ---------------- pomocnicze ---------------- */

static int inBounds(int x, int y, int z) {
    return x >= 0 && x < WX && y >= 0 && y < WY && z >= 0 && z < WZ;
}
static unsigned char getBlock(int x, int y, int z) {
    return inBounds(x, y, z) ? world[x][y][z] : B_AIR;
}
static int isSolid(unsigned char b) { return b != B_AIR && b != B_WATER; }

static float wrapPi(float a) {
    while (a >  PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

static float hash2f(int x, int z) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)z * 668265263u
                   + gSeed * 2654435761u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFu) / 65535.0f;
}
static float hash3f(int x, int y, int z) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 3266489917u
                   + (unsigned int)z * 668265263u + gSeed;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 1023u) / 1023.0f;
}

/* ---------------- generacja kampusu PW ---------------- */

static void paveRect(int x0, int x1, int z0, int z1, unsigned char b) {
    for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++)
        if (inBounds(x, GROUND, z)) world[x][GROUND][z] = b;
}

static void plantTree(int x, int z) {
    if (x < 2 || x >= WX - 2 || z < 2 || z >= WZ - 2) return;
    int h = heightMap[x][z];
    if (world[x][h][z] != B_GRASS) return;
    int trunk = 4 + (int)(hash2f(x, z) * 2.0f);   /* pien 4-5 blokow */
    if (h + trunk + 3 >= WY) return;
    for (int t = 1; t <= trunk; t++) world[x][h + t][z] = B_WOOD;
    for (int k = 0; k < 4; k++) {                 /* korona z lisci */
        int ly = h + trunk - 1 + k;
        int r = (k < 2) ? 2 : 1;
        for (int dx = -r; dx <= r; dx++)
        for (int dz = -r; dz <= r; dz++) {
            if (abs(dx) == r && abs(dz) == r && hash3f(x + dx, ly, z + dz) < 0.5f) continue;
            int lx = x + dx, lz = z + dz;
            if (!inBounds(lx, ly, lz)) continue;
            if (world[lx][ly][lz] == B_AIR) world[lx][ly][lz] = B_LEAVES;
        }
    }
}

/* mini-czcionka 3x5 do napisow z blokow (bit2 = lewa kolumna) */
static const unsigned char GLYPHS[][5] = {
    /*A*/{2,5,7,5,5}, /*C*/{7,4,4,4,7}, /*E*/{7,4,7,4,7}, /*H*/{5,5,7,5,5},
    /*I*/{7,2,2,2,7}, /*K*/{5,5,6,5,5}, /*L*/{4,4,4,4,7}, /*N*/{5,7,7,5,5},
    /*O*/{7,5,5,5,7}, /*P*/{7,5,7,4,4}, /*T*/{7,2,2,2,2}, /*W*/{5,5,5,7,5}
};
static const char GLYPH_KEYS[] = "ACEHIKLNOPTW";

static const unsigned char *glyph(char c) {
    for (int i = 0; GLYPH_KEYS[i]; i++)
        if (GLYPH_KEYS[i] == c) return GLYPHS[i];
    return NULL;
}

/* wpisuje tekst w pionowa sciane na plaszczyznie Z (blokami B_ROOF) */
static void carveText(const char *s, int xStart, int yTop, int z) {
    for (int i = 0; s[i]; i++) {
        const unsigned char *g = glyph(s[i]);
        if (!g) continue;
        for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++) {
            if (!(g[row] & (4 >> col))) continue;
            int x = xStart + i * 4 + col, y = yTop - row;
            if (inBounds(x, y, z)) world[x][y][z] = B_ROOF;
        }
    }
}

/* Gmach Glowny Politechniki Warszawskiej (stylizowany) */
static void buildGmachPW(void) {
    const int X0 = 18, X1 = 77;       /* obrys budynku */
    const int Z0 = 44, Z1 = 83;
    const int CX0 = 28, CX1 = 67;     /* dziedziniec wewnetrzny */
    const int CZ0 = 54, CZ1 = 73;
    const int Y0 = GROUND + 1, YT = GROUND + 13, YR = GROUND + 14;

    /* korpus: sciany zewnetrzne + sciany dziedzinca, w srodku stropy */
    for (int x = X0; x <= X1; x++)
    for (int z = Z0; z <= Z1; z++) {
        world[x][GROUND][z] = B_PAVING;                       /* posadzka */
        int inCourt = (x >= CX0 && x <= CX1 && z >= CZ0 && z <= CZ1);
        if (inCourt) continue;
        int innerWall = (x >= CX0 - 1 && x <= CX1 + 1 && z >= CZ0 - 1 && z <= CZ1 + 1);
        int outerWall = (x == X0 || x == X1 || z == Z0 || z == Z1);
        if (outerWall || innerWall) {
            int corner = ((x == X0 || x == X1) && (z == Z0 || z == Z1));
            for (int y = Y0; y <= YT; y++)
                world[x][y][z] = corner ? B_BRICK : B_PLASTER;
            if (outerWall) world[x][Y0][z] = B_STONE;         /* cokol */
        } else {
            world[x][GROUND + 5][z] = B_WOOD;                 /* stropy pieter */
            world[x][GROUND + 9][z] = B_WOOD;
        }
        world[x][YR][z] = B_ROOF;                             /* dach skrzydel */
    }

    /* okna scian zewnetrznych: dwa rzedy (parter, pietro), rytm co 4 bloki */
    for (int x = X0 + 2; x <= X1 - 2; x++) {
        if (((x - (X0 + 2)) % 4) >= 2) continue;
        for (int r = 0; r < 2; r++)
        for (int y = GROUND + 2 + r * 4; y <= GROUND + 4 + r * 4; y++) {
            if (!(x >= 43 && x <= 52)) world[x][y][Z0] = B_WINDOW;   /* front (bez strefy wejscia) */
            world[x][y][Z1] = B_WINDOW;                              /* tyl */
        }
    }
    for (int z = Z0 + 2; z <= Z1 - 2; z++) {
        if (((z - (Z0 + 2)) % 4) >= 2) continue;
        for (int r = 0; r < 2; r++)
        for (int y = GROUND + 2 + r * 4; y <= GROUND + 4 + r * 4; y++) {
            world[X0][y][z] = B_WINDOW;
            world[X1][y][z] = B_WINDOW;
        }
    }
    /* okna scian dziedzinca */
    for (int x = CX0 + 1; x <= CX1 - 1; x++) {
        if (((x - (CX0 + 1)) % 4) >= 2) continue;
        for (int r = 0; r < 2; r++)
        for (int y = GROUND + 2 + r * 4; y <= GROUND + 4 + r * 4; y++) {
            world[x][y][CZ0 - 1] = B_WINDOW;
            world[x][y][CZ1 + 1] = B_WINDOW;
        }
    }
    for (int z = CZ0 + 1; z <= CZ1 - 1; z++) {
        if (((z - (CZ0 + 1)) % 4) >= 2) continue;
        for (int r = 0; r < 2; r++)
        for (int y = GROUND + 2 + r * 4; y <= GROUND + 4 + r * 4; y++) {
            world[CX0 - 1][y][z] = B_WINDOW;
            world[CX1 + 1][y][z] = B_WINDOW;
        }
    }

    /* wejscie glowne: przejscie przez poludniowe skrzydlo na dziedziniec */
    for (int x = 45; x <= 50; x++)
    for (int y = Y0; y <= GROUND + 5; y++)
    for (int z = Z0; z <= CZ0 - 1; z++)
        world[x][y][z] = B_AIR;
    for (int y = Y0; y <= GROUND + 6; y++) {                  /* ceglana rama portalu */
        world[44][y][Z0] = B_BRICK;
        world[51][y][Z0] = B_BRICK;
    }
    for (int x = 44; x <= 51; x++) world[x][GROUND + 6][Z0] = B_BRICK;

    /* portyk: 4 kolumny + daszek przed wejsciem */
    for (int i = 0; i < 4; i++) {
        int cx = 43 + i * 3;
        for (int y = Y0; y <= GROUND + 5; y++) world[cx][y][41] = B_PLASTER;
    }
    for (int x = 42; x <= 53; x++)
    for (int z = 41; z <= 43; z++)
        world[x][GROUND + 6][z] = B_ROOF;

    /* napis na attyce frontu */
    carveText("POLITECHNIKA", 24, GROUND + 13, Z0);

    /* szklany dach nad dziedzincem (schodkowa kopula) */
    for (int x = CX0; x <= CX1; x++)
    for (int z = CZ0; z <= CZ1; z++) {
        int d = x - CX0;
        if (CX1 - x < d) d = CX1 - x;
        if (z - CZ0 < d) d = z - CZ0;
        if (CZ1 - z < d) d = CZ1 - z;
        int y = YR + (d >= 2 ? 1 : 0) + (d >= 5 ? 1 : 0);
        world[x][y][z] = B_WINDOW;
        if (d == 2 || d == 5) world[x][y - 1][z] = B_WINDOW;  /* pionowe stopnie kopuly */
    }

    /* zielen na dziedzincu */
    world[CX0 + 3][GROUND + 1][CZ0 + 3] = B_LEAVES;
    world[CX1 - 3][GROUND + 1][CZ0 + 3] = B_LEAVES;
    world[CX0 + 3][GROUND + 1][CZ1 - 3] = B_LEAVES;
    world[CX1 - 3][GROUND + 1][CZ1 - 3] = B_LEAVES;
    for (int x = 47; x <= 48; x++)
    for (int z = 63; z <= 64; z++)
        world[x][GROUND + 1][z] = B_LEAVES;
}

static void genWorld(void) {
    /* plaski teren kampusu */
    for (int x = 0; x < WX; x++)
    for (int z = 0; z < WZ; z++) {
        heightMap[x][z] = GROUND;
        for (int y = 0; y <= GROUND; y++) {
            unsigned char b;
            if (y == GROUND)          b = B_GRASS;
            else if (y >= GROUND - 2) b = B_DIRT;
            else                      b = B_STONE;
            world[x][y][z] = b;
        }
    }

    /* Plac Politechniki + aleja dojsciowa */
    paveRect(16, 79, 26, 43, B_PAVING);
    paveRect(43, 52, 0, 25, B_PAVING);

    /* fontanna na placu */
    for (int dx = -3; dx <= 3; dx++)
    for (int dz = -3; dz <= 3; dz++) {
        int x = 48 + dx, z = 34 + dz;
        int m = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
        world[x][GROUND + 1][z] = (m == 3) ? B_STONE : B_WATER;
    }

    buildGmachPW();

    /* aleja drzew wzdluz dojscia */
    for (int z = 5; z <= 23; z += 6) {
        plantTree(40, z);
        plantTree(55, z);
    }
    /* losowe drzewa poza kampusem */
    for (int x = 2; x < WX - 2; x++)
    for (int z = 2; z < WZ - 2; z++) {
        if (x >= 12 && x <= 83 && z >= 20 && z <= 88) continue;
        if (world[x][GROUND][z] != B_GRASS) continue;
        if (hash2f(x * 5 + 17, z * 7 + 11) < 0.02f) plantTree(x, z);
    }
}

/* ---------------- budowanie siatki swiata ---------------- */

typedef struct {
    float *verts;
    unsigned char *cols;
    int count, cap;
} MeshData;

static void mdPush(MeshData *m, float x, float y, float z, Color c) {
    if (m->count >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 8192;
        m->verts = (float *)realloc(m->verts, (size_t)m->cap * 3 * sizeof(float));
        m->cols  = (unsigned char *)realloc(m->cols, (size_t)m->cap * 4);
    }
    m->verts[m->count * 3 + 0] = x;
    m->verts[m->count * 3 + 1] = y;
    m->verts[m->count * 3 + 2] = z;
    m->cols[m->count * 4 + 0] = c.r;
    m->cols[m->count * 4 + 1] = c.g;
    m->cols[m->count * 4 + 2] = c.b;
    m->cols[m->count * 4 + 3] = c.a;
    m->count++;
}

/* rogi scian w kolejnosci CCW patrzac z zewnatrz (winding pod backface culling) */
static const float FACE_CORNERS[6][4][3] = {
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},   /* +Y gora  */
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},   /* -Y dol   */
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},   /* +X       */
    {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},   /* -X       */
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},   /* +Z       */
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}},   /* -Z       */
};
static const int FACE_DIR[6][3] = {
    {0,1,0},{0,-1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}
};
/* proste "oswietlenie": rozna jasnosc kazdej sciany */
static const float FACE_SHADE[6] = { 1.00f, 0.45f, 0.80f, 0.74f, 0.62f, 0.58f };

static Color baseColor(unsigned char b, int face) {
    switch (b) {
        case B_GRASS:   return (face == 0) ? (Color){ 96, 180,  60, 255 }
                                           : (Color){ 125,  92,  62, 255 };
        case B_DIRT:    return (Color){ 125,  92,  62, 255 };
        case B_STONE:   return (Color){ 128, 128, 132, 255 };
        case B_SAND:    return (Color){ 222, 207, 160, 255 };
        case B_WOOD:    return (Color){  98,  74,  46, 255 };
        case B_LEAVES:  return (Color){  52, 128,  44, 255 };
        case B_WATER:   return (Color){  42,  96, 195, 150 };
        case B_BRICK:   return (Color){ 158,  84,  58, 255 };
        case B_PLASTER: return (Color){ 212, 198, 168, 255 };
        case B_ROOF:    return (Color){  66,  68,  76, 255 };
        case B_WINDOW:  return (Color){ 150, 196, 226, 255 };
        case B_PAVING:  return (Color){ 168, 165, 158, 255 };
        default:        return MAGENTA;
    }
}

static const char *blockName(unsigned char b) {
    switch (b) {
        case B_GRASS:   return "Trawa";
        case B_DIRT:    return "Ziemia";
        case B_STONE:   return "Kamien";
        case B_SAND:    return "Piasek";
        case B_WOOD:    return "Drewno";
        case B_LEAVES:  return "Liscie";
        case B_BRICK:   return "Cegla";
        case B_PLASTER: return "Tynk";
        case B_ROOF:    return "Lupek (dach)";
        case B_WINDOW:  return "Szklo";
        case B_PAVING:  return "Bruk";
        default:        return "?";
    }
}

static Color shadeColor(Color c, float f) {
    int r = (int)(c.r * f), g = (int)(c.g * f), b = (int)(c.b * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, c.a };
}

static void pushFace(MeshData *m, int x, int y, int z, int face, Color c, float topY) {
    float px[4], py[4], pz[4];
    for (int i = 0; i < 4; i++) {
        px[i] = x + FACE_CORNERS[face][i][0];
        py[i] = y + (FACE_CORNERS[face][i][1] > 0.5f ? topY : 0.0f);
        pz[i] = z + FACE_CORNERS[face][i][2];
    }
    mdPush(m, px[0], py[0], pz[0], c);
    mdPush(m, px[1], py[1], pz[1], c);
    mdPush(m, px[2], py[2], pz[2], c);
    mdPush(m, px[0], py[0], pz[0], c);
    mdPush(m, px[2], py[2], pz[2], c);
    mdPush(m, px[3], py[3], pz[3], c);
}

static Model gOpaque, gWater;
static int gHasOpaque = 0, gHasWater = 0;

static Model meshDataToModel(MeshData *m) {
    Mesh mesh = { 0 };
    mesh.vertexCount = m->count;
    mesh.triangleCount = m->count / 3;
    mesh.vertices = m->verts;     /* zwalniane pozniej przez UnloadModel */
    mesh.colors = m->cols;
    UploadMesh(&mesh, false);
    return LoadModelFromMesh(mesh);
}

static void rebuildWorldModels(void) {
    if (gHasOpaque) UnloadModel(gOpaque);
    if (gHasWater)  UnloadModel(gWater);
    gHasOpaque = gHasWater = 0;

    MeshData op = { 0 }, wa = { 0 };
    for (int x = 0; x < WX; x++)
    for (int y = 0; y < WY; y++)
    for (int z = 0; z < WZ; z++) {
        unsigned char b = world[x][y][z];
        if (b == B_AIR) continue;

        if (b == B_WATER) {
            /* powierzchnia wody lekko obnizona */
            if (getBlock(x, y + 1, z) == B_AIR)
                pushFace(&wa, x, y, z, 0, baseColor(B_WATER, 0), 0.88f);
            for (int f = 2; f < 6; f++)
                if (getBlock(x + FACE_DIR[f][0], y, z + FACE_DIR[f][2]) == B_AIR)
                    pushFace(&wa, x, y, z, f,
                             shadeColor(baseColor(B_WATER, f), FACE_SHADE[f]), 0.88f);
            continue;
        }

        for (int f = 0; f < 6; f++) {
            unsigned char n = getBlock(x + FACE_DIR[f][0], y + FACE_DIR[f][1], z + FACE_DIR[f][2]);
            if (isSolid(n)) continue;   /* sciana zasloniete - pomijamy */
            float var = 0.88f + 0.18f * hash3f(x, y, z);   /* delikatna "tekstura" */
            Color c = shadeColor(baseColor(b, f), FACE_SHADE[f] * var);
            pushFace(&op, x, y, z, f, c, 1.0f);
        }
    }

    if (op.count > 0) { gOpaque = meshDataToModel(&op); gHasOpaque = 1; }
    else { free(op.verts); free(op.cols); }
    if (wa.count > 0) { gWater = meshDataToModel(&wa); gHasWater = 1; }
    else { free(wa.verts); free(wa.cols); }
}

/* ---------------- fizyka / kolizje ---------------- */

static int boxCollides(Vector3 feet) {
    int x0 = (int)floorf(feet.x - PLAYER_HW), x1 = (int)floorf(feet.x + PLAYER_HW);
    int y0 = (int)floorf(feet.y),             y1 = (int)floorf(feet.y + PLAYER_H - 0.001f);
    int z0 = (int)floorf(feet.z - PLAYER_HW), z1 = (int)floorf(feet.z + PLAYER_HW);
    for (int x = x0; x <= x1; x++)
    for (int y = y0; y <= y1; y++)
    for (int z = z0; z <= z1; z++)
        if (isSolid(getBlock(x, y, z))) return 1;
    return 0;
}

/* raycast po siatce (algorytm DDA / Amanatides-Woo) */
static int raycast(Vector3 ro, Vector3 rd, float maxDist, int hit[3], int prev[3]) {
    int x = (int)floorf(ro.x), y = (int)floorf(ro.y), z = (int)floorf(ro.z);
    prev[0] = x; prev[1] = y; prev[2] = z;
    int sx = (rd.x > 0) - (rd.x < 0);
    int sy = (rd.y > 0) - (rd.y < 0);
    int sz = (rd.z > 0) - (rd.z < 0);
    float tdx = (rd.x != 0.0f) ? fabsf(1.0f / rd.x) : 1e30f;
    float tdy = (rd.y != 0.0f) ? fabsf(1.0f / rd.y) : 1e30f;
    float tdz = (rd.z != 0.0f) ? fabsf(1.0f / rd.z) : 1e30f;
    float tmx = (rd.x > 0) ? ((float)(x + 1) - ro.x) * tdx : ((rd.x < 0) ? (ro.x - (float)x) * tdx : 1e30f);
    float tmy = (rd.y > 0) ? ((float)(y + 1) - ro.y) * tdy : ((rd.y < 0) ? (ro.y - (float)y) * tdy : 1e30f);
    float tmz = (rd.z > 0) ? ((float)(z + 1) - ro.z) * tdz : ((rd.z < 0) ? (ro.z - (float)z) * tdz : 1e30f);

    for (int i = 0; i < 128; i++) {
        if (isSolid(getBlock(x, y, z))) { hit[0] = x; hit[1] = y; hit[2] = z; return 1; }
        prev[0] = x; prev[1] = y; prev[2] = z;
        if (tmx < tmy && tmx < tmz) { if (tmx > maxDist) return 0; x += sx; tmx += tdx; }
        else if (tmy < tmz)         { if (tmy > maxDist) return 0; y += sy; tmy += tdy; }
        else                        { if (tmz > maxDist) return 0; z += sz; tmz += tdz; }
    }
    return 0;
}

/* trzecia osoba: przyciecie kamery, zeby nie wchodzila w bloki */
static float camClampDist(Vector3 o, Vector3 d, float want) {
    for (float t = 0.2f; t < want; t += 0.05f) {
        Vector3 p = { o.x + d.x * t, o.y + d.y * t, o.z + d.z * t };
        if (isSolid(getBlock((int)floorf(p.x), (int)floorf(p.y), (int)floorf(p.z)))) {
            t -= 0.30f;
            return (t < 0.5f) ? 0.5f : t;
        }
    }
    return want;
}

/* ---------------- zdrowie ---------------- */

static int gHp = MAX_HP, gDead = 0;
static float gHurt = 0.0f, gBreath = MAX_BREATH, gLastDmg = -100.0f, gNow = 0.0f;

static void hurt(int d) {
    if (gDead || d <= 0) return;
    gHp -= d;
    gHurt = 0.5f;
    gLastDmg = gNow;
    if (gHp <= 0) { gHp = 0; gDead = 1; }
}

/* ---------------- dzwieki (syntezowane, bez plikow) ---------------- */

static Sound sndBreak, sndPlace, sndStep, sndJump, sndHello;
static unsigned int sndRngState = 0x12345u;

static float sndRandf(void) {   /* bialy szum -1..1 */
    sndRngState = sndRngState * 1664525u + 1013904223u;
    return (float)((sndRngState >> 8) & 0xFFFFu) / 32767.5f - 1.0f;
}

static Sound soundFromSamples(short *d, int n) {
    Wave w = { 0 };
    w.frameCount = (unsigned int)n;
    w.sampleRate = 22050;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = d;
    return LoadSoundFromWave(w);   /* raylib kopiuje probki */
}

static void initSounds(void) {
    const int sr = 22050;
    int n;
    short *d;
    float lp, ph;

    /* niszczenie bloku: "chrupniecie" - szum z opadajacym filtrem */
    n = (int)(sr * 0.20f);
    d = (short *)malloc((size_t)n * sizeof(short));
    lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        lp += (0.65f - 0.50f * t) * (sndRandf() - lp);
        d[i] = (short)(lp * (1 - t) * (1 - t) * 26000);
    }
    sndBreak = soundFromSamples(d, n);
    free(d);

    /* stawianie bloku: krotki tepy klik */
    n = (int)(sr * 0.09f);
    d = (short *)malloc((size_t)n * sizeof(short));
    lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        lp += 0.45f * (sndRandf() - lp);
        d[i] = (short)(lp * (1 - t) * (1 - t) * 22000);
    }
    sndPlace = soundFromSamples(d, n);
    free(d);

    /* krok: cichy, niski tup */
    n = (int)(sr * 0.075f);
    d = (short *)malloc((size_t)n * sizeof(short));
    lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float att = (t < 0.06f) ? t / 0.06f : 1.0f;
        lp += 0.16f * (sndRandf() - lp);
        d[i] = (short)(lp * att * (1 - t) * (1 - t) * 24000);
    }
    sndStep = soundFromSamples(d, n);
    free(d);

    /* skok: krotki "swish" w gore */
    n = (int)(sr * 0.13f);
    d = (short *)malloc((size_t)n * sizeof(short));
    lp = 0;
    ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float f = 150.0f + 230.0f * t;
        ph += 2.0f * PI * f / sr;
        lp += 0.35f * (sndRandf() - lp);
        d[i] = (short)((sinf(ph) * 0.65f + lp * 0.5f) * sinf(PI * t) * 9500);
    }
    sndJump = soundFromSamples(d, n);
    free(d);

    /* powitanie: cieple "dzyn-dzyn" (E5 -> C5) */
    n = (int)(sr * 0.55f);
    d = (short *)malloc((size_t)n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float ts = (float)i / sr;
        float s = 0;
        if (ts < 0.30f)
            s += (sinf(2 * PI * 659.25f * ts) + 0.3f * sinf(2 * PI * 1318.5f * ts)) * expf(-ts * 9.0f);
        if (ts >= 0.18f) {
            float u = ts - 0.18f;
            s += (sinf(2 * PI * 523.25f * u) + 0.3f * sinf(2 * PI * 1046.5f * u)) * expf(-u * 7.0f);
        }
        float v = s * 8500.0f;
        if (v >  32000.0f) v =  32000.0f;
        if (v < -32000.0f) v = -32000.0f;
        d[i] = (short)v;
    }
    sndHello = soundFromSamples(d, n);
    free(d);

    SetSoundVolume(sndBreak, 0.8f);
    SetSoundVolume(sndPlace, 0.6f);
    SetSoundVolume(sndStep, 0.4f);
    SetSoundVolume(sndJump, 0.5f);
    SetSoundVolume(sndHello, 0.8f);
}

static void unloadSounds(void) {
    UnloadSound(sndBreak);
    UnloadSound(sndPlace);
    UnloadSound(sndStep);
    UnloadSound(sndJump);
    UnloadSound(sndHello);
}

/* ---------------- model gracza (lysy czlowiek) ---------------- */

static void drawPlayer(Vector3 feet, float yaw, float pitch, float swing) {
    Color skin  = { 236, 188, 150, 255 };
    Color skin2 = { 214, 165, 130, 255 };
    Color shirt = {  58, 118, 196, 255 };
    Color pants = {  64,  66,  82, 255 };
    Color shoe  = {  45,  45,  50, 255 };
    Color pupil = {  60,  60, 120, 255 };
    Color mouth = { 150, 100,  90, 255 };

    rlPushMatrix();
    rlTranslatef(feet.x, feet.y, feet.z);
    rlRotatef(90.0f - yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);   /* model +Z = kierunek patrzenia */

    /* tulow */
    DrawCube((Vector3){ 0.0f, 1.125f, 0.0f }, 0.50f, 0.75f, 0.25f, shirt);

    /* glowa - lysa (sama skora, bez wlosow) */
    rlPushMatrix();
    rlTranslatef(0.0f, 1.50f, 0.0f);
    rlRotatef(-pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, 0.25f, 0.0f }, 0.50f, 0.50f, 0.50f, skin);
    DrawCube((Vector3){ -0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);   /* oczy */
    DrawCube((Vector3){  0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);
    DrawCube((Vector3){ -0.08f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){  0.12f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){  0.00f, 0.20f, 0.258f }, 0.08f, 0.08f, 0.03f, skin2);   /* nos */
    DrawCube((Vector3){  0.00f, 0.10f, 0.252f }, 0.16f, 0.04f, 0.02f, mouth);   /* usta */
    rlPopMatrix();

    /* rece i nogi z wahadlowa animacja chodu */
    for (int s = -1; s <= 1; s += 2) {
        rlPushMatrix();                                   /* ramie */
        rlTranslatef(s * 0.375f, 1.42f, 0.0f);
        rlRotatef(s * swing, 1.0f, 0.0f, 0.0f);
        DrawCube((Vector3){ 0.0f, -0.28f, 0.0f }, 0.24f, 0.68f, 0.24f, skin);
        DrawCube((Vector3){ 0.0f, -0.05f, 0.0f }, 0.27f, 0.26f, 0.27f, shirt);  /* rekawek */
        rlPopMatrix();

        rlPushMatrix();                                   /* noga */
        rlTranslatef(s * 0.125f, 0.78f, 0.0f);
        rlRotatef(-s * swing, 1.0f, 0.0f, 0.0f);
        DrawCube((Vector3){ 0.0f, -0.39f, 0.0f }, 0.24f, 0.78f, 0.24f, pants);
        DrawCube((Vector3){ 0.0f, -0.70f, 0.01f }, 0.26f, 0.16f, 0.28f, shoe);
        rlPopMatrix();
    }
    rlPopMatrix();
}

/* ---------------- NPC: Jan Pawel II ---------------- */

static const Vector3 NPC_POS = { 53.5f, (float)(GROUND + 1), 39.5f };  /* przy wejsciu do Gmachu */
#define NPC_YAW (-PI / 2.0f)   /* cialem zwrocony na poludnie, w strone placu */

static void drawNPC(float headYawDeg, float headPitchDeg, float waveDeg) {
    Color robe  = { 250, 250, 247, 255 };
    Color cape  = { 233, 233, 228, 255 };
    Color capC  = { 255, 253, 244, 255 };
    Color skin  = { 240, 204, 173, 255 };
    Color skin2 = { 220, 176, 146, 255 };
    Color hair  = { 226, 226, 223, 255 };
    Color gold  = { 218, 180,  62, 255 };
    Color pupil = {  70,  80, 110, 255 };
    Color mouth = { 168, 112,  96, 255 };
    Color shoe  = { 122,  44,  38, 255 };

    rlPushMatrix();
    rlTranslatef(NPC_POS.x, NPC_POS.y, NPC_POS.z);
    rlRotatef(90.0f - NPC_YAW * RAD2DEG, 0.0f, 1.0f, 0.0f);

    /* sutanna + buty */
    DrawCube((Vector3){ 0.0f, 0.45f, 0.0f }, 0.52f, 0.90f, 0.30f, robe);
    DrawCube((Vector3){ -0.12f, 0.04f, 0.12f }, 0.16f, 0.08f, 0.16f, shoe);
    DrawCube((Vector3){  0.12f, 0.04f, 0.12f }, 0.16f, 0.08f, 0.16f, shoe);
    /* tors + pelerynka */
    DrawCube((Vector3){ 0.0f, 1.10f, 0.0f }, 0.52f, 0.55f, 0.28f, robe);
    DrawCube((Vector3){ 0.0f, 1.31f, 0.0f }, 0.56f, 0.24f, 0.32f, cape);
    /* zloty krzyz na piersi */
    DrawCube((Vector3){ 0.0f, 1.05f, 0.155f }, 0.05f, 0.20f, 0.02f, gold);
    DrawCube((Vector3){ 0.0f, 1.10f, 0.155f }, 0.13f, 0.05f, 0.02f, gold);

    /* glowa - rusza sie niezaleznie od ciala */
    rlPushMatrix();
    rlTranslatef(0.0f, 1.52f, 0.0f);
    rlRotatef(headYawDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(headPitchDeg, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, 0.25f, 0.0f }, 0.50f, 0.50f, 0.50f, skin);
    DrawCube((Vector3){ 0.0f, 0.515f, 0.0f }, 0.52f, 0.07f, 0.52f, capC);    /* piuska */
    DrawCube((Vector3){ -0.26f, 0.30f, 0.0f }, 0.03f, 0.20f, 0.42f, hair);   /* siwe wlosy */
    DrawCube((Vector3){  0.26f, 0.30f, 0.0f }, 0.03f, 0.20f, 0.42f, hair);
    DrawCube((Vector3){ -0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);
    DrawCube((Vector3){  0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);
    DrawCube((Vector3){ -0.09f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){  0.11f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){ -0.10f, 0.36f, 0.252f }, 0.12f, 0.03f, 0.02f, hair); /* brwi */
    DrawCube((Vector3){  0.10f, 0.36f, 0.252f }, 0.12f, 0.03f, 0.02f, hair);
    DrawCube((Vector3){  0.00f, 0.20f, 0.258f }, 0.08f, 0.09f, 0.03f, skin2);
    DrawCube((Vector3){  0.00f, 0.10f, 0.252f }, 0.15f, 0.04f, 0.02f, mouth);
    rlPopMatrix();

    /* lewa reka - zwisa, delikatnie sie kolysze */
    rlPushMatrix();
    rlTranslatef(-0.37f, 1.38f, 0.0f);
    rlRotatef(sinf(gNow * 1.3f) * 3.0f, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, -0.26f, 0.0f }, 0.24f, 0.62f, 0.24f, robe);
    DrawCube((Vector3){ 0.0f, -0.60f, 0.0f }, 0.15f, 0.12f, 0.15f, skin);
    rlPopMatrix();

    /* prawa reka - macha na powitanie */
    rlPushMatrix();
    rlTranslatef(0.37f, 1.38f, 0.0f);
    rlRotatef(waveDeg, 0.0f, 0.0f, 1.0f);
    rlRotatef(sinf(gNow * 1.1f) * 3.0f, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, -0.26f, 0.0f }, 0.24f, 0.62f, 0.24f, robe);
    DrawCube((Vector3){ 0.0f, -0.60f, 0.0f }, 0.15f, 0.12f, 0.15f, skin);
    rlPopMatrix();

    rlPopMatrix();
}

/* ---------------- HUD: ikony, serca, pasek ---------------- */

/* pseudo-izometryczna kostka bloku (gora + dwie sciany) */
static void drawItemIcon(float x, float y, float s, unsigned char b) {
    Color ct = shadeColor(baseColor(b, 0), 1.00f);
    Color cl = shadeColor(baseColor(b, 4), 0.62f);
    Color cr = shadeColor(baseColor(b, 2), 0.82f);
    Vector2 T  = { x + s * 0.5f, y };
    Vector2 L  = { x,            y + s * 0.25f };
    Vector2 R  = { x + s,        y + s * 0.25f };
    Vector2 M  = { x + s * 0.5f, y + s * 0.5f };
    Vector2 BL = { x,            y + s * 0.75f };
    Vector2 BR = { x + s,        y + s * 0.75f };
    Vector2 B  = { x + s * 0.5f, y + s };
    DrawTriangle(T, L, M, ct);  DrawTriangle(T, M, R, ct);    /* gora  */
    DrawTriangle(L, BL, B, cl); DrawTriangle(L, B, M, cl);    /* lewa  */
    DrawTriangle(M, B, BR, cr); DrawTriangle(M, BR, R, cr);   /* prawa */
}

/* serce 7x6 pikseli; redCols = ile kolumn wypelnic czerwienia (7=pelne, 4=pol, 0=puste) */
static const unsigned char HEART_PX[6] = { 0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 };

static void drawHeart(int x, int y, int redCols) {
    for (int r = 0; r < 6; r++)
    for (int c = 0; c < 7; c++) {
        if (!(HEART_PX[r] & (0x40 >> c))) continue;
        DrawRectangle(x + c * 2 - 1, y + r * 2 - 1, 4, 4, (Color){ 20, 20, 20, 200 });
    }
    for (int r = 0; r < 6; r++)
    for (int c = 0; c < 7; c++) {
        if (!(HEART_PX[r] & (0x40 >> c))) continue;
        Color col = (c < redCols) ? (Color){ 225, 45, 45, 255 } : (Color){ 70, 70, 70, 255 };
        if (r == 1 && (c == 1 || c == 4) && c < redCols) col = (Color){ 255, 145, 145, 255 };
        DrawRectangle(x + c * 2, y + r * 2, 2, 2, col);
    }
}

/* ---------------- main ---------------- */

static const unsigned char ITEMS[] = {
    B_GRASS, B_DIRT, B_STONE, B_SAND, B_WOOD, B_LEAVES,
    B_BRICK, B_PLASTER, B_ROOF, B_WINDOW, B_PAVING
};
#define ITEM_COUNT ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

int main(void) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "MineClone - Politechnika Warszawska");
    InitAudioDevice();
    SetExitKey(KEY_NULL);
    SetTargetFPS(120);

    genWorld();
    rebuildWorldModels();
    initSounds();

    Vector3 spawn = { 48.5f, (float)(GROUND + 1), 18.5f };
    Vector3 pos = spawn;
    Vector3 vel = { 0 };
    float yaw = PI / 2.0f, pitch = 0.06f;   /* start: patrzymy na Gmach Glowny */
    int onGround = 0, fly = 0, sel = 0, captured = 1;
    int camMode = 0, invOpen = 0;
    unsigned char hotbar[9] = { B_GRASS, B_DIRT, B_STONE, B_SAND, B_WOOD,
                                B_BRICK, B_PLASTER, B_WINDOW, B_PAVING };
    float fallDist = 0, drownT = 0, regenT = 0, nameTimer = 2.5f;
    float walkPhase = 0, walkAmp = 0, stepAcc = 0;
    float npcHeadYaw = 0, npcHeadPitch = 0, npcGreetT = 0, npcWave = 0;
    const char *camNames[3] = { "1. osoba", "3. osoba (zza plecow)", "3. osoba (z przodu)" };
    DisableCursor();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        gNow = (float)GetTime();

        /* --- mysz / kursor / ekwipunek --- */
        if (IsKeyPressed(KEY_E) && !gDead) {
            invOpen = !invOpen;
            if (invOpen) { EnableCursor(); captured = 0; }
            else if (!gDead) { DisableCursor(); captured = 1; }
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (invOpen) { invOpen = 0; if (!gDead) { DisableCursor(); captured = 1; } }
            else if (captured) { EnableCursor(); captured = 0; }
        }
        int justCaptured = 0;
        if (!captured && !invOpen && !gDead && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            DisableCursor();
            captured = 1;
            justCaptured = 1;
        }
        if (gDead && captured) { EnableCursor(); captured = 0; }
        if (gDead) invOpen = 0;

        if (captured) {
            Vector2 md = GetMouseDelta();
            yaw   += md.x * 0.0030f;
            pitch -= md.y * 0.0030f;
            if (pitch >  1.55f) pitch =  1.55f;
            if (pitch < -1.55f) pitch = -1.55f;
        }
        Vector3 fwd   = { cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) };
        Vector3 front = { cosf(yaw), 0.0f, sinf(yaw) };
        Vector3 right = { -sinf(yaw), 0.0f, cosf(yaw) };

        if (IsKeyPressed(KEY_F))  { fly = !fly; vel.y = 0; }
        if (IsKeyPressed(KEY_F5)) camMode = (camMode + 1) % 3;
        for (int i = 0; i < 9; i++)
            if (IsKeyPressed(KEY_ONE + i)) { sel = i; nameTimer = 2.0f; }
        float wheel = GetMouseWheelMove();
        if (!invOpen && wheel != 0.0f) {
            sel = (sel + (wheel < 0.0f ? 1 : 8)) % 9;
            nameTimer = 2.0f;
        }

        /* --- ruch --- */
        float mx = 0, mz = 0;
        if (captured) {
            if (IsKeyDown(KEY_W)) { mx += front.x; mz += front.z; }
            if (IsKeyDown(KEY_S)) { mx -= front.x; mz -= front.z; }
            if (IsKeyDown(KEY_D)) { mx += right.x; mz += right.z; }
            if (IsKeyDown(KEY_A)) { mx -= right.x; mz -= right.z; }
        }
        float ml = sqrtf(mx * mx + mz * mz);
        if (ml > 0.001f) { mx /= ml; mz /= ml; }

        int eyeWater  = getBlock((int)floorf(pos.x), (int)floorf(pos.y + EYE_HEIGHT), (int)floorf(pos.z)) == B_WATER;
        int bodyWater = getBlock((int)floorf(pos.x), (int)floorf(pos.y + 0.5f),       (int)floorf(pos.z)) == B_WATER;

        if (fly) {
            float sp = 12.0f;
            vel.x = mx * sp;
            vel.z = mz * sp;
            vel.y = 0;
            if (captured && IsKeyDown(KEY_SPACE))        vel.y += 9.0f;
            if (captured && IsKeyDown(KEY_LEFT_CONTROL)) vel.y -= 9.0f;
        } else {
            float sp = IsKeyDown(KEY_LEFT_SHIFT) ? 7.0f : 4.5f;
            if (bodyWater) sp *= 0.6f;
            vel.x = mx * sp;
            vel.z = mz * sp;
            if (bodyWater) {                              /* plywanie */
                vel.y -= 10.0f * dt;
                if (captured && IsKeyDown(KEY_SPACE)) vel.y = 3.5f;
                if (vel.y < -3.0f) vel.y = -3.0f;
            } else {                                      /* chodzenie */
                vel.y -= 24.0f * dt;
                if (onGround && captured && IsKeyDown(KEY_SPACE)) {
                    vel.y = 8.0f;
                    SetSoundPitch(sndJump, 0.95f + GetRandomValue(0, 10) / 100.0f);
                    PlaySound(sndJump);
                }
                if (vel.y < -28.0f) vel.y = -28.0f;
            }
        }

        /* kolizje - os po osi */
        float vyApplied = vel.y;
        Vector3 np = pos;
        np.x += vel.x * dt;
        if (boxCollides(np)) { np.x = pos.x; vel.x = 0; }
        np.z += vel.z * dt;
        if (boxCollides(np)) { np.z = pos.z; vel.z = 0; }
        np.y += vel.y * dt;
        onGround = 0;
        if (boxCollides(np)) {
            if (vel.y < 0.0f) {
                np.y = floorf(np.y) + 1.0f;     /* postaw stopy rowno na bloku */
                if (boxCollides(np)) np.y = pos.y;
                onGround = 1;
            } else {
                np.y = pos.y;
            }
            vel.y = 0;
        }
        pos = np;

        /* --- obrazenia od upadku --- */
        if (fly || bodyWater) {
            fallDist = 0;
        } else if (onGround) {
            if (fallDist > 3.0f) hurt((int)(fallDist - 3.0f));
            if (fallDist > 1.2f) {                       /* tupniecie przy ladowaniu */
                SetSoundPitch(sndStep, 0.70f);
                PlaySound(sndStep);
            }
            fallDist = 0;
        } else if (vyApplied < 0.0f) {
            fallDist += -vyApplied * dt;
        }
        if (pos.y < -24.0f) hurt(100);                      /* otchlan */
        if (gDead && pos.y < -60.0f) { pos.y = -60.0f; vel.y = 0; }

        /* --- toniecie / oddech --- */
        if (eyeWater && !gDead) {
            gBreath -= dt;
            if (gBreath <= 0.0f) {
                gBreath = 0.0f;
                drownT += dt;
                if (drownT >= 1.0f) { drownT = 0; hurt(2); }
            }
        } else {
            gBreath += dt * 2.5f;
            if (gBreath > MAX_BREATH) gBreath = MAX_BREATH;
            drownT = 0;
        }

        /* --- regeneracja --- */
        if (!gDead && gHp < MAX_HP && gNow - gLastDmg > 5.0f) {
            regenT += dt;
            if (regenT >= 1.8f) { regenT = 0; gHp++; }
        }
        if (gHurt > 0.0f) gHurt -= dt;

        Vector3 eye = { pos.x, pos.y + EYE_HEIGHT, pos.z };

        /* --- animacja chodu --- */
        float hsp = sqrtf(vel.x * vel.x + vel.z * vel.z);
        walkPhase += hsp * 2.4f * dt;
        float tAmp = (hsp > 0.3f) ? hsp / 4.5f : 0.0f;
        if (tAmp > 1.2f) tAmp = 1.2f;
        walkAmp += (tAmp - walkAmp) * (dt * 8.0f > 1.0f ? 1.0f : dt * 8.0f);
        float swing = sinf(walkPhase) * 42.0f * walkAmp;

        /* --- dzwiek krokow --- */
        if (onGround && !fly && !bodyWater && hsp > 1.5f) {
            stepAcc += hsp * dt;
            if (stepAcc > 2.1f) {
                stepAcc = 0;
                SetSoundPitch(sndStep, 0.85f + GetRandomValue(0, 30) / 100.0f);
                PlaySound(sndStep);
            }
        } else if (!onGround) {
            stepAcc = 1.6f;   /* krok zaraz po wyladowaniu */
        }

        /* --- NPC: Jan Pawel II --- */
        float npcDx = pos.x - NPC_POS.x, npcDz = pos.z - NPC_POS.z;
        float npcDy = (pos.y + EYE_HEIGHT) - (NPC_POS.y + 1.77f);
        float npcDist = sqrtf(npcDx * npcDx + npcDy * npcDy + npcDz * npcDz);
        float npcTYaw, npcTPitch;
        if (npcDist < 8.0f) {                             /* patrzy na gracza */
            npcTYaw = wrapPi(atan2f(npcDz, npcDx) - NPC_YAW);
            if (npcTYaw >  1.15f) npcTYaw =  1.15f;
            if (npcTYaw < -1.15f) npcTYaw = -1.15f;
            npcTPitch = atan2f(npcDy, sqrtf(npcDx * npcDx + npcDz * npcDz));
            if (npcTPitch >  0.5f) npcTPitch =  0.5f;
            if (npcTPitch < -0.5f) npcTPitch = -0.5f;
        } else {                                          /* rozglada sie */
            npcTYaw = sinf(gNow * 0.6f) * 0.55f;
            npcTPitch = sinf(gNow * 0.83f) * 0.10f;
        }
        float npcLerp = dt * 5.0f;
        if (npcLerp > 1.0f) npcLerp = 1.0f;
        npcHeadYaw += (npcTYaw - npcHeadYaw) * npcLerp;
        npcHeadPitch += (npcTPitch - npcHeadPitch) * npcLerp;
        int npcNear = (npcDist < 4.2f) && !invOpen && !gDead;
        if (npcNear && IsKeyPressed(KEY_T) && npcGreetT <= 0.5f) {
            npcGreetT = 3.0f;
            PlaySound(sndHello);
        }
        if (npcGreetT > 0.0f) npcGreetT -= dt;
        float npcWaveT = (npcGreetT > 0.0f) ? 1.0f : 0.0f;
        npcWave += (npcWaveT - npcWave) * (dt * 7.0f > 1.0f ? 1.0f : dt * 7.0f);

        /* --- celowanie + edycja blokow --- */
        int hit[3] = { 0, 0, 0 }, prev[3] = { 0, 0, 0 };
        int hasHit = raycast(eye, fwd, MAX_REACH, hit, prev);
        if (captured && !justCaptured && hasHit && !gDead) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                world[hit[0]][hit[1]][hit[2]] = B_AIR;
                SetSoundPitch(sndBreak, 0.90f + GetRandomValue(0, 20) / 100.0f);
                PlaySound(sndBreak);
                rebuildWorldModels();
            } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && inBounds(prev[0], prev[1], prev[2])) {
                float bx = (float)prev[0], by = (float)prev[1], bz = (float)prev[2];
                int overlap = (bx < pos.x + PLAYER_HW && bx + 1 > pos.x - PLAYER_HW &&
                               by < pos.y + PLAYER_H  && by + 1 > pos.y &&
                               bz < pos.z + PLAYER_HW && bz + 1 > pos.z - PLAYER_HW);
                if (!overlap) {
                    world[prev[0]][prev[1]][prev[2]] = hotbar[sel];
                    SetSoundPitch(sndPlace, 0.90f + GetRandomValue(0, 20) / 100.0f);
                    PlaySound(sndPlace);
                    rebuildWorldModels();
                }
            }
        }

        /* --- kamera --- */
        Camera3D cam = { 0 };
        if (camMode == 0) {
            cam.position = eye;
            cam.target = (Vector3){ eye.x + fwd.x, eye.y + fwd.y, eye.z + fwd.z };
        } else {
            Vector3 dir = (camMode == 1) ? (Vector3){ -fwd.x, -fwd.y, -fwd.z } : fwd;
            float t = camClampDist(eye, dir, 4.0f);
            cam.position = (Vector3){ eye.x + dir.x * t, eye.y + dir.y * t, eye.z + dir.z * t };
            cam.target = eye;
        }
        cam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        cam.fovy = 70.0f;
        cam.projection = CAMERA_PERSPECTIVE;

        /* --- rysowanie --- */
        BeginDrawing();
        ClearBackground((Color){ 135, 190, 255, 255 });

        BeginMode3D(cam);
        if (gHasOpaque) DrawModel(gOpaque, (Vector3){ 0 }, 1.0f, WHITE);
        if (hasHit && captured)
            DrawCubeWires((Vector3){ hit[0] + 0.5f, hit[1] + 0.5f, hit[2] + 0.5f },
                          1.02f, 1.02f, 1.02f, (Color){ 0, 0, 0, 180 });
        if (camMode != 0 && !gDead) drawPlayer(pos, yaw, pitch, swing);
        drawNPC(-npcHeadYaw * RAD2DEG, -npcHeadPitch * RAD2DEG,
                npcWave * (110.0f + 8.0f * sinf(gNow * 11.0f)));
        if (gHasWater) DrawModel(gWater, (Vector3){ 0 }, 1.0f, WHITE);
        EndMode3D();

        int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (eyeWater) DrawRectangle(0, 0, sw, sh, (Color){ 30, 90, 190, 110 });
        if (gHurt > 0.0f)
            DrawRectangle(0, 0, sw, sh, (Color){ 200, 0, 0, (unsigned char)(gHurt * 160) });

        /* celownik */
        if (!invOpen && !gDead) {
            DrawRectangle(sw / 2 - 11, sh / 2 - 2, 22, 4, (Color){ 0, 0, 0, 120 });
            DrawRectangle(sw / 2 - 2, sh / 2 - 11, 4, 22, (Color){ 0, 0, 0, 120 });
            DrawRectangle(sw / 2 - 10, sh / 2 - 1, 20, 2, WHITE);
            DrawRectangle(sw / 2 - 1, sh / 2 - 10, 2, 20, WHITE);
        }

        /* --- pasek narzedzi (hotbar) --- */
        int cell = 46, hbW = cell * 9;
        int hbX = sw / 2 - hbW / 2, hbY = sh - cell - 8;
        for (int i = 0; i < 9; i++) {
            Rectangle r = { (float)(hbX + i * cell), (float)hbY, (float)cell, (float)cell };
            DrawRectangleRec(r, (Color){ 0, 0, 0, 150 });
            DrawRectangleLinesEx(r, 2, (Color){ 95, 95, 100, 230 });
            drawItemIcon(r.x + 9, r.y + 9, cell - 18, hotbar[i]);
            DrawText(TextFormat("%d", i + 1), (int)r.x + 4, (int)r.y + 3, 10, (Color){ 210, 210, 210, 200 });
        }
        DrawRectangleLinesEx((Rectangle){ (float)(hbX + sel * cell - 2), (float)(hbY - 2),
                                          (float)(cell + 4), (float)(cell + 4) }, 3, RAYWHITE);

        /* serca */
        int heartsY = hbY - 17;
        for (int i = 0; i < 10; i++) {
            int rem = gHp - i * 2;
            drawHeart(hbX + i * 16, heartsY, rem >= 2 ? 7 : (rem == 1 ? 4 : 0));
        }
        /* powietrze (babelki) gdy pod woda */
        if (gBreath < MAX_BREATH - 0.05f) {
            int bub = (int)ceilf(gBreath);
            for (int i = 0; i < 10; i++) {
                int cx = hbX + hbW - 160 + i * 16 + 7, cy = heartsY + 6;
                if (i < bub) {
                    DrawCircle(cx, cy, 6, (Color){ 170, 212, 255, 235 });
                    DrawCircleLines(cx, cy, 6, (Color){ 30, 60, 120, 255 });
                }
            }
        }
        /* nazwa wybranego przedmiotu */
        if (nameTimer > 0.0f) {
            nameTimer -= dt;
            const char *nm = blockName(hotbar[sel]);
            int tw = MeasureText(nm, 20);
            unsigned char a = (unsigned char)(nameTimer > 1.0f ? 255 : nameTimer * 255);
            DrawText(nm, sw / 2 - tw / 2, heartsY - 28, 20, (Color){ 255, 255, 255, a });
        }

        /* HUD - pomoc */
        DrawRectangle(6, 6, 660, 96, (Color){ 0, 0, 0, 110 });
        DrawText("[WSAD] ruch | [Spacja] skok | [Shift] sprint | [F] latanie | [F5] kamera | [E] ekwipunek",
                 12, 12, 17, RAYWHITE);
        DrawText("[LPM] niszcz blok | [PPM] postaw blok | [1-9]/kolko myszy: pasek | [ESC] kursor",
                 12, 33, 17, RAYWHITE);
        DrawText(TextFormat("Tryb: %s%s | Kamera: %s", fly ? "LATANIE" : "CHODZENIE",
                            bodyWater ? " (w wodzie)" : "", camNames[camMode]),
                 12, 56, 17, YELLOW);
        DrawText("Mapa: Politechnika Warszawska - Gmach Glowny", 12, 78, 17, (Color){ 180, 255, 180, 255 });
        if (!captured && !invOpen && !gDead)
            DrawText("Kliknij, aby zlapac mysz", sw / 2 - 130, sh / 2 + 34, 22, YELLOW);

        /* --- NPC: tabliczka, dymek, podpowiedz --- */
        if (!gDead) {
            Vector3 camDir = { cam.target.x - cam.position.x,
                               cam.target.y - cam.position.y,
                               cam.target.z - cam.position.z };
            Vector3 toNpc = { NPC_POS.x - cam.position.x,
                              NPC_POS.y + 1.7f - cam.position.y,
                              NPC_POS.z - cam.position.z };
            float infront = camDir.x * toNpc.x + camDir.y * toNpc.y + camDir.z * toNpc.z;
            if (infront > 0.0f && npcDist < 16.0f) {
                Vector2 tagP = GetWorldToScreen((Vector3){ NPC_POS.x, NPC_POS.y + 2.25f, NPC_POS.z }, cam);
                const char *nm = "Jan Pawel II";
                int nw = MeasureText(nm, 17);
                DrawText(nm, (int)tagP.x - nw / 2 + 1, (int)tagP.y + 1, 17, (Color){ 0, 0, 0, 180 });
                DrawText(nm, (int)tagP.x - nw / 2, (int)tagP.y, 17, RAYWHITE);
                if (npcGreetT > 0.0f) {                       /* dymek z powitaniem */
                    Vector2 bp = GetWorldToScreen((Vector3){ NPC_POS.x, NPC_POS.y + 2.75f, NPC_POS.z }, cam);
                    const char *msg = "Dzien dobry!";
                    int mw = MeasureText(msg, 20);
                    Rectangle bub = { bp.x - mw / 2.0f - 12, bp.y - 38, (float)mw + 24, 34 };
                    unsigned char a = (unsigned char)(npcGreetT > 0.5f ? 255 : npcGreetT * 510);
                    DrawRectangleRounded(bub, 0.4f, 6, (Color){ 255, 255, 255, a });
                    Vector2 b1 = { bp.x - 7, bub.y + bub.height };
                    Vector2 b2 = { bp.x + 7, bub.y + bub.height };
                    Vector2 b3 = { bp.x, bub.y + bub.height + 9 };
                    DrawTriangle(b1, b3, b2, (Color){ 255, 255, 255, a });
                    DrawText(msg, (int)(bub.x + 12), (int)(bub.y + 7), 20, (Color){ 20, 20, 20, a });
                }
            }
            if (npcNear && npcGreetT <= 0.0f) {
                const char *ht = "Nacisnij [T], aby sie przywitac";
                int hw = MeasureText(ht, 20);
                DrawText(ht, sw / 2 - hw / 2 + 1, hbY - 75, 20, (Color){ 0, 0, 0, 180 });
                DrawText(ht, sw / 2 - hw / 2, hbY - 76, 20, (Color){ 255, 245, 170, 255 });
            }
        }

        /* --- ekwipunek --- */
        if (invOpen) {
            int icell = 52, pad = 14, cols = 9;
            int rows = (ITEM_COUNT + cols - 1) / cols;
            int pw = cols * icell + pad * 2;
            int ph = pad + 30 + rows * icell + 12 + 22 + icell + 10 + 22 + pad;
            int px = sw / 2 - pw / 2, py = sh / 2 - ph / 2;
            Vector2 mp = GetMousePosition();
            const char *tip = NULL;

            DrawRectangle(px, py, pw, ph, (Color){ 25, 25, 30, 235 });
            DrawRectangleLinesEx((Rectangle){ (float)px, (float)py, (float)pw, (float)ph }, 2,
                                 (Color){ 130, 130, 140, 255 });
            DrawText("EKWIPUNEK", px + pad, py + pad, 22, RAYWHITE);

            int yy = py + pad + 30;
            for (int i = 0; i < ITEM_COUNT; i++) {
                float cx = (float)(px + pad + (i % cols) * icell);
                float cy = (float)(yy + (i / cols) * icell);
                Rectangle r = { cx + 2, cy + 2, (float)icell - 4, (float)icell - 4 };
                int hov = CheckCollisionPointRec(mp, r);
                DrawRectangleRec(r, (Color){ 55, 55, 62, 255 });
                DrawRectangleLinesEx(r, 2, hov ? RAYWHITE : (Color){ 95, 95, 105, 255 });
                drawItemIcon(cx + 11, cy + 11, (float)icell - 22, ITEMS[i]);
                if (hov) {
                    tip = blockName(ITEMS[i]);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        hotbar[sel] = ITEMS[i];
                        nameTimer = 2.0f;
                    }
                }
            }
            yy += rows * icell + 12;
            DrawText("Pasek narzedzi (kliknij slot, aby go wybrac):", px + pad, yy, 16,
                     (Color){ 200, 200, 200, 255 });
            yy += 22;
            for (int i = 0; i < 9; i++) {
                float cx = (float)(px + pad + i * icell);
                Rectangle r = { cx + 2, (float)yy + 2, (float)icell - 4, (float)icell - 4 };
                int hov = CheckCollisionPointRec(mp, r);
                DrawRectangleRec(r, (Color){ 40, 40, 46, 255 });
                DrawRectangleLinesEx(r, (i == sel) ? 3 : 2,
                                     (i == sel) ? (Color){ 255, 210, 80, 255 }
                                                : (hov ? RAYWHITE : (Color){ 95, 95, 105, 255 }));
                drawItemIcon(cx + 11, (float)yy + 11, (float)icell - 22, hotbar[i]);
                DrawText(TextFormat("%d", i + 1), (int)cx + 6, yy + 5, 10, (Color){ 210, 210, 210, 200 });
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { sel = i; nameTimer = 2.0f; }
            }
            DrawText("LPM na blok = wloz do wybranego slotu | [E]/[ESC] zamknij",
                     px + pad, py + ph - pad - 16, 14, (Color){ 170, 170, 170, 255 });
            if (tip) {
                int tw = MeasureText(tip, 18);
                DrawRectangle((int)mp.x + 12, (int)mp.y + 8, tw + 12, 24, (Color){ 0, 0, 0, 200 });
                DrawText(tip, (int)mp.x + 18, (int)mp.y + 11, 18, YELLOW);
            }
        }

        /* --- ekran smierci --- */
        if (gDead) {
            DrawRectangle(0, 0, sw, sh, (Color){ 120, 0, 0, 150 });
            const char *t1 = "NIE ZYJESZ!";
            int t1w = MeasureText(t1, 60);
            DrawText(t1, sw / 2 - t1w / 2 + 3, sh / 2 - 87, 60, (Color){ 40, 0, 0, 255 });
            DrawText(t1, sw / 2 - t1w / 2, sh / 2 - 90, 60, RAYWHITE);
            Rectangle btn = { sw / 2 - 130.0f, sh / 2 + 10.0f, 260.0f, 50.0f };
            int hov = CheckCollisionPointRec(GetMousePosition(), btn);
            DrawRectangleRec(btn, hov ? (Color){ 95, 95, 95, 255 } : (Color){ 60, 60, 60, 255 });
            DrawRectangleLinesEx(btn, 2, RAYWHITE);
            const char *t2 = "ODRODZ SIE  [R]";
            int t2w = MeasureText(t2, 24);
            DrawText(t2, (int)btn.x + (260 - t2w) / 2, (int)btn.y + 13, 24, RAYWHITE);
            if (IsKeyPressed(KEY_R) || (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                pos = spawn;
                vel = (Vector3){ 0 };
                gHp = MAX_HP;
                gDead = 0;
                gBreath = MAX_BREATH;
                gHurt = 0;
                fallDist = 0;
                drownT = 0;
                DisableCursor();
                captured = 1;
            }
        }

        DrawFPS(sw - 95, 10);
        EndDrawing();
    }

    if (gHasOpaque) UnloadModel(gOpaque);
    if (gHasWater)  UnloadModel(gWater);
    unloadSounds();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
