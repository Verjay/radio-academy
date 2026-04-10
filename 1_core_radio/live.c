#include "live.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Définition (initialisation) des variables globales
int is_live_active = 0;
int live_client_socket = -1;
pthread_mutex_t live_mutex = PTHREAD_MUTEX_INITIALIZER;

void *live_server_thread(void *arg) {
    (void)arg; 
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[256]; 

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed"); return NULL;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(12345);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed"); return NULL;
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed"); return NULL;
    }

    printf("🎙️  Serveur LIVE en écoute sur 127.0.0.1:12345...\n");

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            continue;
        }

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(new_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(new_socket, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0'; 
            
            if (strncmp(buffer, LIVE_TOKEN, strlen(LIVE_TOKEN)) == 0) {
                
                pthread_mutex_lock(&live_mutex);
                if (is_live_active == 1) {
                    pthread_mutex_unlock(&live_mutex);
                    printf("\n❌ [LIVE] Un flux est déjà en cours. Rejet de la nouvelle connexion.\n");
                    close(new_socket);
                    continue;
                }
                
                printf("\n🚨 [ALERTE] Demande de LIVE validée ! En attente de fin de piste musicale...\n");
                is_live_active = 1;
                live_client_socket = new_socket;
                pthread_mutex_unlock(&live_mutex);
                
                tv.tv_sec = 0;
                setsockopt(new_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
                
                continue; 
            } else {
                printf("\n❌ [LIVE] Tentative refusée (Mauvais mot de passe).\n");
            }
        }
        close(new_socket);
    }
    return NULL;
}
