#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

const int ITERATIONS_COUNT = 10;

typedef struct printContexts {
  char *strToPrint;
  int amISecond;
  int *currentTurn;
  pthread_mutex_t *mutex;
  pthread_cond_t *conditionalVar;
} printContexts;

enum returnErrorCodes {
  INIT_MUTEX_OR_CONDITIONAL_VAR_SUCCESS = 0,
  INIT_MUTEX_OR_CONDITIONAL_VAR_FAILURE = 1,
  EXIT_THREAD_BODY_FAILURE = 1,
  EXIT_THREAD_BODY_SUCCESS = 0
};

void destroyMutexAndConditional(pthread_mutex_t * const mutex, pthread_cond_t * const conditionalVar) {
        assert(NULL != mutex);
        assert(NULL != conditionalVar);

        if (pthread_mutex_destroy(mutex)) {
                printf("%s", "Error destroying mutex");
        }

        if (pthread_cond_destroy(conditionalVar)) {
                printf("%s", "Error destroying condVar");
        }
}

void handleErrors(printContexts *const printContext, const char *const errorMessage) {
        assert(NULL != printContext);

        destroyMutexAndConditional(printContext->mutex, printContext->conditionalVar);
        if (NULL != errorMessage) {
                printf("%s", errorMessage);
        }
}

void *print(void *data) {
        assert(NULL != data);

        printContexts context = *((printContexts *)data);
        int isMyTurn = !context.amISecond;

        if (pthread_mutex_lock(context.mutex)) {
                printf("%s", "Error locking mutex");
                return (void *) EXIT_THREAD_BODY_FAILURE;
        }

        for (int i = 0; i < ITERATIONS_COUNT; ++i) {
                while (*context.currentTurn != isMyTurn) {
                        if (pthread_cond_wait(context.conditionalVar, context.mutex)) {
                                printf("%s", "Error waiting condition variable");
                                return (void *) EXIT_THREAD_BODY_FAILURE;
                        }
                }
                printf("%s: %d\n", context.strToPrint, i);
                *context.currentTurn = !isMyTurn;

                if (pthread_cond_broadcast(context.conditionalVar)) {
                        printf("%s", "Error signaling condition variable");
                        return (void *) EXIT_THREAD_BODY_FAILURE;
                }
        }

        if (pthread_mutex_unlock(context.mutex)) {
                handleErrors(&context, "Error unlocking mutex");
                exit(EXIT_FAILURE);
        }

        return (void *) EXIT_THREAD_BODY_SUCCESS;
}

int init(pthread_mutex_t * const mutex, pthread_cond_t * const conditionalVar) {
        assert(NULL != mutex);
        assert(NULL != conditionalVar);

        pthread_mutexattr_t mutexAttributes;
        pthread_mutexattr_init(&mutexAttributes);
        if (pthread_mutexattr_settype(&mutexAttributes, PTHREAD_MUTEX_ERRORCHECK)) {
                printf("%s", "Error creating attributes\n");
                return INIT_MUTEX_OR_CONDITIONAL_VAR_FAILURE;
        }

        if (pthread_mutex_init(mutex, &mutexAttributes)) {
                printf("%s", "Error creating mutex");
                return INIT_MUTEX_OR_CONDITIONAL_VAR_FAILURE;
        }

        if (pthread_cond_init(conditionalVar, NULL)) {
                printf("%s", "Error creating condition variable");
                if (pthread_mutex_destroy(mutex)) {
                        printf("%s", "Error destroying mutex");
                }
                return INIT_MUTEX_OR_CONDITIONAL_VAR_FAILURE;
        }

        return INIT_MUTEX_OR_CONDITIONAL_VAR_SUCCESS;
}

int main() {
        pthread_t createdThreadID;
        int threadJoinResult;
        int threadCreatedResult;
        int currentTurn = 1;
        pthread_mutex_t mutex;
        pthread_cond_t conditionalVar;
        int amISecond = 1;
        printContexts secondThreadContext = {"Second", amISecond, &currentTurn, &mutex, &conditionalVar};
        amISecond = 0;
        printContexts firstThreadContext = {"First", amISecond, &currentTurn, &mutex, &conditionalVar};

        if (INIT_MUTEX_OR_CONDITIONAL_VAR_FAILURE == init(&mutex, &conditionalVar)) {
                return EXIT_FAILURE;
        }

        threadCreatedResult = pthread_create(&createdThreadID, NULL, print, (void *)&secondThreadContext);
        if (0 != threadCreatedResult) {
                printf("%s %d", "failed to created child thread, error code ==", threadCreatedResult);
                destroyMutexAndConditional(&mutex, &conditionalVar);
                return EXIT_FAILURE;
        }

        if (EXIT_THREAD_BODY_FAILURE == (long)print((void *)&firstThreadContext)) {
                handleErrors(&firstThreadContext, NULL);
                return EXIT_FAILURE;
        }

        threadJoinResult = pthread_join(createdThreadID, NULL);
        if (0 != threadJoinResult) {
                printf("%s %d", "failed to join child thread, error code ==", threadJoinResult);
                destroyMutexAndConditional(&mutex, &conditionalVar);
                return EXIT_FAILURE;
        }

        destroyMutexAndConditional(&mutex, &conditionalVar);
        pthread_exit(NULL);
        return EXIT_SUCCESS;
}

