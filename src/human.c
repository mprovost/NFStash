/*
 * function for printing (human) size output
 */

#include "human.h"
#include <inttypes.h> /* for PRIu64 */


/* labels for printing size prefixes */
static const char prefix_label[] = {
    /* TODO something better than a space for bytes */
    [BYTE] = ' ', /* nothing for just bytes */
    [KILO] = 'K',
    [MEGA] = 'M',
    [GIGA] = 'G',
    [TERA] = 'T',
    [PETA] = 'P',
    [EXA]  = 'E'
};


/* an NFS size3 is a uint64 in bytes */
/* so the largest value is 18446744073709551615 == 15 exabytes */
/* The longest output for each column (up to 15 exabytes in bytes) is 20 digits */
/* these widths don't include space for labels or trailing NULL */
/* TODO struct so we can put in a label and a width */
static const int prefix_width[] = {
    /* if we're using human output the number will never be longer than 4 digits */
    /* add one for per-result size labels */
    [HUMAN] = 5,
    /* 15EB in B  = 18446744073709551615 */
    [BYTE]  = 20,
    /* 15EB in KB = 18014398509481983 */
    [KILO]  = 17,
    /* 15EB in MB = 17592186044415 */
    [MEGA]  = 14,
    /* 15EB in GB = 17179869183 */
    [GIGA]  = 11,
    /* 15EB in TB = 16777215 */
    [TERA]  = 8,
    /* 15EB in PB = 16383 */
    [PETA]  = 5,
    /* 15EB in EB = 15 */
    [EXA]   = 2,
};


/* generate formatted size strings */
/* takes an input size and a string pointer to output into */
/* returns the size of the output string, including NULL */
/* in "human" mode, calculate the best fit and print the label since each filesystem can have a different "best" unit */
/* otherwise printing the prefix has moved into the header instead of after each number */
/* the "best" fit is the largest unit that fits into 4 digits */
/* note that this is different than other dfs which fit it into 3 digits and use a decimal */
/* in our case, prefer the extra resolution and avoid decimals */
int prefix_print(size3 input, char *output, enum byte_prefix prefix) {
    /* string position */
    int index = 0;
    size3 shifted;
    enum byte_prefix shifted_prefix = prefix;
    size_t width = prefix_width[prefix] + 1; /* add one to length for terminating NULL */

    if (prefix == HUMAN) {
        /* try and find the best fit, starting with exabytes and working down */
        shifted_prefix = EXA;
        while (shifted_prefix) {
            shifted = input >> shifted_prefix;
            if (shifted && shifted > 10)
                break;
            shifted_prefix -= 10;
        }
    } else {
        shifted = input >> shifted_prefix;

        /* check if the prefix is forcing us to print a zero result when there actually is something there */
        /* this can happen if a large unit is specified (terabytes for a small filesystem for example) */
        if (input > 0 && shifted == 0) {
            /* in this case, print a less than sign before the zero to indicate that there was a nonzero result */
            output[index++] = '<';
        }
    }

    /* TODO check the length */
    index += snprintf(&output[index], width, "%" PRIu64 "", shifted);

    /* print the label */
    /* only print this for human mode otherwise stuff the prefix in the header */
    if (prefix == HUMAN) {
        /* don't print a blank space for bytes */
        if (shifted_prefix > BYTE) {
            output[index] = prefix_label[shifted_prefix];
        }
    }

    /* Don't print a B for bytes (ie KB, MB) because this is also used for inodes */

    /* terminate the string */
    output[++index] = '\0';

    return index;
}
