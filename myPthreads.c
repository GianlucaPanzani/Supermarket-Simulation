#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include "shared_data.h"


// --------------PTHREAD--------------

void Pthread_mutex_lock(pthread_mutex_t* mutex) {
    int err;
    if((err = pthread_mutex_lock(&(*mutex))) != 0){
        errno = err;
        perror("In lock acquire");
        pthread_exit(&errno);
    }
}

void Pthread_mutex_unlock(pthread_mutex_t* mutex) {
    int err;
    if((err = pthread_mutex_unlock(&(*mutex))) != 0){
        errno = err;
        perror("In lock release");
        pthread_exit(&errno);
    }
}

void Pthread_cond_signal(pthread_cond_t* condition) {
    int err;
    if((err = pthread_cond_signal(&(*condition))) != 0){
        errno = err;
        perror("Sending signal");
        pthread_exit(&errno);
    }
}

void Pthread_cond_wait(pthread_cond_t* condition, pthread_mutex_t* mutex) {
    int err;
    if((err = pthread_cond_wait(&(*condition), &(*mutex))) != 0){
        errno = err;
        perror("Waiting signal");
        pthread_exit(&errno);
    }
}

