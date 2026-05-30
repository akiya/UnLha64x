/* ------------------------------------------------------------------------ */
/* LHa for UNIX                                                             */
/*              append.c -- append to archive                               */
/*                                                                          */
/*      Modified                Nobutaka Watazaki                           */
/*                                                                          */
/*  Ver. 1.14   Source All chagned              1995.01.14  N.Watazaki      */
/* ------------------------------------------------------------------------ */
#include "lha.h"

int
encode_lzhuf(FILE *infp, FILE *outfp,
             off_t size, off_t *original_size_var, off_t *packed_size_var,
             char *name, char *hdr_method)
{
    static int method = -1;
    unsigned int crc;
    struct interfacing iface;

    if (method < 0) {
        method = compress_method;
        if (method > 0)
            method = encode_alloc(method);
    }

    iface.method = method;

    if (iface.method > 0) {
        iface.infile = infp;
        iface.outfile = outfp;
        iface.original = size;
        start_indicator(name, size, "Freezing", 1 << dicbit);
        crc = encode(&iface);
        *packed_size_var = iface.packed;
        *original_size_var = iface.original;
    } else {
        start_indicator(name, size, "Storing ", 2048);
        *packed_size_var = *original_size_var =
            copyfile(infp, outfp, size, 0, &crc);
    }
    memcpy(hdr_method, "-lh -", 5);
    hdr_method[3] = iface.method + '0';

    finish_indicator2(name, "Frozen",
            (int) ((*packed_size_var * 100L) / *original_size_var));
    return crc;
}
