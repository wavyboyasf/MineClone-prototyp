/*
 * entities.c - postacie i stworzenia: model gracza, NPC (Jan Pawel II),
 * Glazew (wrog z AI), roboty-patrole oraz koty (do oswojenia).
 *
 * Wczesniej caly ten kod siedzial w main.c, kazda postac osobno. Tutaj jest
 * zebrany w jednym miejscu, a wspolne czesci "klockowego czlowieka" (tors,
 * nogi) sa wyciagniete do pomocniczych funkcji, zeby nie powielac geometrii.
 */
#include "entities.h"

#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

/* ====================================================================== */
/* Wspolne prymitywy "klockowego czlowieka" (gracz, Glazew).              */
/* Wywolujacy ustawia juz uklad (translacja do stop + obrot wg yaw).      */
/* ====================================================================== */

static void humanTorso(Color shirt) {
    DrawCube((Vector3){ 0.0f, 1.125f, 0.0f }, 0.50f, 0.75f, 0.25f, shirt);
}

/* nogi z animacja chodzenia (swing w stopniach) */
static void humanLegs(float swing, Color pants, Color shoe) {
    for (int s = -1; s <= 1; s += 2) {
        rlPushMatrix();
        rlTranslatef(s * 0.125f, 0.78f, 0.0f);
        rlRotatef(-s * swing, 1.0f, 0.0f, 0.0f);
        DrawCube((Vector3){ 0.0f, -0.39f, 0.0f }, 0.24f, 0.78f, 0.24f, pants);
        DrawCube((Vector3){ 0.0f, -0.70f, 0.01f }, 0.26f, 0.16f, 0.28f, shoe);
        rlPopMatrix();
    }
}

/* ====================================================================== */
/* Wspolna fizyka stworzenia: grawitacja + kolizje AABB + auto-skok.       */
/* mvx/mvz to predkosc pozioma (j/s). Zwraca 1, gdy stworzenie stoi na     */
/* ziemi. Uzywane przez Glazewa i koty, zeby nie powielac kodu fizyki.     */
/* ====================================================================== */
static int mobStep(Vector3 *pos, Vector3 *vel, float mvx, float mvz,
                   float hw, float h, float jumpV, float dt) {
    vel->y -= 24.0f * dt;
    if (vel->y < -28.0f) vel->y = -28.0f;
    Vector3 np = *pos;
    int blocked = 0;
    np.x += mvx * dt;
    if (boxCollidesHW(np, hw, h)) { np.x = pos->x; blocked = 1; }
    np.z += mvz * dt;
    if (boxCollidesHW(np, hw, h)) { np.z = pos->z; blocked = 1; }
    np.y += vel->y * dt;
    int onGround = 0;
    if (boxCollidesHW(np, hw, h)) {
        if (vel->y < 0.0f) {
            np.y = floorf(np.y) + 1.0f;
            if (boxCollidesHW(np, hw, h)) np.y = pos->y;
            onGround = 1;
        } else {
            np.y = pos->y;
        }
        vel->y = 0;
    }
    *pos = np;
    if (blocked && onGround) vel->y = jumpV;   /* przeszkoda przed nami -> podskok */
    return onGround;
}

/* ====================================================================== */
/* Model gracza (uzywany dla siebie w TPP i dla innych graczy).           */
/* ====================================================================== */

