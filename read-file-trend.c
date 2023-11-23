#define _GNU_SOURCE 1 /* for sched_getcpu(), O_DIRECT */

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h> /* for sched_getcpu() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "my_signal.h"
#include "my_socket.h"
#include "set_timer.h"
#include "set_cpu.h"
#include "get_num.h"
#include "logUtil.h"
#include "drop-page-cache.h"

volatile sig_atomic_t has_alarm = 0;
volatile sig_atomic_t has_int   = 0;

int debug = 0;

int usage(void)
{
    char msg[] = "Usage: ./read-trend-file [-C] [-D] [-c cpu_num] [-d] [-b bufsize] [-i interval] [-s | -r] filename\n"
                 "default: read bufsize 64 kB, interval 1 second\n"
                 "suffix k for kilo, m for mega to speficy bufsize\n"
                 "Options\n"
                 "-C: print if running cpu has changed\n"
                 "-D: don't drop page cache before read\n"
                 "-c cpu_num: set cpu number to be run\n"
                 "-d debug\n"
                 "-b bufsize: read() buffer size (default: 64 kB)\n"
                 "-i sec: print interval (seconds. allow decimal)\n"
                 "-s use POSIX_FADV_SEQUENTIAL\n"
                 "-r use POSIX_FADV_RANDROM\n";

    fprintf(stderr, "%s\n", msg);

    return 0;
}

void sig_alrm(int signo)
{
    has_alarm = 1;
    return;
}

void sig_int(int signo)
{
    has_int = 1;
    return;
}

int print_pid()
{
    pid_t pid = getpid();
    fprintf(stderr, "pid: %d\n", pid);

    return 0;
}

int main(int argc, char *argv[])
{
    int bufsize            = 64*1024; /* 64kB like cat(1) */
    char *buf              = NULL;
    int cpu_num            = -1;
    int run_cpu_prev       = -1;
    int get_cpu_affinity   = 0;
    char *interval_sec_str = "1.0";
    int do_drop_page_cache = 1;
    int open_flags         = O_RDONLY;
    int fadv_sequential    = 0;
    int fadv_random        = 0;

    int c;
    while ( (c = getopt(argc, argv, "c:dDhiI:Pb:Crs")) != -1) {
        switch (c) {
            case 'h':
                usage();
                exit(0);
                break;
            case 'c':
                cpu_num = get_num(optarg);
                break;
            case 'd':
                debug += 1;
                break;
            case 'D':
                do_drop_page_cache = 0;
                break;
            case 'i':
                open_flags |= O_DIRECT;
                break;
            case 'I':
                interval_sec_str = optarg;
                break;
            case 'P':
                print_pid();
                break;
            case 'b':
                bufsize = get_num(optarg);
                break;
            case 'C':
                get_cpu_affinity = 1;
                break;
            case 's':
                fadv_sequential = 1;
                break;
            case 'r':
                fadv_random = 1;
                break;
            default:
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc != 1) {
        usage();
        exit(1);
    }
    char *filename = argv[0];
    if (fadv_sequential == 1 && fadv_random == 1) {
        errx(1, "-s (sequential) and -r (random) options are exclusive");
    }

    if (cpu_num != -1) {
        if (set_cpu(cpu_num) < 0) {
            fprintf(stderr, "set_cpu: %d fail\n", cpu_num);
            exit(1);
        }
    }

    my_signal(SIGALRM, sig_alrm);
    my_signal(SIGINT,  sig_int);
    my_signal(SIGTERM, sig_int);

    buf = aligned_alloc(512, bufsize);
    if (buf == NULL) {
        err(1, "malloc for buf");
    }
    memset(buf, 0, bufsize);

    int fd = open(filename, open_flags);
    if (fd < 0) {
        err(1, "open");
    }
    
    if (do_drop_page_cache) {
        if (drop_page_cache(filename) < 0) {
            errx(1, "drop_page_cache");
        }
    }

    if (fadv_sequential) {
        if (posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL) < 0) {
            err(1, "posix_fadvise(POSIX_FADV_SEQUENTIAL");
        }
    }
    if (fadv_random) {
        if (posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM) < 0) {
            err(1, "posix_fadvise(POSIX_FADV_RANDOM)");
        }
    }
    long interval_read_bytes = 0;
    long total_bytes         = 0;
    long interval_read_count = 0;
    long total_read_count    = 0;

    struct timeval interval;
    conv_str2timeval(interval_sec_str, &interval);
    set_timer(interval.tv_sec, interval.tv_usec, interval.tv_sec, interval.tv_usec);

    struct timeval start, prev;
    gettimeofday(&start, NULL);
    prev = start;

    if (get_cpu_affinity) {
        run_cpu_prev = sched_getcpu();
        printf("0.000000 CPU: %d\n", run_cpu_prev);
    }
    for ( ; ; ) {
        if (has_alarm) {
            has_alarm = 0;
            struct timeval now, elapse;
            gettimeofday(&now, NULL);
            timersub(&now, &start, &elapse);
            timersub(&now, &prev,  &interval);
            double interval_sec = interval.tv_sec + 0.000001*interval.tv_usec;
            double transfer_rate_MB_s = interval_read_bytes / interval_sec / 1024.0 / 1024.0;
            double transfer_rate_Gb_s = MiB2Gb(transfer_rate_MB_s);
            printf("%ld.%06ld %.3f MB/s %.3f Gbps %ld\n",
                elapse.tv_sec, elapse.tv_usec, 
                transfer_rate_MB_s,
                transfer_rate_Gb_s,
                interval_read_count);
            fflush(stdout);
            interval_read_bytes = 0;
            interval_read_count = 0;
            prev = now;
        }
        if (has_int) {
            /* stop alarm */
            my_signal(SIGALRM, SIG_IGN);

            if (close(fd) < 0) {
                err(1, "close");
            }

            struct timeval now, elapse;
            gettimeofday(&now, NULL);
            timersub(&now, &start, &elapse);
            double run_time_sec = elapse.tv_sec + 0.000001*elapse.tv_usec;
            double transfer_rate_MB_s = total_bytes / run_time_sec / 1024.0 / 1024.0;
            double transfer_rate_Gb_s = MiB2Gb(transfer_rate_MB_s);
            fprintf(stderr,
                "# run_sec: %.3f seconds total_bytes: %ld bytes transfer_rate: %.3f MB/s %.3f Gbps total_read_count: %ld\n",
                run_time_sec, total_bytes, transfer_rate_MB_s, transfer_rate_Gb_s, total_read_count);
            exit(0);
        }

        int n = read(fd, buf, bufsize);
        if (n < 0) {
            if (errno == EINTR) {
                if (debug) {
                    fprintf(stderr, "EINTR\n");
                }
                continue;
            }
            else {
                err(1, "read socket");
            }
        }
        if (n == 0) {
            has_int = 1;
            continue;
            // exit(0);
        }

        interval_read_bytes += n;
        total_bytes         += n;
        interval_read_count ++;
        total_read_count ++;

        if (get_cpu_affinity) {
            int run_cpu = sched_getcpu();
            if (run_cpu != run_cpu_prev) {
                struct timeval now, elapse;
                gettimeofday(&now, NULL);
                timersub(&now, &start, &elapse);
                printf("%ld.%06ld CPU: %d total_read_count: %ld\n", elapse.tv_sec, elapse.tv_usec, run_cpu, total_read_count);
                run_cpu_prev = run_cpu;
            }
        }
    }

    return 0;
}
