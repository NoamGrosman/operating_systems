#ifndef LOGGER_H
#define LOGGER_H
/* Initializes global logger to write into filename (truncate/create).
 * Returns 0 on success, -1 on failure (you decide if you exit on failure).
 */
int  logger_init(const char *filename);

/* Closes the logger (safe to call once at the end). */
void logger_close(void);

/* Thread-safe: writes exactly one formatted line to the log file.
 * - Should append '\n' automatically.
 * - Must be atomic per line: no interleaving between threads.
 */
void log_line(const char *fmt, ...);
#endif //LOGGER_H
