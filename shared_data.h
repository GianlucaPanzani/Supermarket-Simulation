#include <pthread.h>
#define M_PRINTF 0

// --------------STRUCT---------------

typedef struct List {
    int index;
    struct List* next;
}List;

typedef struct Queue {
    List* head;
    List* last;
}Queue;

typedef struct Cassa {
        long id;                // identificatore numerico della cassa
        int prodotti;           // numero di prodotti elaborati
        int clienti;            // numero di clienti passati
        int dimcoda;            // dimensione attuale della coda
        int time;               // tempo di apertura totale (ms)
        int chiusure;           // numero di chiusure
        Queue* q;               // coda di clienti
        pthread_mutex_t mtx;    // 1 mutex per cassa
        pthread_cond_t cond;    // 1 condition variable per cassa
        int open;               // open = 1  --> cassa aperta, open = 0  --> cassa chiusa
}Cassa;

typedef struct Cliente {
        long id;                // identificatore numerico del cliente
        int nprodotti;          // numero di prodotti acquistati
        int shoptime;           // tempo passato a fare spesa (ms)
        int queuetime;          // tempo passato in coda (ms)
        int ncode;              // numero di code in cui e' stato il cliente
        pthread_mutex_t mtx;    // 1 mutex per cliente
        pthread_cond_t cond;    // 1 condition variable per cliente
        int esc;                // esc =  0  --> cliente nel supermercato, esc =  1  --> cliente uscito
}Cliente;

typedef struct Pthread_t {
    pthread_t* id;
    int index;
}Pthread_t;

//-------------SHARED DATA---------------

extern Cassa   *casse;      // array di casse (allocato nel main)
extern Cliente *clienti;    // array di clienti (allocato nel main)
extern Queue   *dirqueue;   // coda per chi non ha acquistato prodotti
extern List    *open;       // lista di casse aperte

extern pthread_mutex_t mtxopen;     // mutex della lista "open"
extern pthread_mutex_t mtxclienti;  // mutex dei clienti usciti
extern pthread_mutex_t mtxdirK;     // mutex del gestore delle casse
extern pthread_cond_t  conddirK;    // condition variable del gestore delle casse

extern int K;    // # casse
extern int C;    // # max di clienti
extern int k;    // # casse aperte
extern int E;    // C-E = # minimo di clienti
extern int P;    // # max di prodotti acquistabili dai clienti
extern int T;    // tempo max in cui il cliente puo' acquistare prodotti (ms)
extern int Tp;   // tempo per l'elaborazione di un prodotto alle casse (ms)
extern int Td;   // tempo che indica ogni quanto aggiornare il direttore (ms)
extern int S1;   // se #casse (con #clienti <= 1) = S1, allora si chiude 1 cassa
extern int S2;   // se esiste 1 cassa con #clienti >= S2, allora si apre 1 cassa
extern int NUMC; // # clienti nel supermercato

extern int signalQ;             // flag di SIGQUIT
extern int signalH;             // flag di SIGHUP
extern int clienti_usciti;      // # clienti definitivamente usciti

//-------------SHARED FUNCTIONS---------------

void Pthread_mutex_lock(pthread_mutex_t*);
void Pthread_mutex_unlock(pthread_mutex_t*);
void Pthread_cond_signal(pthread_cond_t*);
void Pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*);

extern void pushCassa(int, int);

//-------------THREADS---------------

extern void* fclienti(void*);
extern void* fcasse(void*);
extern void* cashHandler(void*);
extern void* clientsHandler(void*);
extern void* noBoughtHandler();