void drawPlayerModel(Vector3 feet, float yaw, float pitch, float swing, Color shirt) {
    Color skin  = { 236, 188, 150, 255 };
    Color skin2 = { 214, 165, 130, 255 };
    Color pants = {  64,  66,  82, 255 };
    Color shoe  = {  45,  45,  50, 255 };
    Color pupil = {  60,  60, 120, 255 };
    Color mouth = { 150, 100,  90, 255 };

    rlPushMatrix();
    rlTranslatef(feet.x, feet.y, feet.z);
    rlRotatef(90.0f - yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);

    humanTorso(shirt);

    rlPushMatrix();
    rlTranslatef(0.0f, 1.50f, 0.0f);
    rlRotatef(-pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, 0.25f, 0.0f }, 0.50f, 0.50f, 0.50f, skin);
    DrawCube((Vector3){ -0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);
    DrawCube((Vector3){  0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);
    DrawCube((Vector3){ -0.08f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){  0.12f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){  0.00f, 0.20f, 0.258f }, 0.08f, 0.08f, 0.03f, skin2);
    DrawCube((Vector3){  0.00f, 0.10f, 0.252f }, 0.16f, 0.04f, 0.02f, mouth);
    rlPopMatrix();

    for (int s = -1; s <= 1; s += 2) {
        rlPushMatrix();
        rlTranslatef(s * 0.375f, 1.42f, 0.0f);
        rlRotatef(s * swing, 1.0f, 0.0f, 0.0f);
        DrawCube((Vector3){ 0.0f, -0.28f, 0.0f }, 0.24f, 0.68f, 0.24f, skin);
        DrawCube((Vector3){ 0.0f, -0.05f, 0.0f }, 0.27f, 0.26f, 0.27f, shirt);
        rlPopMatrix();
    }
    humanLegs(swing, pants, shoe);
    rlPopMatrix();
}

/* ====================================================================== */
/* NPC: Jan Pawel II.                                                     */
/* ====================================================================== */

const Vector3 NPC_POS = { 101.5f, (float)(GROUND + 1), 87.5f };

void drawNPC(float headYawDeg, float headPitchDeg, float waveDeg) {
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

    DrawCube((Vector3){ 0.0f, 0.45f, 0.0f }, 0.52f, 0.90f, 0.30f, robe);
    DrawCube((Vector3){ -0.12f, 0.04f, 0.12f }, 0.16f, 0.08f, 0.16f, shoe);
    DrawCube((Vector3){  0.12f, 0.04f, 0.12f }, 0.16f, 0.08f, 0.16f, shoe);
    DrawCube((Vector3){ 0.0f, 1.10f, 0.0f }, 0.52f, 0.55f, 0.28f, robe);
    DrawCube((Vector3){ 0.0f, 1.31f, 0.0f }, 0.56f, 0.24f, 0.32f, cape);
    DrawCube((Vector3){ 0.0f, 1.05f, 0.155f }, 0.05f, 0.20f, 0.02f, gold);
    DrawCube((Vector3){ 0.0f, 1.10f, 0.155f }, 0.13f, 0.05f, 0.02f, gold);

    rlPushMatrix();
    rlTranslatef(0.0f, 1.52f, 0.0f);
    rlRotatef(headYawDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(headPitchDeg, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, 0.25f, 0.0f }, 0.50f, 0.50f, 0.50f, skin);
    DrawCube((Vector3){ 0.0f, 0.515f, 0.0f }, 0.52f, 0.07f, 0.52f, capC);
    DrawCube((Vector3){ -0.26f, 0.30f, 0.0f }, 0.03f, 0.20f, 0.42f, hair);
    DrawCube((Vector3){  0.26f, 0.30f, 0.0f }, 0.03f, 0.20f, 0.42f, hair);
    DrawCube((Vector3){ -0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);
    DrawCube((Vector3){  0.10f, 0.30f, 0.252f }, 0.10f, 0.07f, 0.02f, WHITE);
    DrawCube((Vector3){ -0.09f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){  0.11f, 0.30f, 0.262f }, 0.05f, 0.07f, 0.02f, pupil);
    DrawCube((Vector3){ -0.10f, 0.36f, 0.252f }, 0.12f, 0.03f, 0.02f, hair);
    DrawCube((Vector3){  0.10f, 0.36f, 0.252f }, 0.12f, 0.03f, 0.02f, hair);
    DrawCube((Vector3){  0.00f, 0.20f, 0.258f }, 0.08f, 0.09f, 0.03f, skin2);
    DrawCube((Vector3){  0.00f, 0.10f, 0.252f }, 0.15f, 0.04f, 0.02f, mouth);
    rlPopMatrix();

    rlPushMatrix();
    rlTranslatef(-0.37f, 1.38f, 0.0f);
    rlRotatef(sinf(gNow * 1.3f) * 3.0f, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, -0.26f, 0.0f }, 0.24f, 0.62f, 0.24f, robe);
    DrawCube((Vector3){ 0.0f, -0.60f, 0.0f }, 0.15f, 0.12f, 0.15f, skin);
    rlPopMatrix();

    rlPushMatrix();
    rlTranslatef(0.37f, 1.38f, 0.0f);
    rlRotatef(waveDeg, 0.0f, 0.0f, 1.0f);
    rlRotatef(sinf(gNow * 1.1f) * 3.0f, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, -0.26f, 0.0f }, 0.24f, 0.62f, 0.24f, robe);
    DrawCube((Vector3){ 0.0f, -0.60f, 0.0f }, 0.15f, 0.12f, 0.15f, skin);
    rlPopMatrix();

    rlPopMatrix();
}

