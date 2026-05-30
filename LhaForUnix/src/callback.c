#include "lha.h"
#include <stdarg.h>
#include <errno.h>


void
#if STDC_HEADERS
message(char *fmt, ...)
#else
message(fmt, va_alist)
    char *fmt;
    va_dcl
#endif
{
    int errno_sv = errno;
    va_list v;

    fprintf(stderr, "LHa: ");

    va_init(v, fmt);
    vfprintf(stderr, fmt, v);
    va_end(v);

    fputs("\n", stderr);

    errno =  errno_sv;
}

void
#if STDC_HEADERS
warning(char *fmt, ...)
#else
warning(fmt, va_alist)
    char *fmt;
    va_dcl
#endif
{
    int errno_sv = errno;
    va_list v;

    fprintf(stderr, "LHa: Warning: ");

    va_init(v, fmt);
    vfprintf(stderr, fmt, v);
    va_end(v);

    fputs("\n", stderr);

    errno =  errno_sv;
}

void
#if STDC_HEADERS
error(char *fmt, ...)
#else
error(fmt, va_alist)
    char *fmt;
    va_dcl
#endif
{
    int errno_sv = errno;
    va_list v;

    fprintf(stderr, "LHa: Error: ");

    va_init(v, fmt);
    vfprintf(stderr, fmt, v);
    va_end(v);

    fputs("\n", stderr);

    error_occurred = 1;

    errno =  errno_sv;
}

extern void set_lha_error(const char* msg);

void
#if STDC_HEADERS
fatal_error(char *fmt, ...)
#else
fatal_error(fmt, va_alist)
    char *fmt;
    va_dcl
#endif
{
    int errno_sv = errno;
    va_list v;
    char msg[1024];

    fprintf(stderr, "LHa: Fatal error: ");

    va_init(v, fmt);
    vfprintf(stderr, fmt, v);
    va_end(v);

    va_init(v, fmt);
#if STDC_HEADERS
    vsnprintf(msg, sizeof(msg), fmt, v);
#else
    vsprintf(msg, fmt, v);
#endif
    va_end(v);

    if (errno) {
        fprintf(stderr, ": %s\n", strerror(errno_sv));
        char full_msg[2048];
#if STDC_HEADERS
        snprintf(full_msg, sizeof(full_msg), "%s: %s", msg, strerror(errno_sv));
#else
        sprintf(full_msg, "%s: %s", msg, strerror(errno_sv));
#endif
        set_lha_error(full_msg);
    } else {
        fputs("\n", stderr);
        set_lha_error(msg);
    }

    lha_exit(1);
}

void
cleanup()
{
    if (g_infp) {
        fclose(g_infp);
        g_infp = NULL;
    }
    if (g_outfp) {
        fclose(g_outfp);
        g_outfp = NULL;
    }

    if (temporary_fd != -1) {
        close(temporary_fd);
        temporary_fd = -1;
        unlink(temporary_name);
    }

    if (recover_archive_when_interrupt) {
        rename(backup_archive_name, archive_name);
        recover_archive_when_interrupt = FALSE;
    }
    if (remove_extracting_file_when_interrupt) {
        message("Removing: %s", writing_filename);
        unlink(writing_filename);
        remove_extracting_file_when_interrupt = FALSE;
    }
}

extern void lha_exit_handler(int status);

#undef exit
void
lha_exit(int status)
{
    cleanup();
    lha_exit_handler(status);
    exit(status); /* ハンドラがジャンプしない場合のフォールバック */
}

#ifdef LHA_LIBRARY
/* GUI環境(DLL)用かつ標準出力キャプチャ用の出力関数の定義 */
#undef printf
#undef fprintf
#undef vfprintf
#undef putchar
#undef putc
#undef fputc
#undef puts
#undef fputs
#undef fflush

/* キャプチャ用のグローバル変数実体 (コンパイラ最適化防止のため volatile を付与) */
volatile char* g_capture_buffer = NULL;
volatile size_t g_capture_buffer_size = 0;
volatile size_t g_capture_buffer_written = 0;

/* バッファへの安全な書き込み処理 */
static void capture_write(const char* str, size_t len) {
    if (!g_capture_buffer || g_capture_buffer_size == 0 || !str || len == 0) return;
    if (g_capture_buffer_written >= g_capture_buffer_size - 1) return;
    
    size_t available = g_capture_buffer_size - 1 - g_capture_buffer_written;
    size_t to_write = (len < available) ? len : available;
    
    // volatile 警告を避けるため、一時ポインタでコピー
    char* dest = (char*)g_capture_buffer + g_capture_buffer_written;
    memcpy(dest, str, to_write);
    g_capture_buffer_written += to_write;
    g_capture_buffer[g_capture_buffer_written] = '\0';
}

int lha_printf(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        capture_write(buf, len);
    }
    return len;
}

int lha_putchar(int c) {
    char buf[2] = { (char)c, '\0' };
    capture_write(buf, 1);
    return c;
}

int lha_puts(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    capture_write(s, len);
    capture_write("\n", 1);
    return 0;
}

int lha_fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (f == stdout || f == stderr) {
        char buf[2048];
        int len = vsnprintf(buf, sizeof(buf), fmt, ap);
        if (len > 0) {
            capture_write(buf, len);
        }
        va_end(ap);
        return len;
    }
    int ret = vfprintf(f, fmt, ap);
    va_end(ap);
    return ret;
}

int lha_vfprintf(FILE *f, const char *fmt, va_list ap) {
    if (f == stdout || f == stderr) {
        char buf[2048];
        int len = vsnprintf(buf, sizeof(buf), fmt, ap);
        if (len > 0) {
            capture_write(buf, len);
        }
        return len;
    }
    return vfprintf(f, fmt, ap);
}

int lha_fputc(int c, FILE *f) {
    if (f == stdout || f == stderr) {
        char buf[2] = { (char)c, '\0' };
        capture_write(buf, 1);
        return c;
    }
    return fputc(c, f);
}

int lha_putc(int c, FILE *f) {
    if (f == stdout || f == stderr) {
        char buf[2] = { (char)c, '\0' };
        capture_write(buf, 1);
        return c;
    }
    return fputc(c, f);
}

int lha_fputs(const char *s, FILE *f) {
    if (f == stdout || f == stderr) {
        if (s) {
            size_t len = strlen(s);
            capture_write(s, len);
        }
        return 0;
    }
    return fputs(s, f);
}

int lha_fflush(FILE *f) {
    if (f == stdout || f == stderr || f == NULL) return 0;
    return fflush(f);
}
#endif
