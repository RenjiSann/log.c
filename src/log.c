/*
 * Copyright (c) 2020 rxi
 * Copyright (c) 2023 Dorian Péron
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "log.h"

#include <string.h>

#ifdef LOG_PID
#include <unistd.h>
#endif // LOG_PID

#define MAX_CALLBACKS 32

#ifndef LOG_LOC_LEN
#define LOG_LOC_LEN 16
#endif // LOG_LOC_LEN

typedef struct {
    log_LogFn fn;
    void *udata;
    log_level level;
} Callback;

static struct {
    void *udata;
    log_LockFn lock;
    log_level level;
    bool quiet;
    Callback callbacks[MAX_CALLBACKS];
} L;

static const char *level_strings[] = { "TRACE", "DEBUG", "INFO",
                                       "WARN",  "ERROR", "FATAL" };

#ifdef LOG_USE_COLOR
static const char *level_colors[] = { "\x1b[94mTRACE", "\x1b[36mDEBUG",
                                      "\x1b[32mINFO",  "\x1b[33mWARN",
                                      "\x1b[31mERROR", "\x1b[35mFATAL" };
#define LEVEL_STR level_colors
#else
#define LEVEL_STR level_strings
#endif // LOG_USE_COLOR

#ifdef LOG_LOC_ALIGN
static inline void file_loc(const char *fname, int line, char *out, int len) {
    int fname_len = strlen(fname);
    char line_nb_buf[32];
    int nb_digit = snprintf(line_nb_buf, 32, "%d", line);
    int total = nb_digit + fname_len + 1; // 1 for ':'

    if (total > len) {
        // Truncate
        int offset = total - len + 3 + 1; // 3 for '...'
        snprintf(out, len, "...%s:%s", fname + offset, line_nb_buf);
    } else {
        // Add padding
        int padding = len - total - 1;
        snprintf(out, len, "%*s%s:%s", padding, "", fname, line_nb_buf);
    }
}

#define LOC_FMT "%s"
#define LOC_ARG , buf_loc
#else
#define LOC_FMT "%s:%d"
#define LOC_ARG , ev->file, ev->line
#endif // LOG_LOC_ALIGN

#ifdef LOG_PID
#define PID_FMT "[%6d] "
#define PID_ARG , getpid()
#else
#define PID_FMT
#define PID_ARG
#endif // LOG_PID

#ifdef LOG_USE_COLOR
#define FMT "%s " PID_FMT "%-10s\x1b[0m \x1b[90m" LOC_FMT ":\x1b[0m "
#else
#define FMT "%s " PID_FMT "%-5s " LOC_FMT ": "
#endif // LOG_USE_COLOR


static void stdout_callback(log_Event *ev) {
    char buf[16];
    buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';

#ifdef LOG_LOC_ALIGN
    char buf_loc[LOG_LOC_LEN];
    file_loc(ev->file, ev->line, buf_loc, LOG_LOC_LEN);
#endif // LOG_LOC_ALIGN

    // Macro black magic to minimize the runtime complexity.
    fprintf(ev->udata, FMT, buf PID_ARG, LEVEL_STR[ev->level] LOC_ARG);

    vfprintf(ev->udata, ev->fmt, ev->ap);
    fprintf(ev->udata, "\n");
    fflush(ev->udata);
}

static void file_callback(log_Event *ev) {
    char buf[64];
    buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
    fprintf(ev->udata, "%s %-5s %s:%d: ", buf, level_strings[ev->level],
            ev->file, ev->line);
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fprintf(ev->udata, "\n");
    fflush(ev->udata);
}

static void lock(void) {
    if (L.lock) {
        L.lock(true, L.udata);
    }
}

static void unlock(void) {
    if (L.lock) {
        L.lock(false, L.udata);
    }
}

const char *log_level_string(log_level level) {
    return level_strings[level];
}

void log_set_lock(log_LockFn fn, void *udata) {
    L.lock = fn;
    L.udata = udata;
}

void log_set_level(log_level level) {
    L.level = level;
}

void log_set_quiet(bool enable) {
    L.quiet = enable;
}

int log_add_callback(log_LogFn fn, void *udata, log_level level) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!L.callbacks[i].fn) {
            L.callbacks[i] = (Callback){ fn, udata, level };
            return 0;
        }
    }
    return -1;
}

int log_add_fp(FILE *fp, log_level level) {
    return log_add_callback(file_callback, fp, level);
}

static void init_event(log_Event *ev, void *udata) {
    if (!ev->time) {
        time_t t = time(NULL);
        ev->time = localtime(&t);
    }
    ev->udata = udata;
}

void log_log(log_level level, const char *file, int line, const char *fmt,
             ...) {
    log_Event ev = {
        .fmt = fmt,
        .file = file,
        .line = line,
        .level = level,
    };

    lock();

    if (!L.quiet && level >= L.level) {
        init_event(&ev, stderr);
        va_start(ev.ap, fmt);
        stdout_callback(&ev);
        va_end(ev.ap);
    }

    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        Callback *cb = &L.callbacks[i];
        if (level >= cb->level) {
            init_event(&ev, cb->udata);
            va_start(ev.ap, fmt);
            cb->fn(&ev);
            va_end(ev.ap);
        }
    }

    unlock();
}
