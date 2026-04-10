#ifndef AUDIO_H
#define AUDIO_H

#include <shout/shout.h>

// Déclaration des fonctions publiques de ce module
int play_file(const char *filepath, shout_t *shout);
int play_live(int sock, shout_t *shout);

#endif // AUDIO_H
