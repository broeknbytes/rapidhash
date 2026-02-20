#include "rapidhash.h"
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Hash a single file and print "<hash>  <filename>" to stdout.
 * Returns 0 on success, 1 on any error (error already printed to stderr).
 */
static int hash_file(const char *filename) {
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
  uint64_t hash;

  if (len == 0) {
    hash = rapidhash("", 0);
    close(fd);
  } else {
    void *data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
      perror(filename);
      close(fd);
      return 1;
    }
    close(fd);

    madvise(data, len, MADV_SEQUENTIAL);

    hash = rapidhash(data, len);
    munmap(data, len);
  }

  printf("%016" PRIx64 "  %s\n", hash, filename);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <file> [file...]\n", argv[0]);
    return 1;
  }

  int exit_code = 0;
  for (int i = 1; i < argc; i++) {
    if (hash_file(argv[i]) != 0)
      exit_code = 1;
  }
  return exit_code;
}
