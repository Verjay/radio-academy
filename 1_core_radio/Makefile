CC = gcc
CFLAGS = -Wall -Wextra -O2 -I/usr/include/x86_64-linux-gnu -pthread
LDFLAGS = -lshout -lavformat -lavcodec -lavutil -lswresample -lespeak-ng -lcurl -lcjson -pthread

# Liste tous tes fichiers source ici
SRCS = main.c audio.c tts.c api.c live.c radio.c
# Transforme la liste des .c en liste de .o
OBJS = $(SRCS:.c=.o)
TARGET = radio

all: $(TARGET)

# L'édition de liens utilise maintenant tous les fichiers .o
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Règle générique pour compiler chaque .c en .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