/* ====================================================================== */
/* Glazew - wrog z prosta maszyna stanow i fizyka (grawitacja, auto-skok).*/
/* ====================================================================== */

Glz gGlz;
static int gGlzProvoked = 0;   /* czy Glazew juz raz uderzyl gracza (koty ida na niego) */

void glzInit(Vector3 home) {
    gGlz.home = home;
    gGlz.pos = home;
    gGlz.vel = (Vector3){ 0 };
    gGlz.yaw = -PI / 2.0f;
    gGlz.hp = 12;
    gGlz.active = 1;
    gGlz.state = 0;
    gGlz.stateT = gGlz.hitCool = gGlz.flashT = gGlz.animT = 0;
    gGlzProvoked = 0;
}

void drawGlazew(void) {
    if (!gGlz.active) return;
    float fl = (gGlz.flashT > 0) ? 1.0f : 0.0f;
    Color skin  = { 235, 195, 165, 255 };
    Color shirt = { (unsigned char)(95 + 120 * fl), 98, 104, 255 };   /* szary sweter */
    Color pants = { 40, 42, 48, 255 };
    Color shoe  = { 70, 50, 35, 255 };
    Color hair  = { 88, 62, 40, 255 };
    Color frame = { 25, 25, 28, 255 };
    Color paper = { 252, 252, 250, 255 };

    float swing = sinf(gGlz.animT) * 40.0f *
                  ((gGlz.state == 2 || gGlz.state == 3) ? 1.0f : 0.0f);

    rlPushMatrix();
    rlTranslatef(gGlz.pos.x, gGlz.pos.y, gGlz.pos.z);
    rlRotatef(90.0f - gGlz.yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);

    humanTorso(shirt);

    /* glowa: wlosy, okulary, surowa mina */
    rlPushMatrix();
    rlTranslatef(0.0f, 1.50f, 0.0f);
    DrawCube((Vector3){ 0.0f, 0.25f, 0.0f }, 0.50f, 0.50f, 0.50f, skin);
    DrawCube((Vector3){ 0.0f, 0.47f, -0.02f }, 0.52f, 0.12f, 0.50f, hair);
    DrawCube((Vector3){ 0.0f, 0.38f, -0.24f }, 0.52f, 0.22f, 0.06f, hair);
    DrawCube((Vector3){ -0.10f, 0.30f, 0.252f }, 0.13f, 0.10f, 0.02f, frame);   /* okulary */
    DrawCube((Vector3){  0.10f, 0.30f, 0.252f }, 0.13f, 0.10f, 0.02f, frame);
    DrawCube((Vector3){  0.00f, 0.31f, 0.252f }, 0.08f, 0.03f, 0.02f, frame);
    DrawCube((Vector3){ -0.10f, 0.30f, 0.258f }, 0.08f, 0.05f, 0.02f, WHITE);
    DrawCube((Vector3){  0.10f, 0.30f, 0.258f }, 0.08f, 0.05f, 0.02f, WHITE);
    DrawCube((Vector3){ -0.09f, 0.30f, 0.262f }, 0.04f, 0.05f, 0.02f, frame);
    DrawCube((Vector3){  0.11f, 0.30f, 0.262f }, 0.04f, 0.05f, 0.02f, frame);
    DrawCube((Vector3){  0.00f, 0.18f, 0.258f }, 0.08f, 0.09f, 0.03f, (Color){ 214, 170, 140, 255 });
    DrawCube((Vector3){  0.00f, 0.08f, 0.252f }, 0.14f, 0.03f, 0.02f, (Color){ 120, 75, 65, 255 });
    rlPopMatrix();

    /* lewa reka - wymachuje przy bieganiu */
    rlPushMatrix();
    rlTranslatef(-0.375f, 1.42f, 0.0f);
    rlRotatef(-swing, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, -0.28f, 0.0f }, 0.24f, 0.68f, 0.24f, shirt);
    DrawCube((Vector3){ 0.0f, -0.62f, 0.0f }, 0.18f, 0.14f, 0.18f, skin);
    rlPopMatrix();

    /* prawa reka - wyciagnieta do przodu z biala kartka A3 */
    rlPushMatrix();
    rlTranslatef(0.375f, 1.42f, 0.0f);
    rlRotatef(-75.0f + sinf(gGlz.animT * 0.7f) * 5.0f, 1.0f, 0.0f, 0.0f);
    DrawCube((Vector3){ 0.0f, -0.28f, 0.0f }, 0.24f, 0.68f, 0.24f, shirt);
    DrawCube((Vector3){ 0.0f, -0.62f, 0.0f }, 0.18f, 0.14f, 0.18f, skin);
    DrawCube((Vector3){ -0.18f, -0.72f, 0.0f }, 0.02f, 0.60f, 0.42f, paper);   /* kartka A3 */
    rlPopMatrix();

    humanLegs(swing, pants, shoe);
    rlPopMatrix();
}

