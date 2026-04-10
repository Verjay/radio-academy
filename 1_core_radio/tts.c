#include "tts.h"
#include "audio.h" // Nécessaire pour appeler play_file()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <espeak-ng/speak_lib.h>

// Variables globales propres à ce fichier
static int16_t *tts_buffer = NULL;
static int tts_buffer_samples = 0;

static int tts_callback(short *wav, int numsamples, espeak_EVENT *events) {
    (void)events; 
    if (wav == NULL || numsamples == 0) return 0;

    int16_t *temp = realloc(tts_buffer, (tts_buffer_samples + numsamples) * sizeof(int16_t));
    if (!temp) return 0; 
    tts_buffer = temp;

    memcpy(tts_buffer + tts_buffer_samples, wav, numsamples * sizeof(int16_t));
    tts_buffer_samples += numsamples;
    return 0;
}

static void save_pcm_to_wav(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;

    int byte_rate = 22050 * 2;
    int data_size = tts_buffer_samples * 2;
    int chunk_size = 36 + data_size;
    int subchunk1_size = 16;
    short audio_format = 1, num_channels = 1, block_align = 2, bits_per_sample = 16;
    int sample_rate = 22050; 

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&subchunk1_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(tts_buffer, 2, tts_buffer_samples, f);
    fclose(f);
}

int speak_text(const char *text, shout_t *shout) {
    tts_buffer_samples = 0;
    if (tts_buffer) { free(tts_buffer); tts_buffer = NULL; }

    espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, NULL, 0);
    espeak_SetSynthCallback(tts_callback);
    espeak_SetVoiceByName("fr");

    espeak_Synth(text, strlen(text) + 1, 0, POS_CHARACTER, 0, espeakCHARS_AUTO, NULL, NULL);

    if (tts_buffer_samples > 0) {
        save_pcm_to_wav("tts_temp.wav");
        printf("\n🗣️ Annonce vocale : \"%s\"\n", text);
        play_file("tts_temp.wav", shout);
    }

    if (tts_buffer) { free(tts_buffer); tts_buffer = NULL; }
    return 0;
}

void *piper_generation_thread(void *arg) {
    char *text = (char *)arg;
    char commande[2048];
    static int voice_toggle = 0; 
    const char *model_name;

    model_name = "fr_FR-upmc-medium.onnx";

    /* Si tu veux plusieurs voix qui s'alternent tu peux utiliser ce genre de blocs,
    la j'ai préféré en mettre qu'une pour l'instant
    if (voice_toggle == 0) {
        model_name = "en_GB-alan-medium.onnx"; 
    } else {
        model_name = "fr_FR-upmc-medium.onnx";  
    }*/
    voice_toggle = !voice_toggle;

    snprintf(commande, sizeof(commande), 
             "echo \"%s\" | ./piper/piper --model ./piper/%s --output_file tts_temp.wav", 
             text, model_name);
    
    printf("⚙️  Exécution Piper avec l'animateur : %s\n", model_name);

    int ret = system(commande);
    if (ret != 0) {
        fprintf(stderr, "⚠️ Erreur : Piper a échoué (code retour : %d).\n", ret);
    }
    
    return NULL;
}
