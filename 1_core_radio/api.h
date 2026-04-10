#ifndef API_H
#define API_H

// L'appelant de cette fonction DOIT faire un free() sur le pointeur retourné !
char* fetch_api_secure(const char* url);

#endif // API_H
