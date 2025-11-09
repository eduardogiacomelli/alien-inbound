# Makefile for Anti-Aircraft Game (flicker-free + responsive)

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -pthread
LDFLAGS = -pthread
LIBS = -lncurses -lm

TARGET = anti-aerea
SRCDIR = src

SOURCES = $(SRCDIR)/main.c \
          $(SRCDIR)/game.c \
          $(SRCDIR)/threads.c \
          $(SRCDIR)/render.c \
          $(SRCDIR)/input.c

OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS) $(LIBS)
	@echo "Build complete! Run ./$(TARGET) [0|1|2]"

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Done."

run: $(TARGET)
	./$(TARGET) 1

debug: CFLAGS += -O0 -DDEBUG
debug: all

.PHONY: all clean run debug
