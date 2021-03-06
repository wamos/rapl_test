/* 
 * msr-poll-gaps.cc
 * Find the average gap between RAPL updates by polling via MSR driver.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <vector>
#include <time.h>

/* The number of gaps to be observed */
#define MAX_GAPS 1000

/* Read the RAPL registers on a sandybridge-ep machine                */
/* Code based on Intel RAPL driver by Zhang Rui <rui.zhang@intel.com> */
/*                                                                    */
/* The /dev/cpu/??/msr driver must be enabled and permissions set     */
/* to allow read access for this to work.                             */
/*                                                                    */
/* Code to properly get this info from Linux through a real device    */
/*   driver and the perf tool should be available as of Linux 3.14    */
/* Compile with:   gcc -O2 -Wall -o rapl-read rapl-read.c -lm         */
/*                                                                    */
/* Vince Weaver -- vincent.weaver @ maine.edu -- 29 November 2013     */
/*                                                                    */
/* Additional contributions by:                                       */
/*   Romain Dolbeau -- romain @ dolbeau.org                           */
/*                                                                    */
/* Latency polling modification by:                                   */
/*   Mikael Hirki <mikael.hirki@aalto.fi>                             */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <sched.h>

#define MSR_RAPL_POWER_UNIT		0x606

/*
 * Platform specific RAPL Domains.
 * Note that PP1 RAPL Domain is supported on 062A only
 * And DRAM RAPL Domain is supported on 062D only
 */
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT	0x610
#define MSR_PKG_ENERGY_STATUS		0x611
#define MSR_PKG_PERF_STATUS		0x613
#define MSR_PKG_POWER_INFO		0x614

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT		0x638
#define MSR_PP0_ENERGY_STATUS		0x639
#define MSR_PP0_POLICY			0x63A
#define MSR_PP0_PERF_STATUS		0x63B

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT		0x640
#define MSR_PP1_ENERGY_STATUS		0x641
#define MSR_PP1_POLICY			0x642

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT		0x618
#define MSR_DRAM_ENERGY_STATUS		0x619
#define MSR_DRAM_PERF_STATUS		0x61B
#define MSR_DRAM_POWER_INFO		0x61C

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET	0
#define POWER_UNIT_MASK		0x0F

#define ENERGY_UNIT_OFFSET	0x08
#define ENERGY_UNIT_MASK	0x1F00

#define TIME_UNIT_OFFSET	0x10
#define TIME_UNIT_MASK		0xF000

static int open_msr(int core) {
	
	char msr_filename[BUFSIZ];
	int fd;
	
	sprintf(msr_filename, "/dev/cpu/%d/msr", core);
	fd = open(msr_filename, O_RDONLY);
	if ( fd < 0 ) {
		if ( errno == ENXIO ) {
			fprintf(stderr, "rdmsr: No CPU %d\n", core);
			exit(2);
		} else if ( errno == EIO ) {
			fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
			exit(3);
		} else {
			perror("rdmsr:open");
			fprintf(stderr,"Trying to open %s\n",msr_filename);
			exit(127);
		}
	}
	
	return fd;
}

static uint64_t read_msr(int fd, int which) {
	uint64_t data;
	
	if (pread(fd, &data, sizeof(data), which) != sizeof(data)) {
		perror("rdmsr:pread");
		exit(127);
	}
	
	return data;
}

static int do_affinity(int core) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	return result >= 0;
}

static void timedelta(struct timespec *result, struct timespec *a, struct timespec *b) {
	time_t sec_delta = a->tv_sec - b->tv_sec;
	long nsec_delta = a->tv_nsec - b->tv_nsec;
	if (nsec_delta < 0) {
		sec_delta--;
		nsec_delta += 1000000000L;
	}
	result->tv_sec = sec_delta;
	result->tv_nsec = nsec_delta;
}

