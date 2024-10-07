#define MIMPI_ANY_TAG 0
#define MIMPI_GROUP_FD_OFFSET 22
#define MIMPI_PTP_FD_OFFSET 86
#ifndef MIM_ERR_H
#define MIM_ERR_H

#include <stdnoreturn.h>

extern int world_size;

void MIMPIRUN_set_worldsize(int ws);/*{	world_size=ws;}*/

int MIMPI_ptp_fd(int sender,int receiver);/*{
	return ((world_size-1)*sender+receiver+(receiver>sender ? -1 : 0))*2+MIMPI_FD_OFFSET;
}*/

/* Assert that expression evaluates to zero (otherwise use result as error number, as in pthreads). */
#define ASSERT_ZERO(expr)                                                                  \
    do {                                                                                   \
        int errno = (expr);                                                                \
        if (errno != 0)                                                                    \
            syserr(                                                                        \
                "Failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ",      \
                #expr, __func__, __FILE__, __LINE__                                        \
            );                                                                             \
    } while(0)


/*
    Assert that expression doesn't evaluate to -1 (as almost every system function does in case of error).

    Use as a function, with a semicolon, like: ASSERT_SYS_OK(close(fd));
    (This is implemented with a 'do { ... } while(0)' block so that it can be used between if () and else.)
*/
#define ASSERT_SYS_OK(expr)                                                                \
    do {                                                                                   \
        if ((expr) == -1)                                                                  \
            syserr(                                                                        \
                "System command failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ", \
                #expr, __func__, __FILE__, __LINE__                                        \
            );                                                                             \
    } while(0)

/* Prints with information about system error (errno) and quits. */
_Noreturn extern void syserr(const char* fmt, ...);

/* Prints (like printf) and quits. */
_Noreturn extern void fatal(const char* fmt, ...);

#endif
