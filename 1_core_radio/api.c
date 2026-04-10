#include "api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define MAX_API_RESPONSE_SIZE 8192 

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    if (mem->size + realsize > MAX_API_RESPONSE_SIZE) {
        fprintf(stderr, "\n[ALERTE SECURITE] Reponse API > 8Ko rejetee.\n");
        return 0; 
    }

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0; 

    return realsize;
}

char* fetch_api_secure(const char* url) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Radio-C-Agent/1.0");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        const char *proxy = getenv("HTTP_PROXY");
        if (!proxy) proxy = getenv("http_proxy"); 
        
        if (proxy) {
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
            const char *proxy_user = getenv("PROXY_USER");
            const char *proxy_pass = getenv("PROXY_PASS");
            
            if (proxy_user && proxy_pass) {
                char credentials[256];
                snprintf(credentials, sizeof(credentials), "%s:%s", proxy_user, proxy_pass);
                curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, credentials);
            }
        }

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "Erreur API (%s) : %s\n", url, curl_easy_strerror(res));
            free(chunk.memory);
            chunk.memory = NULL;
        }
        curl_easy_cleanup(curl);
    }
    return chunk.memory; 
}
