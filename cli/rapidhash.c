#include "rapidhash.h"
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Compute the rapidhash of a file.
 * Returns 0 on success with hash written to *out,
 * 1 on error (message already printed to stderr).
 * Thread-safe: no shared state.
 */

static int compute_hash(const char *filename, uint64_t *out, off_t *size_out) {
  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    perror(filename);
    return 1;
  }

  struct stat st;
  if (fstat(fd, &st) == -1) {
    perror(filename);
    close(fd);
    return 1;
  }

  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "%s: not a regular file\n", filename);
    close(fd);
    return 1;
  }

  if (st.st_size < 0) {
    fprintf(stderr, "%s: negative file size\n", filename);
    close(fd);
    return 1;
  }

#if SIZE_MAX < INT64_MAX
  if ((uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
    fprintf(stderr, "%s: file too large for this platform\n", filename);
    close(fd);
    return 1;
  }
#endif

  size_t len = (size_t)st.st_size;
  *size_out = st.st_size;

  if (len == 0) {
    *out = rapidhash("", 0);
    close(fd);
    return 0;
  }

  void *data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data == MAP_FAILED) {
    perror(filename);
    close(fd);
    return 1;
  }
  close(fd);

  madvise(data, len, MADV_SEQUENTIAL);
  *out = rapidhash(data, len);
  munmap(data, len);
  return 0;
}

/* Progress reporting */

struct progress_state {
  int nfiles;
  _Atomic int done;
  _Atomic int stop;
  _Atomic uint64_t bytes_done;
  struct timespec start;
};

static void print_progress(const struct progress_state *ps) {
  int done = atomic_load(&ps->done);
  uint64_t bytes = atomic_load(&ps->bytes_done);
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  double elapsed = (now.tv_sec - ps->start.tv_sec) +
                   (now.tv_nsec - ps->start.tv_nsec) * 1e-9;
  int pct = (int)(100.0 * done / ps->nfiles);

  double throughput = (elapsed > 0) ? (double)bytes / elapsed : 0.0;
  char tput_buf[32];
  if (throughput >= (double)(1ULL << 30))
    snprintf(tput_buf, sizeof(tput_buf), "%6.2f GiB/s", throughput / (double)(1ULL << 30));
  else
    snprintf(tput_buf, sizeof(tput_buf), "%6.2f MiB/s", throughput / (double)(1ULL << 20));

  if (done > 0 && done < ps->nfiles) {
    double eta = elapsed / done * (ps->nfiles - done);
    fprintf(stderr, "\r[elapsed %6.1fs] [%d/%d (%3d%%)] [ETA %6.1fs] [%s]   ",
            elapsed, done, ps->nfiles, pct, eta, tput_buf);
  } else {
    fprintf(stderr, "\r[elapsed %6.1fs] [%d/%d (%3d%%)] [ETA       ?] [%s]   ",
            elapsed, done, ps->nfiles, pct, tput_buf);
  }
  fflush(stderr);
}

static void *progress_thread_fn(void *arg) {
  struct progress_state *ps = arg;
  struct timespec interval = {1, 0};
  while (!atomic_load(&ps->stop)) {
    nanosleep(&interval, NULL);
    if (!atomic_load(&ps->stop))
      print_progress(ps);
  }
  return NULL;
}

static int start_progress(struct progress_state *ps, int nfiles,
                          pthread_t *tid) {
  ps->nfiles = nfiles;
  atomic_init(&ps->done, 0);
  atomic_init(&ps->stop, 0);
  atomic_init(&ps->bytes_done, 0);
  clock_gettime(CLOCK_MONOTONIC, &ps->start);
  return pthread_create(tid, NULL, progress_thread_fn, ps);
}

static void stop_progress(pthread_t tid, struct progress_state *ps) {
  atomic_store(&ps->stop, 1);
  pthread_join(tid, NULL);
  fprintf(stderr, "\r\033[K");
  fflush(stderr);
}

