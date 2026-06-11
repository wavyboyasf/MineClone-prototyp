/*
 * MineClone - Politechnika Warszawska
 * Prototyp gry voxelowej w stylu Minecrafta zbudowany na raylib.
 *
 * Funkcje:
 *  - duza mapa 192x192: kampus PW (Gmach Glowny z salami zajeciowymi,
 *    Laboratorium Robotyki z ruszajacymi sie robotami), park z jeziorem,
 *    pagorki z lasami
 *  - sale zajeciowe: interaktywne krzesla (PPM = siadanie), stoliki
 *    z zeszytami, w ktorych mozna pisac (PPM = edytor)
 *  - narzedzia: kilof / siekiera / lopata / miecz - konkretne bloki
 *    wymagaja konkretnego narzedzia
 *  - pasek narzedzi, ekwipunek [E], serca, obrazenia, respawn
 *  - kamera [F5], model gracza (lysy czlowiek) z animacja chodzenia
 *  - dzwieki syntezowane + JP2: po interakcji [T] losowy 5 s fragment
 *    z pliku jp2.mp3 (lub innego *.mp3 obok exe)
 *  - NPC Glazew: czeka w losowej sali z biala kartka A3, po odkryciu
 *    zaczyna gonic gracza; mozna go pokonac mieczem
 *  - multiplayer LAN (TCP): /host i /join <ip> - wspolny swiat
 *  - czat i komendy [Enter]: /pomoc /nick /gracze /tp /heal /kill /fly ...
 *  - szklo jest transparentne
 *
 * Zbudowane na raylib (https://www.raylib.com)
 */

#include "raylib.h"
#include "rlgl.h"
#include "net.h"
#include "game.h"        /* wspolny rdzen: stale, bloki, API swiata */
#include "entities.h"    /* postacie: gracz, NPC, Glazew, roboty, koty */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Zgodnosc: funkcje obecne w MSVC, ale nie w standardzie C/POSIX (Linux/macOS). */
#ifndef _WIN32
  #include <strings.h>                 /* strcasecmp */
  #define _stricmp strcasecmp
  #define strtok_s  strtok_r           /* identyczna sygnatura jak w MSVC */
  /* strcat_s(dst, cap, src) - bezpieczne dolaczenie z limitem rozmiaru bufora */
  static void strcat_s(char *dst, size_t cap, const char *src) {
      size_t dl = strlen(dst);
      if (dl + 1 >= cap) return;
      size_t i = 0;
      while (src[i] && dl + i + 1 < cap) { dst[dl + i] = src[i]; i++; }
      dst[dl + i] = '\0';
  }
#endif

/* Stale wspolne (WX/WY/WZ, GROUND, EYE_HEIGHT, MAX_PLAYERS), bloki B_*,
   itemy IT_* i operacje OP_* sa w game.h. Ponizej tylko to, czego uzywa
   wylacznie main.c. */
#define PLAYER_HW  0.30f
#define PLAYER_H   1.80f
#define MAX_REACH  6.0f
#define MAX_HP     20
#define MAX_BREATH 10.0f
#define MAX_NOTES 160

/* wiadomosci sieciowe */
enum { M_HELLO = 1, M_WELCOME, M_WORLD, M_POS, M_BLOCK, M_CHAT, M_NICK,
       M_NOTE, M_GLZ, M_GHIT, M_APPLY, M_LEAVE, M_CMD };

unsigned char world[WX][WY][WZ];        /* deklaracja extern w game.h */
static int heightMap[WX][WZ];
static unsigned int gSeed = 1337u;

/* poziom wody na komorke: 0=brak, WATER_SRC(8)=zrodlo, 1..7=plynaca (7 najwyzsza).
   world[x][y][z]==B_WATER  <=>  gWater[x][y][z] > 0 */
#define WATER_SRC 8
static unsigned char gWater[WX][WY][WZ];

/* ---- stan lokalnego gracza (globalny, zeby komendy/siec mialy dostep) ----
   gPos/gNow/gMyId/gDead sa nie-static, bo uzywa ich modul postaci (game.h). */
Vector3 gPos;
static Vector3 gVel, gSpawn;
static float gYaw, gPitch;
static int gFly = 0, gOnGround = 0, gSitting = 0;
static int gHotbar[9] = { IT_SWORD, IT_PICKAXE, IT_AXE, IT_SHOVEL,
                          B_STONE, B_WOOD, B_PLASTER, B_WINDOW, B_PAVING };
static int gSel = 0;
static char gNick[24] = "Gracz";
int gMyId = 0;
static int gHp = MAX_HP;
int gDead = 0;
static float gHurt = 0.0f, gBreath = MAX_BREATH, gLastDmg = -100.0f;
float gNow = 0.0f;
static int gNeedRebuild = 0;

/* ---- pozostali gracze (typ Peer w game.h) ---- */
Peer gPeers[MAX_PLAYERS];

/* ---- zeszyty ---- */
typedef struct { int used, x, y, z; char text[240]; } Note;
static Note gNotes[MAX_NOTES];

/* ---- czat / komunikaty ---- */
static char gChatLog[8][160];
static float gChatT[8];
static char gToast[96];
static float gToastT = 0;

static void chatPush(const char *fmt, ...) {
    for (int i = 7; i > 0; i--) {
        memcpy(gChatLog[i], gChatLog[i - 1], 160);
        gChatT[i] = gChatT[i - 1];
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(gChatLog[0], 160, fmt, ap);
    va_end(ap);
    gChatT[0] = gNow;
}

void toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(gToast, sizeof(gToast), fmt, ap);
    va_end(ap);
    gToastT = 2.2f;
}

/* ---------------- pomocnicze ---------------- */

static int inBounds(int x, int y, int z) {
    return x >= 0 && x < WX && y >= 0 && y < WY && z >= 0 && z < WZ;
}
unsigned char getBlock(int x, int y, int z) {
    return inBounds(x, y, z) ? world[x][y][z] : B_AIR;
}
/* kolizje fizyczne */
int isSolid(unsigned char b) {
    return b != B_AIR && b != B_WATER && b != B_NOTEBOOK;
}
/* pelne, nieprzezroczyste szesciany (do chowania scian sasiadow) */
static int isOpaque(unsigned char b) {
    return isSolid(b) && b != B_WINDOW && b != B_TABLE && b != B_CHAIR;
}
/* co zatrzymuje promien celowania */
static int isRayHit(unsigned char b) {
    return b != B_AIR && b != B_WATER;
}

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
static float smoothNoise(float x, float z) {
    int xi = (int)floorf(x), zi = (int)floorf(z);
    float xf = x - xi, zf = z - zi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = zf * zf * (3.0f - 2.0f * zf);
    float a = hash2f(xi, zi),     b = hash2f(xi + 1, zi);
    float c = hash2f(xi, zi + 1), d = hash2f(xi + 1, zi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float fbm(float x, float z) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int i = 0; i < 4; i++) {
        sum += smoothNoise(x * freq, z * freq) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

/* ---------------- zeszyty ---------------- */

static Note *findNote(int x, int y, int z, int create) {
    for (int i = 0; i < MAX_NOTES; i++)
        if (gNotes[i].used && gNotes[i].x == x && gNotes[i].y == y && gNotes[i].z == z)
            return &gNotes[i];
    if (!create) return NULL;
    for (int i = 0; i < MAX_NOTES; i++)
        if (!gNotes[i].used) {
            gNotes[i].used = 1;
            gNotes[i].x = x; gNotes[i].y = y; gNotes[i].z = z;
            gNotes[i].text[0] = '\0';
            return &gNotes[i];
        }
    return NULL;
}
static void removeNote(int x, int y, int z) {
    Note *n = findNote(x, y, z, 0);
    if (n) n->used = 0;
}

/* ---------------- mini-czcionka 3x5 ---------------- */

static const unsigned char GLYPHS[][5] = {
    /*A*/{2,5,7,5,5}, /*B*/{6,5,6,5,6}, /*C*/{7,4,4,4,7}, /*E*/{7,4,7,4,7},
    /*H*/{5,5,7,5,5}, /*I*/{7,2,2,2,7}, /*K*/{5,5,6,5,5}, /*L*/{4,4,4,4,7},
    /*N*/{5,7,7,5,5}, /*O*/{7,5,5,5,7}, /*P*/{7,5,7,4,4}, /*T*/{7,2,2,2,2},
    /*W*/{5,5,5,7,5}
};
static const char GLYPH_KEYS[] = "ABCEHIKLNOPTW";

static const unsigned char *glyph(char c) {
    for (int i = 0; GLYPH_KEYS[i]; i++)
        if (GLYPH_KEYS[i] == c) return GLYPHS[i];
    return NULL;
}

/* napis w scianie rownoleglej do osi X (na plaszczyznie Z) */
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
/* napis w scianie rownoleglej do osi Z (na plaszczyznie X) */
static void carveTextZ(const char *s, int zStart, int yTop, int x) {
    for (int i = 0; s[i]; i++) {
        const unsigned char *g = glyph(s[i]);
        if (!g) continue;
        for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++) {
            if (!(g[row] & (4 >> col))) continue;
            int z = zStart + i * 4 + col, y = yTop - row;
            if (inBounds(x, y, z)) world[x][y][z] = B_ROOF;
        }
    }
}

/* ---------------- generacja kampusu PW ---------------- */

static Vector3 gClassCtr[4];   /* srodki sal zajeciowych (spawn Glazewa) */

static void paveRect(int x0, int x1, int z0, int z1, unsigned char b) {
    for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++)
        if (inBounds(x, GROUND, z)) world[x][GROUND][z] = b;
}

