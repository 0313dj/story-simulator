CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2
LDFLAGS = -lcurl -lws2_32

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME_S),Windows)
    LDFLAGS += -lws2_32 -lwldap32 -lbcrypt -lcrypt32
endif
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    LDFLAGS += -lws2_32 -lwldap32 -lbcrypt -lcrypt32
endif

SRCS   = main.c httpd.c backend.c json.c log.c character_card.c environment.c memory.c api.c map.c npc.c crypto.c worldstate.c event.c rule.c changeset.c intent.c planner.c narrative.c npc_brain.c economy_sim.c faction_sim.c weather_sim.c population_sim.c world_director.c
OBJS   = $(SRCS:.c=.o)
OUTDIR = ../bin
TARGET = $(OUTDIR)/sim.exe

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS) | $(OUTDIR)
	$(CC) $(CFLAGS) -mwindows -o $@ $(OBJS) $(LDFLAGS) -lcomctl32 -luuid -lole32
	cp -r ../web $(OUTDIR)/

$(OUTDIR):
	mkdir -p $(OUTDIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o
	rm -f $(TARGET)

run: $(TARGET)
	cd $(OUTDIR) && ./sim.exe
