#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "shared_data.h"

pthread_mutex_t mtxdirC  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  conddirC = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mtxdirqueue  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  conddirqueue = PTHREAD_COND_INITIALIZER;

//-------------------------------------------------------

void popDir() {
    if(dirqueue != NULL && dirqueue->head != NULL) {
        List* p         = dirqueue->head;
        dirqueue->head  = dirqueue->head->next;
        free(p);
        return;
    }
}

void pushDir(int ic) {
    List* p  = (List*) malloc(sizeof(List));
    p->index = ic;
    p->next  = NULL;
    if(dirqueue != NULL && dirqueue->head == NULL) {
        dirqueue->head = dirqueue->last = p;
    } else {
        dirqueue->last->next = p;
        dirqueue->last = dirqueue->last->next;
    }
}

// Funzione che permette a nuovi clienti di entrare nel supermercato
void clientUpdate(FILE** log) {
    for(int i = 0; i < C; ++i) {
        if(clienti[i].esc == 1) {
            Pthread_mutex_lock(&clienti[i].mtx);
            int secq    = clienti[i].queuetime/1000;
            int msecq   = clienti[i].queuetime-(secq*1000);
            int sectot  = (clienti[i].shoptime+clienti[i].queuetime)/1000;
            int msectot = (clienti[i].shoptime+clienti[i].queuetime)-(sectot*1000);
            fprintf(*log, "| %ld | %d | %d.%d | %d.%d | %d |/", clienti[i].id, clienti[i].nprodotti, sectot, msectot, secq, msecq, clienti[i].ncode);
            //fprintf(*log, "| C:%ld | %d | %d.%d | %d.%d | %d |/", clienti[i].id, clienti[i].nprodotti, sectot, msectot, secq, msecq, clienti[i].ncode);
            clienti[i].esc = 0;
            if(signalH+signalQ == 0) {
                clienti[i].nprodotti = 0;
                clienti[i].shoptime  = 0;
                clienti[i].queuetime = 0;
                clienti[i].ncode     = 0;
                NUMC++;
            }
            Pthread_cond_signal(&clienti[i].cond);
            Pthread_mutex_unlock(&clienti[i].mtx);
        }
    }
    return;
}

//--------------------------------------------------------

// THREAD GESTORE LISTA CLIENTI SENZA ACQUISTI
void* noBoughtHandler() {
    dirqueue->head = dirqueue->last = NULL;
    struct timespec t = {0,100000000};
    
    // Ciclo di gestione dei clienti senza acquisti
    Pthread_mutex_lock(&mtxdirqueue);
    while(1) {
        while(dirqueue->head == NULL && signalH+signalQ == 0)
            Pthread_cond_wait(&conddirqueue, &mtxdirqueue);
        if(signalH+signalQ != 0 && clienti_usciti == C && dirqueue->head == NULL)
            break;
        else
            nanosleep(&t, NULL);
        if(dirqueue->head != NULL)
            popDir();
        Pthread_cond_signal(&conddirqueue);
    }
    Pthread_mutex_unlock(&mtxdirqueue);
    
    if(M_PRINTF)
        printf("CHIUSURA DEL THREAD GESTORE DEI CLIENTI SENZA ACQUISTI (noBoughtHandler)\n");
    pthread_exit(NULL);
}

// THREAD PER L'AGGIUNTA DEI CLIENTI NEL SUPERMERCATO
void* clientsHandler(void* arg) {
    char* logfile = (char*) arg;
    FILE* log;
    if((log = fopen(logfile, "a")) == NULL) {
        perror("Opening log file");
        exit(errno);
    }
    
    // Ciclo di gestione dei clienti
    Pthread_mutex_lock(&mtxdirC);
    while(clienti_usciti != C) {
        Pthread_cond_wait(&conddirC, &mtxdirC);
        if(NUMC <= C-E)
            clientUpdate(&log);
    }
    Pthread_mutex_unlock(&mtxdirC);
    
    
    if(fclose(log) == -1) {
        perror("Closing log file");
        exit(errno);
    }
    
    // Segnala la chiusura al gestore dei clienti senza acquisti
    Pthread_mutex_lock(&mtxdirqueue);
    Pthread_cond_signal(&conddirqueue);
    Pthread_mutex_unlock(&mtxdirqueue);
    
    // Segnala la chiusura al gestore delle casse
    Pthread_mutex_lock(&mtxdirK);
    Pthread_cond_signal(&conddirK);
    Pthread_mutex_unlock(&mtxdirK);
    
    if(M_PRINTF)
        printf("CHIUSURA DEL THREAD GESTORE DEI CLIENTI (clientsHandler)\n");
    pthread_exit(NULL);
}

