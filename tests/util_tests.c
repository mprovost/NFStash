#include "minunit.h"
#include "src/util.h"

int tests_run = 0;

static char *test_reverse_fqdn() {
    char *fqdn = "www.test.com";
    char *ndqf;
    char *reverse = "com.test.www";

    ndqf = reverse_fqdn(fqdn);
    printf("%s -> %s\n", fqdn, ndqf);
    mu_assert("error, fqdn not reversed!", strcmp(reverse, ndqf) == 0);
    return 0;
}

static char *test_nfs_perror_nfs3ok() {
    nfsstat3 status = NFS3_OK;

    mu_assert("error, NFS3_OK!", nfs_perror(status) == 0);
    return 0;
}

static char *test_nfs_perror_toobig() {
    /* this is the highest status code */
    nfsstat3 status = NFS3ERR_JUKEBOX;

    /* make this too big */
    status++;

    mu_assert("error, input too large!", nfs_perror(status) == -1);
    return 0;
}

static char *test_nfs_perror_toobig_low() {
    /* this is the highest status code in the low range */
    nfsstat3 status = NFS3ERR_REMOTE;

    /* make this one too big */
    status++;

    mu_assert("error, input too large!", nfs_perror(status) == -1);
    return 0;
}

static char *all_tests() {
    mu_run_test(test_reverse_fqdn);
    mu_run_test(test_nfs_perror_nfs3ok);
    mu_run_test(test_nfs_perror_toobig);
    mu_run_test(test_nfs_perror_toobig_low);
    return 0;
}

int main(int __attribute__((unused)) argc, __attribute__((unused)) char **argv) {
    char *result = all_tests();

    if (result != 0)
        printf("%s\n", result);
    else
        printf("ALL TESTS PASSED\n");
    printf("tests run: %d\n", tests_run);

    return result != 0;
}