static void plantTree(int x, int z) {
    if (x < 2 || x >= WX - 2 || z < 2 || z >= WZ - 2) return;
    int h = heightMap[x][z];
    if (world[x][h][z] != B_GRASS) return;
    int trunk = 4 + (int)(hash2f(x, z) * 2.0f);
    if (h + trunk + 3 >= WY) return;
    for (int t = 1; t <= trunk; t++) world[x][h + t][z] = B_WOOD;
    for (int k = 0; k < 4; k++) {
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

/* sala zajeciowa: tablica, lawki (stolik+krzeslo+zeszyt) */
static void buildClassroom(int rx0, int rx1, int z0, int z1, int idx) {
    int cx = (rx0 + rx1) / 2;
    /* tablica przy poludniowej scianie */
    for (int x = cx - 2; x <= cx + 2; x++)
    for (int y = GROUND + 2; y <= GROUND + 3; y++)
        world[x][y][z0] = B_ROOF;
    /* lawki: rzad stolikow + krzesla za nimi (oparcie od +Z, twarza do tablicy) */
    for (int x = rx0 + 2; x <= rx1 - 1; x += 3) {
        world[x][GROUND + 1][z0 + 2] = B_TABLE;
        world[x][GROUND + 2][z0 + 2] = B_NOTEBOOK;
        world[x][GROUND + 1][z0 + 3] = B_CHAIR;
    }
    gClassCtr[idx] = (Vector3){ cx + 0.5f, (float)(GROUND + 1), z0 + 3.5f };
}

/* Gmach Glowny Politechniki Warszawskiej (stylizowany) */
static void buildGmachPW(void) {
    const int X0 = 66, X1 = 125;       /* obrys budynku */
    const int Z0 = 92, Z1 = 131;
    const int CX0 = 76, CX1 = 115;     /* dziedziniec wewnetrzny */
    const int CZ0 = 102, CZ1 = 121;
    const int Y0 = GROUND + 1, YT = GROUND + 13, YR = GROUND + 14;

    for (int x = X0; x <= X1; x++)
    for (int z = Z0; z <= Z1; z++) {
        world[x][GROUND][z] = B_PAVING;
        int inCourt = (x >= CX0 && x <= CX1 && z >= CZ0 && z <= CZ1);
        if (inCourt) continue;
        int innerWall = (x >= CX0 - 1 && x <= CX1 + 1 && z >= CZ0 - 1 && z <= CZ1 + 1);
        int outerWall = (x == X0 || x == X1 || z == Z0 || z == Z1);
        if (outerWall || innerWall) {
            int corner = ((x == X0 || x == X1) && (z == Z0 || z == Z1));
            for (int y = Y0; y <= YT; y++)
                world[x][y][z] = corner ? B_BRICK : B_PLASTER;
            if (outerWall) world[x][Y0][z] = B_STONE;
        } else {
            world[x][GROUND + 5][z] = B_WOOD;     /* stropy pieter */
            world[x][GROUND + 9][z] = B_WOOD;
        }
        world[x][YR][z] = B_ROOF;
    }

    /* okna zewnetrzne: dwa rzedy, rytm co 4 */
    for (int x = X0 + 2; x <= X1 - 2; x++) {
        if (((x - (X0 + 2)) % 4) >= 2) continue;
        for (int r = 0; r < 2; r++)
        for (int y = GROUND + 2 + r * 4; y <= GROUND + 4 + r * 4; y++) {
            if (!(x >= 91 && x <= 100)) world[x][y][Z0] = B_WINDOW;
            world[x][y][Z1] = B_WINDOW;
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
    /* okna dziedzinca */
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

    /* sale zajeciowe w poludniowym skrzydle (parter):
       sale z 93..97, sciana dzialowa z=98, korytarz z 99..100 */
    for (int x = X0 + 1; x <= X1 - 1; x++)
    for (int y = Y0; y <= GROUND + 4; y++) {
        if (x >= 93 && x <= 98) continue;        /* tunel wejsciowy */
        world[x][y][98] = B_PLASTER;             /* sciana sala/korytarz */
    }
    /* sciany dzialowe miedzy salami i przy tunelu */
    int parts[4] = { 80, 92, 99, 111 };
    for (int p = 0; p < 4; p++)
    for (int z = 93; z <= 98; z++)
    for (int y = Y0; y <= GROUND + 4; y++)
        world[parts[p]][y][z] = B_PLASTER;
    /* drzwi z korytarza do sal (w scianie z=98) */
    int rx0s[4] = { 67, 81, 100, 112 };
    int rx1s[4] = { 79, 91, 110, 124 };
    for (int r = 0; r < 4; r++) {
        int cx = (rx0s[r] + rx1s[r]) / 2;
        for (int y = Y0; y <= GROUND + 3; y++) {
            world[cx][y][98] = B_AIR;
            world[cx + 1][y][98] = B_AIR;
        }
        buildClassroom(rx0s[r], rx1s[r], 93, 97, r);
    }

    /* wejscie glowne: tunel przez skrzydlo az na dziedziniec */
    for (int x = 93; x <= 98; x++)
    for (int y = Y0; y <= GROUND + 5; y++)
    for (int z = Z0; z <= CZ0 - 1; z++)
        world[x][y][z] = B_AIR;
    for (int y = Y0; y <= GROUND + 6; y++) {
        world[92][y][Z0] = B_BRICK;
        world[99][y][Z0] = B_BRICK;
    }
    for (int x = 92; x <= 99; x++) world[x][GROUND + 6][Z0] = B_BRICK;

    /* portyk: 4 kolumny + daszek */
    for (int i = 0; i < 4; i++) {
        int cx = 91 + i * 3;
        for (int y = Y0; y <= GROUND + 5; y++) world[cx][y][89] = B_PLASTER;
    }
    for (int x = 90; x <= 101; x++)
    for (int z = 89; z <= 91; z++)
        world[x][GROUND + 6][z] = B_ROOF;

    carveText("POLITECHNIKA", 72, GROUND + 13, Z0);

    /* szklany dach dziedzinca (schodkowa kopula) */
    for (int x = CX0; x <= CX1; x++)
    for (int z = CZ0; z <= CZ1; z++) {
        int d = x - CX0;
        if (CX1 - x < d) d = CX1 - x;
        if (z - CZ0 < d) d = z - CZ0;
        if (CZ1 - z < d) d = CZ1 - z;
        int y = YR + (d >= 2 ? 1 : 0) + (d >= 5 ? 1 : 0);
        world[x][y][z] = B_WINDOW;
        if (d == 2 || d == 5) world[x][y - 1][z] = B_WINDOW;
    }

    /* zielen na dziedzincu */
    world[CX0 + 3][GROUND + 1][CZ0 + 3] = B_LEAVES;
    world[CX1 - 3][GROUND + 1][CZ0 + 3] = B_LEAVES;
    world[CX0 + 3][GROUND + 1][CZ1 - 3] = B_LEAVES;
    world[CX1 - 3][GROUND + 1][CZ1 - 3] = B_LEAVES;
    for (int x = 95; x <= 96; x++)
    for (int z = 111; z <= 112; z++)
        world[x][GROUND + 1][z] = B_LEAVES;
}

/* Laboratorium Robotyki: przeszklony pawilon z dwiema halami */
static void buildLab(void) {
    const int X0 = 144, X1 = 177, Z0 = 74, Z1 = 105;
    const int Y0 = GROUND + 1, YT = GROUND + 7, YR = GROUND + 8;

    for (int x = X0; x <= X1; x++)
    for (int z = Z0; z <= Z1; z++) {
        world[x][GROUND][z] = B_PAVING;
        int wall = (x == X0 || x == X1 || z == Z0 || z == Z1);
        if (wall) {
            int corner = ((x == X0 || x == X1) && (z == Z0 || z == Z1));
            for (int y = Y0; y <= YT; y++) {
                unsigned char b = corner ? B_BRICK : B_PLASTER;
                /* szerokie pasy szkla */
                if (!corner && y >= GROUND + 2 && y <= GROUND + 5) {
                    int along = (z == Z0 || z == Z1) ? x : z;
                    if ((along % 4) != 0) b = B_WINDOW;
                }
                world[x][y][z] = b;
            }
        }
        world[x][YR][z] = B_ROOF;
    }
    /* sciana dzielaca hale + drzwi */
    for (int z = Z0 + 1; z <= Z1 - 1; z++)
    for (int y = Y0; y <= YT; y++)
        world[160][y][z] = B_PLASTER;
    for (int y = Y0; y <= GROUND + 3; y++)
    for (int z = 86; z <= 88; z++) {
        world[160][y][z] = B_AIR;          /* drzwi wewnetrzne */
        world[X0][y][z] = B_AIR;           /* wejscie od placu */
    }
    for (int y = Y0; y <= GROUND + 4; y++) {
        world[X0][y][85] = B_BRICK;
        world[X0][y][89] = B_BRICK;
    }
    carveTextZ("LAB", 92, GROUND + 7, X0);

    /* stoly laboratoryjne wzdluz scian + zeszyty */
    for (int x = X0 + 2; x <= X1 - 2; x += 2) {
        if (x >= 159 && x <= 161) continue;
        world[x][GROUND + 1][Z0 + 1] = B_TABLE;
        if ((x % 6) == 0) world[x][GROUND + 2][Z0 + 1] = B_NOTEBOOK;
        world[x][GROUND + 1][Z1 - 1] = B_TABLE;
    }

    /* roboty: deterministyczne patrole (eliptyczne) - definicja w entities.c */
    robotsInitDefault();
}

static void genWorld(void) {
    /* teren: plaski kampus + pagorki na obrzezach */
    for (int x = 0; x < WX; x++)
    for (int z = 0; z < WZ; z++) {
        /* odleglosc od stref plaskich (kampus + korytarz alei) */
        int d1x = (x < 40) ? 40 - x : (x > 186 ? x - 186 : 0);
        int d1z = (z < 44) ? 44 - z : (z > 158 ? z - 158 : 0);
        int d1 = (d1x > d1z) ? d1x : d1z;
        int d2x = (x < 78) ? 78 - x : (x > 114 ? x - 114 : 0);
        int d2z = (z > 48) ? z - 48 : 0;
        int d2 = (d2x > d2z) ? d2x : d2z;
        int d = (d1 < d2) ? d1 : d2;
        float f = d / 14.0f;
        if (f > 1.0f) f = 1.0f;
        int h = GROUND + (int)((fbm(x * 0.05f + 7.0f, z * 0.05f + 3.0f) - 0.30f) * 16.0f * f);
        if (h < 6) h = 6;
        if (h > GROUND + 9) h = GROUND + 9;
        heightMap[x][z] = h;
        for (int y = 0; y <= h; y++) {
            unsigned char b;
            if (y == h)          b = B_GRASS;
            else if (y >= h - 2) b = B_DIRT;
            else                 b = B_STONE;
            world[x][y][z] = b;
        }
    }

    /* park z jeziorem (zachodnia czesc kampusu) */
    for (int x = 38; x <= 68; x++)
    for (int z = 50; z <= 80; z++) {
        float ex = (x - 52.0f) / 11.0f, ez = (z - 64.0f) / 8.0f;
        float e = ex * ex + ez * ez;
        if (e < 1.0f) {
            for (int y = 9; y <= heightMap[x][z]; y++) world[x][y][z] = B_AIR;
            world[x][8][z] = B_SAND;
            world[x][9][z] = B_WATER;
            world[x][10][z] = B_WATER;
            heightMap[x][z] = 8;
        } else if (e < 1.45f && heightMap[x][z] == GROUND) {
            world[x][GROUND][z] = B_SAND;
        }
    }

    /* place i sciezki */
    paveRect(64, 129, 74, 91, B_PAVING);     /* Plac Politechniki */
    paveRect(92, 101, 0, 73, B_PAVING);      /* aleja od poludnia */
    paveRect(130, 143, 84, 89, B_PAVING);    /* dojscie do laboratorium */
    paveRect(40, 63, 80, 85, B_PAVING);      /* sciezka do parku */

    /* fontanna */
    for (int dx = -3; dx <= 3; dx++)
    for (int dz = -3; dz <= 3; dz++) {
        int x = 96 + dx, z = 82 + dz;
        int m = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
        world[x][GROUND + 1][z] = (m == 3) ? B_STONE : B_WATER;
    }

    buildGmachPW();
    buildLab();

    /* aleja drzew */
    for (int z = 8; z <= 70; z += 7) {
        plantTree(88, z);
        plantTree(103, z);
    }
    /* gesty park */
    for (int x = 38; x < 70; x++)
    for (int z = 46; z < 110; z++) {
        if (world[x][heightMap[x][z]][z] != B_GRASS) continue;
        if (hash2f(x * 3 + 5, z * 11 + 2) < 0.035f) plantTree(x, z);
    }
    /* lasy na pagorkach i reszcie mapy */
    for (int x = 2; x < WX - 2; x++)
    for (int z = 2; z < WZ - 2; z++) {
        if (x >= 58 && x <= 186 && z >= 68 && z <= 140) continue;   /* zabudowa */
        if (x >= 84 && x <= 108 && z < 74) continue;                /* aleja */
        if (x >= 36 && x < 70 && z >= 44 && z < 112) continue;      /* park (osobno) */
        if (world[x][heightMap[x][z]][z] != B_GRASS) continue;
        if (hash2f(x * 5 + 17, z * 7 + 11) < 0.03f) plantTree(x, z);
    }
}

/* ---------------- bloki: kolory, nazwy, narzedzia ---------------- */

static const float FACE_SHADE[6] = { 1.00f, 0.45f, 0.80f, 0.74f, 0.62f, 0.58f };

static Color baseColor(unsigned char b, int face) {
    switch (b) {
        case B_GRASS:    return (face == 0) ? (Color){ 96, 180,  60, 255 }
                                            : (Color){ 125,  92,  62, 255 };
        case B_DIRT:     return (Color){ 125,  92,  62, 255 };
        case B_STONE:    return (Color){ 128, 128, 132, 255 };
        case B_SAND:     return (Color){ 222, 207, 160, 255 };
        case B_WOOD:     return (Color){  98,  74,  46, 255 };
        case B_LEAVES:   return (Color){  52, 128,  44, 255 };
        case B_WATER:    return (Color){  42,  96, 195, 150 };
        case B_BRICK:    return (Color){ 158,  84,  58, 255 };
        case B_PLASTER:  return (Color){ 212, 198, 168, 255 };
        case B_ROOF:     return (Color){  66,  68,  76, 255 };
        case B_WINDOW:   return (Color){ 165, 205, 230, 150 };   /* transparentne */
        case B_PAVING:   return (Color){ 168, 165, 158, 255 };
        case B_TABLE:    return (Color){ 142, 104,  62, 255 };
        case B_CHAIR:    return (Color){ 120,  86,  50, 255 };
        case B_NOTEBOOK: return (Color){ 245, 245, 240, 255 };
        default:         return MAGENTA;
    }
}

static const char *itemName(int it) {
    switch (it) {
        case B_GRASS:    return "Trawa";
        case B_DIRT:     return "Ziemia";
        case B_STONE:    return "Kamien";
        case B_SAND:     return "Piasek";
        case B_WOOD:     return "Drewno";
        case B_LEAVES:   return "Liscie";
        case B_WATER:    return "Woda (zrodlo)";
        case B_BRICK:    return "Cegla";
        case B_PLASTER:  return "Tynk";
        case B_ROOF:     return "Lupek (dach)";
        case B_WINDOW:   return "Szklo";
        case B_PAVING:   return "Bruk";
        case B_TABLE:    return "Stolik";
        case B_CHAIR:    return "Krzeslo";
        case B_NOTEBOOK: return "Zeszyt";
        case IT_PICKAXE: return "Kilof";
        case IT_AXE:     return "Siekiera";
        case IT_SHOVEL:  return "Lopata";
        case IT_SWORD:   return "Miecz";
        default:         return "?";
    }
}

/* jakie narzedzie rozwala dany blok (0 = reka / cokolwiek) */
static int toolFor(unsigned char b) {
    switch (b) {
        case B_STONE: case B_BRICK: case B_PAVING:
        case B_ROOF:  case B_PLASTER:               return IT_PICKAXE;
        case B_WOOD:  case B_TABLE: case B_CHAIR:   return IT_AXE;
        case B_GRASS: case B_DIRT:  case B_SAND:    return IT_SHOVEL;
        default:                                    return 0;
    }
}

static const int ITEMS[] = {
    B_GRASS, B_DIRT, B_STONE, B_SAND, B_WOOD, B_LEAVES, B_BRICK, B_PLASTER,
    B_ROOF, B_WINDOW, B_PAVING, B_WATER, B_TABLE, B_CHAIR, B_NOTEBOOK,
    IT_PICKAXE, IT_AXE, IT_SHOVEL, IT_SWORD
};
#define ITEM_COUNT ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

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

/* rogi scian CCW patrzac z zewnatrz */
static const float FACE_CORNERS[6][4][3] = {
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},   /* +Y */
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},   /* -Y */
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},   /* +X */
    {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},   /* -X */
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},   /* +Z */
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}},   /* -Z */
};
static const int FACE_DIR[6][3] = {
    {0,1,0},{0,-1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}
};

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

