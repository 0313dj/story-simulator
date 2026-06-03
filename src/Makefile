CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -fexec-charset=UTF-8 \
          -Icore -Iworld -Inpc -Iai -Isimulation -Ievents
LDFLAGS = -lcurl

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME_S),Windows)
    LDFLAGS += -lws2_32 -lwldap32 -lbcrypt -lcrypt32
endif
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    LDFLAGS += -lws2_32 -lwldap32 -lbcrypt -lcrypt32
endif

SRCS   = core/main.c \
         core/httpd.c core/backend.c core/json.c core/log.c \
         core/api.c core/crypto.c core/context.c \
         core/typeid.c core/registry.c core/mod_loader.c \
         world/map.c world/environment.c world/worldstate.c world/world_director.c \
         npc/character_card.c npc/memory.c npc/npc.c npc/npc_brain.c npc/emotion.c \
         ai/intent.c ai/planner.c ai/narrative.c \
         simulation/economy_sim.c simulation/faction_sim.c \
         simulation/population_sim.c simulation/weather_sim.c \
         events/event.c events/rule.c events/changeset.c
OBJS   = $(SRCS:.c=.o)
OUTDIR = ../bin
TARGET = $(OUTDIR)/sim.exe

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS) | $(OUTDIR)
	$(CC) $(CFLAGS) -mwindows -o $@ $(OBJS) $(LDFLAGS) -lcomctl32 -luuid -lole32
	rm -rf $(OUTDIR)/web
	cp -r ../web $(OUTDIR)/

$(OUTDIR):
	mkdir -p $(OUTDIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f core/*.o world/*.o npc/*.o ai/*.o simulation/*.o events/*.o
	rm -f $(TARGET)

run: $(TARGET)
	cd $(OUTDIR) && ./sim.exe
