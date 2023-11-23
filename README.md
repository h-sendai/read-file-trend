# read-trend-file

指定されたファイルを読み、N秒に1度、何バイト読んだかを表示する。
N秒のデフォルトは1秒。-I 0.2とすると0.2秒おきに表示する。

```
Usage: ./read-trend-file [-C] [-D] [-c cpu_num] [-d] [-b bufsize] [-i interval] [-s | -r] filename
default: read bufsize 64 kB, interval 1 second
suffix k for kilo, m for mega to speficy bufsize
Options
-C: print if running cpu has changed
-D: don't drop page cache before read
-c cpu_num: set cpu number to be run
-d debug
-b bufsize: read() buffer size (default: 64 kB)
-i sec: print interval (seconds. allow decimal)
-s use POSIX_FADV_SEQUENTIAL
-r use POSIX_FADV_RANDROM
```
