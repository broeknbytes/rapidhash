# Rapidhash CLI

To build:

```sh
make
```

To install to `$HOME/.local/bin`:

```sh
make install
```

To run:

```sh
Usage: rapidhash [-j threads] <file> [file...]

Compute the 64-bit rapidhash of one or more files.
Output format matches sha256sum: '<hash>  <filename>' per line.

Options:
  -j <n>   Use <n> worker threads for hashing.
           0 means use all available processors (same as the default).
           1 runs in sequential mode without threading overhead.
  -h       Show this help and exit.

Threading:
  A single file is always hashed on the calling thread.
  With multiple files and no -j flag, all available processors are
  used by default (equivalent to -j 0).
```

## Performance on MacbookPro M1

Running on folder with 54 CR2 image files (Canon SX50HS), ranging in size from
13 to 16 MB, the below gives an indication of performance. This is inline with
expectations, and shows that `xargs` version is only slightly slower than the
threaded version.

Running with 8 threads is slightly faster than the default 10, perhaps due
to M1 Pro favoring the 8 performance cores over the 2 efficiency cores. 
Threaded and `xargs` are all on the order of 3 times faster than sequential.

```
hyperfine 'rapidhash *.CR2' 'rapidhash -j 8 *.CR2' 'rapidhash -j 1 *.CR2' 'echo  *.CR2  | xargs -P 8 rapidhash' 
```

```
Benchmark 1: rapidhash *.CR2
  Time (mean ± σ):      25.1 ms ±   0.8 ms    [User: 56.7 ms, System: 125.2 ms]
  Range (min … max):    23.0 ms …  27.3 ms    106 runs
 
Benchmark 2: rapidhash -j 8 *.CR2
  Time (mean ± σ):      24.2 ms ±   0.4 ms    [User: 54.6 ms, System: 91.1 ms]
  Range (min … max):    23.2 ms …  25.8 ms    106 runs
 
Benchmark 3: rapidhash -j 1 *.CR2
  Time (mean ± σ):      74.6 ms ±   1.3 ms    [User: 42.0 ms, System: 32.0 ms]
  Range (min … max):    73.4 ms …  78.9 ms    36 runs
 
Benchmark 4: echo  *.CR2  | xargs -P 8 rapidhash
  Time (mean ± σ):      27.6 ms ±   0.8 ms    [User: 57.9 ms, System: 122.3 ms]
  Range (min … max):    25.2 ms …  29.8 ms    99 runs
 
Summary
  rapidhash -j 8 *.CR2 ran
    1.04 ± 0.04 times faster than rapidhash *.CR2
    1.14 ± 0.04 times faster than echo  *.CR2  | xargs -P 8 rapidhash
    3.09 ± 0.08 times faster than rapidhash -j 1 *.CR2
```