static int run_sequential(char **files, int nfiles, int show_size,
                          int show_progress) {
  struct progress_state ps;
  pthread_t ptid;
  if (show_progress) {
    if (start_progress(&ps, nfiles, &ptid) != 0)
      show_progress = 0;
  }

  int exit_code = 0;
  for (int i = 0; i < nfiles; i++) {
    uint64_t hash;
    off_t size = 0;
    int err = compute_hash(files[i], &hash, &size);
    if (err != 0) {
      exit_code = 1;
    } else if (show_size) {
      printf("%016" PRIx64 "\t%s\t%" PRId64 "\n", hash, files[i],
             (int64_t)size);
    } else {
      printf("%016" PRIx64 "\t%s\n", hash, files[i]);
    }
    if (show_progress) {
      if (!err && size > 0)
        atomic_fetch_add(&ps.bytes_done, (uint64_t)size);
      atomic_fetch_add(&ps.done, 1);
    }
  }

  if (show_progress)
    stop_progress(ptid, &ps);

  return exit_code;
}

/* Threaded path section follows.
 *
 */

struct queue {
  char **files;
  int nfiles;
  int show_size;
  _Atomic int next;          // Each thread atomically claims the next index
  _Atomic int exit_code;     // Set to 1 if any file fails
  struct progress_state *ps; // NULL if progress disabled
  pthread_mutex_t out_mu;    // Serialise stdout so lines don't interleave
};

static void *worker(void *arg) {
  struct queue *q = arg;
  for (;;) {
    int i = atomic_fetch_add(&q->next, 1);
    if (i >= q->nfiles)
      break;

    uint64_t hash;
    off_t size = 0;
    int err = compute_hash(q->files[i], &hash, &size);
    if (err != 0) {
      atomic_store(&q->exit_code, 1);
    } else {
      pthread_mutex_lock(&q->out_mu);
      if (q->show_size)
        printf("%016" PRIx64 "\t%s\t%" PRId64 "\n", hash, q->files[i],
               (int64_t)size);
      else
        printf("%016" PRIx64 "\t%s\n", hash, q->files[i]);
      pthread_mutex_unlock(&q->out_mu);
    }

    if (q->ps) {
      if (!err && size > 0)
        atomic_fetch_add(&q->ps->bytes_done, (uint64_t)size);
      atomic_fetch_add(&q->ps->done, 1);
    }
  }
  return NULL;
}

static int run_threaded(char **files, int nfiles, int nthreads, int show_size,
                        int show_progress) {
  if (nthreads > nfiles)
    nthreads = nfiles;

  struct progress_state ps;
  pthread_t ptid;
  int progress_running = 0;
  if (show_progress) {
    if (start_progress(&ps, nfiles, &ptid) == 0)
      progress_running = 1;
  }

  struct queue q;
  q.files = files;
  q.nfiles = nfiles;
  q.show_size = show_size;
  q.ps = progress_running ? &ps : NULL;
  atomic_init(&q.next, 0);
  atomic_init(&q.exit_code, 0);
  pthread_mutex_init(&q.out_mu, NULL);

  pthread_t *threads = malloc((size_t)nthreads * sizeof(pthread_t));
  if (!threads) {
    perror("malloc");
    if (progress_running)
      stop_progress(ptid, &ps);
    pthread_mutex_destroy(&q.out_mu);
    return 1;
  }

  for (int i = 0; i < nthreads; i++) {
    if (pthread_create(&threads[i], NULL, worker, &q) != 0) {
      perror("pthread_create");
      for (int j = 0; j < i; j++)
        pthread_join(threads[j], NULL);
      free(threads);
      if (progress_running)
        stop_progress(ptid, &ps);
      pthread_mutex_destroy(&q.out_mu);
      return 1;
    }
  }

  for (int i = 0; i < nthreads; i++)
    pthread_join(threads[i], NULL);

  if (progress_running)
    stop_progress(ptid, &ps);

  free(threads);
  pthread_mutex_destroy(&q.out_mu);
  return atomic_load(&q.exit_code);
}

