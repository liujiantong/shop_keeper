/*
POSIX getopt for Windows

AT&T Public License

Code given out at the 1985 UNIFORUM conference in Dallas.  

NOTE: TO use this, please remove UNICODE and _UNICODE in VC and define __WINDOWS__
*/

#ifndef __WINDOWS__
#include <getopt.h>

#else

#ifndef _WINGETOPT_H_
#define _WINGETOPT_H_

#ifdef __cplusplus
extern "C" {
#endif

extern int opterr;
extern int optind;
extern int optopt;
extern char *optarg;
extern int getopt(int argc, char **argv, char *opts);

#ifdef __cplusplus
}
#endif

#endif  /* _WINGETOPT_H_ */

#endif  /* __WINDOWS__ */
