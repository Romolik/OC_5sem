#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#define TIME_FOR_A_SECONDS 1
#define TIME_FOR_B_SECONDS 2
#define TIME_FOR_C_SECONDS 3
#define COUNT_OF_THREADS 4
#define COUNT_OF_WIDGETS 5
#define A_ID 0
#define B_ID 1
#define C_ID 2
#define MODULE_ID 3
#define EXECUTION 1

enum returnErrorCodes {
  FAILURE = 1, SUCCESS = 0
};

typedef struct LocalThreadData {
  sem_t *semaphores;
  int threadID;
  int *execution;
} LocalThreadData;

int isSleepTimeExpired(const int secondsLeftToSleep) {
        assert(secondsLeftToSleep >= 0);

        return 0 == secondsLeftToSleep;
}

void sleepForWorksSeconds(const int secondsToSleep) {
        assert(secondsToSleep >= 0);

        int secondsRemainingToSleep = sleep(secondsToSleep);
        while (0 != secondsRemainingToSleep) {
                secondsRemainingToSleep = sleep(secondsRemainingToSleep);
        }
}

int post(sem_t *semaphore) {
        assert(NULL != semaphore);

        if (0 != sem_post(semaphore)) {
                printf("%s", "Error posting semaphore");
                return FAILURE;
        }
        return SUCCESS;
}

int wait(sem_t *semaphore) {
        assert(NULL != semaphore);

        if (0 != sem_wait(semaphore)) {
                printf("%s", "Error waiting semaphore");
                return FAILURE;
        }
        return SUCCESS;
}

int createDetail(LocalThreadData *localThreadData) {
        assert(NULL != localThreadData);

        int timeToSleepSeconds;
        int countInStock = 0;
        char *nameOfDetail = NULL;
        int threadID = localThreadData->threadID;
        sem_t *mySemaphore = &localThreadData->semaphores[threadID];

        if (A_ID == threadID) {
                timeToSleepSeconds = TIME_FOR_A_SECONDS;
                nameOfDetail = "A";
        } else if (B_ID == threadID) {
                timeToSleepSeconds = TIME_FOR_B_SECONDS;
                nameOfDetail = "B";
        } else {
                timeToSleepSeconds = TIME_FOR_C_SECONDS;
                nameOfDetail = "C";
        }

        while (0 != *localThreadData->execution) {
                sleepForWorksSeconds(timeToSleepSeconds);
                if (FAILURE == post(mySemaphore)) {
                        *localThreadData->execution = 0;
                        return FAILURE;
                }

                if (0 != sem_getvalue(mySemaphore, &countInStock)) {
                        printf("%s", "Error: sem_getvalue(detail semaphore) in createDetail");
                }

                printf("%s IN STOCK %d (FROM CREATE DETAILS)\n", nameOfDetail, countInStock);
                printf("New %s\n", nameOfDetail);
        }
        return SUCCESS;
}

int createModule(LocalThreadData *localThreadData) {
        assert(NULL != localThreadData);

        int countInStock = 0;
        sem_t *moduleSemaphore = &localThreadData->semaphores[MODULE_ID];
        sem_t *semaphoreA = &localThreadData->semaphores[A_ID];
        sem_t *semaphoreB = &localThreadData->semaphores[B_ID];
        while (0 != *localThreadData->execution) {
                if (FAILURE == wait(semaphoreA)) {
                        *localThreadData->execution = 0;
                        return FAILURE;
                }

                if (0 != sem_getvalue(semaphoreA, &countInStock)) {
                        printf("%s", "Error: sem_getvalue(semaphoreA) in createModule");
                }
                printf("A IN STOCK %d (FROM CREATE MODULE)\n", countInStock);

                if (FAILURE == wait(semaphoreB)) {
                        *localThreadData->execution = 0;
                        return FAILURE;
                }

                if (0 != sem_getvalue(semaphoreB, &countInStock)) {
                        printf("%s", "Error: sem_getvalue(semaphoreB) in createModule");
                }
                printf("B IN STOCK %d (FROM CREATE MODULE)\n", countInStock);

                if (FAILURE == post(moduleSemaphore)) {
                        *localThreadData->execution = 0;
                        return FAILURE;
                }

                if (0 != sem_getvalue(moduleSemaphore, &countInStock)) {
                        printf("%s", "Error: sem_getvalue(moduleSemaphore) in createModule");
                }
                printf("MODULES IN STOCK %d (FROM CREATE MODULE)\n", countInStock);

                printf("New module AB\n");
        }
        return SUCCESS;
}

