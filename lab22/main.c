#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#define DELAY_BETWEEN_MEALS  30000
#define AMOUNT_FOOD 50
#define NUMBER_PHILOSOPHERS 5
#define SUCCESS  0
#define FAILURE  -1
#define CANNOT_TAKE_FORKS  1
#define CAN_TAKE_FORKS  0

typedef struct Synchronization {
  pthread_mutex_t forks[NUMBER_PHILOSOPHERS];
  pthread_t philosophers[NUMBER_PHILOSOPHERS];
  pthread_mutex_t foodlock;
  pthread_mutex_t startTaking;
  pthread_cond_t conditional;
  int *leftovers;
} Synchronization;

typedef struct LocalThreadData {
  Synchronization *synchronization;
  int philosopherID;
  int cannotTakeForks;
} LocalThreadData;


int lockMutex(pthread_mutex_t *mutex) {
        assert(NULL != mutex);

        if (0 != pthread_mutex_lock(mutex)) {
                printf("%s", "Error locking mutex");
                return FAILURE;
        }
        return SUCCESS;
}

int unlockMutex(pthread_mutex_t *mutex) {
        assert(NULL != mutex);

        if (0 != pthread_mutex_unlock(mutex)) {
                printf("%s", "Error unlocking mutex");
                return FAILURE;
        }
        return SUCCESS;
}

int unlockConditional(pthread_cond_t *conditional) {
        assert(NULL != conditional);

        if (0 != pthread_cond_broadcast(conditional)) {
                printf("%s", "Error broadcasting conditional variable");
                return FAILURE;
        }
        return SUCCESS;
}

int getForks(int leftFork, int rightFork, LocalThreadData *localThreadData) {
        assert(NULL != localThreadData);

        localThreadData->cannotTakeForks = CANNOT_TAKE_FORKS;
        Synchronization *synchronization = localThreadData->synchronization;
        int returnedTryLock;

        if (SUCCESS != lockMutex(&synchronization->startTaking)) {
                return FAILURE;
        }

        do {
                returnedTryLock = pthread_mutex_trylock(&synchronization->forks[leftFork]);
                if (EBUSY != returnedTryLock && 0 != returnedTryLock) {
                        printf("%s", "Error trylocking mutex");
                        return FAILURE;
                }
                if (returnedTryLock) {
                        returnedTryLock = pthread_mutex_trylock(&synchronization->forks[rightFork]);
                        if (EBUSY != returnedTryLock && 0 != returnedTryLock) {
                                printf("%s", "Error trylocking mutex");
                                return FAILURE;
                        }
                }

                if (returnedTryLock) {
                        while (localThreadData->cannotTakeForks) {
                                if (0 != pthread_cond_wait(&synchronization->conditional, &synchronization->startTaking)) {
                                        printf("%s", "Error waiting condition variable");
                                        return FAILURE;
                                }
                        }
                }
        } while(returnedTryLock);

        localThreadData->cannotTakeForks = CANNOT_TAKE_FORKS;

        if (SUCCESS != unlockMutex(&synchronization->startTaking)) {
                return FAILURE;
        }
        return SUCCESS;
}

int downForks(int leftFork, int rightFork, LocalThreadData *localThreadData) {
        assert(NULL != localThreadData);

        if (SUCCESS != lockMutex(&localThreadData->synchronization->startTaking)) {
                return FAILURE;
        }
        if (SUCCESS != unlockMutex(&localThreadData->synchronization->forks[leftFork])) {
                return FAILURE;
        }

        if (SUCCESS != unlockMutex(&localThreadData->synchronization->forks[rightFork])) {
                return FAILURE;
        }

        if (SUCCESS != unlockConditional(&localThreadData->synchronization->conditional)) {
                return FAILURE;
        }

        localThreadData->cannotTakeForks = CAN_TAKE_FORKS;

        if (SUCCESS != unlockMutex(&localThreadData->synchronization->startTaking)) {
                return FAILURE;
        }

        return SUCCESS;
}

void destroySynchronization(Synchronization *synchronization) {
        assert(NULL != synchronization);

        int mutexDestroyResult;

        for (int i = 0; i < NUMBER_PHILOSOPHERS; i++) {
                mutexDestroyResult = pthread_mutex_destroy(&synchronization->forks[i]);
                if (0 != mutexDestroyResult) {
                        printf("%s %d", "failed to destroy mutex(forks), error code == ", mutexDestroyResult);
                }
        }

        mutexDestroyResult = pthread_mutex_destroy(&synchronization->foodlock);
        if (0 != mutexDestroyResult) {
                printf("%s %d", "failed to destroy foodlock, error code == ", mutexDestroyResult);
        }

        mutexDestroyResult = pthread_mutex_destroy(&synchronization->startTaking);
        if (0 != mutexDestroyResult) {
                printf("%s %d", "failed to destroy startTaking, error code == ", mutexDestroyResult);
        }

        int condVarDestroyResult = pthread_cond_destroy(&synchronization->conditional);
        if (0 != condVarDestroyResult) {
                printf("%s %d", "failed to destroy conditional variable, error code == ", condVarDestroyResult);
        }
}