/* dowolny prostopadloscian (meble itp.) - wszystkie 6 scian */
static void pushBox(MeshData *m, float x0, float y0, float z0,
                    float x1, float y1, float z1, Color base) {
    for (int f = 0; f < 6; f++) {
        Color c = shadeColor(base, FACE_SHADE[f]);
        float px[4], py[4], pz[4];
        for (int i = 0; i < 4; i++) {
            px[i] = x0 + (x1 - x0) * FACE_CORNERS[f][i][0];
            py[i] = y0 + (y1 - y0) * FACE_CORNERS[f][i][1];
            pz[i] = z0 + (z1 - z0) * FACE_CORNERS[f][i][2];
        }
        mdPush(m, px[0], py[0], pz[0], c);
        mdPush(m, px[1], py[1], pz[1], c);
        mdPush(m, px[2], py[2], pz[2], c);
        mdPush(m, px[0], py[0], pz[0], c);
        mdPush(m, px[2], py[2], pz[2], c);
        mdPush(m, px[3], py[3], pz[3], c);
    }
}

static Model gOpaque, gTrans;
static int gHasOpaque = 0, gHasTrans = 0;

static Model meshDataToModel(MeshData *m) {
    Mesh mesh = { 0 };
    mesh.vertexCount = m->count;
    mesh.triangleCount = m->count / 3;
    mesh.vertices = m->verts;     /* zwalniane przez UnloadModel */
    mesh.colors = m->cols;
    UploadMesh(&mesh, false);
    return LoadModelFromMesh(mesh);
}

static void rebuildWorldModels(void) {
    if (gHasOpaque) UnloadModel(gOpaque);
    if (gHasTrans)  UnloadModel(gTrans);
    gHasOpaque = gHasTrans = 0;

    MeshData op = { 0 }, tr = { 0 };
    for (int x = 0; x < WX; x++)
    for (int y = 0; y < WY; y++)
    for (int z = 0; z < WZ; z++) {
        unsigned char b = world[x][y][z];
        if (b == B_AIR) continue;

        if (b == B_WATER) {
            /* wysokosc tafli zalezy od poziomu: zrodlo/spadajaca = pelna,
               plynaca tym nizsza, im mniejszy poziom (efekt skosu) */
            unsigned char wl = gWater[x][y][z];
            float topY = (wl >= WATER_SRC || getBlock(x, y + 1, z) == B_WATER)
                         ? 0.88f : 0.12f + 0.095f * (float)wl;
            if (getBlock(x, y + 1, z) == B_AIR)
                pushFace(&tr, x, y, z, 0, baseColor(B_WATER, 0), topY);
            for (int f = 2; f < 6; f++)
                if (getBlock(x + FACE_DIR[f][0], y, z + FACE_DIR[f][2]) == B_AIR)
                    pushFace(&tr, x, y, z, f,
                             shadeColor(baseColor(B_WATER, f), FACE_SHADE[f]), topY);
            continue;
        }
        if (b == B_WINDOW) {        /* transparentne szklo */
            for (int f = 0; f < 6; f++) {
                unsigned char n = getBlock(x + FACE_DIR[f][0], y + FACE_DIR[f][1], z + FACE_DIR[f][2]);
                if (n == B_WINDOW || isOpaque(n)) continue;
                pushFace(&tr, x, y, z, f,
                         shadeColor(baseColor(B_WINDOW, f), FACE_SHADE[f]), 1.0f);
            }
            continue;
        }
        if (b == B_TABLE) {         /* blat + 4 nogi */
            Color w = baseColor(B_TABLE, 0);
            pushBox(&op, x + 0.06f, y + 0.55f, z + 0.06f, x + 0.94f, y + 0.66f, z + 0.94f, w);
            Color l = shadeColor(w, 0.8f);
            pushBox(&op, x + 0.10f, y, z + 0.10f, x + 0.20f, y + 0.55f, z + 0.20f, l);
            pushBox(&op, x + 0.80f, y, z + 0.10f, x + 0.90f, y + 0.55f, z + 0.20f, l);
            pushBox(&op, x + 0.10f, y, z + 0.80f, x + 0.20f, y + 0.55f, z + 0.90f, l);
            pushBox(&op, x + 0.80f, y, z + 0.80f, x + 0.90f, y + 0.55f, z + 0.90f, l);
            continue;
        }
        if (b == B_CHAIR) {         /* siedzisko + nogi + oparcie od +Z */
            Color w = baseColor(B_CHAIR, 0);
            pushBox(&op, x + 0.15f, y + 0.35f, z + 0.15f, x + 0.85f, y + 0.45f, z + 0.85f, w);
            Color l = shadeColor(w, 0.8f);
            pushBox(&op, x + 0.18f, y, z + 0.18f, x + 0.27f, y + 0.35f, z + 0.27f, l);
            pushBox(&op, x + 0.73f, y, z + 0.18f, x + 0.82f, y + 0.35f, z + 0.27f, l);
            pushBox(&op, x + 0.18f, y, z + 0.73f, x + 0.27f, y + 0.35f, z + 0.82f, l);
            pushBox(&op, x + 0.73f, y, z + 0.73f, x + 0.82f, y + 0.35f, z + 0.82f, l);
            pushBox(&op, x + 0.15f, y + 0.45f, z + 0.76f, x + 0.85f, y + 1.05f, z + 0.86f, w);
            continue;
        }
        if (b == B_NOTEBOOK) {      /* zeszyt lezacy na blacie */
            pushBox(&op, x + 0.24f, y - 0.34f, z + 0.30f, x + 0.76f, y - 0.29f, z + 0.70f,
                    baseColor(B_NOTEBOOK, 0));
            pushBox(&op, x + 0.24f, y - 0.285f, z + 0.30f, x + 0.30f, y - 0.28f, z + 0.70f,
                    (Color){ 70, 90, 160, 255 });
            continue;
        }

        for (int f = 0; f < 6; f++) {
            unsigned char n = getBlock(x + FACE_DIR[f][0], y + FACE_DIR[f][1], z + FACE_DIR[f][2]);
            if (isOpaque(n)) continue;
            float var = 0.88f + 0.18f * hash3f(x, y, z);
            Color c = shadeColor(baseColor(b, f), FACE_SHADE[f] * var);
            pushFace(&op, x, y, z, f, c, 1.0f);
        }
    }

    if (op.count > 0) { gOpaque = meshDataToModel(&op); gHasOpaque = 1; }
    else { free(op.verts); free(op.cols); }
    if (tr.count > 0) { gTrans = meshDataToModel(&tr); gHasTrans = 1; }
    else { free(tr.verts); free(tr.cols); }
}

/* ---------------- woda: rozlewanie (model komorkowy a la Minecraft) ----------------
   Woda plynie w dol i rozlewa sie na boki, tracac po 1 poziomie na kratke
   (zrodlo=8, wiec rozlewa sie do ~7 kratek). Aktywne komorki trzymamy w
   kolejce cyklicznej z flaga "w kolejce", a stan przeliczamy z sasiadow
   (model "pull": komorka bierze max poziom z sasiadow - 1; nad woda = 7).
   Symulacja jest lokalna i deterministyczna - edycje blokow sa juz
   synchronizowane po sieci, wiec u kazdego gracza woda rozlewa sie tak samo. */

#define WATERQ_CAP (1 << 16)
static int gWQ[WATERQ_CAP];
static int gWQHead = 0, gWQTail = 0;
static unsigned char gWInQ[WX * WY * WZ];   /* czy komorka jest juz w kolejce */
static float gWaterAcc = 0.0f;

static int wpack(int x, int y, int z) { return (x * WY + y) * WZ + z; }

static void wEnqueue(int x, int y, int z) {
    if (!inBounds(x, y, z)) return;
    int id = wpack(x, y, z);
    if (gWInQ[id]) return;
    int nt = (gWQTail + 1) & (WATERQ_CAP - 1);
    if (nt == gWQHead) return;          /* kolejka pelna - pomijamy (rzadkie) */
    gWInQ[id] = 1;
    gWQ[gWQTail] = id;
    gWQTail = nt;
}

static void wEnqueueNeighbors(int x, int y, int z) {
    wEnqueue(x + 1, y, z); wEnqueue(x - 1, y, z);
    wEnqueue(x, y + 1, z); wEnqueue(x, y - 1, z);
    wEnqueue(x, y, z + 1); wEnqueue(x, y, z - 1);
}

/* Wywolac po KAZDEJ zmianie bloku (lokalnej lub sieciowej). Ustawia poziom
   wody komorki (postawiona woda = zrodlo) i budzi sasiadow. */
static void waterOnEdit(int x, int y, int z) {
    if (!inBounds(x, y, z)) return;
    if (world[x][y][z] == B_WATER) {
        if (gWater[x][y][z] == 0) gWater[x][y][z] = WATER_SRC;   /* nowo postawiona = zrodlo */
    } else {
        gWater[x][y][z] = 0;
    }
    wEnqueue(x, y, z);
    wEnqueueNeighbors(x, y, z);
}

/* Odbudowa poziomow wody ze stanu swiata: kazda woda = zrodlo. Uzywane na
   starcie (po genWorld) i po wczytaniu swiata z sieci (M_WORLD). */
static void waterInit(void) {
    gWQHead = gWQTail = 0;
    memset(gWInQ, 0, sizeof(gWInQ));
    for (int x = 0; x < WX; x++)
    for (int y = 0; y < WY; y++)
    for (int z = 0; z < WZ; z++)
        gWater[x][y][z] = (world[x][y][z] == B_WATER) ? WATER_SRC : 0;
    /* obudz komorki sasiadujace ze zrodlami, zeby ewentualnie poplynely */
    for (int x = 0; x < WX; x++)
    for (int y = 0; y < WY; y++)
    for (int z = 0; z < WZ; z++)
        if (gWater[x][y][z] > 0) wEnqueueNeighbors(x, y, z);
}

static unsigned char wlvl(int x, int y, int z) {
    return inBounds(x, y, z) ? gWater[x][y][z] : 0;
}

static void waterTick(void) {
    int changed = 0;
    int budget = 8192;                  /* limit komorek na tik (plynnosc) */
    while (budget-- > 0 && gWQHead != gWQTail) {
        int id = gWQ[gWQHead];
        gWQHead = (gWQHead + 1) & (WATERQ_CAP - 1);
        gWInQ[id] = 0;
        int x = id / (WY * WZ), r = id % (WY * WZ), y = r / WZ, z = r % WZ;

        unsigned char b = world[x][y][z];
        unsigned char lvl = gWater[x][y][z];

        if (lvl == WATER_SRC) continue;             /* zrodlo trwa bez zmian */
        if (isSolid(b)) continue;                   /* staly blok - woda nie wejdzie */

        /* docelowy poziom z sasiadow: cos nad nami = pelne 7 (woda spada),
           a po bokach max-1. Kluczowa regula jak w Minecraft: sasiad rozlewa
           sie w bok TYLKO gdy nie moze spasc (pod nim blok staly lub woda) -
           dzieki temu wodospad jest waski i rozlewa sie dopiero na dnie. */
        unsigned char target = 0;
        if (getBlock(x, y + 1, z) == B_WATER) target = 7;
        static const int H[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int k = 0; k < 4; k++) {
            int nx = x + H[k][0], nz = z + H[k][1];
            unsigned char nl = wlvl(nx, y, nz);
            if (nl == 0) continue;
            /* sasiad rozlewa sie w bok tylko gdy NIE moze spasc: stoi na bloku
               stalym albo na zrodle. Jesli pod nim powietrze lub plynaca woda,
               to woda tamtedy spada w dol (waski wodospad), nie rozlewa sie. */
            if (!isSolid(getBlock(nx, y - 1, nz)) && wlvl(nx, y - 1, nz) < WATER_SRC) continue;
            unsigned char eff = (nl >= WATER_SRC) ? 8 : nl;
            if ((unsigned char)(eff - 1) > target) target = (unsigned char)(eff - 1);
        }
        if (target > 7) target = 7;

        if (target != lvl) {
            gWater[x][y][z] = target;
            world[x][y][z] = (target > 0) ? B_WATER : B_AIR;
            wEnqueueNeighbors(x, y, z);             /* zmiana - przelicz sasiadow */
            changed = 1;
        }
    }
    if (changed) gNeedRebuild = 1;
}

