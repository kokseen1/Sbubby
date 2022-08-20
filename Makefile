CC = gcc
TARGET_EXEC = sbubby.exe
SOURCES = main.c slre.c

OBJ_DIR = obj
OBJS = $(SOURCES:%.c=$(OBJ_DIR)/%.o)

DEPS := $(OBJS:.o=.d)

LIB_DIR = external/lib
LIB_DIRS = $(wildcard $(LIB_DIR)/*)

_LIBS = mingw32 SDL2main SDL2 mpv
LIBS = $(addprefix -l, $(_LIBS))

INC_DIRS = external/include
INCLUDES = $(addprefix -I, $(INC_DIRS))

CFLAGS = -Wall -g -MMD -MP $(INCLUDES)
LFLAGS = $(addprefix -L, $(LIB_DIRS))

all: $(TARGET_EXEC)

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(OBJ_DIR):
	@mkdir obj

$(TARGET_EXEC): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LFLAGS) $(LIBS)

clean: | $(OBJ_DIR)
	@rmdir $(OBJ_DIR) /s /q

-include $(DEPS)