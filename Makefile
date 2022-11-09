CC = gcc

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = build

EXE = $(BIN_DIR)/sbubby.exe
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

LDLIBS = -lmingw32 -lSDL2main -lSDL2 -lmpv
INCLUDES = -Iinclude

CPPFLAGS = $(INCLUDES) -MMD -MP
CFLAGS = -Wall

.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJ) | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) -c -o $@ $< $(CPPFLAGS) $(CFLAGS)

$(BIN_DIR) $(OBJ_DIR):
	@mkdir $@

clean: | $(OBJ_DIR)
	@rmdir $(OBJ_DIR) /s /q

-include $(OBJ:.o=.d)