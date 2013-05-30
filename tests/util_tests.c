#include <stdio.h>
#include "minunit.h"
#include "util.h"

int tests_run = 0;

static char *test_reverse_fqdn() {
    char *fqdn = "www.test.com";
    char *ndqf;
    char *reverse = "com.test.www";

    char *result;

    ndqf = reverse_fqdn(fqdn);
    printf("%s -> %s\n", fqdn, ndqf);
    mu_assert("error, fqdn not reversed!", strcmp(reverse, ndqf) == 0);
    return 0;
}

static char *all_tests() {
    mu_run_test(test_reverse_fqdn);
    return 0;
}

int main(int argc, char **argv) {
    char *result = all_tests();

    if (result != 0)
        printf("%s\n", result);
    else
        printf("ALL TESTS PASSED\n");
    printf("tests run: %d\n", tests_run);

    return result != 0;
}
