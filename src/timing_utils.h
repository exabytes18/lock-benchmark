#ifndef KIWI_TIMING_UTILS_H_
#define KIWI_TIMING_UTILS_H_


namespace TimingUtils {
    void Nanotime(struct timespec* timespec);
    void Normalize(struct timespec* result);
    void Add(struct timespec* result, const struct timespec* a, const struct timespec* b);
    void Subtract(struct timespec* result, const struct timespec* a, const struct timespec* b);
}

#endif  // KIWI_TIMING_UTILS_H_