void *creator(void *threadData) {
        assert(NULL != threadData);

        LocalThreadData *localThreadData = (LocalThreadData *)threadData;
        int threadID = localThreadData->threadID;

        if (A_ID == threadID || B_ID == threadID || C_ID == threadID) {
                if (FAILURE == createDetail(localThreadData)) {
                        return NULL;
                }
        } else {
                if (FAILURE == createModule(localThreadData)) {
                        return NULL;
                }
        }
        return NULL;
}

int createWidget(sem_t *semaphores, int *execution) {
        assert(NULL != semaphores);
        assert(NULL != execution);

        int countOfWidgets = 0;
        int countInStock = 0;
        sem_t *semaphoreC = &semaphores[C_ID];
        sem_t *semaphoreAB = &semaphores[MODULE_ID];

        while (COUNT_OF_WIDGETS != countOfWidgets && *execution == EXECUTION) {
                countOfWidgets++;

                if (FAILURE == wait(semaphoreAB)) {
                        *execution = 0;
                        return FAILURE;
                }
                if (0 != sem_getvalue(semaphoreAB, &countInStock)) {
                        printf("%s", "Error: set_getvalue(semaphoreAB) in createWidget");
                }
                printf("MODULES IN STOCK %d (FROM CREATE WIDGET)\n", countInStock);

                if (FAILURE == wait(semaphoreC)) {
                        *execution = 0;
                        return FAILURE;
                }

                if (0 != sem_getvalue(semaphoreC, &countInStock)) {
                        printf("%s", "Error: set_getvalue(semaphoreC) in createWidget");
                }
                printf("C IN STOCK %d (FROM CREATE WIDGET)\n", countInStock);

                printf("New widget\n");
        }

        if (0 == *execution) {
                return FAILURE;
        }

        return SUCCESS;
}

void destroySemaphore(sem_t *semaphore) {
        assert(NULL != semaphore);

        if (0 != sem_destroy(semaphore)) {
                printf("%s", "Error destroying semaphore");
        }
}

int main() {
        pthread_t threads[COUNT_OF_THREADS];
        LocalThreadData localThreadData[COUNT_OF_THREADS];
        sem_t semaphores[COUNT_OF_THREADS];
        int execution = EXECUTION;

        for (int i = 0; i < COUNT_OF_THREADS; i++) {
                if (0 != sem_init(&semaphores[i], 0, 0)) {
                        printf("%s", "Error initialization semaphore");
                        for (int j = 0; j < i; j++) {
                                destroySemaphore(&semaphores[j]);
                        }
                        return EXIT_FAILURE;
                }
        }

        for (int i = 0; i < COUNT_OF_THREADS; i++) {
                localThreadData[i].threadID = i;
                localThreadData[i].semaphores = semaphores;
                localThreadData[i].execution = &execution;
                int threadCreationResult;
                threadCreationResult = pthread_create(&threads[i], NULL, creator, (void *)&localThreadData[i]);
                if (0 != threadCreationResult) {
                        printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                        for (int j = 0; j < i; j++) {
                                if (0 != pthread_join(threads[i], NULL)) {
                                        printf("%s", "pthread_join after pthread_create error");
                                }
                        }

                        for (int i = 0; i < COUNT_OF_THREADS; i++) {
                                destroySemaphore(&semaphores[i]);
                        }
                        return EXIT_FAILURE;
                }
        }

        if (FAILURE == createWidget(semaphores, &execution)) {
                for (int i = 0; i < COUNT_OF_THREADS; i++) {
                        destroySemaphore(&semaphores[i]);
                }
                return EXIT_FAILURE;
        }

        execution = 0;

        for (int i = 0; i < COUNT_OF_THREADS; i++) {
                int threadJoinResult;
                threadJoinResult = pthread_join(threads[i], NULL);
                if (0 != threadJoinResult) {
                        printf("%s %d", "failed to join thread, error code ==", threadJoinResult);
                        return EXIT_FAILURE;
                }
        }

        for (int i = 0; i < COUNT_OF_THREADS; i++) {
                destroySemaphore(&semaphores[i]);
        }

        return EXIT_SUCCESS;
}