/* ---------------- fizyka / kolizje ---------------- */

int boxCollidesHW(Vector3 feet, float hw, float h) {
    int x0 = (int)floorf(feet.x - hw), x1 = (int)floorf(feet.x + hw);
    int y0 = (int)floorf(feet.y),      y1 = (int)floorf(feet.y + h - 0.001f);
    int z0 = (int)floorf(feet.z - hw), z1 = (int)floorf(feet.z + hw);
    for (int x = x0; x <= x1; x++)
    for (int y = y0; y <= y1; y++)
    for (int z = z0; z <= z1; z++)
        if (isSolid(getBlock(x, y, z))) return 1;
    return 0;
}
static int boxCollides(Vector3 feet) { return boxCollidesHW(feet, PLAYER_HW, PLAYER_H); }

/* raycast po siatce (DDA) */
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

    for (int i = 0; i < 192; i++) {
        if (isRayHit(getBlock(x, y, z))) { hit[0] = x; hit[1] = y; hit[2] = z; return 1; }
        prev[0] = x; prev[1] = y; prev[2] = z;
        if (tmx < tmy && tmx < tmz) { if (tmx > maxDist) return 0; x += sx; tmx += tdx; }
        else if (tmy < tmz)         { if (tmy > maxDist) return 0; y += sy; tmy += tdy; }
        else                        { if (tmz > maxDist) return 0; z += sz; tmz += tdz; }
    }
    return 0;
}

/* czy z punktu a widac punkt b (brak pelnych blokow po drodze) */
int lineOfSight(Vector3 a, Vector3 b) {
    Vector3 d = { b.x - a.x, b.y - a.y, b.z - a.z };
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 0.01f) return 1;
    d.x /= len; d.y /= len; d.z /= len;
    int hit[3], prev[3];
    if (!raycast(a, d, len, hit, prev)) return 1;
    return 0;
}

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

static void hurt(int d) {
    if (gDead || d <= 0) return;
    gHp -= d;
    gHurt = 0.5f;
    gLastDmg = gNow;
    if (gHp <= 0) { gHp = 0; gDead = 1; }
}

/* ---------------- dzwieki (syntezowane) ---------------- */

static Sound sndBreak, sndPlace, sndStep, sndJump, sndHello, sndHit;
static unsigned int sndRngState = 0x12345u;

static float sndRandf(void) {
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

    /* niszczenie: chrupniecie */
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

    /* stawianie: klik */
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

    /* krok: tup */
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

    /* skok: swish */
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

    /* powitanie: dzyn-dzyn */
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

    /* cios mieczem: tepe uderzenie */
    n = (int)(sr * 0.11f);
    d = (short *)malloc((size_t)n * sizeof(short));
    lp = 0;
    ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        ph += 2.0f * PI * (220.0f - 120.0f * t) / sr;
        lp += 0.30f * (sndRandf() - lp);
        d[i] = (short)((sinf(ph) * 0.6f + lp * 0.7f) * (1 - t) * (1 - t) * 18000);
    }
    sndHit = soundFromSamples(d, n);
    free(d);

    SetSoundVolume(sndBreak, 0.8f);
    SetSoundVolume(sndPlace, 0.6f);
    SetSoundVolume(sndStep, 0.4f);
    SetSoundVolume(sndJump, 0.5f);
    SetSoundVolume(sndHello, 0.8f);
    SetSoundVolume(sndHit, 0.8f);
}

static void unloadSounds(void) {
    UnloadSound(sndBreak);
    UnloadSound(sndPlace);
    UnloadSound(sndStep);
    UnloadSound(sndJump);
    UnloadSound(sndHello);
    UnloadSound(sndHit);
}

/* ---------------- JP2: losowy fragment mp3 ---------------- */

static Music gJp2Mus;
static int gHasJp2 = 0;
static float gJp2StopAt = -1.0f;

static void initJp2Music(void) {
    const char *dir = GetApplicationDirectory();
    char path[512];
    snprintf(path, sizeof(path), "%sjp2.mp3", dir);
    if (!FileExists(path)) {
        /* dowolny inny mp3 obok exe */
        FilePathList fl = LoadDirectoryFilesEx(dir, ".mp3", false);
        if (fl.count > 0) snprintf(path, sizeof(path), "%s", fl.paths[0]);
        else path[0] = '\0';
        UnloadDirectoryFiles(fl);
    }
    if (path[0]) {
        gJp2Mus = LoadMusicStream(path);
        if (gJp2Mus.frameCount > 0) {
            gJp2Mus.looping = false;
            SetMusicVolume(gJp2Mus, 0.95f);
            gHasJp2 = 1;
            TraceLog(LOG_INFO, "JP2 mp3 zaladowane: %s", path);
        }
    }
}

/* zwraca 1 gdy odtworzono mp3, 0 gdy trzeba uzyc dzwonka */
static int playJp2Fragment(void) {
    if (!gHasJp2) return 0;
    float len = GetMusicTimeLength(gJp2Mus);
    float maxStart = len - 5.5f;
    if (maxStart < 0.0f) maxStart = 0.0f;
    float start = maxStart * (float)GetRandomValue(0, 1000) / 1000.0f;
    StopMusicStream(gJp2Mus);
    PlayMusicStream(gJp2Mus);
    SeekMusicStream(gJp2Mus, start);
    gJp2StopAt = gNow + 5.0f;
    return 1;
}

static void updateJp2Music(void) {
    if (!gHasJp2) return;
    if (IsMusicStreamPlaying(gJp2Mus)) {
        UpdateMusicStream(gJp2Mus);
        if (gNow > gJp2StopAt) StopMusicStream(gJp2Mus);
    }
}

/* ---------------- HUD: ikony, serca ---------------- */

static void drawCubeIcon(float x, float y, float s, int b) {
    Color ct = shadeColor(baseColor((unsigned char)b, 0), 1.00f);
    Color cl = shadeColor(baseColor((unsigned char)b, 4), 0.62f);
    Color cr = shadeColor(baseColor((unsigned char)b, 2), 0.82f);
    ct.a = cl.a = cr.a = 255;
    Vector2 T  = { x + s * 0.5f, y };
    Vector2 L  = { x,            y + s * 0.25f };
    Vector2 R  = { x + s,        y + s * 0.25f };
    Vector2 M  = { x + s * 0.5f, y + s * 0.5f };
    Vector2 BL = { x,            y + s * 0.75f };
    Vector2 BR = { x + s,        y + s * 0.75f };
    Vector2 B  = { x + s * 0.5f, y + s };
    DrawTriangle(T, L, M, ct);  DrawTriangle(T, M, R, ct);
    DrawTriangle(L, BL, B, cl); DrawTriangle(L, B, M, cl);
    DrawTriangle(M, B, BR, cr); DrawTriangle(M, BR, R, cr);
}

static void drawItemIcon(float x, float y, float s, int it) {
    if (it < 100) { drawCubeIcon(x, y, s, it); return; }
    Color wood = { 130, 95, 55, 255 };
    Color iron = { 185, 190, 200, 255 };
    Vector2 c = { x + s * 0.5f, y + s * 0.5f };
    /* trzonek po przekatnej */
    if (it != IT_SWORD)
        DrawRectanglePro((Rectangle){ c.x, c.y, s * 0.16f, s * 0.95f },
                         (Vector2){ s * 0.08f, s * 0.475f }, 45.0f, wood);
    switch (it) {
        case IT_PICKAXE:
            DrawRectanglePro((Rectangle){ c.x - s * 0.30f, c.y - s * 0.30f, s * 0.75f, s * 0.16f },
                             (Vector2){ s * 0.375f, s * 0.08f }, -45.0f, iron);
            break;
        case IT_AXE:
            DrawRectanglePro((Rectangle){ c.x - s * 0.22f, c.y - s * 0.22f, s * 0.34f, s * 0.30f },
                             (Vector2){ s * 0.17f, s * 0.15f }, 45.0f, iron);
            break;
        case IT_SHOVEL:
            DrawRectanglePro((Rectangle){ c.x + s * 0.26f, c.y + s * 0.26f, s * 0.26f, s * 0.30f },
                             (Vector2){ s * 0.13f, s * 0.15f }, 45.0f, iron);
            break;
        case IT_SWORD:
            DrawRectanglePro((Rectangle){ c.x, c.y - s * 0.06f, s * 0.13f, s * 0.80f },
                             (Vector2){ s * 0.065f, s * 0.40f }, 45.0f, iron);
            DrawRectanglePro((Rectangle){ c.x + s * 0.20f, c.y + s * 0.20f, s * 0.34f, s * 0.10f },
                             (Vector2){ s * 0.17f, s * 0.05f }, -45.0f, (Color){ 190, 150, 50, 255 });
            DrawRectanglePro((Rectangle){ c.x + s * 0.30f, c.y + s * 0.30f, s * 0.12f, s * 0.26f },
                             (Vector2){ s * 0.06f, s * 0.13f }, 45.0f, wood);
            break;
    }
}

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

/* ---------------- protokol sieciowy ---------------- */

static unsigned char gNB[sizeof(world) + 64];     /* bufor nadawczy */
static unsigned char gRB[sizeof(world) + 64];     /* bufor odbiorczy */
static int gNBLen = 0;

static void nbBegin(int type) { gNB[0] = (unsigned char)type; gNBLen = 1; }
static void nbU8(int v)       { gNB[gNBLen++] = (unsigned char)v; }
static void nbF32(float f)    { memcpy(gNB + gNBLen, &f, 4); gNBLen += 4; }
static void nbStr(const char *s) {
    size_t l = strlen(s);
    if (l > 150) l = 150;
    memcpy(gNB + gNBLen, s, l);
    gNBLen += (int)l;
    gNB[gNBLen++] = 0;
}
static float rdF32(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }

static const char *nickOf(int id) {
    if (id == gMyId) return gNick;
    if (id >= 0 && id < MAX_PLAYERS && gPeers[id].active) return gPeers[id].nick;
    return "???";
}
static int findIdByNick(const char *n) {
    if (_stricmp(n, gNick) == 0) return gMyId;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (gPeers[i].active && _stricmp(n, gPeers[i].nick) == 0) return i;
    return -1;
}
static Vector3 posOf(int id) {
    return (id == gMyId) ? gPos : gPeers[id].pos;
}

/* wyslij do wszystkich (host: klienci, klient: host) */
static void netOut(void) {
    if (netRole() == NET_HOST) netBroadcastExcept(-1, gNB, gNBLen);
    else if (netRole() == NET_CLIENT) netSendHost(gNB, gNBLen);
}

static void netSendBlock(int x, int y, int z, int b) {
    if (netRole() == NET_OFF) return;
    nbBegin(M_BLOCK); nbU8(x); nbU8(y); nbU8(z); nbU8(b);
    netOut();
}
static void netSendNote(const Note *n) {
    if (netRole() == NET_OFF) return;
    nbBegin(M_NOTE); nbU8(n->x); nbU8(n->y); nbU8(n->z); nbStr(n->text);
    netOut();
}

/* komunikat systemowy: lokalnie + do wszystkich */
void sysChat(const char *fmt, ...) {
    char tmp[150];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    chatPush("[i] %s", tmp);
    if (netRole() == NET_HOST) {
        nbBegin(M_CHAT); nbU8(254); nbStr(tmp);
        netBroadcastExcept(-1, gNB, gNBLen);
    }
}

static void cmdReply(int sender, const char *fmt, ...) {
    char tmp[150];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (sender == gMyId) {
        chatPush("[i] %s", tmp);
    } else if (netRole() == NET_HOST) {
        nbBegin(M_CHAT); nbU8(254); nbStr(tmp);
        netSendTo(sender, gNB, gNBLen);
    }
}

