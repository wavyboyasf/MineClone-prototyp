/*
 * net.h - prosta warstwa sieciowa TCP (LAN) dla MineClone.
 * Oddzielny modul, bo winsock2.h/windows.h koliduje nazwami z raylib.h
 * (Rectangle, CloseWindow, ShowCursor, DrawText...).
 */
#ifndef NET_H
#define NET_H

#define NET_MAX_CLIENTS 7          /* + host = 8 graczy */
#define NET_PORT_DEFAULT 7777

/* role */
enum { NET_OFF = 0, NET_HOST = 1, NET_CLIENT = 2 };

int  netStartHost(int port);                 /* 0 = ok */
int  netConnect(const char *ip, int port);   /* 0 = ok (laczenie nieblokujace dokonczone) */
void netStop(void);
int  netRole(void);

void netPump(void);                          /* przyjmij polaczenia + odbierz dane */

/* zdarzenia (host): id klienta 1..NET_MAX_CLIENTS albo -1 */
int  netNextJoined(void);
int  netNextLeft(void);
int  netClientAlive(int id);                 /* host: czy klient id podlaczony */
int  netConnectedToHost(void);               /* klient: czy polaczenie zyje */

/* wiadomosci: zwraca 1 i wypelnia out/outLen/fromId (host: 1..N, klient: 0) */
int  netNextMsg(unsigned char *out, int maxLen, int *outLen, int *fromId);

void netSendTo(int id, const void *data, int len);          /* host -> klient id */
void netSendHost(const void *data, int len);                /* klient -> host  */
void netBroadcastExcept(int exceptId, const void *data, int len);  /* host -> wszyscy poza id */

void netLocalIp(char *out, int outCap);      /* najlepszy lokalny adres IPv4 */

#endif