void glzSwordHit(float dirx, float dirz) {   /* tylko autorytet */
    if (!gGlz.active) return;
    gGlz.hp -= 4;
    gGlz.flashT = 0.25f;
    gGlz.state = 2;
    Vector3 np = gGlz.pos;
    np.x += dirx * 1.4f;
    np.z += dirz * 1.4f;
    if (!boxCollidesHW(np, 0.3f, 1.8f)) gGlz.pos = np;
    gGlz.vel.y = 4.5f;
    if (gGlz.hp <= 0) {
        gGlz.active = 0;
        sysChat("Glazew zostal pokonany! (komenda /glazew przywroci go)");
    }
}

static void glzUpdate(float dt) {
    if (!gGlz.active) return;
    gGlz.hitCool -= dt;
    gGlz.flashT -= dt;

    /* najblizszy zywy gracz */
    int targetId = -1;
    float best = 1e9f;
    Vector3 tp = { 0 };
    if (!gDead) {
        best = sqrtf((gPos.x - gGlz.pos.x) * (gPos.x - gGlz.pos.x) +
                     (gPos.z - gGlz.pos.z) * (gPos.z - gGlz.pos.z));
        targetId = gMyId;
        tp = gPos;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!gPeers[i].active || i == gMyId) continue;
        float d = sqrtf((gPeers[i].pos.x - gGlz.pos.x) * (gPeers[i].pos.x - gGlz.pos.x) +
                        (gPeers[i].pos.z - gGlz.pos.z) * (gPeers[i].pos.z - gGlz.pos.z));
        if (d < best) { best = d; targetId = i; tp = gPeers[i].pos; }
    }

    Vector3 head = { gGlz.pos.x, gGlz.pos.y + 1.6f, gGlz.pos.z };
    Vector3 teye = { tp.x, tp.y + EYE_HEIGHT, tp.z };
    float mvx = 0, mvz = 0;

    switch (gGlz.state) {
        case 0:   /* czeka w sali z kartka */
            if (targetId >= 0 && best < 9.0f && lineOfSight(head, teye)) {
                gGlz.state = 1;
                gGlz.stateT = 0;
            }
            break;
        case 1:   /* zauwazyl - chwila grozy */
            gGlz.yaw = atan2f(tp.z - gGlz.pos.z, tp.x - gGlz.pos.x);
            gGlz.stateT += dt;
            if (gGlz.stateT > 1.2f) gGlz.state = 2;
            break;
        case 2: { /* goni */
            if (targetId < 0) { gGlz.state = 3; break; }
            float dx = tp.x - gGlz.pos.x, dz = tp.z - gGlz.pos.z;
            float l = sqrtf(dx * dx + dz * dz);
            gGlz.yaw = atan2f(dz, dx);
            if (l > 0.01f) { mvx = dx / l * 5.0f; mvz = dz / l * 5.0f; }   /* szybszy niz chod (4.5), wolniejszy niz sprint (7.0) - uciekniesz sprintem */
            gGlz.animT += 11.0f * dt;
            if (best < 1.35f && gGlz.hitCool <= 0.0f) {
                gGlz.hitCool = 1.2f;
                applyToPlayer(targetId, OP_DMG, 3, 0, 0);
                gGlzProvoked = 1;            /* pierwszy cios - oswojone koty ruszaja na Glazewa */
            }
            if (best > 45.0f) gGlz.state = 3;
            break;
        }
        case 3: { /* wraca do sali */
            float dx = gGlz.home.x - gGlz.pos.x, dz = gGlz.home.z - gGlz.pos.z;
            float l = sqrtf(dx * dx + dz * dz);
            if (l < 0.8f) { gGlz.state = 0; break; }
            gGlz.yaw = atan2f(dz, dx);
            mvx = dx / l * 3.5f;
            mvz = dz / l * 3.5f;
            gGlz.animT += 8.0f * dt;
            if (targetId >= 0 && best < 7.0f && lineOfSight(head, teye)) gGlz.state = 1;
            break;
        }
    }

    /* fizyka z auto-skokiem (wspolna z kotami) */
    mobStep(&gGlz.pos, &gGlz.vel, mvx, mvz, 0.3f, 1.8f, 8.0f, dt);
    if (gGlz.pos.y < -24.0f) { gGlz.pos = gGlz.home; gGlz.vel = (Vector3){ 0 }; gGlz.state = 3; }
}