static inline double timespec_to_double(struct timespec *a) {
	return a->tv_sec + a->tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
	int fd = -1;
	int core = 0;
	int c = 0;
	uint64_t result = 0;
	int i = 0, iteration = 0, duration = 1;
	
	opterr=0;
	
	while ((c = getopt (argc, argv, "c:t:")) != -1) {
		switch (c)
		{
			case 'c':
				core = atoi(optarg);
				break;
			case 't':
				duration = atoi(optarg);
				break;
			default:
				exit(-1);
		}
	}
	
	do_affinity(core);
	
	fd=open_msr(core);
	
	// Benchmark MSR register reads
	uint64_t prev_energy = read_msr(fd, MSR_PKG_ENERGY_STATUS);
	struct timespec tstart = {0, 0};
	clock_gettime(CLOCK_REALTIME, &tstart);
	struct timespec tprev = {0, 0};
	struct timespec tnow = {0, 0};
	struct timespec tgap = {0, 0};
	double fgap = 0.0;
	std::vector<double> gaps;
	gaps.reserve(duration * MAX_GAPS);
	double sum_gaps = 0.0;
	double biggest_gap = 0.0;
	int num_gaps = -1;
	for (iteration = 0; num_gaps < duration * MAX_GAPS; iteration++) {
		result = read_msr(fd, MSR_PKG_ENERGY_STATUS);
		if (result != prev_energy) {
			prev_energy = result;
			clock_gettime(CLOCK_REALTIME, &tnow);
			timedelta(&tgap, &tnow, &tprev);
			fgap = timespec_to_double(&tgap);
			num_gaps++;
			// Ignore the first gap
			if (num_gaps > 0) {
				sum_gaps += fgap;
				if (fgap > biggest_gap) {
					biggest_gap = fgap;
				}
				gaps.push_back(fgap);
			}
			memcpy(&tprev, &tnow, sizeof(tprev));
		}
	}
	
	clock_gettime(CLOCK_REALTIME, &tnow);
	struct timespec ttotal = {0, 0};
	timedelta(&ttotal, &tnow, &tstart);
	double time_spent = timespec_to_double(&ttotal);
	printf("%d iterations in %f seconds.\n", iteration, time_spent);
	printf("Polling rate of %f hz.\n", iteration / time_spent);
	printf("MSR polling delay of %f microseconds.\n", time_spent / iteration * 1000000.0);
	printf("Biggest gap was %f millisecond.\n", biggest_gap * 1000.0);
	double avg_gap = sum_gaps / num_gaps;
	printf("Average gap of %f milliseconds.\n", avg_gap * 1000.0);
	
	// Calculate standard deviation
	double sum_squares = 0.0;
	for (i = 0; i < num_gaps; i++) {
		double diff = gaps[i] - avg_gap;
		sum_squares += diff * diff;
	}
	double std_dev = sqrt(sum_squares / num_gaps);
	printf("Standard deviation of the gaps is %f microseconds.\n", std_dev * 1000000.0);
	
#if 0
	// Calculate skewnewss
	double sum_cubes = 0.0;
	for (i = 0; i < num_gaps; i++) {
		double diff = gaps[i] - avg_gap;
		sum_cubes += diff * diff * diff;
	}
	double third_moment = sum_cubes / num_gaps;
	double skewness = third_moment / pow(std_dev, 3.0);
	printf("Skewness is %f microseconds.\n", skewness * 1000000.0);
#endif
	
	// Dump the gaps to a file
	FILE *fp = fopen("/tmp/gaps_msr.txt", "w");
	if (!fp) {
		fprintf(stderr, "Failed to open gaps_msr.txt!\n");
	} else {
		printf("Dumping data to gaps-msr.txt\n");
		for (i = 0; i < num_gaps; i++) {
			fprintf(fp, "%.9f\n", gaps[i]);
		}
		fclose(fp);
	}
	
	// Kill compiler warnings
	(void)argc;
	(void)argv;
	(void)result;
	
	return 0;
}
