#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

const int NUMBER_THREADS_CREATE = 4;

void *threadBody(void *parameter) {
        if(NULL == parameter) {
                printf("%s", "thread input argument error");
                return NULL;
        }

        char **t;
        for(t = (char **)parameter; *t != NULL; t++) {
                printf("%s\n", *t);
        }
        return NULL;
}

int main() {
        pthread_t createdThreadsID[NUMBER_THREADS_CREATE];
        int threadCreationResult = 0;
        int threadJoinResult = 0;
        int  countCreatedThreads = NUMBER_THREADS_CREATE;

        char *param[] = {"str00", "str01", "str02", NULL,
                                        "str10", "str11", "str12", NULL,
                                        "str20", "str21", "str22", NULL,
                                        "str30", "str31", "str32", NULL};

        for(int i = 0; i < NUMBER_THREADS_CREATE; i++) {
                threadCreationResult = pthread_create(&createdThreadsID[i], NULL, threadBody, &param[i * NUMBER_THREADS_CREATE]);
                if(0 != threadCreationResult) {
                        countCreatedThreads = i;
                        printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                        break;
                }
        }

        for(int i = 0; i < countCreatedThreads; ++i) {
                threadJoinResult = pthread_join(createdThreadsID[i], NULL);
                if(0 != threadJoinResult) {
                        printf("%s %d", "failed to join child thread, error code ==", threadJoinResult);
                        return EXIT_FAILURE;
                }
        }

        pthread_exit(NULL);
        return EXIT_SUCCESS;
}