/* ====================================================================== */
/* Roboty - deterministyczne patrole eliptyczne (bez synchronizacji).     */
/* ====================================================================== */

typedef struct { float cx, cz, rx, rz, sp, ph; } Robot;
static Robot gRobots[6];
static int gRobotN = 0;

void robotsInitDefault(void) {
    gRobotN = 0;
    gRobots[gRobotN++] = (Robot){ 152.0f, 90.0f, 5.0f, 6.0f, 0.50f, 0.0f };
    gRobots[gRobotN++] = (Robot){ 149.0f, 82.0f, 3.0f, 3.5f, 0.80f, 2.0f };
    gRobots[gRobotN++] = (Robot){ 155.0f, 97.0f, 2.5f, 4.0f, 0.65f, 4.0f };
    gRobots[gRobotN++] = (Robot){ 168.0f, 90.0f, 5.0f, 6.0f, 0.45f, 1.0f };
    gRobots[gRobotN++] = (Robot){ 165.0f, 80.0f, 3.0f, 2.5f, 0.70f, 3.0f };
    gRobots[gRobotN++] = (Robot){ 172.0f, 97.0f, 3.0f, 4.0f, 0.60f, 5.0f };
}

void drawRobots(void) {
    for (int i = 0; i < gRobotN; i++) {
        Robot *r = &gRobots[i];
        float a = gNow * r->sp + r->ph;
        float x = r->cx + cosf(a) * r->rx;
        float z = r->cz + sinf(a) * r->rz;
        float dx = -sinf(a) * r->rx * r->sp;
        float dz =  cosf(a) * r->rz * r->sp;
        float yaw = atan2f(dz, dx);
        float y = GROUND + 1.0f + 0.04f * sinf(gNow * 6.0f + i);
        Color body = { 150, 158, 170, 255 };
        Color dark = {  60,  62,  70, 255 };
        Color led  = { 60, (unsigned char)(150 + 100 * sinf(gNow * 5.0f + i * 2)), 220, 255 };

        rlPushMatrix();
        rlTranslatef(x, y, z);
        rlRotatef(90.0f - yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
        DrawCube((Vector3){ 0, 0.10f, 0 },  0.40f, 0.12f, 0.50f, dark);       /* podwozie */
        DrawCube((Vector3){ -0.22f, 0.10f, -0.15f }, 0.08f, 0.16f, 0.16f, dark);
        DrawCube((Vector3){  0.22f, 0.10f, -0.15f }, 0.08f, 0.16f, 0.16f, dark);
        DrawCube((Vector3){ -0.22f, 0.10f,  0.15f }, 0.08f, 0.16f, 0.16f, dark);
        DrawCube((Vector3){  0.22f, 0.10f,  0.15f }, 0.08f, 0.16f, 0.16f, dark);
        DrawCube((Vector3){ 0, 0.42f, 0 },  0.36f, 0.50f, 0.40f, body);       /* korpus */
        rlPushMatrix();                                                        /* glowa sie kreci */
        rlTranslatef(0, 0.78f, 0);
        rlRotatef(sinf(gNow * 2.0f + i) * 45.0f, 0.0f, 1.0f, 0.0f);
        DrawCube((Vector3){ 0, 0.10f, 0 },  0.26f, 0.22f, 0.26f, body);
        DrawCube((Vector3){ -0.06f, 0.10f, 0.135f }, 0.06f, 0.06f, 0.02f, led);
        DrawCube((Vector3){  0.06f, 0.10f, 0.135f }, 0.06f, 0.06f, 0.02f, led);
        DrawCube((Vector3){ 0, 0.28f, 0 },  0.03f, 0.14f, 0.03f, dark);       /* antenka */
        DrawCube((Vector3){ 0, 0.37f, 0 },  0.07f, 0.07f, 0.07f, led);
        rlPopMatrix();
        rlPopMatrix();
    }
}

/* ====================================================================== */
/* Koty - bladzace stworzenia, ktore mozna oswoic (PPM). Oswojony kot     */
/* siada lub chodzi za wlascicielem. Lokalnie u kazdego gracza (jak       */
/* roboty), bez synchronizacji sieciowej.                                 */
/* ====================================================================== */

typedef struct {
    Vector3 pos, vel, home;
    float yaw, wyaw;          /* kierunek patrzenia / kierunek wedrowki */
    float animT, wanderT, idleT, hitCool;
    int state;               /* 0 bladzi, 1 idzie za wlascicielem, 2 siedzi */
    int owner;               /* -1 = dziki, inaczej id gracza-wlasciciela */
    int variant;
    int active;
} Cat;

static Cat gCats[8];
static int gCatN = 0;

static const Color CAT_FUR[4] = {
    { 232, 150,  70, 255 },   /* rudy */
    {  55,  55,  60, 255 },   /* czarny */
    { 235, 235, 232, 255 },   /* bialy */
    { 150, 120,  90, 255 },   /* bury */
};

static Color catMul(Color c, float f) {
    return (Color){ (unsigned char)(c.r * f), (unsigned char)(c.g * f),
                    (unsigned char)(c.b * f), 255 };
}

static float catRand01(void) { return (float)rand() / (float)RAND_MAX; }

void catsSpawnDefault(void) {
    const Vector3 spots[] = {
        {  72.5f, GROUND + 1.0f, 78.5f }, {  85.5f, GROUND + 1.0f, 88.5f },
        { 112.5f, GROUND + 1.0f, 78.5f }, { 120.5f, GROUND + 1.0f, 88.5f },
        { 100.5f, GROUND + 1.0f, 68.5f },
    };
    gCatN = (int)(sizeof(spots) / sizeof(spots[0]));
    for (int i = 0; i < gCatN; i++) {
        Cat *c = &gCats[i];
        c->pos = c->home = spots[i];
        c->vel = (Vector3){ 0 };
        c->yaw = c->wyaw = catRand01() * 2.0f * PI;
        c->animT = 0;
        c->wanderT = catRand01() * 2.0f;
        c->idleT = 0;
        c->hitCool = 0;
        c->state = 0;
        c->owner = -1;
        c->variant = i & 3;
        c->active = 1;
    }
}

static Vector3 catOwnerPos(int owner) {
    if (owner == gMyId) return gPos;
    return gPeers[owner].pos;
}

static int catOwnerAlive(int owner) {
    if (owner < 0) return 0;
    if (owner == gMyId) return 1;
    return gPeers[owner].active;
}

static void catsUpdate(float dt, int authority) {
    for (int i = 0; i < gCatN; i++) {
        Cat *c = &gCats[i];
        if (!c->active) continue;
        float mvx = 0, mvz = 0;
        c->hitCool -= dt;

        if (gGlzProvoked && gGlz.active && c->owner >= 0) {    /* obrona wlasciciela: atak na Glazewa */
            float dx = gGlz.pos.x - c->pos.x, dz = gGlz.pos.z - c->pos.z;
            float d = sqrtf(dx * dx + dz * dz);
            c->yaw = atan2f(dz, dx);
            if (d > 1.1f) { mvx = dx / d * 4.8f; mvz = dz / d * 4.8f; }   /* szybsze niz Glazew */
            c->animT += 14.0f * dt;
            if (authority && d < 1.5f && c->hitCool <= 0.0f) {
                c->hitCool = 0.7f;
                gGlz.hp -= 1;
                gGlz.flashT = 0.2f;
                if (gGlz.hp <= 0) {
                    gGlz.active = 0;
                    sysChat("Koty przepedzily Glazewa!");
                }
            }
        } else if (c->state == 1 && catOwnerAlive(c->owner)) {        /* idzie za wlascicielem */
            Vector3 op = catOwnerPos(c->owner);
            float dx = op.x - c->pos.x, dz = op.z - c->pos.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d > 14.0f) {                                   /* za daleko - teleport */
                c->pos = (Vector3){ op.x + (catRand01() - 0.5f), op.y + 0.4f, op.z + (catRand01() - 0.5f) };
                c->vel = (Vector3){ 0 };
            } else if (d > 2.2f) {
                c->yaw = atan2f(dz, dx);
                float sp = (d > 6.0f) ? 4.2f : 2.4f;           /* dogania biegiem */
                mvx = dx / d * sp; mvz = dz / d * sp;
                c->animT += 12.0f * dt;
            }
        } else if (c->state == 0) {                            /* bladzi blisko domu */
            if (c->idleT > 0.0f) {
                c->idleT -= dt;
            } else {
                c->wanderT -= dt;
                if (c->wanderT <= 0.0f) {
                    c->wanderT = 2.5f + catRand01() * 2.5f;
                    if (catRand01() < 0.34f) {
                        c->idleT = 1.0f + catRand01() * 2.0f;  /* przystaje */
                    } else {
                        float hx = c->home.x - c->pos.x, hz = c->home.z - c->pos.z;
                        if (sqrtf(hx * hx + hz * hz) > 10.0f)
                            c->wyaw = atan2f(hz, hx);           /* wraca do domu */
                        else
                            c->wyaw = catRand01() * 2.0f * PI;
                    }
                }
                if (c->idleT <= 0.0f) {
                    c->yaw = c->wyaw;
                    mvx = cosf(c->wyaw) * 1.3f;
                    mvz = sinf(c->wyaw) * 1.3f;
                    c->animT += 8.0f * dt;
                }
            }
        }
        /* state 2 (siedzi) = bez ruchu */

        mobStep(&c->pos, &c->vel, mvx, mvz, 0.28f, 0.5f, 6.0f, dt);
        if (c->pos.y < -24.0f) { c->pos = c->home; c->vel = (Vector3){ 0 }; }
    }
}

