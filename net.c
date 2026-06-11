/*
 * net.c - prosta warstwa sieciowa TCP (LAN) dla MineClone.
 * Nieblokujace gniazda + ramki [u32 dlugosc][dane].
 */
#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

#define NET_BUFCAP (2u * 1024u * 1024u + 4096u)   /* miesci pelny swiat */

typedef struct {
    SOCKET s;
    int active;
    unsigned char *buf;
    unsigned int len;
} Conn;

static int gRole = NET_OFF;
static int gWsaUp = 0;
static SOCKET gListen = INVALID_SOCKET;
static Conn gConns[NET_MAX_CLIENTS + 1];   /* [0] = polaczenie klienta z hostem */
static int gJoinQ[16], gJoinN = 0;
static int gLeftQ[16], gLeftN = 0;

static void wsaUp(void) {
    if (!gWsaUp) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        gWsaUp = 1;
    }
}

static void setNonBlocking(SOCKET s) {
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
}

static void connOpen(Conn *c, SOCKET s) {
    c->s = s;
    c->active = 1;
    c->len = 0;
    if (!c->buf) c->buf = (unsigned char *)malloc(NET_BUFCAP);
}

static void connClose(Conn *c) {
    if (c->active) {
        closesocket(c->s);
        c->active = 0;
        c->len = 0;
    }
}

int netRole(void) { return gRole; }

int netStartHost(int port) {
    wsaUp();
    netStop();
    gListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (gListen == INVALID_SOCKET) return -1;
    int one = 1;
    setsockopt(gListen, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((u_short)port);
    if (bind(gListen, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(gListen, 4) != 0) {
        closesocket(gListen);
        gListen = INVALID_SOCKET;
        return -1;
    }
    setNonBlocking(gListen);
    gRole = NET_HOST;
    return 0;
}

int netConnect(const char *ip, int port) {
    wsaUp();
    netStop();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
        closesocket(s);
        return -1;
    }
    /* blokujace connect z krotkim timeoutem przez gniazdo nieblokujace */
    setNonBlocking(s);
    connect(s, (struct sockaddr *)&a, sizeof(a));
    fd_set wr, ex;
    FD_ZERO(&wr); FD_ZERO(&ex);
    FD_SET(s, &wr); FD_SET(s, &ex);
    struct timeval tv = { 3, 0 };   /* 3 s */
    int r = select(0, NULL, &wr, &ex, &tv);
    if (r <= 0 || FD_ISSET(s, &ex)) {
        closesocket(s);
        return -1;
    }
    connOpen(&gConns[0], s);
    gRole = NET_CLIENT;
    return 0;
}

void netStop(void) {
    for (int i = 0; i <= NET_MAX_CLIENTS; i++) connClose(&gConns[i]);
    if (gListen != INVALID_SOCKET) {
        closesocket(gListen);
        gListen = INVALID_SOCKET;
    }
    gRole = NET_OFF;
    gJoinN = gLeftN = 0;
}

static void pumpConn(Conn *c, int id) {
    if (!c->active) return;
    for (;;) {
        if (c->len >= NET_BUFCAP) break;   /* przepelnienie - czekamy na odbior ramek */
        int n = recv(c->s, (char *)c->buf + c->len, (int)(NET_BUFCAP - c->len), 0);
        if (n > 0) {
            c->len += (unsigned int)n;
            continue;
        }
        if (n == 0) {                       /* rozlaczenie */
            connClose(c);
            if (gLeftN < 16) gLeftQ[gLeftN++] = id;
            return;
        }
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) break;
        connClose(c);                       /* blad = rozlaczenie */
        if (gLeftN < 16) gLeftQ[gLeftN++] = id;
        return;
    }
}

void netPump(void) {
    if (gRole == NET_HOST) {
        for (;;) {
            SOCKET s = accept(gListen, NULL, NULL);
            if (s == INVALID_SOCKET) break;
            int slot = -1;
            for (int i = 1; i <= NET_MAX_CLIENTS; i++)
                if (!gConns[i].active) { slot = i; break; }
            if (slot < 0) { closesocket(s); continue; }
            setNonBlocking(s);
            connOpen(&gConns[slot], s);
            if (gJoinN < 16) gJoinQ[gJoinN++] = slot;
        }
        for (int i = 1; i <= NET_MAX_CLIENTS; i++) pumpConn(&gConns[i], i);
    } else if (gRole == NET_CLIENT) {
        pumpConn(&gConns[0], 0);
    }
}

