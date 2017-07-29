# x86-64 Compiler Performance

## Hello world

`hello_world.c`:

    #include <stdio.h>

    int main()
    {
    	printf("Hello world\n");
    	return 0;
    }

Compile with `gcc -O2 hello_world.c -o hello_world`.

Running `perf stat -r 10 ./hello_world`:

    Performance counter stats for './hello_world' (10 runs):
    
       0.709710      task-clock (msec)      #    0.557 CPUs utilized
              0      context-switches       #    0.000 K/sec
              0      cpu-migrations         #    0.000 K/sec
             43      page-faults            #    0.067 M/sec
        560,399      cycles                 #    0.877 GHz
        437,301      instructions           #    0.87  insns per cycle
         86,975      branches               #  136.144 M/sec
          2,872      branch-misses          #    3.41% of all branches
    
    0.001274521 seconds time elapsed        ( +-  3.68% )

Running `perf stat -r 10 ./dirt-test`:

    Performance counter stats for './dirt-test' (10 runs):
    
       0.124050      task-clock (msec)      #    0.181 CPUs utilized
              0      context-switches       #    0.000 K/sec
              0      cpu-migrations         #    0.000 K/sec
              2      page-faults            #    0.017 M/sec
         93,843      cycles                 #    0.782 GHz
         51,886      instructions           #    0.57  insns per cycle
          8,704      branches               #   72.546 M/sec
            819      branch-misses          #    9.27% of all branches
    
    0.000683937 seconds time elapsed        ( +-  2.83% )

This results in a 1.86 speedup. Much lower CPU utilization and a factor of 6 less cycles.

Idea: benchmark programs with respect to system calls.