/* czworonozny klockowy kot; przod (glowa) wzdluz +Z lokalnego ukladu */
void drawCats(void) {
    for (int i = 0; i < gCatN; i++) {
        Cat *c = &gCats[i];
        if (!c->active) continue;
        Color fur  = CAT_FUR[c->variant & 3];
        Color dark = catMul(fur, 0.78f);
        Color pink = { 240, 175, 185, 255 };
        Color eye  = { 70, 190, 120, 255 };
        int sit = (c->state == 2);
        float sw = sinf(c->animT) * 24.0f;        /* zamarza, gdy kot stoi (animT staly) */

        rlPushMatrix();
        rlTranslatef(c->pos.x, c->pos.y, c->pos.z);
        rlRotatef(90.0f - c->yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);

        /* tulow */
        DrawCube((Vector3){ 0.0f, 0.30f, -0.02f }, 0.26f, 0.24f, 0.52f, fur);
        /* zad uniesiony, gdy siedzi */
        if (sit) DrawCube((Vector3){ 0.0f, 0.20f, -0.26f }, 0.26f, 0.34f, 0.16f, fur);

        /* glowa */
        rlPushMatrix();
        rlTranslatef(0.0f, sit ? 0.50f : 0.40f, 0.30f);
        DrawCube((Vector3){ 0.0f, 0.0f, 0.0f }, 0.28f, 0.26f, 0.24f, fur);
        DrawCube((Vector3){ -0.09f, 0.17f, 0.0f }, 0.08f, 0.10f, 0.06f, dark);   /* uszy */
        DrawCube((Vector3){  0.09f, 0.17f, 0.0f }, 0.08f, 0.10f, 0.06f, dark);
        DrawCube((Vector3){ -0.07f, 0.02f, 0.13f }, 0.05f, 0.06f, 0.02f, eye);   /* oczy */
        DrawCube((Vector3){  0.07f, 0.02f, 0.13f }, 0.05f, 0.06f, 0.02f, eye);
        DrawCube((Vector3){  0.00f, -0.06f, 0.13f }, 0.05f, 0.04f, 0.02f, pink); /* nosek */
        rlPopMatrix();

        /* ogon - unosi sie */
        rlPushMatrix();
        rlTranslatef(0.0f, 0.42f, -0.28f);
        rlRotatef(35.0f + sinf(c->animT * 0.8f + (float)i) * 12.0f, 1.0f, 0.0f, 0.0f);
        DrawCube((Vector3){ 0.0f, 0.16f, 0.0f }, 0.07f, 0.36f, 0.07f, dark);
        rlPopMatrix();

        /* lapy: przednie (z+) i tylne (z-), animowane przeciwbieznie */
        for (int s = -1; s <= 1; s += 2) {
            /* przednie */
            rlPushMatrix();
            rlTranslatef(s * 0.09f, 0.18f, 0.18f);
            if (!sit) rlRotatef(s * sw, 1.0f, 0.0f, 0.0f);
            DrawCube((Vector3){ 0.0f, -0.09f, 0.0f }, 0.08f, 0.22f, 0.08f, dark);
            rlPopMatrix();
            /* tylne - przy siadaniu zlozone pod tulowiem */
            rlPushMatrix();
            if (sit) rlTranslatef(s * 0.10f, 0.10f, -0.20f);
            else     rlTranslatef(s * 0.09f, 0.18f, -0.20f);
            if (!sit) rlRotatef(-s * sw, 1.0f, 0.0f, 0.0f);
            DrawCube((Vector3){ 0.0f, -0.09f, 0.0f }, 0.09f, sit ? 0.14f : 0.22f, 0.09f, dark);
            rlPopMatrix();
        }

        /* obroza, gdy oswojony */
        if (c->owner >= 0)
            DrawCube((Vector3){ 0.0f, 0.38f, 0.18f }, 0.30f, 0.06f, 0.26f, (Color){ 210, 60, 60, 255 });

        rlPopMatrix();
    }
}

