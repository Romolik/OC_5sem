#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#define COUNT_OF_THREADS 2
#define MAX_LENGTH_OF_STRING 80
#define FAILURE -1
#define SUCCESS 0
#define TIME_TO_SLEEP_SECONDS 5
#define EXECUTION 1

typedef struct Node {
  char *data;
  struct Node *next;
} Node;

typedef struct SharedData {
  pthread_mutex_t mutex;
  int currentListSize;
  struct Node *head;
  int execution;
} SharedData;

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

void cleanSharedData(SharedData *sharedData) {
        assert(NULL != sharedData);

        int mutexDestroyResult;
        mutexDestroyResult = pthread_mutex_destroy(&sharedData->mutex);
        if (0 != mutexDestroyResult) {
                printf("%s %d", "failed to destroy mutex(forks), error code == ", mutexDestroyResult);
        }
}

void destroyList(SharedData *sharedData) {
        assert(NULL != sharedData);

        Node *current = sharedData->head;
        Node *tmp = NULL;
        while (NULL != current) {
                tmp = current->next;
                free(current);
                current = tmp;
        }
}

void printList(SharedData *sharedData) {
        assert(NULL != sharedData);

        Node *currentNode = sharedData->head;

        printf("%s", "________Printing_________\n");
        while (NULL != currentNode) {
                printf( "%s\n", currentNode->data);
                currentNode = currentNode->next;
        }
        printf("%s", "________The end__________\n\n\n");
}

Node *addFirstElement(SharedData *sharedData, char *string) {
        assert(NULL != sharedData);
        assert(NULL != string);

        Node *newElement = NULL;
        Node *first = sharedData->head;

        newElement = malloc(sizeof(Node));
        if (NULL == newElement) {
                printf("%s", "Memory allocation error\n");
                return NULL;
        }

        sharedData->currentListSize += 1;
        if (NULL != first) {
                newElement->next = first;
        } else {
                newElement->next = NULL;
        }
        newElement->data = string;
        sharedData->head = newElement;
        return newElement;
}

void swap(Node *a, Node *b) {
        assert(NULL != a);
        assert(NULL != b);

        char *tmp = a->data;
        a->data = b->data;
        b->data = tmp;
}

void sortBubble(SharedData *sharedData) {
        assert(NULL != sharedData);

        Node *list = (Node *)sharedData->head;
        Node *iteri = NULL;

        for (iteri = list; iteri; iteri = iteri->next) {
                Node *iterj = NULL;
                for (iterj = iteri->next; iterj; iterj = iterj->next) {
                        if (0 < strcmp(iteri->data, iterj->data)) {
                                swap(iteri, iterj);
                        }
                }
        }
}

void *sortList(void *threadData) {
        assert(NULL != threadData);

        SharedData *sharedData = (SharedData *)threadData;

        while (EXECUTION == sharedData->execution) {
                unsigned int secondsRemainingToSleep = sleep(TIME_TO_SLEEP_SECONDS);
                while (0 != secondsRemainingToSleep) {
                        secondsRemainingToSleep = sleep(TIME_TO_SLEEP_SECONDS);
                }

                if (SUCCESS != lockMutex(&sharedData->mutex)) {
                        return NULL;
                }

                sortBubble(sharedData);

                if (SUCCESS != unlockMutex(&sharedData->mutex)) {
                        return NULL;
                }
        }

        return NULL;
}

int main() {
        pthread_t sortingThread;
        Node *returned = NULL;
        char *currentString = NULL;
        int SUCCESS_FINISH;

        SharedData sharedData[COUNT_OF_THREADS];
        sharedData->execution = EXECUTION;

        int mutexInitResult;
        mutexInitResult =  pthread_mutex_init(&sharedData->mutex, NULL);
        if (0 != mutexInitResult) {
                printf("%s %d", "failed to init mutex, error code == ", mutexInitResult);
                return EXIT_FAILURE;
        }

        sharedData->head = NULL;

        int threadCreationResult;
        threadCreationResult = pthread_create(&sortingThread, NULL, sortList, (void *)sharedData);
        if (0 != threadCreationResult) {
                printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                cleanSharedData(sharedData);
                return EXIT_FAILURE;
        }

        while (EXECUTION) {
                currentString = malloc(MAX_LENGTH_OF_STRING * sizeof(char));
                if (NULL == currentString) {
                        printf("%s", "Memory allocation error\n");
                        SUCCESS_FINISH = 0;
                        break;
                }

                char* fgetsResult;
                fgetsResult = fgets(currentString, MAX_LENGTH_OF_STRING, stdin);
                if (NULL == fgetsResult) {
                        printf("%s", "An error occurred while reading from the stdin\n");
                        SUCCESS_FINISH = 0;
                        break;
                } else if (EOF == *fgetsResult) {
                        break;
                }

                if ('\n' != currentString[0]) {
                        if ('\n' == currentString[strlen(currentString) - 1]) {
                                currentString[strlen(currentString) - 1] = '\0';
                        }

                        if (SUCCESS != lockMutex(&sharedData->mutex)) {
                                return EXIT_FAILURE;
                        }

                        returned = addFirstElement(sharedData, currentString);

                        if (SUCCESS != unlockMutex(&sharedData->mutex)) {
                                return EXIT_FAILURE;
                        }

                        if (NULL == returned) {
                                SUCCESS_FINISH = 0;
                                break;
                        }
                } else {
                        if (SUCCESS != lockMutex(&sharedData->mutex)) {
                                return EXIT_FAILURE;
                        }

                        printList(sharedData);

                        if (SUCCESS != unlockMutex(&sharedData->mutex)) {
                                return EXIT_FAILURE;
                        }
                }
        }

        sharedData->execution = 0;

        int threadJoinResult;
        threadJoinResult = pthread_join(sortingThread, NULL);
        if (0 != threadJoinResult) {
                printf("%s %d", "failed to join thread, error code ==", threadJoinResult);
                return EXIT_FAILURE;
        }

        cleanSharedData(sharedData);
        destroyList(sharedData);

        if (!SUCCESS_FINISH) {
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

