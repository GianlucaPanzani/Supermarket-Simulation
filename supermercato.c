#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include "shared_data.h"
#define MAXK     20     // massimo numero di casse nel supermercato
#define MAXC    100     // massimo numero di clienti che il supermercato puo' contenere
#define MINT     10     // minimo tempo degli acquisti
#define MAXT   1000     // massimo tempo degli acquisti
#define MAXP    500     // massimo numero di prodotti acquistabili
#define MAXTP   100     // massimo tempo di elaborazione di un prodotto
#define MINTD   300     // minimo tempo per aggiornare/informare il direttore
#define MAXTD  1000     // massimo tempo per aggiornare/informare il direttore
#define MINS      2     // minimo valore di s1 e s2
#define CONFIG_VALUE() \
    printf("LEARN: 0<K<%d, 0<C<%d, 0<E<C+1, %d<T<%d, 0<P<%d, 1<k<K+1, 0<Tp<%d, %d<Td<%d, %d<S1<K+1, %d<S2<C+1\n", MAXK+1, MAXC+1, MINT-1, MAXT+1, MAXP+1, MAXTP+1, MINTD-1, MAXTD+1, MINS-1, MINS-1);
#define BUFDIM   50     // dimensione del buffer

int K;    // # di casse
int C;    // # di clienti
int E;    // C-E = # minimo di clienti
int k;    // # casse aperte
int P;    // # max di prodotti acquistabili dai clienti
int T;    // tempo max in cui il cliente puo' acquistare prodotti (ms)
int Tp;   // tempo per l'elaborazione di un prodotto alle casse (ms)
int Td;   // tempo che indica ogni quanto aggiornare il direttore (ms)
int S1;   // se #casse (con #clienti <= 1) = S1, allora si chiude 1 cassa
int S2;   // se esiste 1 cassa con #clienti >= S2, allora si apre 1 cassa

int NUMC;              // numero di clienti nel supermercato
Cassa     *casse;      // array di casse (allocato nel main)
Cliente   *clienti;    // array di clienti (allocato nel main)
Queue     *dirqueue;   // coda per chi non ha acquistato prodotti
List      *open;       // lista di casse aperte
pthread_mutex_t mtxopen     = PTHREAD_MUTEX_INITIALIZER;   // mutex lista "open"
pthread_mutex_t mtxclienti  = PTHREAD_MUTEX_INITIALIZER;   // mutex dei clienti usciti

Pthread_t *idCasse, *idClienti;

int signalQ;
int signalH;
int clienti_usciti;

//--------------------------------------------------------------------------------

