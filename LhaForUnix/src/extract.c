/* ------------------------------------------------------------------------ */
/* LHa for UNIX                                                             */
/*              extract.c -- extrcat from archive                           */
/*                                                                          */
/*      Modified                Nobutaka Watazaki                           */
/*                                                                          */
/*  Ver. 1.14   Source All chagned              1995.01.14  N.Watazaki      */
/* ------------------------------------------------------------------------ */
#include "lha.h"

int
decode_lzhuf(FILE *infp, FILE *outfp,
             off_t original_size, off_t packed_size,
             char *name, int method, off_t *read_sizep)
{
    unsigned int crc;
    struct interfacing iface;

    iface.method = method;
    iface.infile = infp;
    iface.outfile = outfp;
    iface.original = original_size;
    iface.packed = packed_size;
    iface.read_size = 0;

    switch (method) {
    case LZHUFF0_METHOD_NUM:    /* -lh0- */
        iface.dicbit = LZHUFF0_DICBIT;
        break;
    case LZHUFF1_METHOD_NUM:    /* -lh1- */
        iface.dicbit = LZHUFF1_DICBIT;
        break;
    case LZHUFF2_METHOD_NUM:    /* -lh2- */
        iface.dicbit = LZHUFF2_DICBIT;
        break;
    case LZHUFF3_METHOD_NUM:    /* -lh2- */
        iface.dicbit = LZHUFF3_DICBIT;
        break;
    case LZHUFF4_METHOD_NUM:    /* -lh4- */
        iface.dicbit = LZHUFF4_DICBIT;
        break;
    case LZHUFF5_METHOD_NUM:    /* -lh5- */
        iface.dicbit = LZHUFF5_DICBIT;
        break;
    case LZHUFF6_METHOD_NUM:    /* -lh6- */
        iface.dicbit = LZHUFF6_DICBIT;
        break;
    case LZHUFF7_METHOD_NUM:    /* -lh7- */
        iface.dicbit = LZHUFF7_DICBIT;
        break;
    case LARC_METHOD_NUM:       /* -lzs- */
        iface.dicbit = LARC_DICBIT;
        break;
    case LARC5_METHOD_NUM:      /* -lz5- */
        iface.dicbit = LARC5_DICBIT;
        break;
    case LARC4_METHOD_NUM:      /* -lz4- */
        iface.dicbit = LARC4_DICBIT;
        break;
    case PMARC0_METHOD_NUM:     /* -pm0- */
        iface.dicbit = PMARC0_DICBIT;
        break;
    case PMARC2_METHOD_NUM:     /* -pm2- */
        iface.dicbit = PMARC2_DICBIT;
        break;
    default:
        warning("unknown method %d", method);
        iface.dicbit = LZHUFF5_DICBIT; /* for backward compatibility */
        break;
    }

    if (iface.dicbit == 0) { /* LZHUFF0_DICBIT or LARC4_DICBIT or PMARC0_DICBIT*/
        start_indicator(name,
                        original_size,
                        verify_mode ? "Testing " : "Melting ",
                        2048);

        if (dump_lzss)
            printf("no use slide\n");

        *read_sizep = copyfile(infp, (verify_mode ? NULL : outfp),
                               original_size, 2, &crc);
    }
    else {
        start_indicator(name,
                        original_size,
                        verify_mode ? "Testing " : "Melting ",
                        1 << iface.dicbit);
        if (dump_lzss)
            printf("\n");

        crc = decode(&iface);
        *read_sizep = iface.read_size;
    }

    finish_indicator(name, verify_mode ? "Tested  " : "Melted  ");

    return crc;
}
