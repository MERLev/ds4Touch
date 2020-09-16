#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <string.h>

#define LOG_PREFIX " |[ds4touch]| "
#define LOG_PATH "ux0:/log/"
#define LOG_FILE LOG_PATH "ds4touch.txt"

#ifdef LOG_DEBUG
	static int prefixFlag = 1;
	#define LOG(...) \
	do { \
		if (prefixFlag){\
			ksceDebugPrintf(LOG_PREFIX); \
			prefixFlag = 0;\
		}\
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), ##__VA_ARGS__); \
		char *pch, *pchPrev = &buffer[0]; \
		pch = strchr(buffer,'\n'); \
		while (pch != NULL && pch[1] != '\0') { \
			*pch = '\0'; \
			ksceDebugPrintf(pchPrev); \
			ksceDebugPrintf("\n"); \
			ksceDebugPrintf(LOG_PREFIX); \
			pchPrev = pch + 1; \
			pch = strchr(pch + 1,'\n'); \
		} \
		ksceDebugPrintf(pchPrev); \
		if (pch != NULL && pch[1] == '\0') \
			prefixFlag = 1; \
	} while (0)
	#define LOGF LOG
#elif LOG_DISC
	void log_reset(); 
	void log_write(const char *buffer, size_t length);
	void log_flush();
	#define LOG(...) \
	do { \
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), ##__VA_ARGS__); \
		log_write(buffer, strlen(buffer)); \
	} while (0)
	#define LOGF(...) \
	do { \
		char buffer[256]; \
		snprintf(buffer, sizeof(buffer), ##__VA_ARGS__); \
		log_write(buffer, strlen(buffer)); \
		log_flush(); \
	} while (0)
#else
	#define LOG(...) (void)0
	#define LOGF LOG
#endif

#endif
