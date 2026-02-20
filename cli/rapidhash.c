#include "rapidhash.h"
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Compute the rapidhash of a file.
 * Returns 0 on success with hash written to *out,
 * 1 on error (message already printed to stderr).
 * Thread-safe: no shared state.
 */
static int compute_hash(const char *filename, uint64_t *out)
{
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

/* ------------------------------------------------------------------ */
/* Sequential path  (-j 1, or only one file)                          */
/* ------------------------------------------------------------------ */

static int run_sequential(char **files, int nfiles)
{
  int exit_code = 0;
  for (int i = 0; i < nfiles; i++) {
    uint64_t hash;
    if (compute_hash(files[i], &hash) != 0) {
      exit_code = 1;
    } else {
      printf("%016" PRIx64 "  %s\n", hash, files[i]);
    }
  }
  return exit_code;
}

/* ------------------------------------------------------------------ */
/* Threaded path                                                       */
/* ------------------------------------------------------------------ */

struct queue {
  char          **files;
  int             nfiles;
  _Atomic int     next;      /* each thread atomically claims the next index */
  _Atomic int     exit_code; /* set to 1 if any file fails                   */
  pthread_mutex_t out_mu;    /* serialise stdout so lines don't interleave   */
};

static void *worker(void *arg)
{
  struct queue *q = arg;
  for (;;) {
    int i = atomic_fetch_add(&q->next, 1);
    if (i >= q->nfiles)
      break;

    uint64_t hash;
    if (compute_hash(q->files[i], &hash) != 0) {
      atomic_store(&q->exit_code, 1);
      continue;
    }

    pthread_mutex_lock(&q->out_mu);
    printf("%016" PRIx64 "  %s\n", hash, q->files[i]);
    pthread_mutex_unlock(&q->out_mu);
  }
  return NULL;
}

static int run_threaded(char **files, int nfiles, int nthreads)
{
  if (nthreads > nfiles)
    nthreads = nfiles;

  struct queue q;
  q.files  = files;
  q.nfiles = nfiles;
  atomic_init(&q.next, 0);
  atomic_init(&q.exit_code, 0);
  pthread_mutex_init(&q.out_mu, NULL);

  pthread_t *threads = malloc((size_t)nthreads * sizeof(pthread_t));
  if (!threads) {
    perror("malloc");
    pthread_mutex_destroy(&q.out_mu);
    return 1;
  }

  for (int i = 0; i < nthreads; i++) {
    if (pthread_create(&threads[i], NULL, worker, &q) != 0) {
      perror("pthread_create");
      for (int j = 0; j < i; j++)
        pthread_join(threads[j], NULL);
      free(threads);
      pthread_mutex_destroy(&q.out_mu);
      return 1;
    }
  }

  for (int i = 0; i < nthreads; i++)
    pthread_join(threads[i], NULL);

  free(threads);
  pthread_mutex_destroy(&q.out_mu);
  return atomic_load(&q.exit_code);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
  int nthreads = (int)sysconf(_SC_NPROCESSORS_ONLN);

  int opt;
  while ((opt = getopt(argc, argv, "j:")) != -1) {
    switch (opt) {
    case 'j': {
      int n = atoi(optarg);
      if (n < 0) {
        fprintf(stderr, "rapidhash: -j must be >= 0\n");
        return 1;
      }
      nthreads = (n == 0) ? (int)sysconf(_SC_NPROCESSORS_ONLN) : n;
      break;
    }
    default:
      fprintf(stderr, "Usage: %s [-j threads] <file> [file...]\n", argv[0]);
      return 1;
    }
  }

  char **files  = argv + optind;
  int    nfiles = argc - optind;

  if (nfiles < 1) {
    fprintf(stderr, "Usage: %s [-j threads] <file> [file...]\n", argv[0]);
    return 1;
  }

  /* -j 1 or a single file: skip all threading machinery */
  if (nthreads == 1 || nfiles == 1)
    return run_sequential(files, nfiles);

  return run_threaded(files, nfiles, nthreads);
}
