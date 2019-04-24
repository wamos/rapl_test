# rapl_test

To run Intel RAPL with MSR (machine specific register), you need to do the following items
1. `sudo modprobe msr` to load the msr driver module
2. `make test_rapl` to compile the RAPL measurement program
3. `sudo ./test_rapl [duration_of_your_measurement]` to run the program for the peirod of [duration_of_your_measurement]
4. The program reads MSR values every second for each CPU package. 
5. The result will appear under `/tmp` like `rapl_[unix_timestamp_in_second].txt` 
6. The output file is formatted as `[timestamp],[package0_power],[packaage1_power] \n`.