int initSynchroData(Synchronization *synchronization) {
        assert(NULL != synchronization);

        int mutexInitResult;
        for (int i = 0; i < NUMBER_PHILOSOPHERS; i++) {
                mutexInitResult = pthread_mutex_init(&synchronization->forks[i], NULL);
                if (0 != mutexInitResult) {
                        printf("%s %d", "failed to init mutex(forks), error code == ", mutexInitResult);
                        return FAILURE;
                }
        }

        mutexInitResult = pthread_mutex_init(&synchronization->foodlock, NULL);
        if (0 != mutexInitResult) {
                printf("%s %d", "failed to init foodlock, error code == ", mutexInitResult);
                return FAILURE;
        }

        mutexInitResult = pthread_mutex_init(&synchronization->startTaking, NULL);
        if (0 != mutexInitResult) {
                printf("%s %d", "failed to init startTaking, error code == ", mutexInitResult);
                return FAILURE;
        }

        int condVarInitResult = pthread_cond_init(&synchronization->conditional, NULL);
        if (0 != condVarInitResult) {
                printf("%s %d", "failed to init conditional variable, error code == ", condVarInitResult);
                return FAILURE;
        }

        return SUCCESS;
}

int foodOnTable(Synchronization *synchronization) {
        assert(NULL != synchronization);

        if (SUCCESS != lockMutex(&synchronization->foodlock)) {
                return FAILURE;
        }

        if (*synchronization->leftovers > 0) {
                (*synchronization->leftovers)--;
        }

        if (SUCCESS != unlockMutex(&synchronization->foodlock)) {
                return FAILURE;
        }
        return *synchronization->leftovers;
}

void *philosopher(void *threadData) {
        assert(NULL != threadData);

        LocalThreadData *localThreadData = (LocalThreadData *)threadData;
        int philosopher_number = localThreadData->philosopherID;
        Synchronization *synchronization = localThreadData->synchronization;
        int leftFork, rightFork, leftovers;

        printf("Philosopher %d sitting down to dinner.\n", philosopher_number);

        rightFork = philosopher_number;
        leftFork = (philosopher_number + 1) % NUMBER_PHILOSOPHERS;

        leftovers = foodOnTable(synchronization);
        while (leftovers) {
                if (leftovers < 0) {
                        return NULL;
                }

                printf("Philosopher %d: get dish %d.\n", philosopher_number, leftovers);
                if (SUCCESS != getForks(leftFork, rightFork, localThreadData)) {
                        return NULL;
                }
                printf("Philosopher %d: eating.\n", philosopher_number);
                if (usleep(DELAY_BETWEEN_MEALS * (AMOUNT_FOOD - leftovers + 1))) {
                        printf("%s", "Error usleep");
                }

                if (SUCCESS != downForks(leftFork, rightFork, localThreadData)) {
                        return NULL;
                }
                leftovers = foodOnTable(synchronization);
        }

        printf("Philosopher %d is done eating.\n", philosopher_number);

        return NULL;
}

int main() {
        LocalThreadData localThreadData[NUMBER_PHILOSOPHERS];
        Synchronization synchronization;
        int cannotTakeForks = 0;
        int leftovers = AMOUNT_FOOD;

        if (SUCCESS != initSynchroData(&synchronization)) {
                return EXIT_FAILURE;
        }

        synchronization.leftovers = &leftovers;

        int threadCreationResult;
        for (int i = 0; i < NUMBER_PHILOSOPHERS; i++) {
                localThreadData[i].philosopherID = i;
                localThreadData[i].synchronization = &synchronization;
                localThreadData[i].cannotTakeForks = cannotTakeForks;
                threadCreationResult = pthread_create(&synchronization.philosophers[i], NULL, philosopher, (void *)&localThreadData[i]);
                if (0 != threadCreationResult) {
                        printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                        for (int j = 0; j < i; j++) {
                                if (0 != pthread_join(synchronization.philosophers[i], NULL)) {
                                        destroySynchronization(&synchronization);
                                        printf("%s", "pthread_join after pthread_create error");
                                        return EXIT_FAILURE;
                                }
                        }

                        destroySynchronization(&synchronization);

                        return EXIT_FAILURE;
                }
        }

        int threadJoinResult;
        for (int i = 0; i < NUMBER_PHILOSOPHERS; i++) {
                threadJoinResult = pthread_join(synchronization.philosophers[i], NULL);
                if (0 != threadJoinResult) {
                        destroySynchronization(&synchronization);
                        printf("%s %d", "failed to join thread, error code ==", threadJoinResult);
                        return EXIT_FAILURE;
                }
        }

        destroySynchronization(&synchronization);

        return EXIT_SUCCESS;
}

