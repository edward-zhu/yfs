#ifndef gettime_h
#define gettime_h

#ifdef __APPLE__

int clock_gettime(clockid_t clk_id, struct timespec *tp);
#endif

#endif