static void print_help(const char *prog) {
  printf(
      "Usage: %s [-j threads] [-s] [-p] [file...]\n"
      "       find . -name '*.jpg' | %s [-j threads] [-s] [-p]\n"
      "\n"
      "Compute the 64-bit rapidhash of one or more files.\n"
      "Output format: '<hash>\\t<filename>' per line (tab-separated).\n"
      "\n"
      "Files are read from stdin (one path per line) when stdin is a pipe;\n"
      "otherwise they are taken from the command-line arguments.\n"
      "\n"
      "Options:\n"
      "  -j <n>   Use <n> worker threads for hashing.\n"
      "           0 means use all available processors (same as the default).\n"
      "           1 runs in sequential mode without threading overhead.\n"
      "  -s       Print file size in bytes after the filename.\n"
      "  -p       Show progress (elapsed time, ETA, files processed) on "
      "stderr.\n"
      "  -h       Show this help and exit.\n"
      "\n"
      "Threading:\n"
      "  A single file is always hashed on the calling thread.\n"
      "  With multiple files and no -j flag, all available processors are\n"
      "  used by default (equivalent to -j 0).\n",
      prog, prog);
}

int main(int argc, char **argv) {
  int nthreads = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (nthreads < 1)
    nthreads = 1;
  int show_size = 0;
  int show_progress = 0;

  int opt;
  while ((opt = getopt(argc, argv, "j:shp")) != -1) {
    switch (opt) {
    case 'j': {
      char *end;
      long n = strtol(optarg, &end, 10);
      if (*end != '\0' || n < 0) {
        fprintf(stderr, "rapidhash: -j requires a non-negative integer\n");
        return 1;
      }
      if (n == 0) {
        int cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = cpus > 0 ? cpus : 1;
      } else {
        nthreads = (int)n;
      }
      break;
    }
    case 's':
      show_size = 1;
      break;
    case 'p':
      show_progress = 1;
      break;
    case 'h':
      print_help(argv[0]);
      return 0;
    default:
      print_help(argv[0]);
      return 1;
    }
  }

  char **files;
  int nfiles = 0;
  char **stdin_files = NULL;

  /* When stdin is a pipe/redirect and no file arguments were given,
   * read filenames from it (one per line).  This allows both:
   *   find . -name '*.jpg' | rapidhash          (stdin mode)
   *   find . -print0 | xargs -0 rapidhash       (argv mode, stdin=/dev/null)
   */
  if (!isatty(STDIN_FILENO) && optind >= argc) {
    int cap = 0;
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, stdin)) > 0) {
      /* Strip trailing newline (and any CR before it). */
      while (linelen > 0 &&
             (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        line[--linelen] = '\0';
      if (linelen == 0)
        continue;

      if (nfiles == cap) {
        cap = cap ? cap * 2 : 64;
        char **tmp = realloc(stdin_files, (size_t)cap * sizeof(char *));
        if (!tmp) {
          perror("realloc");
          free(line);
          free(stdin_files);
          return 1;
        }
        stdin_files = tmp;
      }

      stdin_files[nfiles] = strdup(line);
      if (!stdin_files[nfiles]) {
        perror("strdup");
        free(line);
        free(stdin_files);
        return 1;
      }
      nfiles++;
    }
    free(line);
    files = stdin_files;
  } else {
    files = argv + optind;
    nfiles = argc - optind;
  }

  if (nfiles < 1) {
    print_help(argv[0]);
    free(stdin_files);
    return 0;
  }

  /* -j 1 or a single file: skip all threading machinery */
  int rc;
  if (nthreads == 1 || nfiles == 1)
    rc = run_sequential(files, nfiles, show_size, show_progress);
  else
    rc = run_threaded(files, nfiles, nthreads, show_size, show_progress);

  free(stdin_files);
  return rc;
}
