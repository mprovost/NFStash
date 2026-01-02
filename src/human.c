/*
 * function for printing (human) size output
 */

#include "human.h"
#include <inttypes.h> /* for PRIu64 */
#include <stdio.h> /* for snprintf() */


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

    /* skip zeroes */
    if (input > 0) {
        if (prefix == HUMAN) {
            /* try and find the best fit, starting with exabytes and working down */
            shifted_prefix = EXA;
            /* BYTE == 0 */
            while (shifted_prefix >= BYTE) {
                shifted = input >> shifted_prefix;
                if (shifted && shifted >= 10)
                    break;
                shifted_prefix -= 10;
            }
        } else {
            shifted = input >> shifted_prefix;

            /* check if the prefix is forcing us to print a zero result when there actually is something there */
            /* this can happen if a large unit is specified (MB for a 1KB file for example) */
            if (input > 0 && shifted == 0) {
                /* in this case, print a greater than sign before the zero to indicate that there was a nonzero result */
                output[index++] = '>';
            }
        }
    } else {
        shifted = input;
    }

    /* TODO check the length */
    index += snprintf(&output[index], width, "%" PRIu64 "", shifted);

    /* print the label */
    /* only print this for human mode otherwise stuff the prefix in the header */
    /* don't print label for zero results */
    if (input > 0 && prefix == HUMAN) {
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