// Condizioni sui valori del file di configurazione
int condConfigValues() {
    if(K < 1 || K > MAXK) {
        perror("This supermarket will not open. In config file 'K' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(k > K || k < 1)
        k = 1;
    if(C < 2 || C > MAXC) {
        perror("This supermarket will not open. In config file 'C' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(E >= C || E < 1) {
        perror("This supermarket will not open. In config file 'E' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(T < MINT || T > MAXT) {
        perror("This supermarket will not open. In config file 'T' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(P < 0 || P > MAXP) {
        perror("This supermarket will not open. In config file 'P' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(S1 < MINS || S1 > K) {
        perror("This supermarket will not open. In config file 'S1' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(S2 < MINS || S2 > C) {
        perror("This supermarket will not open. In config file 'S2' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(Td < MINTD || Td > MAXTD) {
        perror("This supermarket will not open. In config file 'Td' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    if(Tp < 1 || Tp > MAXTP) {
        perror("This supermarket will not open. In config file 'Tp' has bad value");
        CONFIG_VALUE();
        return 1;
    }
    return 0;
}

// Handler di SIGQUIT
void signalQ_Set() {
    printf("### SIGQUIT signal recived ###\n");
    printf("Thread in chiusura rapida.\n");
    Pthread_mutex_lock(&mtxopen);
    clienti_usciti = C;
    signalQ = 1;
    Pthread_mutex_unlock(&mtxopen);
}

// Handler di SIGHUP
void signalH_Set() {
    printf("### SIGHUP signal recived ###\n");
    printf("Thread in chiusura, attendere...\n");
    signalH = 1;
}

// Funzione di pulizia della memoria (deallocazioni)
void clean_all() {
    int err;
    List* l;
    while(open != NULL) {
        l = open;
        open = open->next;
        free(l);
    }
    for(int i = 0; i < C; ++i) {
        if((err = pthread_mutex_destroy(&clienti[i].mtx)) != 0) {
            errno = err;
            perror("Destroying mutex");
            exit(errno);
        }
        if((err = pthread_cond_destroy(&clienti[i].cond)) != 0) {
            errno = err;
            perror("Destroying mutex");
            exit(errno);
        }
        if(idClienti[i].id != NULL)
            free(idClienti[i].id);
    }
    for(int i = 0; i < K; ++i) {
        if((err = pthread_mutex_destroy(&casse[i].mtx)) != 0) {
            errno = err;
            perror("Destroying mutex");
            exit(errno);
        }
        if((err = pthread_cond_destroy(&casse[i].cond)) != 0) {
            errno = err;
            perror("Destroying mutex");
            exit(errno);
        }
        if(casse[i].q != NULL) {
            while(casse[i].q->head != NULL) {
                l = casse[i].q->head;
                casse[i].q->head = casse[i].q->head->next;
                free(l);
            }
            free(casse[i].q);
        }
        if(idCasse[i].id != NULL)
            free(idCasse[i].id);
    }
    if(dirqueue != NULL) {
        while(dirqueue->head != NULL) {
            l = dirqueue->head;
            dirqueue->head = dirqueue->head->next;
            free(l);
        }
        free(dirqueue);
    }
    free(idClienti);
    free(idCasse);
    free(clienti);
    free(casse);
}

/*==================================================================================
  =======================================MAIN=======================================
  ==================================================================================*/

int main(int argc, char* argv[]) {
    FILE *config;
    int err;
    
    // Gestione degli argomenti:
    switch(argc) {
        case 1: { // --> open of config.txt (default case)
            if((config = fopen("config.txt", "r")) == NULL) {
                perror("Opening configuration file");
                exit(errno);
            }
            break;
        }
        case 2: { // --> open of argv[1]
            if((config = fopen(argv[1], "r")) == NULL) {
                perror("Opening configuration file");
                exit(errno);
            }
            break;
        }
        default: { // --> error
            perror("Usage:\t./a.out\n\t./a.out config.txt\nToo much arguments");
            exit(errno);
            break;
        }
    }
    char buf[BUFDIM], *token, *logfile;
    k = 0;
    
    // inizializzazione dei valori di uscita + handler dei segnali
    signalQ = signalH = clienti_usciti = 0;
    signal(SIGQUIT, signalQ_Set);
    signal(SIGHUP, signalH_Set);
    
    // fgets: legge la prima riga del file di configurazione (Parsing di config.txt)
    if(fgets(buf, BUFDIM, config) != NULL) {
        int cont = 0;
        token = strtok(buf, ":");
        while (token) {
	        switch(cont) {
                case 0:{
                    logfile = (char*) malloc(sizeof(char)*(strlen(token)+1));
                    strcpy(logfile, token);
                    break;
                }
                case 1: { K   = atoi(token);       break; }
                case 2: { C   = atoi(token);       break; }
                case 3: { E   = atoi(token);       break; }
                case 4: { T   = atoi(token);       break; }
                case 5: { P   = atoi(token);       break; }
                case 6: { k   = atoi(token);       break; }
                case 7: { Tp  = atoi(token);       break; }
                case 8: { Td  = atoi(token);       break; }
                case 9: { S1  = atoi(token);       break; }
                case 10:{ S2  = atoi(token);       break; }
                default:{
                    CONFIG_VALUE();
                    perror("Bad format of configuration file");
                    exit(1);
                }
            }
            cont++;
	        token = strtok(NULL, ":");
        }
    } else {
        perror("Learning from Configuration file");
        exit(errno);
    }
    
    // Chiusura del file di configurazione
    if(fclose(config) == -1) {
        perror("Closing configuration file");
        exit(errno);
    }
    
    // Condizioni sui valori letti dal file di configurazione
    if(condConfigValues() == 1) {
        free(logfile);
        exit(1);
    }
    
    printf("\n===========================================\nAPERTURA DEL SUPERMERCATO\n");
    printf("\nFormato del file di configurazione corretto.\n");
    
    // Creazione delle CASSE
    casse     = (Cassa*) malloc(sizeof(Cassa)*K);
    idCasse   = (Pthread_t*) malloc(sizeof(Pthread_t)*K);
    int openk = k;
    open = NULL;
    for(int i = 0; i < K; ++i) {
        if((err = pthread_mutex_init(&casse[i].mtx, NULL)) != 0) {
            errno = err;
            perror("Initializing mutex (cliente)");
            exit(errno);
        }
        if((err = pthread_cond_init(&casse[i].cond, NULL)) != 0) {
            errno = err;
            perror("Initializing mutex (cliente)");
            exit(errno);
        }
        idCasse[i].id     = (pthread_t*) malloc(sizeof(pthread_t));
        idCasse[i].index  = i;
        casse[i].prodotti = 0;
        casse[i].clienti  = 0;
        casse[i].dimcoda  = 0;
        casse[i].time     = 0;
        casse[i].chiusure = 0;
        casse[i].q        = (Queue*) malloc(sizeof(Queue));
        casse[i].q->head  = casse[i].q->last = NULL;
        if(openk != 0) {
            casse[i].open = 1;
            List* l = (List*) malloc(sizeof(List));
            l->index = i;
            l->next = open;
            open = l;
            openk--;
        } else {      
            casse[i].open = 0;
        }
        if((err = pthread_create(&(*(idCasse[i].id)), NULL, (void*) &fcasse, (void*) &idCasse[i].index)) != 0) {
            errno = err;
            perror("Creating thread");
            exit(errno);
        }
    }
    
    // Creazione del thread direttore (gestore dei clienti senza acquisti)
    pthread_t idqueue;
    dirqueue = (Queue*) malloc(sizeof(Queue));
    if((err = pthread_create(&idqueue, NULL, (void*) &noBoughtHandler, NULL)) != 0) {
        errno = err;
        perror("Creating thread");
        exit(errno);
    }
    
    // Creazione delle struct Clienti e idClienti
    clienti   = (Cliente*) malloc(sizeof(Cliente)*C);
    idClienti = (Pthread_t*) malloc(sizeof(Pthread_t)*C);
    NUMC = C;
    for(int i = 0; i < C; ++i) {
        if((err = pthread_mutex_init(&clienti[i].mtx, NULL)) != 0) {
            errno = err;
            perror("Initializing mutex (cliente)");
            exit(errno);
        }
        if((err = pthread_cond_init(&clienti[i].cond, NULL)) != 0) {
            errno = err;
            perror("Initializing mutex (cliente)");
            exit(errno);
        }
        idClienti[i].id      = (pthread_t*) malloc(sizeof(pthread_t));
        idClienti[i].index   = i;
        clienti[i].nprodotti = 0;
        clienti[i].shoptime  = 0;
        clienti[i].queuetime = 0;
        clienti[i].ncode     = 0;
        clienti[i].esc       = 0;
    }
    
    // Creazione del thread direttore (gestore sell'aggiunta di clienti)
    pthread_t idc;
    if((err = pthread_create(&idc, NULL, (void*) &clientsHandler, (void*) logfile)) == -1) {
        errno = err;
        perror("Creating thread");
        exit(errno);
    }
    
    // Creazione del thread direttore (gestore delle casse)
    pthread_t idk;
    if((err = pthread_create(&idk, NULL, (void*) &cashHandler, (void*) logfile)) != 0) {
        errno = err;
        perror("Creating thread");
        exit(errno);
    }
    
    // Creazione dei thread CLIENTI
    struct timespec t = {1,0};
    nanosleep(&t, NULL);    // tempo di attesa affinche' aprano tutte le casse
    for(int i = 0; i < C; ++i) {
        if((err = pthread_create(&(*(idClienti[i].id)), NULL, (void*) &fclienti, &idClienti[i].index)) != 0) {
            errno = err;
            perror("Creating thread");
            exit(errno);
        }
    }
    
    if(!M_PRINTF)
        printf("Attendere, programma in esecuzione...\n");
    
    // JOIN su tutti i thread:
    for(int i = 0; i < C; ++i) {                        // fclienti
        if((err = pthread_join(*idClienti[i].id, NULL)) != 0) {
            errno = err;
            perror("Closing thread");
            exit(errno);
        }
    }
    if((err = pthread_join(idc, NULL)) != 0) {          // clientsHandler
        errno = err;
        perror("Closing thread");
        exit(errno);
    }
    if((err = pthread_join(idqueue, NULL)) != 0) {      // noBoughtsHandler
        errno = err;
        perror("Closing thread");
        exit(errno);
    }
    for(int i = 0; i < K; ++i) {                        // fcasse
        if((err = pthread_join(*idCasse[i].id, NULL)) != 0) {
            errno = err;
            perror("Closing thread");
            exit(errno);
        }
    }
    if((err = pthread_join(idk, NULL)) != 0) {          // cashHandler
        errno = err;
        perror("Closing thread");
        exit(errno);
    }
    printf("\nChiusura dei thread avvenuta con successo.\n");
    
    // deallocazioni
    clean_all();
    free(logfile);
    printf("Deallocazioni avvenute con successo.\n\n");
    
    printf("CHIUSURA DEL SUPERMERCATO\n===========================================\n");
    return 0;
}




