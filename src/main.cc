#include <iostream>
#include <pthread.h>
#include "config.h"
#include "timing_utils.h"

#ifdef HAVE_DARWIN_SPINLOCKS
#include <libkern/OSAtomic.h>
#endif

#ifdef HAVE_UNFAIR_LOCKS
#include <os/lock.h>
#endif

using namespace std;

enum class LockType {SPINLOCK, MUTEX};

struct shared_state {
    shared_state(void (*lock_function)(shared_state*), void (*unlock_function)(shared_state*), int target_iterations, int num_threads, bool* ready_threads) :
            lock_function(lock_function),
            unlock_function(unlock_function),
            bounces(0),
            iterations(0),
            target_iterations(target_iterations),
            num_threads(num_threads),
            ready(false),
            ready_threads(ready_threads),
            recorded_end(false) {
        pthread_mutex_init(&ready_mutex, nullptr);
        pthread_cond_init(&ready_cond, nullptr);

        pthread_mutex_init(&victim_mutex, nullptr);
        #if defined(HAVE_DARWIN_SPINLOCKS)
            victim_spinlock = 0;
        #elif defined(HAVE_PTHREAD_SPINLOCKS)
            pthread_spin_init(&victim_spinlock, PTHREAD_PROCESS_PRIVATE);
        #elif defined(HAVE_UNFAIR_LOCKS)
            victim_spinlock = 0;
        #endif

        #if defined(HAVE_UNFAIR_LOCKS)
            victim_unfair_lock = OS_UNFAIR_LOCK_INIT;
        #endif
    }

    ~shared_state() {
        pthread_mutex_destroy(&ready_mutex);
        pthread_cond_destroy(&ready_cond);

        pthread_mutex_destroy(&victim_mutex);

        #ifdef HAVE_DARWIN_SPINLOCKS
            /* nothing to destroy */
        #elif defined(HAVE_PTHREAD_SPINLOCKS)
            pthread_spin_destroy(&victim_spinlock);
        #elif defined(HAVE_UNFAIR_LOCKS)
            /* nothing to destroy */
        #endif

        #if defined(HAVE_UNFAIR_LOCKS)
            /* nothing to destroy */
        #endif
    }

    bool all_ready() {
        for (int i = 0; i < num_threads; i++) {
            if (!ready_threads[i]) {
                return false;
            }
        }
        return true;
    }

    void block_until_all_threads_are_ready(int thread_idx) {
        int err = pthread_mutex_lock(&ready_mutex);
        if (err != 0) {
            throw runtime_error("pthread_mutex_lock(): " + string(strerror(err)));
        }

        ready_threads[thread_idx] = true;
        while (!ready) {
            if (all_ready()) {
                ready = true;
                err = pthread_cond_broadcast(&ready_cond);
                if (err != 0) {
                    throw runtime_error("pthread_cond_broadcast(): " + string(strerror(err)));
                }

                TimingUtils::Nanotime(&start);

            } else {
                err = pthread_cond_wait(&ready_cond, &ready_mutex);
                if (err != 0) {
                    throw runtime_error("pthread_cond_wait(): " + string(strerror(err)));
                }
            }
        }

        err = pthread_mutex_unlock(&ready_mutex);
        if (err != 0) {
            throw runtime_error("pthread_mutex_unlock(): " + string(strerror(err)));
        }
    }

    void (*lock_function)(shared_state*);
    void (*unlock_function)(shared_state*);
    int bounces;
    int iterations;
    int target_iterations;
    int num_threads;
    bool ready;
    bool* ready_threads;
    pthread_mutex_t ready_mutex;
    pthread_cond_t ready_cond;

    pthread_mutex_t victim_mutex;
    struct timespec start;
    struct timespec end;
    bool recorded_end;

    #if defined(HAVE_DARWIN_SPINLOCKS)
        OSSpinLock victim_spinlock;
    #elif defined(HAVE_PTHREAD_SPINLOCKS)
        pthread_spinlock_t victim_spinlock;
    #elif defined(HAVE_UNFAIR_LOCKS)
        os_unfair_lock victim_spinlock;
    #endif

    #if defined(HAVE_UNFAIR_LOCKS)
        os_unfair_lock victim_unfair_lock;
    #endif
};


struct thread_state {
    thread_state() : thread_state(0, nullptr) {}
    thread_state(int thread_idx, shared_state* shared_state) :
            thread_idx(thread_idx),
            last_iteration(0),
            shared_state(shared_state) {}

    thread_state(const thread_state& mE)            = default;
    thread_state(thread_state&& mE)                 = default;
    thread_state& operator=(const thread_state& mE) = default;
    thread_state& operator=(thread_state&& mE)      = default;

    int thread_idx;
    int last_iteration;
    shared_state* shared_state;
};


void spinlock_lock(shared_state* shared_state) {
    #if defined(HAVE_DARWIN_SPINLOCKS)
        OSSpinLockLock(&shared_state->victim_spinlock);
    #elif defined(HAVE_PTHREAD_SPINLOCKS)
        pthread_spin_lock(&shared_state->victim_spinlock);
    #elif defined(HAVE_UNFAIR_LOCKS)
        os_unfair_lock_lock(&shared_state->victim_unfair_lock);
    #else
        throw runtime_error("No spinlock implementation available!");
    #endif
}