int netNextJoined(void) {
    if (gJoinN <= 0) return -1;
    int id = gJoinQ[0];
    memmove(gJoinQ, gJoinQ + 1, (size_t)(--gJoinN) * sizeof(int));
    return id;
}

int netNextLeft(void) {
    if (gLeftN <= 0) return -1;
    int id = gLeftQ[0];
    memmove(gLeftQ, gLeftQ + 1, (size_t)(--gLeftN) * sizeof(int));
    return id;
}

int netClientAlive(int id) {
    return (id >= 1 && id <= NET_MAX_CLIENTS) ? gConns[id].active : 0;
}

int netConnectedToHost(void) {
    return gRole == NET_CLIENT && gConns[0].active;
}

int netNextMsg(unsigned char *out, int maxLen, int *outLen, int *fromId) {
    int lo = (gRole == NET_HOST) ? 1 : 0;
    int hi = (gRole == NET_HOST) ? NET_MAX_CLIENTS : 0;
    for (int i = lo; i <= hi; i++) {
        Conn *c = &gConns[i];
        if (!c->active || c->len < 4) continue;
        unsigned int flen;
        memcpy(&flen, c->buf, 4);
        if (flen > NET_BUFCAP - 4) {        /* uszkodzona ramka */
            connClose(c);
            if (gLeftN < 16) gLeftQ[gLeftN++] = i;
            continue;
        }
        if (c->len < 4 + flen) continue;    /* niekompletna */
        int n = (int)flen;
        if (n > maxLen) n = maxLen;
        memcpy(out, c->buf + 4, (size_t)n);
        *outLen = n;
        *fromId = i;
        memmove(c->buf, c->buf + 4 + flen, c->len - 4 - flen);
        c->len -= 4 + flen;
        return 1;
    }
    return 0;
}

static void sendAll(Conn *c, const void *data, int len) {
    if (!c->active) return;
    const char *p = (const char *)data;
    int left = len;
    int guard = 0;
    while (left > 0) {
        int n = send(c->s, p, left, 0);
        if (n > 0) { p += n; left -= n; guard = 0; continue; }
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            Sleep(1);
            if (++guard > 5000) { connClose(c); return; }   /* ~5 s */
            continue;
        }
        connClose(c);
        return;
    }
}

static void sendFramed(Conn *c, const void *data, int len) {
    unsigned int flen = (unsigned int)len;
    sendAll(c, &flen, 4);
    sendAll(c, data, len);
}

void netSendTo(int id, const void *data, int len) {
    if (gRole == NET_HOST && id >= 1 && id <= NET_MAX_CLIENTS)
        sendFramed(&gConns[id], data, len);
}

void netSendHost(const void *data, int len) {
    if (gRole == NET_CLIENT)
        sendFramed(&gConns[0], data, len);
}

void netBroadcastExcept(int exceptId, const void *data, int len) {
    if (gRole == NET_HOST) {
        for (int i = 1; i <= NET_MAX_CLIENTS; i++)
            if (i != exceptId && gConns[i].active)
                sendFramed(&gConns[i], data, len);
    } else if (gRole == NET_CLIENT) {
        sendFramed(&gConns[0], data, len);
    }
}

void netLocalIp(char *out, int outCap) {
    wsaUp();
    strncpy_s(out, (size_t)outCap, "127.0.0.1", _TRUNCATE);
    char host[256];
    if (gethostname(host, sizeof(host)) != 0) return;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        struct sockaddr_in *sa = (struct sockaddr_in *)p->ai_addr;
        char tmp[64];
        if (inet_ntop(AF_INET, &sa->sin_addr, tmp, sizeof(tmp))) {
            if (strncmp(tmp, "127.", 4) != 0) {
                strncpy_s(out, (size_t)outCap, tmp, _TRUNCATE);
                break;
            }
        }
    }
    freeaddrinfo(res);
}
