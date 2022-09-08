#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

const int DELAY_BETWEEN_MEALS = 30000;
const int AMOUNT_FOOD = 50;
const int NUMBER_PHILOSOPHERS = 5;

typedef struct philosophers_parameters {
  pthread_mutex_t *foodlock;
  pthread_mutex_t *mutex_for_two_forks;
  pthread_mutex_t *forks;
  int philosopher_number;
  int *leftovers;
} philosophers_parameters;

void destroy_mutex(const int count_destroy_mutex_forks, philosophers_parameters *const philosopher_parameters) {
        int mutexDestroyResult;
        mutexDestroyResult = pthread_mutex_destroy(philosopher_parameters->foodlock);
        if (mutexDestroyResult) {
                printf("%s %d", "failed to destroy mutex, error code == ", mutexDestroyResult);
        }
        mutexDestroyResult = pthread_mutex_destroy(philosopher_parameters->mutex_for_two_forks);
        if (mutexDestroyResult) {
                printf("%s %d", "failed to destroy mutex, error code == ", mutexDestroyResult);
        }
        for (int j = 0; j < count_destroy_mutex_forks; ++j) {
                mutexDestroyResult = pthread_mutex_destroy(&philosopher_parameters->forks[j]);
                if (mutexDestroyResult) {
                        printf("%s %d", "failed to destroy mutex, error code == ", mutexDestroyResult);
                }
        }
}

void atExit(const char * const errorMsg) {
        if (NULL != errorMsg) {
                printf("%s", errorMsg);
        }
        pthread_exit(NULL);
}

void lockMutex(pthread_mutex_t *const mutex) {
        if (pthread_mutex_lock(mutex)) {
                atExit("Error locking mutex");
        }
}

void unlockMutex(pthread_mutex_t *const mutex) {
        if (pthread_mutex_unlock(mutex)) {
                atExit("Error unlocking mutex");
        }
}

void down_forks(const int left_fork, const int right_fork, philosophers_parameters *const philosopher_parameters) {
        lockMutex(philosopher_parameters->mutex_for_two_forks);
        unlockMutex(&philosopher_parameters->forks[left_fork]);
        unlockMutex(&philosopher_parameters->forks[right_fork]);
        unlockMutex(philosopher_parameters->mutex_for_two_forks);
}

int food_on_table(philosophers_parameters *const philosopher_parameters) {
        lockMutex(philosopher_parameters->foodlock);
        if (*philosopher_parameters->leftovers > 0) {
                (*philosopher_parameters->leftovers)--;
        }
        unlockMutex(philosopher_parameters->foodlock);
        return *philosopher_parameters->leftovers;
}

void get_forks(const int left_fork, const int right_fork, philosophers_parameters *const philosopher_parameters) {
        int result_trylock_left_fork;
        int result_trylock_right_fork;

        do {
                lockMutex(philosopher_parameters->mutex_for_two_forks);

                result_trylock_left_fork = pthread_mutex_trylock(&philosopher_parameters->forks[left_fork]);
                if (result_trylock_left_fork != EBUSY && result_trylock_left_fork != 0) {
                        atExit("Error trylocking mutex");
                }

                result_trylock_right_fork = pthread_mutex_trylock(&philosopher_parameters->forks[right_fork]);
                if (result_trylock_right_fork != EBUSY && result_trylock_right_fork != 0) {
                        atExit("Error trylocking mutex");
                }

                if (result_trylock_right_fork && !result_trylock_left_fork) {
                        unlockMutex(&philosopher_parameters->forks[left_fork]);
                }

                if (!result_trylock_right_fork && result_trylock_left_fork) {
                        unlockMutex(&philosopher_parameters->forks[right_fork]);
                }

                unlockMutex(philosopher_parameters->mutex_for_two_forks);
        } while (result_trylock_left_fork + result_trylock_right_fork);
}

