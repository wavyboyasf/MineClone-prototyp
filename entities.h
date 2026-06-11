/*
 * entities.h - postacie i stworzenia gry (poza graczem-blokami swiata):
 * model gracza, NPC (Jan Pawel II), Glazew (wrog z AI), roboty oraz koty.
 * Implementacja w entities.c; main.c korzysta z tego API.
 */
#ifndef ENTITIES_H
#define ENTITIES_H

#include "game.h"

/* Glazew - stan wystawiony, bo main synchronizuje go po sieci (M_GLZ). */
typedef struct {
    Vector3 pos, vel, home;
    float yaw, stateT, hitCool, flashT, animT;
    int hp, active, state;   /* 0 czeka, 1 zauwazyl, 2 goni, 3 wraca */
} Glz;
extern Glz gGlz;

/* NPC: Jan Pawel II - pozycja stala, uzywana tez przez main (powitanie). */
extern const Vector3 NPC_POS;
#define NPC_YAW (-PI / 2.0f)

/* ---- rysowanie ---- */
void drawPlayerModel(Vector3 feet, float yaw, float pitch, float swing, Color shirt);
void drawNPC(float headYawDeg, float headPitchDeg, float waveDeg);
void drawGlazew(void);
void drawRobots(void);
void drawCats(void);

/* ---- inicjalizacja / logika ---- */
void glzInit(Vector3 home);
void glzSwordHit(float dirx, float dirz);       /* cios mieczem (tylko autorytet) */
void robotsInitDefault(void);                   /* ustawia patrole robotow */
void catsSpawnDefault(void);                    /* rozstawia koty na kampusie */
void entitiesUpdate(float dt, int glzAuthority);/* AI Glazewa (jesli autorytet) + koty */

/* PPM na kota: oswajanie / siad. Zwraca 1, gdy trafiono kota (main pomija blok). */
int  catInteract(Vector3 eye, Vector3 fwd, float maxDist, int ownerId);

#endif
