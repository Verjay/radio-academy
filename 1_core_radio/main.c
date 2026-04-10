#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <shout/shout.h>

// Inclusion de nos nouveaux modules
#include "audio.h"
#include "live.h"
#include "radio.h"

#define MAX_TRACKS 2048 

int main() {
    shout_init();
    shout_t *shout = shout_new();

    if (!shout) {
        fprintf(stderr, "Erreur d'allocation de libshout\n");
        return 1;
    }

    srand(time(NULL));

    if (shout_set_host(shout, "localhost") != SHOUTERR_SUCCESS ||
        shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS ||
        shout_set_port(shout, 8000) != SHOUTERR_SUCCESS ||
        shout_set_password(shout, "pezdia!!57") != SHOUTERR_SUCCESS ||
        shout_set_mount(shout, "/stream.ogg") != SHOUTERR_SUCCESS ||
        shout_set_user(shout, "source") != SHOUTERR_SUCCESS ||
        shout_set_format(shout, SHOUT_FORMAT_OGG) != SHOUTERR_SUCCESS) {
        
        fprintf(stderr, "Erreur de configuration: %s\n", shout_get_error(shout));
        return 1;
    }

    printf("Tentative de connexion a Icecast...\n");
    while (shout_open(shout) != SHOUTERR_SUCCESS) {
        fprintf(stderr, "Echec (%s). Nouvel essai dans 2 secondes...\n", shout_get_error(shout));
        sleep(2);
    }
    printf("Connecte avec succes a Icecast !\n");

    FILE *file = fopen("test.ogg", "rb");
    if (!file) {
        fprintf(stderr, "Erreur: Fichier 'test.ogg' introuvable dans le dossier actuel.\n");
        shout_close(shout);
        return 1;
    }
    fclose(file); // N'oublie pas de fermer le fichier test après l'avoir vérifié !
    
    printf("Lancement de la radio. Lecture du dossier 'playlist'...\n");
    time_t last_announce_time = time(NULL);
    
    pthread_t live_thread;
    pthread_create(&live_thread, NULL, live_server_thread, NULL);
    
    while (1) {
        
        pthread_mutex_lock(&live_mutex);
        int local_live = is_live_active;
        int local_sock = live_client_socket;
        pthread_mutex_unlock(&live_mutex);

        if (local_live && local_sock != -1) {
            printf("\n🎙️ PASSAGE EN DIRECT ! La playlist est en pause.\n");
            play_live(local_sock, shout);
            printf("\n🛑 FIN DU DIRECT ! Reprise automatique de la playlist.\n");
            close(local_sock);
            
            pthread_mutex_lock(&live_mutex);
            is_live_active = 0;
            live_client_socket = -1;
            pthread_mutex_unlock(&live_mutex);
            continue;
        }

        DIR *dir = opendir("playlist");
        if (dir == NULL) {
            fprintf(stderr, "Erreur: Impossible d'ouvrir le dossier 'playlist'.\n");
            sleep(5);
            continue;
        }

        char *tracks[MAX_TRACKS];
        int track_count = 0;
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL && track_count < MAX_TRACKS) {
            if (ent->d_name[0] == '.') continue; 
            tracks[track_count] = strdup(ent->d_name); 
            track_count++;
        }
        closedir(dir);

        if (track_count == 0) {
            printf("Le dossier playlist est vide !\n");
            sleep(5);
            continue;
        }

        for (int i = track_count - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            char *temp = tracks[i];
            tracks[i] = tracks[j];
            tracks[j] = temp;
        }

        printf("\n--- Début de la playlist (Mélangée : %d titres) ---\n", track_count);
        
        for (int i = 0; i < track_count; i++) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "playlist/%s", tracks[i]);
            
            printf("Lecture en cours : %s\n", filepath);
            if (play_file(filepath, shout) < 0) {
                fprintf(stderr, "Erreur de lecture : %s\n", filepath);
            }

            time_t now = time(NULL);
            if (difftime(now, last_announce_time) >= 10) { 
                announce_time_and_weather(shout);
                last_announce_time = time(NULL);
            }

            pthread_mutex_lock(&live_mutex);
            int break_for_live = is_live_active;
            pthread_mutex_unlock(&live_mutex);
            
            if (break_for_live) {
                printf("\n⚠️ Interruption demandée pour le direct !\n");
                break; 
            }
        }

        for (int i = 0; i < track_count; i++) {
            free(tracks[i]);
        }
    }

    shout_close(shout);
    shout_free(shout);
    shout_shutdown();

    return 0;
}
