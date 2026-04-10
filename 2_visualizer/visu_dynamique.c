#include "raylib.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <unistd.h>

#define FFT_SIZE 1024
#define BARS_COUNT 60
#define RING_BUFFER_SIZE 65536 // Environ 1.5s de sécurité

// --- 1. FONCTION FFT (DOIT ÊTRE ICI EN HAUT) ---
void compute_fft(float complex buf[], int n) {
    if (n <= 1) return;
    float complex even[n/2], odd[n/2];
    for (int i = 0; i < n/2; i++) {
        even[i] = buf[i*2];
        odd[i] = buf[i*2 + 1];
    }
    compute_fft(even, n/2);
    compute_fft(odd, n/2);
    for (int i = 0; i < n/2; i++) {
        float complex t = cexp(-I * 2 * PI * i / n) * odd[i];
        buf[i] = even[i] + t;
        buf[i + n/2] = even[i] - t;
    }
}

// --- 2. GESTION DU BUFFER CIRCULAIRE ---
short ring_buffer[RING_BUFFER_SIZE];
int write_ptr = 0;
int read_ptr = 0;
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- 3. CALLBACK AUDIO (Appelé par la carte son) ---
void AudioInputCallback(void *buffer, unsigned int frames) {
    short *out = (short *)buffer;
    pthread_mutex_lock(&buffer_mutex);
    
    for (unsigned int i = 0; i < frames; i++) {
        if (read_ptr != write_ptr) {
            out[i] = ring_buffer[read_ptr];
            read_ptr = (read_ptr + 1) % RING_BUFFER_SIZE;
        } else {
            out[i] = 0; // Silence si on est à sec
        }
    }
    pthread_mutex_unlock(&buffer_mutex);
}

// --- 4. THREAD DE RÉCEPTION (Lit FFmpeg) ---
void* NetworkThread(void* arg) {
    short temp[512];
    while (fread(temp, sizeof(short), 512, stdin) == 512) {
        pthread_mutex_lock(&buffer_mutex);
        for (int i = 0; i < 512; i++) {
            ring_buffer[write_ptr] = temp[i];
            write_ptr = (write_ptr + 1) % RING_BUFFER_SIZE;
        }
        pthread_mutex_unlock(&buffer_mutex);
    }
    return NULL;
}

int main() {
    // Initialisation graphique
    InitWindow(800, 450, "Pro Audio Visualizer - Callback Mode");
    SetTargetFPS(60);

    // Initialisation Audio
    InitAudioDevice();
    AudioStream stream = LoadAudioStream(44100, 16, 1);
    SetAudioStreamCallback(stream, AudioInputCallback);
    PlayAudioStream(stream);

    // Lancement du thread de lecture
    pthread_t net_thread;
    pthread_create(&net_thread, NULL, NetworkThread, NULL);

    float complex fft_data[FFT_SIZE];
    float heights[BARS_COUNT] = {0};
    float max_seen = 0.1f;

    // Attendre un tout petit peu que le buffer se remplisse (0.5s)
    printf("Buffering...\n");
    usleep(500000);

    while (!WindowShouldClose()) {
        // On récupère les données pour la FFT sans faire avancer le read_ptr
        pthread_mutex_lock(&buffer_mutex);
        int temp_ptr = read_ptr;
        
        for (int i = 0; i < FFT_SIZE; i++) {
            // Calcul de la fenêtre de Hann (adoucit les bords du signal)
            float multiplier = 0.5f * (1.0f - cosf(2.0f * PI * i / (FFT_SIZE - 1)));
            
            // On applique le multiplicateur à l'échantillon
            float sample = (ring_buffer[(temp_ptr + i) % RING_BUFFER_SIZE] / 32768.0f);
            fft_data[i] = (sample * multiplier) + 0.0f * I;
        }
        
        pthread_mutex_unlock(&buffer_mutex);

        compute_fft(fft_data, FFT_SIZE);

        BeginDrawing();
        ClearBackground(BLACK);
        
        float barWidth = (float)GetScreenWidth() / BARS_COUNT;
        
        // Ce multiplicateur fixe remplace l'auto-gain. 
        // S'il est trop bas, les barres seront petites. S'il est trop haut, elles satureront.
        // 1200.0f est un bon point de départ pour un volume système normal.
        const float FIXED_GAIN = 1200.0f; 
        
        for (int i = 0; i < BARS_COUNT; i++) {
            float ratio = (float)i / (BARS_COUNT - 1);
            
            // On garde l'excellente séparation des fréquences (Axe X logarithmique)
            int bin = (int)powf(250.0f, ratio) + 1; 
        
            // 1. Normalisation de base
            float m = cabsf(fft_data[bin]) / (FFT_SIZE / 2.0f);
        
            // 2. Égalisation (EQ Tilt) : on booste les aigus
            // Comme on n'est plus en dB, il faut booster les aigus beaucoup plus fort 
            // pour qu'ils soient visibles face aux basses.
            m = m * (1.0f + (ratio * 40.0f)); 
        
            // 3. Contraste Dynamique (Le retour du "Punch")
            // En mettant la valeur au carré, les sons faibles restent écrasés en bas,
            // et les fréquences dominantes explosent vers le haut.
            // (Si c'est TROP contrasté, enlève le powf et garde juste "m")
            m = powf(m, 1.5f); 
        
            // Calcul de la cible avec notre gain fixe
            float target = m * FIXED_GAIN;
            
            // Plafond strict à 450 pixels (hauteur de la fenêtre)
            if (target < 0.0f) target = 0.0f;
            if (target > 450.0f) target = 450.0f;
        
            // Lissage pour garder le mouvement fluide
            heights[i] = heights[i] * 0.75f + target * 0.25f;
        
            Color col = ColorFromHSV((float)i * 360 / BARS_COUNT, 0.8f, 0.9f);
            DrawRectangleV((Vector2){i * barWidth, 450 - heights[i]}, (Vector2){barWidth - 2, heights[i]}, col);
        }
        
        EndDrawing();
    }

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
