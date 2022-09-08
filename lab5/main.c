#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <zconf.h>

const int TRUE = 1;
const int EXECUTING_HANDLER = 1;
const int SLEEP_TIME = 2;

void cancelHandler() {
        printf("%s", "\nChild thread canceled\n");
}

void *threadBody() {
        pthread_cleanup_push(cancelHandler, NULL);
                while(TRUE) {
                        write(0, "Child\n", 6);
                        pthread_testcancel();
                }
        pthread_cleanup_pop(EXECUTING_HANDLER);
        return NULL;
}

int main() {
        pthread_t createdThreadID = 0;
        int threadCreationResult = 0;
        int threadCancelResult = 0;

        threadCreationResult = pthread_create(&createdThreadID, NULL, threadBody, NULL);
        if(0 != threadCreationResult) {
                printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                return EXIT_FAILURE;
        }

        int time = SLEEP_TIME;
        while(0 != time) {
                time = sleep(time);
        }


        threadCancelResult = pthread_cancel(createdThreadID);
        if(0 != threadCancelResult) {
                printf("%s %d", "failed to cancel thread, error code ==", threadCancelResult);
                return EXIT_FAILURE;
        }

        pthread_exit(NULL);
        return EXIT_SUCCESS;
}
