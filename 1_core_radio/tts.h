#ifndef TTS_H
#define TTS_H

#include <shout/shout.h>

int speak_text(const char *text, shout_t *shout);
void *piper_generation_thread(void *arg);

#endif // TTS_H
