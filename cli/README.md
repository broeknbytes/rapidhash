# Rapidhash CLI

To build:

```sh
make
```

To install to _$HOME/.local/bin_:

```sh
make install
```

To run:

```sh
Usage: rapidhash [-j threads] [-s] [-p] [file...]
       find . -name '*.jpg' | rapidhash [-j threads] [-s] [-p]

Compute the 64-bit rapidhash of one or more files.
Output format: '<hash>\t<filename>' per line (tab-separated).

Files are read from stdin (one path per line) when stdin is a pipe;
otherwise they are taken from the command-line arguments.

Options:
  -j <n>   Use <n> worker threads for hashing.
           0 means use all available processors (same as the default).
           1 runs in sequential mode without threading overhead.
  -s       Print file size in bytes after the filename.
  -p       Show progress (elapsed time, ETA, files processed) on stderr.
  -h       Show this help and exit.

Threading:
  A single file is always hashed on the calling thread.
  With multiple files and no -j flag, all available processors are
  used by default (equivalent to -j 0).
```

## Performance on MacbookPro M1

### Example 1: Small number of files 700 MB

Running on folder with 54 CR2 image files (Canon SX50HS), ranging in size from
13 to 16 MB, the below gives an indication of performance. This is inline with
expectations, and shows that `xargs` version is only slightly slower than the
threaded version.

Running with 8 threads is slightly faster than the default 10, perhaps due
to M1 Pro favoring the 8 performance cores over the 2 efficiency cores. 
Threaded and `xargs` are all on the order of 3 times faster than sequential.

```sh
hyperfine 'rapidhash *.CR2' 'rapidhash -j 8 *.CR2'\
'rapidhash -j 1 *.CR2' 'echo  *.CR2 | xargs -P 8 rapidhash' 
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

### Example 2: Medium number of files 340 GB
This example contains Sony RAW files, that are ~50MB each.

#### Total size of files
Use `find` to list the total size of files

```sh
find "$(PWD)" -type f -iname '*.arw' -print0 | xargs -0 du -b |\
awk '{sum+=$1; n++} END {print sum/1024/1024/1024 " GB  (" n " files)"}'
```
_340.154 GB  (8961 files)_

<details>
<summary><h4>Using fd instead of find</h4></summary>

> Using [fd](https://github.com/sharkdp/fd) can be slightly faster and somewhat nicer to use.
>
> ```sh
> fd . -a -tf -e arw -X du -b |\
> awk '{sum+=$1; n++} END {print sum/1024/1024/1024 " GB  (" n " files)"}'
> ```
> _340.154 GB  (8961 files)_
> 
> ```sh
> files=$(fd . -a -tf -e arw -X du -b | sort -k1 | uniq -w16 -D | cut -f2)
> ```
</details>

#### Measure performance
Lets run and time over a range of threads to see how it performs, we
will not --warmup as it runs around a minute each time.

```sh
hyperfine -r 1 -L num 0,10,20,40,60,80,160,320 \
'fd . -a -tf -e arw -X rapidhash -j {num} 1>/dev/null'
```

Results below show that adding more threads `-j 40` (56 sec), than number of actual
physical cores `-j 0/-j 10` (74 sec), performs the best in this case.

```
Benchmark 1: fd . -a -tf -e arw -X rapidhash -j 0 1>/dev/null
  Time (abs ≡):        74.177 s               [User: 24.477 s, System: 90.178 s]
 
Benchmark 2: fd . -a -tf -e arw -X rapidhash -j 10 1>/dev/null
  Time (abs ≡):        73.968 s               [User: 24.518 s, System: 90.395 s]
 
Benchmark 3: fd . -a -tf -e arw -X rapidhash -j 20 1>/dev/null
  Time (abs ≡):        60.132 s               [User: 25.788 s, System: 108.421 s]
 
Benchmark 4: fd . -a -tf -e arw -X rapidhash -j 40 1>/dev/null
  Time (abs ≡):        56.080 s               [User: 28.041 s, System: 163.064 s]
 
Benchmark 5: fd . -a -tf -e arw -X rapidhash -j 60 1>/dev/null
  Time (abs ≡):        56.110 s               [User: 27.881 s, System: 159.042 s]
 
Benchmark 6: fd . -a -tf -e arw -X rapidhash -j 80 1>/dev/null
  Time (abs ≡):        56.286 s               [User: 27.979 s, System: 157.176 s]
 
Benchmark 7: fd . -a -tf -e arw -X rapidhash -j 160 1>/dev/null
  Time (abs ≡):        60.127 s               [User: 28.437 s, System: 206.600 s]
 
Benchmark 8: fd . -a -tf -e arw -X rapidhash -j 320 1>/dev/null
  Time (abs ≡):        89.913 s               [User: 28.355 s, System: 448.870 s]
 
Summary
  fd . -a -tf -e arw -X rapidhash -j 40 1>/dev/null ran
    1.00 times faster than fd . -a -tf -e arw -X rapidhash -j 60 1>/dev/null
    1.00 times faster than fd . -a -tf -e arw -X rapidhash -j 80 1>/dev/null
    1.07 times faster than fd . -a -tf -e arw -X rapidhash -j 160 1>/dev/null
    1.07 times faster than fd . -a -tf -e arw -X rapidhash -j 20 1>/dev/null
    1.32 times faster than fd . -a -tf -e arw -X rapidhash -j 10 1>/dev/null
    1.32 times faster than fd . -a -tf -e arw -X rapidhash -j 0 1>/dev/null
    1.60 times faster than fd . -a -tf -e arw -X rapidhash -j 320 1>/dev/null
```
<details>
<summary><h4>Finding duplicate files</h4></summary>

We could iterate over all perms for N duplicates, and use `cmp` to
compare files. The number of checks for N, is `N*(N-1)/2`, thus
performance can be expected to decrease proportional to $N^2$. Logic
might be slightly more complex with nested loops and added book
keeping.

Another thing we might do is on first pass find all files with
duplicate sizes, then run these through `rapidhash`. In a lot
of cases it might not make much difference to find files of same size,
especially RAW files which can all have the same file size. 

The simplest generally is just to compute the hash for all files
and then later we can run a simple `awk` script to find duplicates.

```sh
hyperfine --show-output -r 1 -L num 0,10,20,40,80,160,320\
 'fd . -a -tf -e arw -X du -b | sort -k1 | uniq -w16 -D | cut -f2 |\
 rapidhash -j {num} 1>/dev/null'
```

For the RAW files it made a difference of about 1sec faster as
duplicate file sizes dominated, leaving only a very small percentage of
files that were unique. 
</details>


### Example 3: Print wasted space due to duplicates

```sh
fd . -a -tf -e arw -X rapidhash -j 40 -s | awk -F'\t' ' 
{
    hash=$1; file=$2; size=$3
    count[hash]++
    files[hash] = files[hash] (count[hash]==1 ? "" : "\n  ") file
    sizes[hash] = size
}
END {
    total = 0
    num_files=0
    total_dupes=0
    for (hash in count) {
        if (count[hash] > 1) {
            wasted = (count[hash]-1) * sizes[hash]
            total += wasted
            num_files++
            total_dupes+=count[hash]
            printf "dupes: %d  wasted: %.1f MiB  files:\n  %s\n\n", count[hash], wasted/1024/1024, files[hash]
        }
    }
    printf "Total wasted: %.1f MiB\nUnique duplicates: %d\nDuplicate files: %d\nCould remove or symlink %d files\n", total/1024/1024, num_files, total_dupes, total_dupes - num_files
}
'
```
