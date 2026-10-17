/* CC BY-NC-SA 4.0; see LICENSE.md.
 * Windows syntax check of the actual POSIX adapter, NOT a QNX ABI/runtime test.
 * Only declarations missing on Windows are supplied. No mocked adapter runs.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
typedef int pid_t;
typedef long ssize_t;
#define CLOCK_MONOTONIC 1
#define SIGUSR1 10
#define SIGINT 2
#define SIGKILL 9
#define O_CREAT 1
#define O_RDWR 2
#define O_RDONLY 0
#define F_WRLCK 1
#define F_SETLK 2
#define F_SETFD 3
#define FD_CLOEXEC 1
#define WNOHANG 1
struct flock { short l_type, l_whence; };
int open(const char *, int, ...);
int close(int);
ssize_t read(int, void *, size_t);
int fcntl(int, int, ...);
int chmod(const char *, int);
#define unlink test_unlink
int unlink(const char *);
int kill(pid_t, int);
pid_t getpid(void);
pid_t waitpid(pid_t, int *, int);
int usleep(unsigned);
int clock_gettime(int, struct timespec *);
pid_t daemon_start_mirror(const char *, const char *);
#include "cluster_lifecycle_daemon.h"
int compile_actual_adapter(int argc, char **argv) { return daemon_run(argc,argv); }
