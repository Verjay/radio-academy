#ifndef LIVE_H
#define LIVE_H

#include <pthread.h>

// Le mot de passe pour prendre le direct
#define LIVE_TOKEN "ANTENNE"

// Variables globales partagées avec main.c
extern int is_live_active;
extern int live_client_socket;
extern pthread_mutex_t live_mutex;

void *live_server_thread(void *arg);

#endif // LIVE_H