void spinlock_unlock(shared_state* shared_state) {
    #if defined(HAVE_DARWIN_SPINLOCKS)
        OSSpinLockUnlock(&shared_state->victim_spinlock);
    #elif defined(HAVE_PTHREAD_SPINLOCKS)
        pthread_spin_unlock(&shared_state->victim_spinlock);
    #elif defined(HAVE_UNFAIR_LOCKS)
        os_unfair_lock_unlock(&shared_state->victim_unfair_lock);
    #else
        throw runtime_error("No spinlock implementation available!");
    #endif
}


void mutex_lock(shared_state* shared_state) {
    int err = pthread_mutex_lock(&shared_state->victim_mutex);
    if (err != 0) {
        throw runtime_error("pthread_mutex_lock(): " + string(strerror(err)));
    }
}


void mutex_unlock(shared_state* shared_state) {
    int err = pthread_mutex_unlock(&shared_state->victim_mutex);
    if (err != 0) {
        throw runtime_error("pthread_mutex_unlock(): " + string(strerror(err)));
    }
}


void unfair_lock_lock(shared_state* shared_state) {
    #if defined(HAVE_UNFAIR_LOCKS)
        os_unfair_lock_lock(&shared_state->victim_unfair_lock);
    #else
        throw runtime_error("No unfair_lock implementation available!");
    #endif
}


void unfair_lock_unlock(shared_state* shared_state) {
    #if defined(HAVE_UNFAIR_LOCKS)
        os_unfair_lock_unlock(&shared_state->victim_unfair_lock);
    #else
        throw runtime_error("No unfair_lock implementation available!");
    #endif
}


bool increment(thread_state* thread_state) {
    bool keep_iterating = true;

    shared_state* shared_state = thread_state->shared_state;
    shared_state->lock_function(shared_state);

    int current_value = shared_state->iterations;
    int new_value = current_value + 1;
    shared_state->iterations = new_value;
    if (new_value >= shared_state->target_iterations) {
        keep_iterating = false;
    }

    if (current_value != thread_state->last_iteration) {
        shared_state->bounces++;
    }

    if (!keep_iterating && !shared_state->recorded_end) {
        TimingUtils::Nanotime(&shared_state->end);
        shared_state->recorded_end = true;
    }

    thread_state->last_iteration = new_value;
    shared_state->unlock_function(shared_state);

    return keep_iterating;
}


void thread_run(thread_state* thread_state) {
    shared_state* shared_state = thread_state->shared_state;
    shared_state->block_until_all_threads_are_ready(thread_state->thread_idx);

    for (;;) {
        if (!increment(thread_state)) {
            break;
        }
    }
}


void* thread_main(void* ptr) {
    thread_state* thread_state = static_cast<struct thread_state*>(ptr);
    try {
        thread_run(thread_state);
    } catch (exception const& e) {
        cerr << "Thread crashed: " << e.what() << endl;
        abort();
    } catch (...) {
        cerr << "Thread crashed" << endl;
        abort();
    }
    return nullptr;
}


void run(char * const argv[]) {
    char * const type = argv[0];
    int target_iterations = stoi(argv[1]);
    int num_threads = stoi(argv[2]);

    void (*lock_function)(shared_state*);
    void (*unlock_function)(shared_state*);
    if (strcasecmp(type, "spinlock") == 0) {
        lock_function = spinlock_lock;
        unlock_function = spinlock_unlock;
    } else if (strcasecmp(type, "mutex") == 0) {
        lock_function = mutex_lock;
        unlock_function = mutex_unlock;
    } else if (strcasecmp(type, "unfair_lock") == 0) {
        lock_function = unfair_lock_lock;
        unlock_function = unfair_lock_unlock;
    } else {
        throw runtime_error("Unknown lock type: " + string(type));
    }

    bool* ready_threads = new bool[num_threads];
    shared_state shared_state(lock_function, unlock_function, target_iterations, num_threads, ready_threads);

    pthread_t* threads = new pthread_t[num_threads];
    thread_state* thread_states = new thread_state[num_threads];

    for (int i = 0; i < num_threads; i++) {
        thread_states[i] = thread_state(i, &shared_state);
        int err = pthread_create(&threads[i], nullptr, thread_main, &thread_states[i]);
        if (err != 0) {
            throw runtime_error("Error creating thread: " + string(strerror(err)));
        }
    }

    for (int i = 0; i < num_threads; i++) {
        int err = pthread_join(threads[i], nullptr);
        if (err != 0) {
            throw runtime_error("Error joining thread: " + string(strerror(err)));
        }
    }

    struct timespec total_time;
    TimingUtils::Subtract(&total_time, &shared_state.end, &shared_state.start);

    double seconds = total_time.tv_sec + total_time.tv_nsec / 1e9;
    printf("Total time:             %.3fs\n", seconds);
    printf("Lock/Unlock per second: %.0f\n", target_iterations / seconds);
    printf("\n");
    printf("Bounces:                %d\n", shared_state.bounces);
    printf("Bounces per second:     %.0f\n", shared_state.bounces / seconds);
}


int main(int argc, char * const argv[]) {
    if (argc != 4) {
        cerr << "usage: " << argv[0] << " (spinlock|mutex|unfair_lock) iterations threads" << endl;
        return 1;
    }

    try {
        run(argv + 1);
        return 0;
    } catch (exception const& e) {
        cerr << "Problem running test: " << e.what() << endl;
        return 1;
    } catch (...) {
        cerr << "Problem running test" << endl;
        return 1;
    }
}
