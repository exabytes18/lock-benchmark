#include <iostream>
#include <string.h>
#include "timing_utils.h"

#define SECONDS_TO_NANOSECONDS(seconds) (seconds * 1000000000);


#ifdef __APPLE__
#include <mach/mach_time.h>
#include <pthread.h>

static mach_timebase_info_data_t _TIMEBASE;
static pthread_once_t _TIMEBASE_INIT = PTHREAD_ONCE_INIT;

static void _timebase_init(void) {
    if (mach_timebase_info(&_TIMEBASE) != KERN_SUCCESS) {
        std::cerr << "CRITICAL! mach_timebase_info() failed\n" << std::endl;
        abort();
    }
}

void TimingUtils::Nanotime(struct timespec* timespec) {
    uint64_t t;

    pthread_once(&_TIMEBASE_INIT, _timebase_init);
    t = mach_absolute_time() * _TIMEBASE.numer / _TIMEBASE.denom;
    timespec->tv_sec = t / SECONDS_TO_NANOSECONDS(1);
    timespec->tv_nsec = t - SECONDS_TO_NANOSECONDS(timespec->tv_sec);
}
#else
void TimingUtils::Nanotime(struct timespec* timespec) {
    if (clock_gettime(CLOCK_MONOTONIC, timespec) != 0) {
        std::cerr << "CRITICAL! clock_gettime(): " << strerror(errno) << std::endl;
        abort();
    }
}
#endif


inline void TimingUtils::Normalize(struct timespec* result) {
    long overflow_seconds = result->tv_nsec / SECONDS_TO_NANOSECONDS(1);
    result->tv_sec += overflow_seconds;
    result->tv_nsec -= SECONDS_TO_NANOSECONDS(overflow_seconds);

    if (result->tv_sec > 0 && result->tv_nsec < 0) {
        result->tv_sec -= 1;
        result->tv_nsec += SECONDS_TO_NANOSECONDS(1);
    }

    if (result->tv_sec < 0 && result->tv_nsec > 0) {
        result->tv_sec += 1;
        result->tv_nsec -= SECONDS_TO_NANOSECONDS(1);
    }
}


void TimingUtils::Add(struct timespec* result, const struct timespec* a, const struct timespec* b) {
    result->tv_sec = a->tv_sec + b->tv_sec;
    result->tv_nsec = a->tv_nsec + b->tv_nsec;
    Normalize(result);
}


void TimingUtils::Subtract(struct timespec* result, const struct timespec* a, const struct timespec* b) {
    result->tv_sec = a->tv_sec - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
    Normalize(result);
}
