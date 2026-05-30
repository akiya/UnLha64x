/* MSVC manual config.h for LHa core logic */

#ifndef CONFIG_H
#define CONFIG_H

#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_CTYPE_H 1
#define HAVE_ERRNO_H 1

#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRCASECMP 1
#define HAVE_MKTIME 1
#define HAVE_TIME_H 1
#define HAVE_VSNPRINTF 1

#define SIZEOF_LONG 4
#define HAVE_LONG_LONG 1
#define HAVE_UINT64_T 1
#define SIZEOF_OFF_T 8

/* MSVC specific types */
#ifdef _MSC_VER
#include <BaseTsd.h>
#include <sys/types.h>
typedef SSIZE_T ssize_t;
#define HAVE_SSIZE_T 1
#define off_t __int64
#define RETSIGTYPE void
#define HAVE_CHSIZE 1
#define chsize _chsize
#define boolean lha_bool
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <sys/utime.h>
#define HAVE_UTIME_H 1
#define ADDITIONAL_SUFFIXES "lzs,pma"
#define READ_BINARY "rb"
#define WRITE_BINARY "wb"
#define HAVE_DIRENT_H 1
#define HAVE_FNMATCH_H 1
#define strcasecmp _stricmp
#endif
#define SUPPORT_LH7 1
#define MULTIBYTE_FILENAME 2
#define DEFAULT_LZHUFF_METHOD LZHUFF5_METHOD_NUM
#define NEED_INCREMENTAL_INDICATOR 1

/* Platform string */
#define PLATFORM "Windows x64"
#define PACKAGE_NAME "LHa for Windows"
#define PACKAGE_VERSION "1.14i-ac"
#define LHA_CONFIGURE_OPTIONS ""

/* MSVC specific */
#ifndef __cplusplus
#define inline __inline
#define restrict
#endif

#endif /* CONFIG_H */
