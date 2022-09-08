#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

const int ITERATIONS_COUNT = 10;

void *threadBody() {
        for(int i = 0; i < ITERATIONS_COUNT; i++) {
                printf("Child\n");
        }
        return NULL;
}

int main() {
        pthread_t createdThreadID = 0;
        int threadCreationResult = 0;
        int threadJoinResult = 0;

        threadCreationResult = pthread_create(&createdThreadID, NULL, threadBody, NULL);
        if(0 != threadCreationResult) {
                printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                return EXIT_FAILURE;
        }

        threadJoinResult = pthread_join(createdThreadID, NULL);
        if(0 != threadJoinResult) {
                printf("%s %d", "failed to join child thread, error code ==", threadJoinResult);
                return EXIT_FAILURE;
        }

        for(int i = 0; i < ITERATIONS_COUNT; i++) {
                printf("Parent\n");
        }

        pthread_exit(NULL);
        return EXIT_SUCCESS;
}

