
#include <stdio.h>  // printf
#include <ctype.h>  // isprint

void hexdump(const void * const ptr, const unsigned len) {
    unsigned char *buf = (unsigned char*)ptr;

    fprintf(stderr, "%d\n", len);
    for (unsigned i = 0; i < len; i += 16) {
        fprintf(stderr, "%06x: ", i);
        for (unsigned j = 0; j < 16; j++)
            if (i+j < len)
                fprintf(stderr, "%02x ", buf[i+j]);
            else
                fprintf(stderr, "   ");
        fprintf(stderr, " ");
        for (unsigned j = 0; j < 16; j++)
            if (i+j < len)
                fprintf(stderr, "%c", isprint(buf[i+j]) ? buf[i+j] : '.');
        fprintf(stderr, "\n");
    }
}
