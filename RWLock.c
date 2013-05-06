#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct RWLock {
    pthread_mutex_t * write;
    pthread_mutex_t * read;
    pthread_mutex_t * readCountLock;
    int readCount;
};

void RWLock_W_Lock(struct RWLock* lock) {
    pthread_mutex_lock(lock->write);
    pthread_mutex_lock(lock->read);
    pthread_mutex_lock(lock->readCountLock);
}

void RWLock_W_Unlock(struct RWLock* lock) {
    pthread_mutex_unlock(lock->readCountLock);
    pthread_mutex_unlock(lock->read);
    pthread_mutex_unlock(lock->write);
}

void RWLock_R_Lock(struct RWLock* lock) {
    pthread_mutex_lock(lock->write);
    pthread_mutex_lock(lock->readCountLock);
    if (!(lock->readCount++)) {
        pthread_mutex_lock(lock->read);
    }
    pthread_mutex_unlock(lock->write);
    pthread_mutex_unlock(lock->readCountLock);
}

void RWLock_R_Unlock(struct RWLock* lock) {
    pthread_mutex_lock(lock->readCountLock);
    if (!(--lock->readCount)) {
        pthread_mutex_unlock(lock->read);
    }
    pthread_mutex_unlock(lock->readCountLock);
}

void RWLock_Create(struct RWLock* lock) {
    if (NULL == (lock = malloc(sizeof(struct RWLock)))) {
        fprintf(stderr, "Failed to allocate memory for RWLock.\n");
        return;
    }
    pthread_mutex_init(lock->write, NULL);
    pthread_mutex_init(lock->read, NULL);
    pthread_mutex_init(lock->readCountLock, NULL);
    lock->readCount = 0;
}

