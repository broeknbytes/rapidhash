#include "rapidhash.h"
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  const char *filename = argv[1];

  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    perror("open");
    return 1;
  }

  struct stat st;
  if (fstat(fd, &st) == -1) {
    perror("fstat");
    close(fd);
    return 1;
  }

  size_t len = (size_t)st.st_size;

  if (len == 0) {
    // Empty file: hash is rapidhash("", 0) = 0x2d358dccaa6c78a5

    printf("%016" PRIx64 "\n", rapidhash("", 0));
    close(fd);
    return 0;
  }

  void *data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }

  uint64_t hash = rapidhash(data, len);

  munmap(data, len);
  close(fd);

  printf("%016" PRIx64 "\n", hash);
  return 0;
}
