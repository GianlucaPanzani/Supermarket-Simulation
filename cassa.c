#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "shared_data.h"

pthread_mutex_t mtxdirK  = PTHREAD_MUTEX_INITIALIZER;   // mutex gestore casse
pthread_cond_t  conddirK = PTHREAD_COND_INITIALIZER;    // condition var gestore casse

//----------------------------------------------------------

void popCassa(int ik) {
    if(casse[ik].q->head != NULL) {
        List* p = casse[ik].q->head->next;
        casse[ik].dimcoda--;
        casse[ik].clienti++;
        if(casse[ik].q->last == casse[ik].q->head)
            casse[ik].q->last = NULL;
        free(casse[ik].q->head);
        if(casse[ik].q->last == NULL)
            casse[ik].q->head = casse[ik].q->last = p;
        else
            casse[ik].q->head = p;
        return;
    }
}

void pushCassa(int ik, int ic) {
    List* p  = (List*) malloc(sizeof(List));
    p->index = ic;
    p->next  = NULL;
    if(casse[ik].q->head == NULL) {
        casse[ik].q->head = casse[ik].q->last = p;
        casse[ik].dimcoda++;
        return;
    }
    casse[ik].q->last->next = p;
    casse[ik].q->last       = casse[ik].q->last->next;
    casse[ik].dimcoda++;
    return;
}

void chiudiCassa(int index) {
    if(signalH == 0 && k == 1)
        return;
    
    // rimozione cassa (da chiudere) dalla lista casse aperte
    List* l = open;
    List* prec = NULL;
    while(l != NULL && l->index != index) {
        prec = l;
        l = l->next;
    }
    if(prec != NULL)
        prec->next = l->next;
    else
        open = open->next;
    free(l);
    k--;
    casse[index].chiusure++;
    casse[index].open = 0;
    
    // esce se non ci sono clienti da spostare
    if(casse[index].q->head == NULL)
        return;
    
    // esce se non ci sono altre casse aperte
    if(k == 0)
        return;
    
    // spostamento dei clienti dalla cassa chiusa alla cassa con meno clienti
    l = open;
    int min = 1000, minqueue = 1000;
    while(l != NULL) {
        Pthread_mutex_lock(&casse[l->index].mtx);          // LOCK CASSA
        if(minqueue > casse[l->index].dimcoda) {
            min = l->index;
            minqueue = casse[l->index].dimcoda;
        }
        Pthread_mutex_unlock(&casse[l->index].mtx);        // UNLOCK CASSA
        l = l->next;
    }
    Pthread_mutex_lock(&casse[min].mtx);            // LOCK CASSA (in cui inserire)
    prec = casse[min].q->head;
    while(prec != NULL) {
        Pthread_mutex_lock(&clienti[prec->index].mtx);
        clienti[prec->index].ncode++;
        Pthread_mutex_unlock(&clienti[prec->index].mtx);
        prec = prec->next;
    }
    if(casse[min].q->last != NULL) {
        casse[min].q->last->next = casse[index].q->head;
        casse[min].q->last       = casse[index].q->last;
    } else {
        casse[min].q->head = casse[index].q->head;
        casse[min].q->last = casse[index].q->last;
    }
    casse[min].dimcoda  += casse[index].dimcoda;
    casse[index].q->head = casse[index].q->last = NULL;
    casse[index].dimcoda = 0;
    Pthread_mutex_unlock(&casse[min].mtx);          // UNLOCK CASSA (in cui inserire)
    return;
}

void apriCassa() {
    for(int i = 0; i < K; ++i) {
        if(casse[i].open == 0) {
            Pthread_mutex_lock(&casse[i].mtx);
            casse[i].open = 1;
            List* l = (List*) malloc(sizeof(List));
            l->index = i;
            l->next = open;
            open = l;
            k++;
            if(M_PRINTF)
                printf("Cassa%d[%ld]: apertura della cassa - casse aperte: %d/%d\n", i, casse[i].id, k, K);
            Pthread_cond_signal(&casse[i].cond);
            Pthread_mutex_unlock(&casse[i].mtx);
            break;
        }
    }
}