void *philosopher(void *philosopher_parameters) {
        if (NULL == philosopher_parameters) {
                printf("%s", "thread input argument error");
                return NULL;
        }

        int philosopher_number;
        int left_fork, right_fork, leftovers;
        int total_eatings = 0;

        philosopher_number = ((philosophers_parameters *)philosopher_parameters)->philosopher_number;
        if (philosopher_number < 0 || philosopher_number > NUMBER_PHILOSOPHERS) {
                printf("%s", "wrong number of the philosopher");
                return NULL;
        }
        printf("Philosopher %d sitting down to dinner.\n", philosopher_number);
        right_fork = philosopher_number;
        left_fork = (philosopher_number + 1) % NUMBER_PHILOSOPHERS;

        leftovers = food_on_table(philosopher_parameters);
        while (leftovers > 0) {
                printf("Philosopher %d: get dish %d.\n", philosopher_number, leftovers);
                get_forks(right_fork, left_fork, philosopher_parameters);
                printf("Philosopher %d: eating.\n", philosopher_number);
                if (usleep(DELAY_BETWEEN_MEALS * (AMOUNT_FOOD - leftovers + 1))) {
                        printf("%s", "Error usleep");
                }
                down_forks(left_fork, right_fork, philosopher_parameters);
                total_eatings++;
                leftovers = food_on_table(philosopher_parameters);
        }

        printf("Philosopher %d is done eating. Total: %d\n", philosopher_number, total_eatings);
        return NULL;
}

int main() {
        long i;
        int mutexInitResult;
        int mutexDestroyResult;
        int threadCreationResult;
        int threadJoinResult;
        int countCreatedThreads = NUMBER_PHILOSOPHERS;
        int leftovers = AMOUNT_FOOD;
        pthread_mutex_t forks[NUMBER_PHILOSOPHERS];
        pthread_t phils[NUMBER_PHILOSOPHERS];
        pthread_mutex_t foodlock;
        pthread_mutex_t mutex_for_two_forks;

        philosophers_parameters philosopher_parameters[countCreatedThreads];
        mutexInitResult = pthread_mutex_init(&foodlock, NULL);
        if (0 != mutexInitResult) {
                printf("%s %d", "failed to init mutex, error code == ", mutexInitResult);
                return EXIT_FAILURE;
        }
        mutexInitResult = pthread_mutex_init(&mutex_for_two_forks, NULL);
        if (mutexInitResult) {
                printf("%s %d", "failed to init mutex, error code == ", mutexInitResult);
                mutexDestroyResult = pthread_mutex_destroy(&foodlock);
                if (0 != mutexDestroyResult) {
                        printf("%s %d", "failed to destroy mutex, error code == ", mutexInitResult);
                }
                return EXIT_FAILURE;
        }

        for (int i = 0; i < countCreatedThreads; ++i) {
                philosopher_parameters[i].mutex_for_two_forks = &mutex_for_two_forks;
                philosopher_parameters[i].foodlock = &foodlock;
                philosopher_parameters[i].forks = forks;
                philosopher_parameters[i].leftovers = &leftovers;
        }

        for (i = 0; i < NUMBER_PHILOSOPHERS; i++) {
                mutexInitResult = pthread_mutex_init(&forks[i], NULL);
                if (0 != mutexInitResult) {
                        printf("%s %d", "failed to init mutex, error code == ", mutexInitResult);
                        destroy_mutex(i, philosopher_parameters);
                        return EXIT_FAILURE;
                }
        }

        for (i = 0; i < NUMBER_PHILOSOPHERS; i++) {
                philosopher_parameters[i].philosopher_number = i;
                threadCreationResult = pthread_create(&phils[i], NULL, philosopher, (void *)&philosopher_parameters[i]);
                if (0 != threadCreationResult) {
                        printf("%s %d", "failed to create thread, error code ==", threadCreationResult);
                        countCreatedThreads = i;
                        break;
                }
        }

        for (i = 0; i < countCreatedThreads; i++) {
                threadJoinResult = pthread_join(phils[i], NULL);
                if (0 != threadJoinResult) {
                        printf("%s %d", "failed to join child thread, error code ==", threadJoinResult);
                        destroy_mutex(NUMBER_PHILOSOPHERS, philosopher_parameters);
                        return EXIT_FAILURE;
                }
        }

        destroy_mutex(NUMBER_PHILOSOPHERS, philosopher_parameters);
        pthread_exit(NULL);
        return EXIT_SUCCESS;
}
