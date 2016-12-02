#include "rpcsrc/nfs_prot.h" /* for size3 */

/* for shifting */
enum byte_prefix {
    NONE  = -1,
    BYTE  =  0,
    KILO  = 10,
    MEGA  = 20,
    GIGA  = 30,
    TERA  = 40,
    PETA  = 50,
    EXA   = 60,
    HUMAN = 99
};

/* 20 is enough for 15 exabytes in bytes, plus three for the label and a trailing NUL */
static const int max_prefix_width = 25;

/* prototype */
int prefix_print(size3, char *, enum byte_prefix);