// THREAD CLIENTE
void* fclienti(void* arg) {
    int index = *((int*) arg);
    long msec;
    struct timespec tstart, tend;
    int i = 0;
    unsigned int seed;
    
    while(1) {
        if(signalH+signalQ != 0)
            break;
        i++;
        clienti[index].id = (long) pthread_self()*i;
        if(M_PRINTF)
            printf("Cliente%d[%ld]: e' nel supermercato\n", i, clienti[index].id);
        
        // tempo a fare spesa
        seed = time(NULL)^clienti[index].id;
        long r = rand_r(&seed)%(T*1000000);
        struct timespec t = {0,r};
        nanosleep(&t, NULL);
        Pthread_mutex_lock(&clienti[index].mtx);        // LOCK CLIENTI
        clienti[index].shoptime = r/1000000;
        Pthread_mutex_unlock(&clienti[index].mtx);      // UNLOCK CLIENTI
        
        // numero massimo di prodotti
        seed = time(NULL)^clienti[index].id;
        Pthread_mutex_lock(&clienti[index].mtx);        // LOCK CLIENTI
        clienti[index].nprodotti = rand_r(&seed)%P;
        Pthread_mutex_unlock(&clienti[index].mtx);      // UNLOCK CLIENTI
        if(clienti[index].nprodotti == 0) {
            Pthread_mutex_lock(&mtxdirqueue);               // LOCK NOBOUGHTSHANDLER
            if(signalQ != 0) {
                Pthread_cond_wait(&conddirqueue, &mtxdirqueue);
                Pthread_mutex_unlock(&mtxdirqueue);
                break;
            }
            pushDir(index);
            if(M_PRINTF)
                printf("Cliente%d[%ld]: si trova nella coda senza acquisti\n", index, clienti[index].id);
            clienti[index].ncode++;
            clock_gettime(CLOCK_REALTIME, &tstart);
            Pthread_cond_signal(&conddirqueue);
            // attesa in coda (senza acquisti)
            Pthread_cond_wait(&conddirqueue, &mtxdirqueue);
            Pthread_mutex_unlock(&mtxdirqueue);             // UNLOCK NOBOUGHTSHANDLER 
        } else {
            // scelta random di una cassa aperta
            seed = time(NULL)^clienti[index].id;
            Pthread_mutex_lock(&mtxopen);                   // LOCK LISTA CASSE
            if(signalQ != 0) {              
                Pthread_mutex_unlock(&mtxopen);
                break; //----------------> !!!: nella relazione specificare che alcuni
            }                           // degli ultimi clienti possono avere tempo
            int i = rand_r(&seed)%k;    // in coda = 0 e ncode = 0 perche' usciti
            List* l = open;             // ancor prima di essere inseriti (con SIGQUIT).
            while(l->next != NULL) {
                if(i == 0)
                    break;
                i--;
                l = l->next;
            }
            Pthread_mutex_lock(&casse[l->index].mtx);       // LOCK CASSE
            pushCassa(l->index, index);
            if(M_PRINTF)
                printf("Cliente%d[%ld]: si trova in coda alla cassa%d\n", index, clienti[index].id, l->index);
            clienti[index].ncode++;
            clock_gettime(CLOCK_REALTIME, &tstart);
            Pthread_cond_signal(&casse[l->index].cond);
            Pthread_mutex_unlock(&casse[l->index].mtx);     // UNLOCK CASSE
            Pthread_mutex_unlock(&mtxopen);                 // UNLOCK LISTA CASSE
            
            if(signalQ != 0)
                break;
            
            // attesa in coda
            Pthread_mutex_lock(&clienti[index].mtx);        // LOCK CLIENTI
            Pthread_cond_wait(&clienti[index].cond, &clienti[index].mtx);
            Pthread_mutex_unlock(&clienti[index].mtx);      // UNLOCK CLIENTI
        }
        clock_gettime(CLOCK_REALTIME, &tend);
        msec  = (tend.tv_sec - tstart.tv_sec)*1000;
        msec += (tend.tv_nsec - tstart.tv_nsec)/1000000;
        clienti[index].queuetime = msec;
        
        if(signalH+signalQ != 0)
            break;
        
        // aspetta che venga reinserito come nuovo cliente
        Pthread_mutex_lock(&clienti[index].mtx);        // LOCK CLIENTI
        Pthread_mutex_lock(&mtxdirC);                   // LOCK DIR CLIENTHANDLER
        Pthread_cond_signal(&conddirC);
        Pthread_mutex_unlock(&mtxdirC);                 // UNLOCK DIR CLIENTHANDLER
        clienti[index].esc = 1;
        if(M_PRINTF)
            printf("Cliente%d[%ld]: uscito dal supermercato con %d prodotti\n", i, clienti[index].id, clienti[index].nprodotti);
        Pthread_cond_wait(&clienti[index].cond, &clienti[index].mtx);
        Pthread_mutex_unlock(&clienti[index].mtx);      // UNLOCK CLIENTI
    }
    clienti[index].esc = 1;
    
    if(signalQ == 0) {
        Pthread_mutex_lock(&mtxclienti);            // LOCK CLIENTI USCITI
        clienti_usciti++;
        Pthread_mutex_unlock(&mtxclienti);          // UNLOCK CLIENTI USCITI
    }
    
    Pthread_mutex_lock(&mtxdirC);                   // LOCK DIR CLIENTHANDLER
    Pthread_cond_signal(&conddirC);
    Pthread_mutex_unlock(&mtxdirC);                 // UNLOCK DIR CLIENTHANDLER
    if(M_PRINTF)
        printf("CHIUSURA DEL THREAD CLIENTE%d - Clienti usciti=%d/%d\n", index, clienti_usciti, C);
    pthread_exit(NULL);
}





