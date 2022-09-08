#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>

const int FORK_OR_WAIT_ERROR_RETURN_CODE = -1;
const int ITERATIONS_COUNT = 10;
const char *firstSemaphoreName = "/First";
const char *secondSemaphoreName = "/Second";

enum returnErrorCodes {
  POST_OR_WAIT_SEMAPHORES_FAILURE = 1,
  POST_OR_WAIT_SEMAPHORES_SUCCESS = 0
};

void unlinkSemaphore(const char *const semaphoreName) {
        assert(NULL != semaphoreName);

        if (sem_unlink(semaphoreName)) {
                printf("%s", "Error unlinking semaphore");
        }
}

void destroySemaphore(sem_t *const semaphore) {
        assert(semaphore);

        if (sem_close(semaphore)) {
                printf("%s", "Error destroying semaphore");
        }
}

void handleErrors(const char *const errorMessage, sem_t *const semaphoreFirst, sem_t *const semaphoreSecond) {
        assert(NULL != semaphoreFirst);
        assert(NULL != semaphoreSecond);

        destroySemaphore(semaphoreFirst);
        destroySemaphore(semaphoreSecond);
        unlinkSemaphore(firstSemaphoreName);
        unlinkSemaphore(secondSemaphoreName);

        if (NULL != errorMessage) {
                printf("%s", errorMessage);
        }
}

int printMessage(sem_t *const semaphoreFirst, sem_t *const semaphoreSecond, const char *const message) {
        assert(NULL != semaphoreFirst);
        assert(NULL != semaphoreSecond);
        assert(NULL != message);

        for (int i = 0; i < ITERATIONS_COUNT; ++i) {
                if (sem_wait(semaphoreFirst)) {
                        handleErrors("Error waiting semaphore", semaphoreFirst, semaphoreSecond);
                        return POST_OR_WAIT_SEMAPHORES_FAILURE;
                }

                printf("%s: %d\n", message, i);

                if (sem_post(semaphoreSecond)) {
                        handleErrors("Error posting to semaphore", semaphoreFirst, semaphoreSecond);
                        return POST_OR_WAIT_SEMAPHORES_FAILURE;
                }
        }
        return POST_OR_WAIT_SEMAPHORES_SUCCESS;
}

int main() {
        const unsigned int initValueFirstSemaphore = 1;
        const unsigned int initValueSecondSemaphore = 0;
        const mode_t mode = 0777;


        sem_t *semaphoreFirst = sem_open(firstSemaphoreName, O_CREAT, mode, initValueFirstSemaphore);
        if (SEM_FAILED == semaphoreFirst) {
                printf("%s", "Error creating first semaphore");
                return EXIT_FAILURE;
        }

        sem_t *semaphoreSecond = sem_open(secondSemaphoreName, O_CREAT, mode, initValueSecondSemaphore);
        if (SEM_FAILED == semaphoreSecond) {
                printf("%s", "Error creating second semaphore");
                destroySemaphore(semaphoreFirst);
                unlinkSemaphore(firstSemaphoreName);
                return EXIT_FAILURE;
        }

        pid_t pid = fork();
        if (FORK_OR_WAIT_ERROR_RETURN_CODE == pid) {
                handleErrors("Error creating process", semaphoreFirst, semaphoreSecond);
                return EXIT_FAILURE;
        }

        if (0 == pid) {
                if (POST_OR_WAIT_SEMAPHORES_FAILURE == printMessage(semaphoreSecond, semaphoreFirst, secondSemaphoreName)) {
                        return EXIT_FAILURE;
                }
        } else {
                if (POST_OR_WAIT_SEMAPHORES_FAILURE == printMessage(semaphoreFirst, semaphoreSecond, firstSemaphoreName)) {
                        return EXIT_FAILURE;
                }
        }

        if (pid) {
                if (FORK_OR_WAIT_ERROR_RETURN_CODE == wait(NULL)) {
                        handleErrors("Error waiting child", semaphoreFirst, semaphoreSecond);
                        return EXIT_FAILURE;
                }
                unlinkSemaphore(firstSemaphoreName);
                unlinkSemaphore(secondSemaphoreName);
        }

        destroySemaphore(semaphoreFirst);
        destroySemaphore(semaphoreSecond);
        return EXIT_SUCCESS;
}

