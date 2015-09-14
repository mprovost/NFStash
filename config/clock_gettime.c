#include <stdio.h>
#include <time.h>

int test_clock_gettime() {
    struct timespec tp;

#ifdef CLOCK_MONOTONIC_RAW
    return clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
#else
    return clock_gettime(CLOCK_MONOTONIC, &tp);
#endif
}

int main(int argc, char **argv) {
    return test_clock_gettime();
}
