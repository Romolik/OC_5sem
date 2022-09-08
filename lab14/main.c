#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <assert.h>

#define SEMAPHORES_COUNT 2
#define ITERATIONS_COUNT 10

enum semaphoreNumbers {
  FIRST_SEMAPHORE = 0,
  SECOND_SEMAPHORE
};

enum returnErrorCodes {
  INIT_SEMAPHORES_SUCCESS = 0,
  INIT_SEMAPHORES_FAILURE = 1,
  POST_OR_WAIT_SEMAPHORES_FAILURE = 1,
  POST_OR_WAIT_SEMAPHORES_SUCCESS = 0
};

void destroySemaphores(sem_t *const semaphores) {
        assert(NULL != semaphores);

        if (sem_destroy(&semaphores[FIRST_SEMAPHORE])) {
                printf("%s", "Error destroying semaphore");
        }

        if (sem_destroy(&semaphores[SECOND_SEMAPHORE])) {
                printf("%s", "Error destroying semaphore");
        }
}

int initSemaphores(sem_t * const semaphores) {
        assert(NULL != semaphores);

        const unsigned int initValueFirstSemaphore = 1;
        const unsigned int initValueSecondSemaphore = 0;
        const int pshared = 0;

        if (sem_init(&semaphores[FIRST_SEMAPHORE], pshared, initValueFirstSemaphore)) {
                printf("%s", "Error creating semaphore");
                return INIT_SEMAPHORES_FAILURE;
        }

        if (sem_init(&semaphores[SECOND_SEMAPHORE], pshared, initValueSecondSemaphore)) {
                printf("%s", "Error creating semaphore");
                if (sem_destroy(&semaphores[FIRST_SEMAPHORE])) {
                        printf("%s", "Error destroying semaphore");
                }
                return INIT_SEMAPHORES_FAILURE;
        }
        return INIT_SEMAPHORES_SUCCESS;
}

long printAlternating(sem_t *const semaphoreFirst, sem_t *const semaphoreSecond, const char *const message) {
        assert(NULL != semaphoreFirst);
        assert(NULL != semaphoreSecond);
        assert(NULL != message);

        for (int i = 0; i < ITERATIONS_COUNT; ++i) {
                if (sem_wait(semaphoreFirst)) {
                        printf("%s", "Error waiting first semaphore");
                        return POST_OR_WAIT_SEMAPHORES_FAILURE;
                }

                printf("%s: %d\n", message, i);

                if(sem_post(semaphoreSecond)){
                        printf("%s", "Error posting to second semaphore");
                        return POST_OR_WAIT_SEMAPHORES_FAILURE;
                }
        }

        return POST_OR_WAIT_SEMAPHORES_SUCCESS;
}

void *secondPrint(void *parameter) {
        assert(NULL != parameter);
        const char* const messageForPrint = "second";
        sem_t *semaphores = (sem_t *)parameter;

        return (void *)printAlternating(&semaphores[SECOND_SEMAPHORE], &semaphores[FIRST_SEMAPHORE], messageForPrint);
}

int main() {
        pthread_t createdThreadID;
        sem_t semaphores[SEMAPHORES_COUNT];

        if (INIT_SEMAPHORES_FAILURE == initSemaphores(semaphores)) {
                return EXIT_FAILURE;
        }

        int threadCreationResult;
        threadCreationResult = pthread_create(&createdThreadID, NULL, secondPrint, (void *)&semaphores);
        if (0 != threadCreationResult) {
                printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                destroySemaphores(semaphores);
                return EXIT_FAILURE;
        }

        const char *const messageForPrint = "first";
        if (POST_OR_WAIT_SEMAPHORES_FAILURE == printAlternating(&semaphores[FIRST_SEMAPHORE],
                                                                                                                        &semaphores[SECOND_SEMAPHORE],
                                                                                                                        messageForPrint)) {
                destroySemaphores(semaphores);
                return EXIT_FAILURE;
        }

        int threadJoinResult;
        threadJoinResult = pthread_join(createdThreadID, NULL);
        if (0 != threadJoinResult) {
                printf("%s %d", "failed to join thread, error code ==", threadCreationResult);
                destroySemaphores(semaphores);
                return EXIT_FAILURE;
        }

        destroySemaphores(semaphores);
        pthread_exit(NULL);
        return EXIT_SUCCESS;
}

