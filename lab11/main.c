#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <zconf.h>
#include <assert.h>
#include <errno.h>

#define MUTEX_COUNT 3
#define ITERATIONS_COUNT 10

enum returnErrorCodes {
  INIT_MUTEX_SUCCESS = 0,
  INIT_MUTEX_FAILURE = 1,
  LOCK_OR_UNLOCK_MUTEX_FAILURE = 1,
  LOCK_OR_UNLOCK_MUTEX_SUCCESS = 0
  };

enum mutexNumbers {
  FIRST_MUTEX = 0,
  SECOND_MUTEX,
  THIRD_MUTEX
};

void destroyMutexes(const int countMutexDestroy, pthread_mutex_t *const mutexes) {
        assert(NULL != mutexes);

        if (countMutexDestroy < 0 || countMutexDestroy > MUTEX_COUNT) {
                printf("%s", "invalid function parameter");
                return;
        }

        for (int i = 0; i < countMutexDestroy; ++i) {
                if (pthread_mutex_destroy(&mutexes[i]) < 0) {
                        printf("%s", "Error destroying mutex");
                }
        }
}

void errorHandlerMutex(pthread_mutex_t *const mutexes, const char *const errorMessage) {
        assert(NULL != mutexes);

        destroyMutexes(MUTEX_COUNT, mutexes);
        if (NULL != errorMessage) {
                printf("%s", errorMessage);
        }
}

int lockMutex(const int num, pthread_mutex_t *const mutexes) {
        if (num < 0 || num >= MUTEX_COUNT || NULL == mutexes) {
                printf("%s", "invalid function parameter");
                return LOCK_OR_UNLOCK_MUTEX_FAILURE;
        }

        if (pthread_mutex_lock(&mutexes[num])) {
                printf("%s",  "Error locking mutex");
                return LOCK_OR_UNLOCK_MUTEX_FAILURE;
        }
        return  LOCK_OR_UNLOCK_MUTEX_SUCCESS;
}

int unlockMutex(const int num, pthread_mutex_t *const mutexes) {
        if (num < 0 || num >= MUTEX_COUNT || NULL == mutexes) {
                printf("%s", "invalid function parameter");
                return LOCK_OR_UNLOCK_MUTEX_FAILURE;
        }

        if (pthread_mutex_unlock(&mutexes[num])) {
                printf("%s",  "Error unlocking mutex");
                return LOCK_OR_UNLOCK_MUTEX_FAILURE;
        }
        return LOCK_OR_UNLOCK_MUTEX_SUCCESS;
}

int initMutexes(pthread_mutex_t *const mutexes) {
        assert(NULL != mutexes);

        pthread_mutexattr_t mutexAttributes;
        pthread_mutexattr_init(&mutexAttributes);
        if (pthread_mutexattr_settype(&mutexAttributes, PTHREAD_MUTEX_ERRORCHECK)) {
                printf("%s", "Error creating attributes\n");
                return INIT_MUTEX_FAILURE;
        }

        for (int i = 0; i < MUTEX_COUNT; ++i) {
                if (pthread_mutex_init(&mutexes[i], &mutexAttributes)) {
                        destroyMutexes(i, mutexes);
                        printf("%s", "Error creating mutex");
                        return INIT_MUTEX_FAILURE;
                }
        }

        if (0 != pthread_mutexattr_destroy(&mutexAttributes)) {
                printf("%s", "Error destroy mutex_attribute");
        }

        return INIT_MUTEX_SUCCESS;
}

void *secondPrint(void *parameter) {
        assert(NULL != parameter);

        pthread_mutex_t *mutexes = (pthread_mutex_t *)parameter;

        if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(THIRD_MUTEX, mutexes)) {
                return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
        }

        for (int i = 0; i < ITERATIONS_COUNT; ++i) {
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(SECOND_MUTEX, mutexes)) {
                        return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                printf("Second: %d\n", i);
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(THIRD_MUTEX, mutexes)){
                        return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(FIRST_MUTEX, mutexes)) {
                        return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(SECOND_MUTEX, mutexes)) {
                        return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(THIRD_MUTEX, mutexes)) {
                        return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(FIRST_MUTEX, mutexes)) {
                        return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
        }
        if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(THIRD_MUTEX, mutexes)) {
                return (void *)LOCK_OR_UNLOCK_MUTEX_FAILURE;
        }
        return (void *) LOCK_OR_UNLOCK_MUTEX_SUCCESS;
}

int firstPrint(pthread_mutex_t *const mutexes) {
        assert(NULL != mutexes);

        for (int i = 0; i < ITERATIONS_COUNT; ++i) {
                printf("First: %d\n", i);
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(FIRST_MUTEX, mutexes)) {
                        return LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(SECOND_MUTEX, mutexes)) {
                        return LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(THIRD_MUTEX, mutexes)) {
                        return LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(FIRST_MUTEX, mutexes)) {
                        return LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(SECOND_MUTEX, mutexes)) {
                        return LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
                if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(THIRD_MUTEX, mutexes)) {
                        return LOCK_OR_UNLOCK_MUTEX_FAILURE;
                }
        }
        if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(SECOND_MUTEX, mutexes)) {
                return LOCK_OR_UNLOCK_MUTEX_FAILURE;
        }

        return LOCK_OR_UNLOCK_MUTEX_SUCCESS;
}

int main() {
        pthread_t createdThreadID;
        int threadCreationResult;
        int threadJoinResult;
        pthread_mutex_t mutexes[MUTEX_COUNT];
        int childStarted = 0;

        if (INIT_MUTEX_FAILURE == initMutexes(mutexes)) {
                return EXIT_FAILURE;
        }

        if (LOCK_OR_UNLOCK_MUTEX_FAILURE == lockMutex(SECOND_MUTEX, mutexes)) {
                errorHandlerMutex(mutexes, "Error locking mutex");
                return EXIT_FAILURE;
        }

        threadCreationResult = pthread_create(&createdThreadID, NULL, secondPrint, (void *)&mutexes);
        if (0 != threadCreationResult) {
                printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                destroyMutexes(MUTEX_COUNT, mutexes);
                return EXIT_FAILURE;
        }

        while (EBUSY != childStarted) {
                childStarted = pthread_mutex_trylock(&mutexes[THIRD_MUTEX]);
                if (childStarted) {
                        if (EBUSY != childStarted) {
                                errorHandlerMutex(mutexes, "Error trylocking mutex");
                                return EXIT_FAILURE;
                        }
                } else {
                        if (LOCK_OR_UNLOCK_MUTEX_FAILURE == unlockMutex(THIRD_MUTEX, mutexes)) {
                                errorHandlerMutex(mutexes, "Error unlocking mutex");
                                return EXIT_FAILURE;
                        }
                }
        }

        if (LOCK_OR_UNLOCK_MUTEX_FAILURE == firstPrint(mutexes)) {
                destroyMutexes(MUTEX_COUNT, mutexes);
                return EXIT_FAILURE;
        }

        threadJoinResult = pthread_join(createdThreadID, NULL);
        if (0 != threadJoinResult) {
                printf("%s %d", "failed to join thread, error code ==", threadCreationResult);
                destroyMutexes(MUTEX_COUNT, mutexes);
                return EXIT_FAILURE;
        }

        destroyMutexes(MUTEX_COUNT, mutexes);
        pthread_exit(NULL);
        return EXIT_SUCCESS;
}
