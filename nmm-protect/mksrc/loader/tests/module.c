#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
static unsigned order;
static unsigned char zero_data[512];
static double (*volatile remainder_function)(double, double) = fmod;
__attribute__((constructor(101))) static void first(void) {
    for (unsigned i = 0; i < sizeof(zero_data); ++i) if (zero_data[i]) abort();
    order = 7;
}
__attribute__((constructor(102))) static void second(void) {
    if (order != 7) abort();
    order = order * 3 + 1;
    zero_data[511] = 19;
}
__attribute__((visibility("default")))
int nmmp_inner_bootstrap_v1(void) {
    int descriptor = open("/dev/null", O_RDONLY, 0);
    if (descriptor < 0 || close(descriptor)) return -2;
    char text[32];
    int n = snprintf(text, sizeof(text), "%u:%u", order, zero_data[511]);
    return n == 5 && !strcmp(text, "22:19") && remainder_function(17.5, 4.0) == 1.5 ? 12345 : -1;
}
