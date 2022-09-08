#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

typedef struct threadParameters {
  int threadNumber;
  int countThreads;
  double result;
} threadParameters;

const int numSteps = 200000000;
const int MINIMUM_NUMBER_THREADS = 1;
const size_t MAXIMUM_ORDER_NUMBER_THREADS = 7;

void *calculate(void *parameter) {
        if(NULL == parameter) {
                printf("%s", "thread input argument error");
                return NULL;
        }

        double localPi = 0.0;
        int i = ((threadParameters *)parameter)->threadNumber;
        for(; i < numSteps; i += ((threadParameters *)parameter)->countThreads) {
                localPi += 1.0 / (i * 4.0 + 1.0);
                localPi -= 1.0 / (i * 4.0 + 3.0);
        }

        ((threadParameters *)parameter)->result = localPi;
        return parameter;
}

enum CliArguments {
  THREADS_COUNT = 0,

  REQUIRED_COUNT
};

int main(int argc, char **argv) {
        double pi = 0;
        int threadCreationResult = 0;
        int threadJoinResult = 0;
        int countThreads = 0;
        pthread_t *createdThreadsID = 0;
        threadParameters *parameters = 0;

        if(argc != REQUIRED_COUNT + 1) {
                printf("%s", "Incorrect number of command line arguments");
                return EXIT_FAILURE;
        } else {
                if(strlen(argv[THREADS_COUNT + 1]) > MAXIMUM_ORDER_NUMBER_THREADS) {
                        printf("%s", "Incorrect number of threads");
                        return EXIT_FAILURE;
                } else {
                        countThreads = atoi(argv[THREADS_COUNT + 1]);
                }
        }

        if(countThreads < MINIMUM_NUMBER_THREADS || countThreads < 0) {
                printf("%s", "Incorrect number of threads specified");
                return EXIT_FAILURE;
        }

        parameters = malloc(countThreads * sizeof(threadParameters));
        if(NULL == parameters) {
                printf("%s", "Error allocating memory for parameters");
                return EXIT_FAILURE;
        }

        createdThreadsID = malloc(countThreads * sizeof(pthread_t));
        if(NULL == createdThreadsID) {
                printf("%s", "Error allocating memory for createdThreadsID");
                free(parameters);
                return EXIT_FAILURE;
        }

        for(int i = 0; i < countThreads; i++) {
                parameters[i].threadNumber = i;
                parameters[i].countThreads = countThreads;
                threadCreationResult = pthread_create(createdThreadsID + i, NULL, calculate, (void *)(parameters + i));
                if(0 != threadCreationResult) {
                        printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                        for(int j = 0; j < i; ++j) {
                                threadJoinResult = pthread_join(createdThreadsID[i], NULL);
                                if(0 != threadJoinResult) {
                                        printf("%s %d", "failed to join child thread, error code ==", threadJoinResult);
                                        return EXIT_FAILURE;
                                }
                        }
                        free(createdThreadsID);
                        free(parameters);
                        pthread_exit(NULL);
                }
        }

        for(int i = 0; i < countThreads; i++) {
                threadJoinResult = pthread_join(createdThreadsID[i], NULL);
                if(0 != threadJoinResult) {
                        printf("%s %d", "failed to join child thread, error code ==", threadJoinResult);
                        free(createdThreadsID);
                        free(parameters);
                        return EXIT_FAILURE;
                }
                pi += parameters[i].result;
        }

        pi *= 4.0;
        printf("pi done - %.15g \n", pi);

        free(createdThreadsID);
        free(parameters);
        return EXIT_SUCCESS;
}