/* operacja na innym graczu (host -> klient) albo na sobie */
void applyToPlayer(int id, int op, float a, float b, float c) {
    if (id == gMyId) {
        switch (op) {
            case OP_TP:   gPos = (Vector3){ a, b, c }; gVel = (Vector3){ 0 }; gSitting = 0; break;
            case OP_HEAL: gHp = MAX_HP; gDead = 0; break;
            case OP_KILL: hurt(100); break;
            case OP_FLY:  gFly = !gFly; gVel.y = 0; break;
            case OP_DMG:  hurt((int)a); break;
        }
    } else if (netRole() == NET_HOST) {
        nbBegin(M_APPLY); nbU8(id); nbU8(op); nbF32(a); nbF32(b); nbF32(c);
        netSendTo(id, gNB, gNBLen);
    }
}

/* ---------------- Glazew: logika ---------------- */

static int glzAuthority(void) { return netRole() != NET_CLIENT; }


/* ---------------- komendy ---------------- */

static void runCommand(const char *line, int sender) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", line + 1);   /* bez '/' */
    char *ctx = NULL;
    char *cmd = strtok_s(buf, " ", &ctx);
    if (!cmd) return;
    char *a1 = strtok_s(NULL, " ", &ctx);
    char *a2 = strtok_s(NULL, " ", &ctx);
    char *a3 = strtok_s(NULL, " ", &ctx);

    if (_stricmp(cmd, "pomoc") == 0) {
        cmdReply(sender, "/nick /gracze /tp /heal /kill /fly /glazew /host /join /rozlacz");
        cmdReply(sender, "/tp <gracz> | /tp <x> <y> <z> | /tp <kogo> <do_kogo>");
    } else if (_stricmp(cmd, "nick") == 0) {
        if (sender != gMyId) { cmdReply(sender, "/nick ustawia sie u siebie"); return; }
        if (!a1) { cmdReply(sender, "Uzycie: /nick <imie>"); return; }
        snprintf(gNick, sizeof(gNick), "%s", a1);
        cmdReply(sender, "Nick: %s", gNick);
        if (netRole() != NET_OFF) {
            nbBegin(M_NICK); nbU8(gMyId); nbStr(gNick);
            netOut();
        }
    } else if (_stricmp(cmd, "gracze") == 0) {
        char list[150] = "";
        snprintf(list, sizeof(list), "%s", gNick);
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (gPeers[i].active && i != gMyId) {
                strcat_s(list, sizeof(list), ", ");
                strcat_s(list, sizeof(list), gPeers[i].nick);
            }
        cmdReply(sender, "Gracze: %s", list);
    } else if (_stricmp(cmd, "tp") == 0) {
        if (a1 && a2 && a3) {
            applyToPlayer(sender, OP_TP, (float)atof(a1), (float)atof(a2), (float)atof(a3));
            cmdReply(sender, "Teleport na %s %s %s", a1, a2, a3);
        } else if (a1 && a2) {
            int who = findIdByNick(a1), dst = findIdByNick(a2);
            if (who < 0 || dst < 0) { cmdReply(sender, "Nie znam takiego gracza"); return; }
            Vector3 p = posOf(dst);
            applyToPlayer(who, OP_TP, p.x, p.y, p.z);
            cmdReply(sender, "%s -> %s", nickOf(who), nickOf(dst));
        } else if (a1) {
            int dst = findIdByNick(a1);
            if (dst < 0) { cmdReply(sender, "Nie znam gracza %s", a1); return; }
            Vector3 p = posOf(dst);
            applyToPlayer(sender, OP_TP, p.x, p.y, p.z);
            cmdReply(sender, "Teleport do %s", nickOf(dst));
        } else {
            cmdReply(sender, "Uzycie: /tp <gracz> | /tp <x> <y> <z> | /tp <kogo> <do_kogo>");
        }
    } else if (_stricmp(cmd, "heal") == 0) {
        int who = a1 ? findIdByNick(a1) : sender;
        if (who < 0) { cmdReply(sender, "Nie znam gracza %s", a1); return; }
        applyToPlayer(who, OP_HEAL, 0, 0, 0);
        cmdReply(sender, "Uleczono: %s", nickOf(who));
    } else if (_stricmp(cmd, "kill") == 0) {
        int who = a1 ? findIdByNick(a1) : sender;
        if (who < 0) { cmdReply(sender, "Nie znam gracza %s", a1); return; }
        applyToPlayer(who, OP_KILL, 0, 0, 0);
        sysChat("%s zostal usmiercony komenda", nickOf(who));
    } else if (_stricmp(cmd, "fly") == 0) {
        int who = a1 ? findIdByNick(a1) : sender;
        if (who < 0) { cmdReply(sender, "Nie znam gracza %s", a1); return; }
        applyToPlayer(who, OP_FLY, 0, 0, 0);
        cmdReply(sender, "Przelaczono latanie: %s", nickOf(who));
    } else if (_stricmp(cmd, "glazew") == 0) {
        if (!glzAuthority()) { cmdReply(sender, "To moze zrobic tylko host"); return; }
        glzInit(gClassCtr[GetRandomValue(0, 3)]);
        sysChat("Glazew pojawil sie w jednej z sal...");
    } else if (_stricmp(cmd, "host") == 0) {
        if (sender != gMyId) return;
        int port = a1 ? atoi(a1) : NET_PORT_DEFAULT;
        if (netStartHost(port) == 0) {
            char ip[64];
            netLocalIp(ip, sizeof(ip));
            gMyId = 0;
            chatPush("[i] Serwer LAN dziala. Znajomi: /join %s %d", ip, port);
        } else {
            chatPush("[i] Nie udalo sie uruchomic serwera (port zajety?)");
        }
    } else if (_stricmp(cmd, "join") == 0) {
        if (sender != gMyId) return;
        if (!a1) { chatPush("[i] Uzycie: /join <ip> [port]"); return; }
        int port = a2 ? atoi(a2) : NET_PORT_DEFAULT;
        memset(gPeers, 0, sizeof(gPeers));
        if (netConnect(a1, port) == 0) {
            nbBegin(M_HELLO);
            nbStr(gNick);
            netSendHost(gNB, gNBLen);
            chatPush("[i] Polaczono z %s:%d - pobieram swiat...", a1, port);
        } else {
            chatPush("[i] Nie udalo sie polaczyc z %s:%d", a1, port);
        }
    } else if (_stricmp(cmd, "rozlacz") == 0) {
        if (sender != gMyId) return;
        netStop();
        memset(gPeers, 0, sizeof(gPeers));
        gMyId = 0;
        chatPush("[i] Rozlaczono - gra lokalna");
    } else {
        cmdReply(sender, "Nieznana komenda: /%s (sprobuj /pomoc)", cmd);
    }
}

/* ---------------- przetwarzanie sieci ---------------- */

static void hostOnHello(int id, const char *nick) {
    gPeers[id].active = 1;
    snprintf(gPeers[id].nick, sizeof(gPeers[id].nick), "%s", nick);
    gPeers[id].pos = gPeers[id].disp = gSpawn;
    nbBegin(M_WELCOME); nbU8(id);
    netSendTo(id, gNB, gNBLen);
    nbBegin(M_NICK); nbU8(gMyId); nbStr(gNick);
    netSendTo(id, gNB, gNBLen);
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (gPeers[i].active && i != id) {
            nbBegin(M_NICK); nbU8(i); nbStr(gPeers[i].nick);
            netSendTo(id, gNB, gNBLen);
        }
    nbBegin(M_WORLD);
    memcpy(gNB + 1, world, sizeof(world));
    gNBLen = 1 + (int)sizeof(world);
    netSendTo(id, gNB, gNBLen);
    for (int i = 0; i < MAX_NOTES; i++)
        if (gNotes[i].used) {
            nbBegin(M_NOTE); nbU8(gNotes[i].x); nbU8(gNotes[i].y); nbU8(gNotes[i].z);
            nbStr(gNotes[i].text);
            netSendTo(id, gNB, gNBLen);
        }
    nbBegin(M_NICK); nbU8(id); nbStr(nick);
    netBroadcastExcept(id, gNB, gNBLen);
    sysChat("%s dolaczyl do gry", nick);
}

static void processNet(void) {
    if (netRole() == NET_OFF) return;
    netPump();
    int id;
    while ((id = netNextJoined()) != -1) { /* czekamy na M_HELLO */ }
    while ((id = netNextLeft()) != -1) {
        if (netRole() == NET_HOST) {
            if (gPeers[id].active) {
                sysChat("%s wyszedl z gry", gPeers[id].nick);
                gPeers[id].active = 0;
                nbBegin(M_LEAVE); nbU8(id);
                netBroadcastExcept(id, gNB, gNBLen);
            }
        } else {
            chatPush("[i] Utracono polaczenie z hostem - gra lokalna");
            netStop();
            memset(gPeers, 0, sizeof(gPeers));
            gMyId = 0;
        }
    }
    int len, from;
    while (netNextMsg(gRB, (int)sizeof(gRB) - 1, &len, &from)) {
        if (len < 1) continue;
        int t = gRB[0];
        gRB[len] = 0;   /* domkniecie stringow */
        if (netRole() == NET_HOST) {
            switch (t) {
                case M_HELLO:
                    hostOnHello(from, (char *)gRB + 1);
                    break;
                case M_POS:
                    if (len >= 23 && gPeers[from].active) {
                        gPeers[from].pos = (Vector3){ rdF32(gRB + 2), rdF32(gRB + 6), rdF32(gRB + 10) };
                        gPeers[from].yaw = rdF32(gRB + 14);
                        gPeers[from].pitch = rdF32(gRB + 18);
                        gRB[1] = (unsigned char)from;
                        netBroadcastExcept(from, gRB, len);
                    }
                    break;
                case M_BLOCK:
                    if (len >= 5) {
                        int x = gRB[1], y = gRB[2], z = gRB[3], b = gRB[4];
                        if (inBounds(x, y, z)) {
                            if (world[x][y][z] == B_NOTEBOOK && b == B_AIR) removeNote(x, y, z);
                            world[x][y][z] = (unsigned char)b;
                            waterOnEdit(x, y, z);
                            gNeedRebuild = 1;
                            netBroadcastExcept(from, gRB, len);
                        }
                    }
                    break;
                case M_CHAT:
                    if (len >= 3) {
                        chatPush("<%s> %s", nickOf(from), (char *)gRB + 2);
                        nbBegin(M_CHAT); nbU8(from); nbStr((char *)gRB + 2);
                        netBroadcastExcept(from, gNB, gNBLen);
                    }
                    break;
                case M_NICK:
                    if (len >= 3) {
                        snprintf(gPeers[from].nick, sizeof(gPeers[from].nick), "%s", (char *)gRB + 2);
                        gPeers[from].active = 1;
                        gRB[1] = (unsigned char)from;
                        netBroadcastExcept(from, gRB, len);
                    }
                    break;
                case M_NOTE:
                    if (len >= 5) {
                        Note *n = findNote(gRB[1], gRB[2], gRB[3], 1);
                        if (n) snprintf(n->text, sizeof(n->text), "%s", (char *)gRB + 4);
                        netBroadcastExcept(from, gRB, len);
                    }
                    break;
                case M_GHIT:
                    if (len >= 9) glzSwordHit(rdF32(gRB + 1), rdF32(gRB + 5));
                    break;
                case M_CMD:
                    if (len >= 2 && gRB[1] == '/') runCommand((char *)gRB + 1, from);
                    break;
            }
        } else {   /* klient */
            switch (t) {
                case M_WELCOME:
                    if (len >= 2) { gMyId = gRB[1]; chatPush("[i] Witaj na serwerze!"); }
                    break;
                case M_WORLD:
                    if (len - 1 == (int)sizeof(world)) {
                        memcpy(world, gRB + 1, sizeof(world));
                        waterInit();          /* odtworz poziomy wody z pobranego swiata */
                        memset(gNotes, 0, sizeof(gNotes));
                        gNeedRebuild = 1;
                        chatPush("[i] Swiat pobrany - milej gry!");
                    }
                    break;
                case M_POS:
                    if (len >= 23) {
                        int pid = gRB[1];
                        if (pid != gMyId && pid >= 0 && pid < MAX_PLAYERS) {
                            Peer *p = &gPeers[pid];
                            Vector3 np = { rdF32(gRB + 2), rdF32(gRB + 6), rdF32(gRB + 10) };
                            if (!p->active) { p->active = 1; p->disp = np; }
                            p->pos = np;
                            p->yaw = rdF32(gRB + 14);
                            p->pitch = rdF32(gRB + 18);
                        }
                    }
                    break;
                case M_BLOCK:
                    if (len >= 5) {
                        int x = gRB[1], y = gRB[2], z = gRB[3], b = gRB[4];
                        if (inBounds(x, y, z)) {
                            if (world[x][y][z] == B_NOTEBOOK && b == B_AIR) removeNote(x, y, z);
                            world[x][y][z] = (unsigned char)b;
                            waterOnEdit(x, y, z);
                            gNeedRebuild = 1;
                        }
                    }
                    break;
                case M_CHAT:
                    if (len >= 3) {
                        int cid = gRB[1];
                        if (cid == 254) chatPush("[i] %s", (char *)gRB + 2);
                        else chatPush("<%s> %s", nickOf(cid), (char *)gRB + 2);
                    }
                    break;
                case M_NICK:
                    if (len >= 3) {
                        int pid = gRB[1];
                        if (pid != gMyId && pid >= 0 && pid < MAX_PLAYERS) {
                            gPeers[pid].active = 1;
                            snprintf(gPeers[pid].nick, sizeof(gPeers[pid].nick), "%s", (char *)gRB + 2);
                        }
                    }
                    break;
                case M_NOTE:
                    if (len >= 5) {
                        Note *n = findNote(gRB[1], gRB[2], gRB[3], 1);
                        if (n) snprintf(n->text, sizeof(n->text), "%s", (char *)gRB + 4);
                    }
                    break;
                case M_GLZ:
                    if (len >= 20) {
                        gGlz.pos = (Vector3){ rdF32(gRB + 1), rdF32(gRB + 5), rdF32(gRB + 9) };
                        gGlz.yaw = rdF32(gRB + 13);
                        gGlz.state = gRB[17];
                        gGlz.hp = (signed char)gRB[18];
                        gGlz.active = gRB[19];
                    }
                    break;
                case M_APPLY:
                    if (len >= 15 && gRB[1] == gMyId)
                        applyToPlayer(gMyId, gRB[2], rdF32(gRB + 3), rdF32(gRB + 7), rdF32(gRB + 11));
                    break;
                case M_LEAVE:
                    if (len >= 2) {
                        int pid = gRB[1];
                        if (pid >= 0 && pid < MAX_PLAYERS && gPeers[pid].active) {
                            chatPush("[i] %s wyszedl z gry", gPeers[pid].nick);
                            gPeers[pid].active = 0;
                        }
                    }
                    break;
            }
        }
    }
}

