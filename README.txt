MineClone - Politechnika Warszawska (prototyp gry voxelowej)
=============================================================

URUCHOMIENIE
------------
Windows: uruchom MineClone.exe (Windows 10/11, 64-bit).
Linux:   zbuduj przez "make" (instrukcja nizej) i uruchom ./mineclone.

Uwaga (Windows): SmartScreen moze ostrzec przed nieznanym wydawca
(plik nie jest podpisany cyfrowo). Kliknij "Wiecej informacji"
-> "Uruchom mimo to". Pelny kod zrodlowy znajdziesz w main.c / net.c,
wiec mozesz tez zbudowac plik samodzielnie (instrukcja nizej).

CO JEST W GRZE
--------------
- Mapa: kampus Politechniki Warszawskiej z Gmachem Glownym
  (dziedziniec ze szklanym dachem, portyk z kolumnami,
   napis POLITECHNIKA na attyce, plac z fontanna, aleja drzew)
- Pasek narzedzi (hotbar) na 9 slotow jak w Minecraft
- Ekwipunek [E] ze wszystkimi rodzajami blokow (11 rodzajow)
- Zdrowie: 10 serc, obrazenia od upadku, toniecie (paski powietrza),
  regeneracja po czasie, ekran smierci i respawn
- Kamera [F5]: pierwsza osoba / trzecia osoba zza plecow / z przodu
- Model gracza: lysy czlowiek w stylu Minecraft z animacja chodzenia
- Dzwieki (syntezowane w locie, bez plikow): niszczenie i stawianie
  blokow, kroki, skok, ladowanie, dzwoneczek powitania
- NPC: Jan Pawel II stoi przy wejsciu do Gmachu Glownego - rozglada sie,
  patrzy na gracza gdy podejdziesz, a po nacisnieciu [T] mowi
  "Dzien dobry!" i macha reka
- Tryb chodzenia: grawitacja, skakanie, kolizje; plywanie; tryb latania
- Niszczenie i stawianie blokow

STEROWANIE
----------
W/S/A/D ........ ruch
Mysz ........... rozgladanie sie
Spacja ......... skok / wynurzanie / lot w gore
Lewy Shift ..... sprint
Lewy Ctrl ...... lot w dol (w trybie latania)
F .............. przelacz chodzenie <-> latanie
F5 ............. zmiana kamery (FPP / TPP zza plecow / TPP z przodu)
E .............. otworz/zamknij ekwipunek
LPM ............ zniszcz blok
PPM ............ postaw blok z wybranego slotu
1-9 ............ wybor slotu paska narzedzi
Kolko myszy .... przewijanie slotow paska narzedzi
T .............. przywitaj sie z NPC (gdy stoisz blisko)
R .............. respawn (po smierci)
ESC ............ zamknij ekwipunek / uwolnij kursor myszy

Kod sklada sie z dwoch plikow zrodlowych: main.c (gra) oraz net.c
(siec LAN). net.c jest wieloplatformowy - na Windows uzywa Winsock2,
na Linux gniazd POSIX - wiec ten sam kod kompiluje sie na obu systemach.

SAMODZIELNA KOMPILACJA (Linux, najprosciej: make)
-------------------------------------------------
Wymagane: gcc oraz make. Biblioteka raylib 5.5 dla Linuksa jest dolaczona
w katalogu raylib/ (libraylib.so + naglowki) - NIE trzeba nic instalowac
ani uzywac sudo. W katalogu projektu:

       make            # zbuduj  ->  ./mineclone
       make run        # zbuduj i uruchom
       make clean      # posprzataj

Jesli wolisz raylib z systemu (sudo apt install libraylib-dev), make wykryje
go automatycznie przez pkg-config i uzyje wersji systemowej.

Recznie (bez make), uzywajac dolaczonej biblioteki:
       gcc -O2 -std=gnu11 main.c net.c -o mineclone -I raylib/include \
           -L raylib/lib -l:libraylib.so -lm -lpthread -ldl \
           -Wl,-rpath,'$ORIGIN/raylib/lib'

SAMODZIELNA KOMPILACJA (Windows, MSVC)
--------------------------------------
Wymagane: Visual Studio Build Tools (cl.exe). Biblioteka raylib 5.5
dla MSVC jest dolaczona w katalogu raylib\ (include + lib).
W terminalu "x64 Native Tools Command Prompt":

    cl /O2 /MD /std:c11 main.c net.c /Fe:MineClone.exe /I raylib\include ^
       /link /LIBPATH:raylib\lib raylib.lib opengl32.lib gdi32.lib ^
       winmm.lib ws2_32.lib user32.lib shell32.lib ^
       /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

SAMODZIELNA KOMPILACJA (Windows, MSYS2/MinGW)
---------------------------------------------
1. Zainstaluj MSYS2 (https://www.msys2.org), otworz terminal "MINGW64":
       pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-raylib make
2. W katalogu z main.c (mingw32-make uzywa dolaczonego Makefile):
       mingw32-make
   albo recznie:
       gcc main.c net.c -o MineClone.exe -O2 -lraylib \
           -lopengl32 -lgdi32 -lwinmm -lws2_32 -mwindows

Zbudowano w oparciu o raylib 5.5 (https://www.raylib.com, licencja zlib).
