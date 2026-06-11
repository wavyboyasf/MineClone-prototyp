# Makefile - MineClone (wieloplatformowy: Linux + Windows)
#
#   make            - zbuduj gre
#   make run        - zbuduj i uruchom
#   make test       - zbuduj wariant testowy (main_test.c)
#   make clean      - usun pliki wynikowe
#
# Linux:   wymaga gcc. Biblioteka raylib jest dolaczona w raylib/ (uzywana
#          jest wersja systemowa, jesli dostepna przez pkg-config).
# Windows: uzyj MinGW (mingw32-make) - patrz README.txt po opcje MSVC.

CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu11 -Wall
SRC      = main.c net.c entities.c
INCLUDE  = -I raylib/include

# --- wykrycie systemu operacyjnego --------------------------------------
ifeq ($(OS),Windows_NT)
    PLATFORM = windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM = macos
    else
        PLATFORM = linux
    endif
endif

# --- ustawienia per platforma -------------------------------------------
ifeq ($(PLATFORM),windows)
    BIN      = MineClone.exe
    BINTEST  = MineCloneTest.exe
    LDFLAGS  = -L raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -mwindows
endif

ifeq ($(PLATFORM),linux)
    BIN      = mineclone
    BINTEST  = mineclone_test
    # Preferuj raylib systemowy (pkg-config); w przeciwnym razie uzyj
    # dolaczonej biblioteki wspoldzielonej z raylib/lib (rpath wzgledem binarki).
    HAVE_PKG := $(shell pkg-config --exists raylib && echo yes)
    ifeq ($(HAVE_PKG),yes)
        INCLUDE  = $(shell pkg-config --cflags raylib)
        LDFLAGS  = $(shell pkg-config --libs raylib) -lm
    else
        LDFLAGS  = -L raylib/lib -l:libraylib.so -lm -lpthread -ldl \
                   -Wl,-rpath,'$$ORIGIN/raylib/lib'
    endif
endif

ifeq ($(PLATFORM),macos)
    BIN      = mineclone
    BINTEST  = mineclone_test
    LDFLAGS  = -L raylib/lib -lraylib -lm \
               -framework CoreVideo -framework IOKit -framework Cocoa \
               -framework GLUT -framework OpenGL
endif

# --- cele ----------------------------------------------------------------
.PHONY: all run test clean

all: $(BIN)

$(BIN): $(SRC) net.h raylib/include/raylib.h
	$(CC) $(CFLAGS) $(SRC) -o $@ $(INCLUDE) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

test: net.c net.h
	$(CC) $(CFLAGS) main_test.c net.c -o $(BINTEST) $(INCLUDE) $(LDFLAGS)

# Usuwa tylko wynik budowania na Linux. Dolaczone w repo binarki Windows
# (MineClone.exe, *.obj) celowo NIE sa kasowane.
clean:
	rm -f mineclone mineclone_test