/* PPM na kota: promien-kula. Oswaja (z szansa) lub przelacza siad/chodzenie. */
int catInteract(Vector3 eye, Vector3 fwd, float maxDist, int ownerId) {
    int bestIdx = -1;
    float bestT = maxDist;
    for (int i = 0; i < gCatN; i++) {
        Cat *c = &gCats[i];
        if (!c->active) continue;
        Vector3 ctr = { c->pos.x, c->pos.y + 0.35f, c->pos.z };
        Vector3 oc = { ctr.x - eye.x, ctr.y - eye.y, ctr.z - eye.z };
        float t = oc.x * fwd.x + oc.y * fwd.y + oc.z * fwd.z;   /* rzut na promien */
        if (t < 0.0f || t > bestT) continue;
        float px = eye.x + fwd.x * t, py = eye.y + fwd.y * t, pz = eye.z + fwd.z * t;
        float dd = (px - ctr.x) * (px - ctr.x) + (py - ctr.y) * (py - ctr.y) +
                   (pz - ctr.z) * (pz - ctr.z);
        if (dd < 0.5f * 0.5f) { bestT = t; bestIdx = i; }
    }
    if (bestIdx < 0) return 0;

    Cat *c = &gCats[bestIdx];
    if (c->owner < 0) {                                  /* dziki - proba oswojenia */
        if (catRand01() < 0.34f) {
            c->owner = ownerId;
            c->state = 1;
            toast("Kot oswojony! Mruczy z radosci.");
        } else {
            toast("Kot prycha i odsuwa sie...");
        }
    } else if (c->owner == ownerId) {                    /* moj kot - siad / chodzenie */
        c->state = (c->state == 2) ? 1 : 2;
        toast(c->state == 2 ? "Kot siada." : "Kot rusza za Toba.");
    } else {
        toast("To czyjis kot.");
    }
    return 1;
}

/* ====================================================================== */
/* Aktualizacja calego modulu postaci.                                    */
/* ====================================================================== */

void entitiesUpdate(float dt, int glzAuthority) {
    if (glzAuthority) glzUpdate(dt);
    else if (gGlz.state == 2 || gGlz.state == 3) gGlz.animT += 10.0f * dt;
    catsUpdate(dt, glzAuthority);
}