void cashUpdate(FILE** log) {
    for(int i = 0; i < K; ++i) {
        Pthread_mutex_lock(&casse[i].mtx);
        int sectot   = casse[i].time/1000;
        int msectot  = casse[i].time - (sectot*1000);
        int tserv;
        if(casse[i].clienti != 0)
            tserv = casse[i].time/casse[i].clienti;
        else
            tserv = 0;
        int secserv  = tserv/1000;
        int msecserv = tserv - (secserv*1000);
        fprintf(*log, "| %ld | %d | %d | %d.%d | %d.%d | %d |/", casse[i].id, casse[i].prodotti, casse[i].clienti, sectot, msectot, secserv, msecserv, casse[i].chiusure);
        //fprintf(*log, "| K:%ld | %d | %d | %d.%d | %d.%d | %d |/", casse[i].id, casse[i].prodotti, casse[i].clienti, sectot, msectot, secserv, msecserv, casse[i].chiusure);
        Pthread_mutex_unlock(&casse[i].mtx);
    }
}

//-----------------------------------------------------------


// THREAD GESTORE DELLE CASSE (APERTURA E CHIUSURA)
void* cashHandler(void* arg) {
    char *logfile = (char*) arg;
    FILE *log;
    Pthread_mutex_lock(&mtxdirK);
    while(1) {
        Pthread_cond_wait(&conddirK, &mtxdirK);
        
        if(signalQ != 0)
            break;
        if(signalH != 0) {
            int dimcoda = 0;
            for(int i = 0; i < K; ++i)
                dimcoda += casse[i].dimcoda;
            if(dimcoda == 0)
                break;
            else
                continue;
        }
        
        int smallqueue = 0;
        for(int i = 0; i < K; ++i) {
            Pthread_mutex_lock(&mtxopen);                   // LOCK LISTA CASSE
            Pthread_mutex_lock(&casse[i].mtx);              // LOCK CASSE
            if(casse[i].open == 1 && casse[i].dimcoda < 2 && k > 1) {
                smallqueue++;
                if(S1 == smallqueue) {
                    chiudiCassa(i);
                    if(M_PRINTF)
                        printf("Cassa%d[%ld]: chiusura della cassa - casse aperte: %d/%d\n", i, casse[i].id, k, K);
                }
            } else {
                if(casse[i].open == 1 && casse[i].dimcoda >= S2 && k < K) {
                    Pthread_mutex_unlock(&casse[i].mtx);        // UNLOCK CASSE
                    apriCassa();
                    Pthread_mutex_lock(&casse[i].mtx);          // LOCK CASSE
                }
            }
            Pthread_mutex_unlock(&casse[i].mtx);            // UNLOCK CASSE
            Pthread_mutex_unlock(&mtxopen);                 // UNLOCK LISTA CASSE
        }
    }
    Pthread_mutex_unlock(&mtxdirK);
    
    // risveglia eventuali clienti rimasti nel supermercato (in quanto in chiusura)
    for(int i = 0; i < C; ++i) {
        Pthread_mutex_lock(&clienti[i].mtx);
        Pthread_cond_signal(&clienti[i].cond);
        Pthread_mutex_unlock(&clienti[i].mtx);
    }
    
    // risveglia le casse affinche' possano chiudere
    struct timespec t = {1,500000000};
    nanosleep(&t, NULL);
    for(int i = 0; i < K; ++i) {
        Pthread_mutex_lock(&casse[i].mtx);
        Pthread_cond_signal(&casse[i].cond);
        Pthread_mutex_unlock(&casse[i].mtx);
    }
    
    // aggiorna il file di log con i dati ricavati dalle casse
    t.tv_sec  = 0;
    t.tv_nsec = 500000000;
    nanosleep(&t, NULL);
    if((log = fopen(logfile, "a")) == NULL) {
        perror("Opening log file");
        exit(errno);
    }
    cashUpdate(&log);
    if(fclose(log) == -1) {
        perror("Closing log file");
        exit(errno);
    }
    
    if(M_PRINTF)
        printf("CHIUSURA DEL THREAD GESTORE DELLE CASSE (cashHandler)\n");
    pthread_exit(NULL);
}


