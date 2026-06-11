/*
 * game.h - wspolny rdzen gry wspoldzielony przez main.c i entities.c.
 * Zawiera stale, typy bloków/itemow oraz "API swiata": funkcje i stan, ktore
 * definiuje main.c, a z ktorych korzysta modul postaci (entities.c).
 */
#ifndef GAME_H
#define GAME_H

#include "raylib.h"

#define WX 192             /* szerokosc swiata (X) */
#define WY 48              /* wysokosc swiata (Y) */
#define WZ 192             /* glebokosc swiata (Z) */
#define GROUND 10          /* poziom gruntu kampusu */
#define EYE_HEIGHT 1.62f
#define MAX_PLAYERS 8      /* host + 7 klientow */

enum { B_AIR = 0, B_GRASS, B_DIRT, B_STONE, B_SAND, B_WOOD, B_LEAVES, B_WATER,
       B_BRICK, B_PLASTER, B_ROOF, B_WINDOW, B_PAVING, B_TABLE, B_CHAIR, B_NOTEBOOK };

/* itemy: < 100 to bloki, >= 100 to narzedzia */
enum { IT_PICKAXE = 100, IT_AXE, IT_SHOVEL, IT_SWORD };

/* operacje M_APPLY (dzialania na graczu) */
enum { OP_TP = 1, OP_HEAL, OP_KILL, OP_FLY, OP_DMG };

/* ---- pozostali gracze (sieciowi) ---- */
typedef struct {
    int active;
    char nick[24];
    Vector3 pos, disp, prevDisp;
    float yaw, pitch, swingAmp;
} Peer;

/* ---------------------------------------------------------------------- */
/* API swiata/gracza: definiowane w main.c, uzywane przez entities.c.      */
/* ---------------------------------------------------------------------- */
extern unsigned char world[WX][WY][WZ];
extern float   gNow;                 /* czas gry (s) */
extern Vector3 gPos;                 /* pozycja lokalnego gracza (stopy) */
extern int     gDead, gMyId;
extern Peer    gPeers[MAX_PLAYERS];

unsigned char getBlock(int x, int y, int z);
int  isSolid(unsigned char b);
int  boxCollidesHW(Vector3 feet, float hw, float h);   /* kolizja AABB ze swiatem */
int  lineOfSight(Vector3 a, Vector3 b);                /* czy z a widac b */
void applyToPlayer(int id, int op, float a, float b, float c);
void toast(const char *fmt, ...);                      /* krotki komunikat na HUD */
void sysChat(const char *fmt, ...);                    /* wpis systemowy na czacie */

#endif