/* komendy obslugiwane lokalnie u klienta */
static int isLocalCmd(const char *line) {
    static const char *loc[] = { "nick", "host", "join", "rozlacz", "pomoc", "gracze" };
    char w[24] = { 0 };
    int i = 0;
    for (const char *p = line + 1; *p && *p != ' ' && i < 23; p++) w[i++] = *p;
    for (int k = 0; k < 6; k++)
        if (_stricmp(w, loc[k]) == 0) return 1;
    return 0;
}

/* ---------------- main ---------------- */

int main(void) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "MineClone - Politechnika Warszawska");
    InitAudioDevice();
    SetExitKey(KEY_NULL);
    SetTargetFPS(120);

    snprintf(gNick, sizeof(gNick), "Gracz%d", GetRandomValue(100, 999));
    genWorld();
    waterInit();          /* poziomy wody ze stanu swiata (jeziora/fontanna = zrodla) */
    rebuildWorldModels();
    initSounds();
    initJp2Music();
    glzInit(gClassCtr[GetRandomValue(0, 3)]);
    catsSpawnDefault();

    gSpawn = (Vector3){ 96.5f, (float)(GROUND + 1), 60.5f };
    gPos = gSpawn;
    gVel = (Vector3){ 0 };
    gYaw = PI / 2.0f;
    gPitch = 0.06f;
    int captured = 1, camMode = 0, invOpen = 0, chatOpen = 0;
    Note *noteEdit = NULL;
    char chatBuf[152] = "";
    int chatLen = 0;
    float fallDist = 0, drownT = 0, regenT = 0, nameTimer = 2.5f, stepAcc = 0;
    float walkPhase = 0, walkAmp = 0;
    float npcHeadYaw = 0, npcHeadPitch = 0, npcGreetT = 0, npcWave = 0;
    float posTimer = 0, glzTimer = 0;
    int prevGlzState = 0;
    int sitCell[3] = { 0, 0, 0 };
    const char *camNames[3] = { "1. osoba", "3. osoba (zza plecow)", "3. osoba (z przodu)" };
    DisableCursor();
    chatPush("[i] Witaj na Politechnice! [Enter] = czat i komendy, /pomoc = lista");
    chatPush("[i] Multiplayer LAN: /host u jednej osoby, /join <ip> u drugiej");

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.033f) dt = 0.033f;
        gNow = (float)GetTime();

        processNet();
        entitiesUpdate(dt, glzAuthority());   /* AI Glazewa (jesli autorytet) + koty */
        if (gGlz.state == 1 && prevGlzState == 0) {
            toast("Glazew Cie zauwazyl...");
            SetSoundPitch(sndHit, 0.55f);
            PlaySound(sndHit);
        }
        prevGlzState = gGlz.state;
        updateJp2Music();

        int uiBlock = invOpen || chatOpen || (noteEdit != NULL) || gDead;

        /* --- czat / edytor zeszytu / otwieranie --- */
        if (chatOpen) {
            int ch;
            while ((ch = GetCharPressed()) > 0)
                if (ch >= 32 && ch < 127 && chatLen < 149) {
                    chatBuf[chatLen++] = (char)ch;
                    chatBuf[chatLen] = 0;
                }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && chatLen > 0)
                chatBuf[--chatLen] = 0;
            if (IsKeyPressed(KEY_ESCAPE)) { chatOpen = 0; chatLen = 0; chatBuf[0] = 0; }
            else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                if (chatLen > 0) {
                    if (chatBuf[0] == '/') {
                        if (netRole() == NET_CLIENT && !isLocalCmd(chatBuf)) {
                            nbBegin(M_CMD); nbStr(chatBuf);
                            netSendHost(gNB, gNBLen);
                        } else {
                            runCommand(chatBuf, gMyId);
                        }
                    } else {
                        chatPush("<%s> %s", gNick, chatBuf);
                        if (netRole() == NET_HOST) {
                            nbBegin(M_CHAT); nbU8(0); nbStr(chatBuf);
                            netBroadcastExcept(-1, gNB, gNBLen);
                        } else if (netRole() == NET_CLIENT) {
                            nbBegin(M_CHAT); nbU8(gMyId); nbStr(chatBuf);
                            netSendHost(gNB, gNBLen);
                        }
                    }
                }
                chatOpen = 0; chatLen = 0; chatBuf[0] = 0;
            }
        } else if (noteEdit) {
            int ch;
            while ((ch = GetCharPressed()) > 0) {
                int l = (int)strlen(noteEdit->text);
                if (ch >= 32 && ch < 127 && l < 238) {
                    noteEdit->text[l] = (char)ch;
                    noteEdit->text[l + 1] = 0;
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
                int l = (int)strlen(noteEdit->text);
                if (l > 0) noteEdit->text[l - 1] = 0;
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                int l = (int)strlen(noteEdit->text), nl = 0;
                for (int i = 0; i < l; i++) if (noteEdit->text[i] == '\n') nl++;
                if (l < 238 && nl < 8) {
                    noteEdit->text[l] = '\n';
                    noteEdit->text[l + 1] = 0;
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                netSendNote(noteEdit);
                noteEdit = NULL;
                toast("Zapisano zeszyt");
            }
        } else if (!gDead && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
            chatOpen = 1;
        }

        /* --- ekwipunek / kursor --- */
        if (!chatOpen && !noteEdit) {
            if (IsKeyPressed(KEY_E) && !gDead) {
                invOpen = !invOpen;
                if (invOpen) { EnableCursor(); captured = 0; }
                else { DisableCursor(); captured = 1; }
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (invOpen) { invOpen = 0; if (!gDead) { DisableCursor(); captured = 1; } }
                else if (captured) { EnableCursor(); captured = 0; }
            }
        }
        int justCaptured = 0;
        if (!captured && !invOpen && !gDead && !chatOpen && !noteEdit &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            DisableCursor();
            captured = 1;
            justCaptured = 1;
        }
        if (gDead && captured) { EnableCursor(); captured = 0; }
        if (gDead) { invOpen = 0; chatOpen = 0; noteEdit = NULL; }
        uiBlock = invOpen || chatOpen || (noteEdit != NULL) || gDead;

        if (captured) {
            Vector2 md = GetMouseDelta();
            gYaw   += md.x * 0.0030f;
            gPitch -= md.y * 0.0030f;
            if (gPitch >  1.55f) gPitch =  1.55f;
            if (gPitch < -1.55f) gPitch = -1.55f;
        }
        Vector3 fwd   = { cosf(gPitch) * cosf(gYaw), sinf(gPitch), cosf(gPitch) * sinf(gYaw) };
        Vector3 front = { cosf(gYaw), 0.0f, sinf(gYaw) };
        Vector3 right = { -sinf(gYaw), 0.0f, cosf(gYaw) };

        if (!uiBlock) {
            if (IsKeyPressed(KEY_F))  { gFly = !gFly; gVel.y = 0; }
            if (IsKeyPressed(KEY_F5)) camMode = (camMode + 1) % 3;
            for (int i = 0; i < 9; i++)
                if (IsKeyPressed(KEY_ONE + i)) { gSel = i; nameTimer = 2.0f; }
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                gSel = (gSel + (wheel < 0.0f ? 1 : 8)) % 9;
                nameTimer = 2.0f;
            }
        }

        /* --- ruch --- */
        float mx = 0, mz = 0;
        if (captured && !uiBlock && !gSitting) {
            if (IsKeyDown(KEY_W)) { mx += front.x; mz += front.z; }
            if (IsKeyDown(KEY_S)) { mx -= front.x; mz -= front.z; }
            if (IsKeyDown(KEY_D)) { mx += right.x; mz += right.z; }
            if (IsKeyDown(KEY_A)) { mx -= right.x; mz -= right.z; }
        }
        float ml = sqrtf(mx * mx + mz * mz);
        if (ml > 0.001f) { mx /= ml; mz /= ml; }

        int eyeWater  = getBlock((int)floorf(gPos.x), (int)floorf(gPos.y + EYE_HEIGHT), (int)floorf(gPos.z)) == B_WATER;
        int bodyWater = getBlock((int)floorf(gPos.x), (int)floorf(gPos.y + 0.5f),       (int)floorf(gPos.z)) == B_WATER;

        if (gSitting) {
            gVel = (Vector3){ 0 };
            fallDist = 0;
            gOnGround = 1;
            if (captured && !uiBlock &&
                (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_S) ||
                 IsKeyPressed(KEY_A) || IsKeyPressed(KEY_D))) {
                gSitting = 0;
                gPos = (Vector3){ sitCell[0] + 0.5f, sitCell[1] + 1.02f, sitCell[2] + 0.5f };
            }
        } else {
            if (gFly) {
                float sp = 12.0f;
                gVel.x = mx * sp;
                gVel.z = mz * sp;
                gVel.y = 0;
                if (captured && !uiBlock && IsKeyDown(KEY_SPACE))        gVel.y += 9.0f;
                if (captured && !uiBlock && IsKeyDown(KEY_LEFT_CONTROL)) gVel.y -= 9.0f;
            } else {
                float sp = IsKeyDown(KEY_LEFT_SHIFT) ? 7.0f : 4.5f;
                if (bodyWater) sp *= 0.6f;
                gVel.x = mx * sp;
                gVel.z = mz * sp;
                if (bodyWater) {
                    gVel.y -= 10.0f * dt;
                    if (captured && !uiBlock && IsKeyDown(KEY_SPACE)) gVel.y = 3.5f;
                    if (gVel.y < -3.0f) gVel.y = -3.0f;
                } else {
                    gVel.y -= 24.0f * dt;
                    if (gOnGround && captured && !uiBlock && IsKeyDown(KEY_SPACE)) {
                        gVel.y = 8.0f;
                        SetSoundPitch(sndJump, 0.95f + GetRandomValue(0, 10) / 100.0f);
                        PlaySound(sndJump);
                    }
                    if (gVel.y < -28.0f) gVel.y = -28.0f;
                }
            }

            float vyApplied = gVel.y;
            Vector3 np = gPos;
            np.x += gVel.x * dt;
            if (boxCollides(np)) { np.x = gPos.x; gVel.x = 0; }
            np.z += gVel.z * dt;
            if (boxCollides(np)) { np.z = gPos.z; gVel.z = 0; }
            np.y += gVel.y * dt;
            gOnGround = 0;
            if (boxCollides(np)) {
                if (gVel.y < 0.0f) {
                    np.y = floorf(np.y) + 1.0f;
                    if (boxCollides(np)) np.y = gPos.y;
                    gOnGround = 1;
                } else {
                    np.y = gPos.y;
                }
                gVel.y = 0;
            }
            gPos = np;

            if (gFly || bodyWater) {
                fallDist = 0;
            } else if (gOnGround) {
                if (fallDist > 3.0f) hurt((int)(fallDist - 3.0f));
                if (fallDist > 1.2f) {
                    SetSoundPitch(sndStep, 0.70f);
                    PlaySound(sndStep);
                }
                fallDist = 0;
            } else if (vyApplied < 0.0f) {
                fallDist += -vyApplied * dt;
            }
        }
        if (gPos.y < -24.0f) hurt(100);
        if (gDead && gPos.y < -60.0f) { gPos.y = -60.0f; gVel.y = 0; }

        /* --- toniecie / regeneracja --- */
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
        if (!gDead && gHp < MAX_HP && gNow - gLastDmg > 5.0f) {
            regenT += dt;
            if (regenT >= 1.8f) { regenT = 0; gHp++; }
        }
        if (gHurt > 0.0f) gHurt -= dt;

        Vector3 eye = { gPos.x, gPos.y + EYE_HEIGHT, gPos.z };

        /* --- animacja chodu + kroki --- */
        float hsp = sqrtf(gVel.x * gVel.x + gVel.z * gVel.z);
        walkPhase += hsp * 2.4f * dt;
        float tAmp = (hsp > 0.3f) ? hsp / 4.5f : 0.0f;
        if (tAmp > 1.2f) tAmp = 1.2f;
        walkAmp += (tAmp - walkAmp) * (dt * 8.0f > 1.0f ? 1.0f : dt * 8.0f);
        float swing = sinf(walkPhase) * 42.0f * walkAmp;
        if (gOnGround && !gFly && !bodyWater && !gSitting && hsp > 1.5f) {
            stepAcc += hsp * dt;
            if (stepAcc > 2.1f) {
                stepAcc = 0;
                SetSoundPitch(sndStep, 0.85f + GetRandomValue(0, 30) / 100.0f);
                PlaySound(sndStep);
            }
        } else if (!gOnGround) {
            stepAcc = 1.6f;
        }

        /* --- NPC: Jan Pawel II --- */
        float npcDx = gPos.x - NPC_POS.x, npcDz = gPos.z - NPC_POS.z;
        float npcDy = (gPos.y + EYE_HEIGHT) - (NPC_POS.y + 1.77f);
        float npcDist = sqrtf(npcDx * npcDx + npcDy * npcDy + npcDz * npcDz);
        float npcTYaw, npcTPitch;
        if (npcDist < 8.0f) {
            npcTYaw = wrapPi(atan2f(npcDz, npcDx) - NPC_YAW);
            if (npcTYaw >  1.15f) npcTYaw =  1.15f;
            if (npcTYaw < -1.15f) npcTYaw = -1.15f;
            npcTPitch = atan2f(npcDy, sqrtf(npcDx * npcDx + npcDz * npcDz));
            if (npcTPitch >  0.5f) npcTPitch =  0.5f;
            if (npcTPitch < -0.5f) npcTPitch = -0.5f;
        } else {
            npcTYaw = sinf(gNow * 0.6f) * 0.55f;
            npcTPitch = sinf(gNow * 0.83f) * 0.10f;
        }
        float npcLerp = dt * 5.0f;
        if (npcLerp > 1.0f) npcLerp = 1.0f;
        npcHeadYaw += (npcTYaw - npcHeadYaw) * npcLerp;
        npcHeadPitch += (npcTPitch - npcHeadPitch) * npcLerp;
        int npcNear = (npcDist < 4.2f) && !uiBlock;
        if (npcNear && IsKeyPressed(KEY_T) && npcGreetT <= 0.5f) {
            npcGreetT = 3.0f;
            if (!playJp2Fragment()) PlaySound(sndHello);
            else npcGreetT = 5.0f;   /* dymek na czas mp3 */
        }
        if (npcGreetT > 0.0f) npcGreetT -= dt;
        float npcWaveT = (npcGreetT > 0.0f) ? 1.0f : 0.0f;
        npcWave += (npcWaveT - npcWave) * (dt * 7.0f > 1.0f ? 1.0f : dt * 7.0f);

        /* --- celowanie + akcje --- */
        int hit[3] = { 0, 0, 0 }, prev[3] = { 0, 0, 0 };
        int hasHit = raycast(eye, fwd, MAX_REACH, hit, prev);
        if (captured && !justCaptured && !uiBlock) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int swordHit = 0;
                if (gHotbar[gSel] == IT_SWORD && gGlz.active) {   /* cios mieczem */
                    Vector3 C = { gGlz.pos.x, gGlz.pos.y + 0.9f, gGlz.pos.z };
                    Vector3 toC = { C.x - eye.x, C.y - eye.y, C.z - eye.z };
                    float t = toC.x * fwd.x + toC.y * fwd.y + toC.z * fwd.z;
                    if (t > 0.0f && t < 3.6f) {
                        float cx = eye.x + fwd.x * t - C.x;
                        float cy = eye.y + fwd.y * t - C.y;
                        float cz = eye.z + fwd.z * t - C.z;
                        if (cx * cx + cy * cy + cz * cz < 0.85f) {
                            swordHit = 1;
                            SetSoundPitch(sndHit, 0.9f + GetRandomValue(0, 20) / 100.0f);
                            PlaySound(sndHit);
                            if (glzAuthority()) {
                                glzSwordHit(fwd.x, fwd.z);
                            } else {
                                nbBegin(M_GHIT); nbF32(fwd.x); nbF32(fwd.z);
                                netSendHost(gNB, gNBLen);
                            }
                        }
                    }
                }
                if (!swordHit && hasHit) {                        /* niszczenie bloku */
                    unsigned char b = world[hit[0]][hit[1]][hit[2]];
                    int need = toolFor(b);
                    if (need && gHotbar[gSel] != need) {
                        toast("%s wymaga: %s!", itemName(b), itemName(need));
                    } else {
                        if (b == B_NOTEBOOK) removeNote(hit[0], hit[1], hit[2]);
                        world[hit[0]][hit[1]][hit[2]] = B_AIR;
                        waterOnEdit(hit[0], hit[1], hit[2]);
                        SetSoundPitch(sndBreak, 0.90f + GetRandomValue(0, 20) / 100.0f);
                        PlaySound(sndBreak);
                        netSendBlock(hit[0], hit[1], hit[2], B_AIR);
                        gNeedRebuild = 1;
                    }
                }
            } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
              if (catInteract(eye, fwd, 3.0f, gMyId)) {
                /* interakcja z kotem (oswajanie / siad) - nie stawiamy bloku */
              } else if (hasHit) {
                unsigned char hb = world[hit[0]][hit[1]][hit[2]];
                if (hb == B_CHAIR) {                              /* siadanie */
                    gSitting = 1;
                    sitCell[0] = hit[0]; sitCell[1] = hit[1]; sitCell[2] = hit[2];
                    gPos = (Vector3){ hit[0] + 0.5f, hit[1] + 0.35f, hit[2] + 0.5f };
                    gVel = (Vector3){ 0 };
                    toast("Siedzisz (skok/WSAD = wstan)");
                } else if (hb == B_NOTEBOOK) {                    /* pisanie w zeszycie */
                    noteEdit = findNote(hit[0], hit[1], hit[2], 1);
                    if (!noteEdit) toast("Za duzo zapisanych zeszytow!");
                } else if (gHotbar[gSel] < 100 && inBounds(prev[0], prev[1], prev[2])) {
                    float bx = (float)prev[0], by = (float)prev[1], bz = (float)prev[2];
                    int overlap = (bx < gPos.x + PLAYER_HW && bx + 1 > gPos.x - PLAYER_HW &&
                                   by < gPos.y + PLAYER_H  && by + 1 > gPos.y &&
                                   bz < gPos.z + PLAYER_HW && bz + 1 > gPos.z - PLAYER_HW);
                    if (!overlap) {
                        world[prev[0]][prev[1]][prev[2]] = (unsigned char)gHotbar[gSel];
                        waterOnEdit(prev[0], prev[1], prev[2]);
                        SetSoundPitch(sndPlace, 0.90f + GetRandomValue(0, 20) / 100.0f);
                        PlaySound(sndPlace);
                        netSendBlock(prev[0], prev[1], prev[2], gHotbar[gSel]);
                        gNeedRebuild = 1;
                    }
                } else if (gHotbar[gSel] >= 100) {
                    toast("Trzymasz narzedzie - wybierz blok, aby budowac");
                }
              }
            }
        }
        gWaterAcc += dt;                  /* symulacja rozlewania wody (~5/s) */
        if (gWaterAcc >= 0.18f) { gWaterAcc = 0.0f; waterTick(); }
        if (gNeedRebuild) { rebuildWorldModels(); gNeedRebuild = 0; }

        /* --- siec: wysylka stanu --- */
        if (netRole() != NET_OFF) {
            posTimer += dt;
            if (posTimer > 0.05f) {
                posTimer = 0;
                nbBegin(M_POS); nbU8(gMyId);
                nbF32(gPos.x); nbF32(gPos.y); nbF32(gPos.z);
                nbF32(gYaw); nbF32(gPitch); nbU8(0);
                netOut();
            }
        }
        if (netRole() == NET_HOST) {
            glzTimer += dt;
            if (glzTimer > 0.066f) {
                glzTimer = 0;
                nbBegin(M_GLZ);
                nbF32(gGlz.pos.x); nbF32(gGlz.pos.y); nbF32(gGlz.pos.z); nbF32(gGlz.yaw);
                nbU8(gGlz.state); nbU8(gGlz.hp & 0xFF); nbU8(gGlz.active);
                netBroadcastExcept(-1, gNB, gNBLen);
            }
        }
        /* interpolacja innych graczy */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Peer *p = &gPeers[i];
            if (!p->active || i == gMyId) continue;
            p->prevDisp = p->disp;
            float k = dt * 12.0f;
            if (k > 1.0f) k = 1.0f;
            p->disp.x += (p->pos.x - p->disp.x) * k;
            p->disp.y += (p->pos.y - p->disp.y) * k;
            p->disp.z += (p->pos.z - p->disp.z) * k;
            float sp = sqrtf((p->disp.x - p->prevDisp.x) * (p->disp.x - p->prevDisp.x) +
                             (p->disp.z - p->prevDisp.z) * (p->disp.z - p->prevDisp.z)) / (dt > 0 ? dt : 1);
            float ta = sp / 4.5f;
            if (ta > 1.2f) ta = 1.2f;
            if (sp < 0.3f) ta = 0;
            p->swingAmp += (ta - p->swingAmp) * k;
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

        /* --- rysowanie 3D --- */
        BeginDrawing();
        ClearBackground((Color){ 135, 190, 255, 255 });

        BeginMode3D(cam);
        if (gHasOpaque) DrawModel(gOpaque, (Vector3){ 0 }, 1.0f, WHITE);
        if (hasHit && captured && !uiBlock)
            DrawCubeWires((Vector3){ hit[0] + 0.5f, hit[1] + 0.5f, hit[2] + 0.5f },
                          1.02f, 1.02f, 1.02f, (Color){ 0, 0, 0, 180 });
        if (camMode != 0 && !gDead)
            drawPlayerModel(gPos, gYaw, gPitch, swing, (Color){ 58, 118, 196, 255 });
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Peer *p = &gPeers[i];
            if (!p->active || i == gMyId) continue;
            Color shirts[4] = { { 196, 90, 60, 255 }, { 90, 170, 80, 255 },
                                { 200, 160, 60, 255 }, { 150, 90, 190, 255 } };
            drawPlayerModel(p->disp, p->yaw, p->pitch,
                            sinf(gNow * 10.0f) * 42.0f * p->swingAmp, shirts[i % 4]);
        }
        drawNPC(-npcHeadYaw * RAD2DEG, -npcHeadPitch * RAD2DEG,
                npcWave * (110.0f + 8.0f * sinf(gNow * 11.0f)));
        drawGlazew();
        drawRobots();
        drawCats();
        if (gHasTrans) DrawModel(gTrans, (Vector3){ 0 }, 1.0f, WHITE);
        EndMode3D();

        int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (eyeWater) DrawRectangle(0, 0, sw, sh, (Color){ 30, 90, 190, 110 });
        if (gHurt > 0.0f)
            DrawRectangle(0, 0, sw, sh, (Color){ 200, 0, 0, (unsigned char)(gHurt * 160) });

        if (!uiBlock) {
            DrawRectangle(sw / 2 - 11, sh / 2 - 2, 22, 4, (Color){ 0, 0, 0, 120 });
            DrawRectangle(sw / 2 - 2, sh / 2 - 11, 4, 22, (Color){ 0, 0, 0, 120 });
            DrawRectangle(sw / 2 - 10, sh / 2 - 1, 20, 2, WHITE);
            DrawRectangle(sw / 2 - 1, sh / 2 - 10, 2, 20, WHITE);
        }

        /* --- pasek narzedzi + serca --- */
        int cell = 46, hbW = cell * 9;
        int hbX = sw / 2 - hbW / 2, hbY = sh - cell - 8;
        for (int i = 0; i < 9; i++) {
            Rectangle r = { (float)(hbX + i * cell), (float)hbY, (float)cell, (float)cell };
            DrawRectangleRec(r, (Color){ 0, 0, 0, 150 });
            DrawRectangleLinesEx(r, 2, (Color){ 95, 95, 100, 230 });
            drawItemIcon(r.x + 9, r.y + 9, cell - 18, gHotbar[i]);
            DrawText(TextFormat("%d", i + 1), (int)r.x + 4, (int)r.y + 3, 10, (Color){ 210, 210, 210, 200 });
        }
        DrawRectangleLinesEx((Rectangle){ (float)(hbX + gSel * cell - 2), (float)(hbY - 2),
                                          (float)(cell + 4), (float)(cell + 4) }, 3, RAYWHITE);
        int heartsY = hbY - 17;
        for (int i = 0; i < 10; i++) {
            int rem = gHp - i * 2;
            drawHeart(hbX + i * 16, heartsY, rem >= 2 ? 7 : (rem == 1 ? 4 : 0));
        }
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
        if (nameTimer > 0.0f) {
            nameTimer -= dt;
            const char *nm = itemName(gHotbar[gSel]);
            int tw = MeasureText(nm, 20);
            unsigned char a = (unsigned char)(nameTimer > 1.0f ? 255 : nameTimer * 255);
            DrawText(nm, sw / 2 - tw / 2, heartsY - 28, 20, (Color){ 255, 255, 255, a });
        }
        if (gToastT > 0.0f) {
            gToastT -= dt;
            int tw = MeasureText(gToast, 20);
            unsigned char a = (unsigned char)(gToastT > 0.5f ? 255 : gToastT * 510);
            DrawText(gToast, sw / 2 - tw / 2 + 1, heartsY - 55, 20, (Color){ 0, 0, 0, a });
            DrawText(gToast, sw / 2 - tw / 2, heartsY - 56, 20, (Color){ 255, 220, 120, a });
        }

        /* --- HUD pomoc --- */
        DrawRectangle(6, 6, 700, 96, (Color){ 0, 0, 0, 110 });
        DrawText("[WSAD] ruch | [Spacja] skok | [F] latanie | [F5] kamera | [E] ekwipunek | [Enter] czat/komendy",
                 12, 12, 16, RAYWHITE);
        DrawText("[LPM] niszcz (wlasciwe narzedzie!) / cios mieczem | [PPM] postaw / usiadz / pisz w zeszycie",
                 12, 32, 16, RAYWHITE);
        const char *netStat = (netRole() == NET_HOST) ? "HOST" :
                              (netRole() == NET_CLIENT) ? "KLIENT" : "gra lokalna";
        int playersOn = 1;
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (gPeers[i].active && i != gMyId) playersOn++;
        DrawText(TextFormat("Tryb: %s%s | Kamera: %s | Siec: %s (graczy: %d)",
                            gFly ? "LATANIE" : "CHODZENIE", bodyWater ? " (woda)" : "",
                            camNames[camMode], netStat, playersOn),
                 12, 52, 16, YELLOW);
        DrawText(TextFormat("%s | Politechnika Warszawska | [T] przy JP2 = powitanie", gNick),
                 12, 74, 16, (Color){ 180, 255, 180, 255 });
        if (!captured && !uiBlock)
            DrawText("Kliknij, aby zlapac mysz", sw / 2 - 130, sh / 2 + 34, 22, YELLOW);

        /* --- tabliczki nad postaciami --- */
        if (!gDead) {
            Vector3 camDir = { cam.target.x - cam.position.x,
                               cam.target.y - cam.position.y,
                               cam.target.z - cam.position.z };
            /* JP2 */
            Vector3 toN = { NPC_POS.x - cam.position.x, NPC_POS.y + 1.7f - cam.position.y,
                            NPC_POS.z - cam.position.z };
            if (camDir.x * toN.x + camDir.y * toN.y + camDir.z * toN.z > 0.0f && npcDist < 16.0f) {
                Vector2 tp = GetWorldToScreen((Vector3){ NPC_POS.x, NPC_POS.y + 2.25f, NPC_POS.z }, cam);
                const char *nm = "Jan Pawel II";
                int nw = MeasureText(nm, 17);
                DrawText(nm, (int)tp.x - nw / 2 + 1, (int)tp.y + 1, 17, (Color){ 0, 0, 0, 180 });
                DrawText(nm, (int)tp.x - nw / 2, (int)tp.y, 17, RAYWHITE);
                if (npcGreetT > 0.0f) {
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
            /* Glazew */
            if (gGlz.active) {
                Vector3 toG = { gGlz.pos.x - cam.position.x, gGlz.pos.y + 1.5f - cam.position.y,
                                gGlz.pos.z - cam.position.z };
                float gd = sqrtf(toG.x * toG.x + toG.y * toG.y + toG.z * toG.z);
                if (camDir.x * toG.x + camDir.y * toG.y + camDir.z * toG.z > 0.0f && gd < 40.0f &&
                    lineOfSight(cam.position,
                                (Vector3){ gGlz.pos.x, gGlz.pos.y + 1.5f, gGlz.pos.z })) {
                    Vector2 tp = GetWorldToScreen((Vector3){ gGlz.pos.x, gGlz.pos.y + 2.25f, gGlz.pos.z }, cam);
                    const char *nm = (gGlz.state == 1) ? "Glazew  !" : "Glazew";
                    Color nc = (gGlz.state >= 2) ? (Color){ 255, 80, 80, 255 } :
                               (gGlz.state == 1) ? (Color){ 255, 200, 80, 255 } : RAYWHITE;
                    int nw = MeasureText(nm, 17);
                    DrawText(nm, (int)tp.x - nw / 2 + 1, (int)tp.y + 1, 17, (Color){ 0, 0, 0, 180 });
                    DrawText(nm, (int)tp.x - nw / 2, (int)tp.y, 17, nc);
                    /* pasek HP */
                    DrawRectangle((int)tp.x - 24, (int)tp.y + 20, 48, 5, (Color){ 0, 0, 0, 160 });
                    DrawRectangle((int)tp.x - 24, (int)tp.y + 20, 48 * gGlz.hp / 12, 5,
                                  (Color){ 220, 60, 60, 255 });
                }
            }
            /* inni gracze */
            for (int i = 0; i < MAX_PLAYERS; i++) {
                Peer *p = &gPeers[i];
                if (!p->active || i == gMyId) continue;
                Vector3 toP = { p->disp.x - cam.position.x, p->disp.y + 1.5f - cam.position.y,
                                p->disp.z - cam.position.z };
                float pd = sqrtf(toP.x * toP.x + toP.y * toP.y + toP.z * toP.z);
                if (camDir.x * toP.x + camDir.y * toP.y + camDir.z * toP.z > 0.0f && pd < 50.0f) {
                    Vector2 tp = GetWorldToScreen((Vector3){ p->disp.x, p->disp.y + 2.2f, p->disp.z }, cam);
                    int nw = MeasureText(p->nick, 16);
                    DrawText(p->nick, (int)tp.x - nw / 2 + 1, (int)tp.y + 1, 16, (Color){ 0, 0, 0, 180 });
                    DrawText(p->nick, (int)tp.x - nw / 2, (int)tp.y, 16, (Color){ 190, 230, 255, 255 });
                }
            }
        }

        /* --- czat --- */
        {
            int cy = sh - 96;
            for (int i = 0; i < 8; i++) {
                if (!gChatLog[i][0]) continue;
                float age = gNow - gChatT[i];
                if (!chatOpen && age > 8.0f) continue;
                unsigned char a = 255;
                if (!chatOpen && age > 7.0f) a = (unsigned char)((8.0f - age) * 255);
                int tw = MeasureText(gChatLog[i], 16);
                DrawRectangle(8, cy - 2, tw + 8, 20, (Color){ 0, 0, 0, (unsigned char)(a / 2) });
                DrawText(gChatLog[i], 12, cy, 16, (Color){ 255, 255, 255, a });
                cy -= 22;
            }
            if (chatOpen) {
                DrawRectangle(8, sh - 70, sw - 16, 26, (Color){ 0, 0, 0, 190 });
                DrawText(TextFormat("> %s_", chatBuf), 14, sh - 65, 18, RAYWHITE);
            }
        }

        /* --- edytor zeszytu --- */
        if (noteEdit) {
            int pw = 480, ph = 360;
            int px = sw / 2 - pw / 2, py = sh / 2 - ph / 2;
            DrawRectangle(px + 6, py + 8, pw, ph, (Color){ 0, 0, 0, 90 });
            DrawRectangle(px, py, pw, ph, (Color){ 250, 248, 240, 250 });
            DrawRectangleLinesEx((Rectangle){ (float)px, (float)py, (float)pw, (float)ph }, 2,
                                 (Color){ 120, 110, 90, 255 });
            DrawRectangle(px, py, 26, ph, (Color){ 90, 110, 170, 255 });   /* grzbiet */
            DrawText("ZESZYT", px + 40, py + 12, 24, (Color){ 60, 60, 70, 255 });
            DrawText("[Enter] nowa linia, [ESC] zapisz i zamknij", px + 40, py + ph - 26, 14,
                     (Color){ 130, 130, 130, 255 });
            /* tekst z lamaniem linii */
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s_", noteEdit->text);
            int lx = px + 40, ly = py + 50;
            char line[64];
            int li = 0;
            for (int i = 0;; i++) {
                char c = tmp[i];
                if (c == '\n' || c == '\0' || li >= 40) {
                    line[li] = 0;
                    DrawText(line, lx, ly, 18, (Color){ 40, 40, 60, 255 });
                    ly += 24;
                    li = 0;
                    if (c == '\0' || ly > py + ph - 50) break;
                    if (c != '\n') line[li++] = c;
                } else {
                    line[li++] = c;
                }
            }
            for (int yl = py + 46; yl < py + ph - 40; yl += 24)
                DrawLine(px + 36, yl + 20, px + pw - 24, yl + 20, (Color){ 160, 180, 210, 120 });
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
                float cyy = (float)(yy + (i / cols) * icell);
                Rectangle r = { cx + 2, cyy + 2, (float)icell - 4, (float)icell - 4 };
                int hov = CheckCollisionPointRec(mp, r);
                DrawRectangleRec(r, (Color){ 55, 55, 62, 255 });
                DrawRectangleLinesEx(r, 2, hov ? RAYWHITE : (Color){ 95, 95, 105, 255 });
                drawItemIcon(cx + 11, cyy + 11, (float)icell - 22, ITEMS[i]);
                if (hov) {
                    tip = itemName(ITEMS[i]);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        gHotbar[gSel] = ITEMS[i];
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
                DrawRectangleLinesEx(r, (i == gSel) ? 3.0f : 2.0f,
                                     (i == gSel) ? (Color){ 255, 210, 80, 255 }
                                                 : (hov ? RAYWHITE : (Color){ 95, 95, 105, 255 }));
                drawItemIcon(cx + 11, (float)yy + 11, (float)icell - 22, gHotbar[i]);
                DrawText(TextFormat("%d", i + 1), (int)cx + 6, yy + 5, 10, (Color){ 210, 210, 210, 200 });
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { gSel = i; nameTimer = 2.0f; }
            }
            DrawText("LPM na przedmiot = wloz do wybranego slotu | [E]/[ESC] zamknij",
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
                gPos = gSpawn;
                gVel = (Vector3){ 0 };
                gHp = MAX_HP;
                gDead = 0;
                gBreath = MAX_BREATH;
                gHurt = 0;
                gSitting = 0;
                fallDist = 0;
                drownT = 0;
                DisableCursor();
                captured = 1;
            }
        }

        DrawFPS(sw - 95, 10);
        EndDrawing();
    }

    netStop();
    if (gHasOpaque) UnloadModel(gOpaque);
    if (gHasTrans)  UnloadModel(gTrans);
    if (gHasJp2) UnloadMusicStream(gJp2Mus);
    unloadSounds();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
