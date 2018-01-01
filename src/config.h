// cmake will use this template (config.h.in) to generate config.h

/* #undef HAVE_PTHREAD_SPINLOCKS */
#define HAVE_DARWIN_SPINLOCKS
#define HAVE_UNFAIR_LOCKS
