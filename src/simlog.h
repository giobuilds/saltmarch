#ifndef SIMLOG_H
#define SIMLOG_H

/* ========================================================= */

/* printf-style; a trailing newline is added. */
#if defined(__GNUC__)
void sim_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void sim_log(const char *fmt, ...);
#endif

/* 1 = print (the default), 0 = discard. Returns the previous setting. */
int  sim_log_set_enabled(int enabled);

#endif /* SIMLOG_H */