void* fcasse(void* arg) {
    long msec;
    struct timespec tstart, tend, dirstart, dirend;
    int t, openflag = 1, indexK = *((int*) arg);
    unsigned int seed = time(NULL)^pthread_self();
    t  = rand_r(&seed)%60000000;
    t += 20000000;
    struct timespec t1 = {0,t};
    struct timespec t2 = {0,Tp*1000000};
    casse[indexK].id = (long) pthread_self();
    clock_gettime(CLOCK_REALTIME, &tstart);
    clock_gettime(CLOCK_REALTIME, &dirstart);

    while(1) {
        Pthread_mutex_lock(&casse[indexK].mtx);         // LOCK CASSE
        while((casse[indexK].open == 0 || casse[indexK].q->head == NULL) && clienti_usciti != C) {
            if(casse[indexK].open == 0 && openflag == 1) {
                openflag = 0;
                clock_gettime(CLOCK_REALTIME, &tend);
                msec  = (tend.tv_sec - tstart.tv_sec)*1000;
                msec += (tend.tv_nsec - tstart.tv_nsec)/1000000;
                casse[indexK].time += msec;
            }
            if(signalH+signalQ != 0) {
                Pthread_mutex_lock(&mtxdirK);
                Pthread_cond_signal(&conddirK);
                Pthread_mutex_unlock(&mtxdirK);
            }
            Pthread_cond_wait(&casse[indexK].cond, &casse[indexK].mtx);
            if(casse[indexK].open == 1 && openflag == 0) {
                clock_gettime(CLOCK_REALTIME, &tstart);
                clock_gettime(CLOCK_REALTIME, &dirstart);
                openflag = 1;
            }
        }
        
        // Condizioni di uscita
        if(signalQ+signalH != 0 && clienti_usciti == C)
            break;
        
        // Elaborazione dei prodotti del cliente alla cassa
        int indexC = casse[indexK].q->head->index;
        casse[indexK].prodotti += clienti[indexC].nprodotti;
        Pthread_mutex_unlock(&casse[indexK].mtx);           // UNLOCK CASSE
        nanosleep(&t1, NULL);
        for(int i = 0; i < clienti[indexC].nprodotti; ++i)
            nanosleep(&t2, NULL);
        
        // Controllo di comunicazione col gestore delle casse
        clock_gettime(CLOCK_REALTIME, &dirend);
        msec  = (dirend.tv_sec - dirstart.tv_sec)*1000;
        msec += (dirend.tv_nsec - dirstart.tv_nsec)/1000000;
        if(Td <= msec) {
            Pthread_mutex_lock(&mtxdirK);                       // LOCK DIRETTORE
            Pthread_cond_signal(&conddirK);
            Pthread_mutex_unlock(&mtxdirK);                     // UNLOCK DIRETTORE
            clock_gettime(CLOCK_REALTIME, &dirstart);
        }
        
        // pop sul cliente
        Pthread_mutex_lock(&casse[indexK].mtx);             // LOCK CASSE
        popCassa(indexK);
        NUMC--;
        Pthread_mutex_unlock(&casse[indexK].mtx);           // UNLOCK CASSE
        
        // Segnala al cliente che puo' uscire
        Pthread_mutex_lock(&clienti[indexC].mtx);           // LOCK CLIENTI
        Pthread_cond_signal(&clienti[indexC].cond);
        Pthread_mutex_unlock(&clienti[indexC].mtx);         // UNLOCK CLIENTI
    }
    
    // Aggiorna il tempo in cui la cassa e' rimasta aperta
    if(openflag == 1) {
        clock_gettime(CLOCK_REALTIME, &tend);
        msec  = (tend.tv_sec - tstart.tv_sec)*1000;
        msec += (tend.tv_nsec - tstart.tv_nsec)/1000000;
        casse[indexK].time += msec;
    }
    casse[indexK].chiusure++;
    Pthread_mutex_unlock(&casse[indexK].mtx);           // UNLOCK CASSE
    
    if(M_PRINTF)
        printf("CHIUSURA DEL THREAD CASSA%d\n", indexK);
    pthread_exit(NULL);
}


