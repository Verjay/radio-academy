#include "raylib.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <unistd.h>

#define FFT_SIZE 1024
#define BARS_COUNT 60
#define RING_BUFFER_SIZE 65536 

// --- 1. FONCTION FFT ---
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

// --- 3. CALLBACK AUDIO ---
void AudioInputCallback(void *buffer, unsigned int frames) {
    short *out = (short *)buffer;
    pthread_mutex_lock(&buffer_mutex);
    for (unsigned int i = 0; i < frames; i++) {
        if (read_ptr != write_ptr) {
            out[i] = ring_buffer[read_ptr];
            read_ptr = (read_ptr + 1) % RING_BUFFER_SIZE;
        } else {
            out[i] = 0; 
        }
    }
    pthread_mutex_unlock(&buffer_mutex);
}

// --- 4. THREAD RÉSEAU (FFMPEG) ---
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
    // 1. Fenêtre plus grande : 1000px de large, 900px de haut (3 x 300px)
    InitWindow(1000, 900, "DSP Analysis Dashboard - 3 Tiers (Tall)");
    SetTargetFPS(60);

    InitAudioDevice();
    AudioStream stream = LoadAudioStream(44100, 16, 1);
    SetAudioStreamCallback(stream, AudioInputCallback);
    PlayAudioStream(stream);

    pthread_t net_thread;
    pthread_create(&net_thread, NULL, NetworkThread, NULL);

    float complex fft_raw[FFT_SIZE];
    float complex fft_deriv[FFT_SIZE];
    
    float heights_bot[BARS_COUNT] = {0};
    float heights_mid[BARS_COUNT] = {0};
    float heights_top[BARS_COUNT] = {0};
    
    float prev_mags[BARS_COUNT] = {0};

    printf("Buffering...\n");
    usleep(500000);

    while (!WindowShouldClose()) {
        pthread_mutex_lock(&buffer_mutex);
        int temp_ptr = read_ptr; 
        
        float prev_sample = ring_buffer[(temp_ptr - 1 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE] / 32768.0f;
        
        for (int i = 0; i < FFT_SIZE; i++) {
            float sample = ring_buffer[(temp_ptr + i) % RING_BUFFER_SIZE] / 32768.0f;
            float window = 0.5f * (1.0f - cosf(2.0f * PI * i / (FFT_SIZE - 1)));
            
            fft_raw[i] = (sample * window) + 0.0f * I;
            fft_deriv[i] = ((sample - prev_sample) * window) + 0.0f * I;
            prev_sample = sample;
        }
        pthread_mutex_unlock(&buffer_mutex);

        compute_fft(fft_raw, FFT_SIZE);
        compute_fft(fft_deriv, FFT_SIZE);

        BeginDrawing();
        ClearBackground(BLACK);

        // 2. Nouveaux repères Y pour les textes (0, 300, 600)
        DrawText("3. DÉRIVÉE TEMPORELLE (Filtre Passe-Haut extrême)", 10, 10, 10, LIGHTGRAY);
        DrawText("2. SPECTRAL FLUX (Détection d'attaque / Percussions)", 10, 310, 10, LIGHTGRAY);
        DrawText("1. AMPLITUDE BRUTE (EQ Classique)", 10, 610, 10, LIGHTGRAY);
        
        // 3. Lignes de séparation adaptées
        DrawLine(0, 300, 1000, 300, DARKGRAY);
        DrawLine(0, 600, 1000, 600, DARKGRAY);

        float barWidth = (float)GetScreenWidth() / BARS_COUNT;

        for (int i = 0; i < BARS_COUNT; i++) {
            float ratio = (float)i / (BARS_COUNT - 1);
            int bin = (int)powf(250.0f, ratio) + 1; 

            // --- REZ-DE-CHAUSSÉE (Amplitude Brute) ---
            float mag_raw = cabsf(fft_raw[bin]) / (FFT_SIZE / 2.0f);
            float current_raw = mag_raw; 
            
            mag_raw = mag_raw * (1.0f + (ratio * 40.0f));
            // Gain augmenté à 1800 pour remplir les 300px
            float target_bot = powf(mag_raw, 1.5f) * 1800.0f;
            if (target_bot > 290.0f) target_bot = 290.0f; // Plafond à 290px
            heights_bot[i] = heights_bot[i] * 0.75f + target_bot * 0.25f;

            // --- ÉTAGE DU MILIEU (Spectral Flux) ---
            float flux = current_raw - prev_mags[i];
            if (flux < 0.0f) flux = 0.0f; 
            prev_mags[i] = current_raw;   

            flux = flux * (1.0f + (ratio * 40.0f));
            // Gain augmenté à 6000
            float target_mid = powf(flux, 1.5f) * 6000.0f; 
            if (target_mid > 290.0f) target_mid = 290.0f;
            heights_mid[i] = heights_mid[i] * 0.50f + target_mid * 0.50f; 

            // --- ÉTAGE DU HAUT (Dérivée Temporelle) ---
            float mag_deriv = cabsf(fft_deriv[bin]) / (FFT_SIZE / 2.0f);
            mag_deriv = mag_deriv * (1.0f + (ratio * 5.0f)); 
            // Gain augmenté à 22500
            float target_top = powf(mag_deriv, 1.5f) * 22500.0f;
            if (target_top > 290.0f) target_top = 290.0f;
            heights_top[i] = heights_top[i] * 0.75f + target_top * 0.25f;

            // --- DESSIN DES 3 ÉTAGES ---
            Color col = ColorFromHSV((float)i * 360 / BARS_COUNT, 0.8f, 0.9f);
            
            // Dessin Haut (Base à 300)
            DrawRectangleV((Vector2){i * barWidth, 300 - heights_top[i]}, (Vector2){barWidth - 2, heights_top[i]}, col);
            // Dessin Milieu (Base à 600)
            DrawRectangleV((Vector2){i * barWidth, 600 - heights_mid[i]}, (Vector2){barWidth - 2, heights_mid[i]}, col);
            // Dessin Bas (Base à 900)
            DrawRectangleV((Vector2){i * barWidth, 900 - heights_bot[i]}, (Vector2){barWidth - 2, heights_bot[i]}, col);
        }
        EndDrawing();
    }

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
