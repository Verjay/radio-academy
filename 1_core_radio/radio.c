#include "radio.h"
#include "tts.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

void announce_time_and_weather(shout_t *shout) {
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    
    char random_info[256] = ""; 
    FILE *f = fopen("infos.txt", "r");
    
    if (f) {
        char lines[50][256]; 
        int count = 0;
        
        while (fgets(lines[count], sizeof(lines[count]), f) != NULL && count < 50) {
            lines[count][strcspn(lines[count], "\n")] = 0;
            if (strlen(lines[count]) > 0) {
                count++;
            }
        }
        fclose(f);
        
        if (count > 0) {
            int rand_index = rand() % count;
            strncpy(random_info, lines[rand_index], sizeof(random_info) - 1);
        }
    }

    if (local_time != NULL) {
        char phrase[512];
        
        if (strlen(random_info) > 0) {
            snprintf(phrase, sizeof(phrase), "Bonjour, il est exactement %d heure %d. %s. Retour à la musique.", 
                     local_time->tm_hour, local_time->tm_min, random_info);
        } else {
            snprintf(phrase, sizeof(phrase), "Bonjour, il est exactement %d heure %d. Retour à la musique.", 
                     local_time->tm_hour, local_time->tm_min);
        }
        
        printf("🎙️ Lancement de la génération IA en arrière-plan...\n");
        
        pthread_t tts_thread;
        pthread_create(&tts_thread, NULL, piper_generation_thread, (void *)phrase);
        
        const char *mes_jingles[] = {
            "jingle1.ogg",
            "jingle2.ogg"
        };
        
        int nb_jingles = sizeof(mes_jingles) / sizeof(mes_jingles[0]);
        int index_choisi = rand() % nb_jingles;
        
        printf("🎵 Lecture du Jingle d'attente : %s...\n", mes_jingles[index_choisi]);
        play_file(mes_jingles[index_choisi], shout);
    
        pthread_join(tts_thread, NULL);
    
        printf("🗣️ Lecture de la voix IA...\n");
        play_file("tts_temp.wav", shout);
    } else {
        speak_text("Bonjour. Impossible de lire l'horloge système. La musique continue.", shout);
    }
}
