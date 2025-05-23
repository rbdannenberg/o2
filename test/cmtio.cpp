/*
 **********************************************************************
 *              File io.c
 **********************************************************************
 *
 * Non blocking input routine
 * Works by puttng the terminal in CBREAK mode and using the FIONREAD
 * ioctl call to determine the number of characters in the input queue
 */

#include "testassert.h"
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <sgtty.h>
#include "cmtio.h"
//#include "cext.h"

int     IOinputfd;      /* input file descriptor (usually 0) */

int     IOnochar;       /* Value to be returned by IOgetchar()
                   where there is no input to be had */

static  struct sgttyb IOoldmodes, IOcurrentmodes;
                /* Initial and current tty modes */

/*
 * IOsetup(inputfd)
 *   Args:
 *      inputfd - input file descriptor (should be zero for standard input)
 *   Returns:
 *      0 - if all goes well
 *      -1 - if an ioctl fails (also calls perror)
 *   Side Effects:
 *      Puts the terminal in CBREAK mode - before process termination
 *      IOcleanup() should be called to restore old terminal modes
 *      Catch's interrupts (if they are not already being caught) and
 *      calls IOcleanup before exiting
 *
 */

#define ERROR(s)        return (perror(s), -1)


/*
 * IOcleanup()
 *   Returns:
 *      0 - if all goes well
 *      -1 - if an ioctl fails (also calls perror)
 *   Side Effects:
 *      Restores initial terminal modes
 */

int IOcleanup()
{
    if(ioctl(IOinputfd, TIOCSETP,  &IOoldmodes) < 0)
        ERROR("IOclean");
    return 0;
}


static void IOdiegracefully()
{
    write(2, "\nBye\n", 5);
    IOcleanup();
    exit(2);
}


int IOsetup(inputfd)
{

    void (*interrupt_handler)(int);

    IOinputfd = inputfd;
    IOnochar = NOCHAR;
    if(ioctl(IOinputfd, TIOCGETP,  &IOoldmodes) < 0)
        ERROR("IOsetup");

    IOcurrentmodes = IOoldmodes;
    IOcurrentmodes.sg_flags |= CBREAK;
    IOcurrentmodes.sg_flags &= ~ECHO;
    if(ioctl(IOinputfd, TIOCSETP,  &IOcurrentmodes))
        ERROR("IOsetup-2");

    if( (interrupt_handler = signal(SIGINT, IOdiegracefully)) != 0)
        signal(SIGINT, interrupt_handler);
    return 0;
}


/*
 * IOgetchar()
 *    Returns:
 *      A character off the input queue if there is one,
 *      IOnochar if there is no character waiting to be read,
 *      -1 if an ioctl fails (shouldn't happen if IOsetup went OK)
 */

int IOgetchar()
{
    int n;
    char c;

    if(ioctl(IOinputfd, FIONREAD, &n) < 0)
        ERROR("IOgetchar");
    if(n <= 0)
        return IOnochar;
    switch(read(IOinputfd, &c, 1)) {
    case 1:
        return c;
    case 0:
        return EOF;
    default:
        ERROR("IOgetchar-read");
    }
}

#ifdef IOGETCHAR2
int IOgetchar2()
{
    int nfds, readfds = 1 << IOinputfd;
    char c;
    static struct timeval zero;

    if(IOinputfd < 0 || IOinputfd >= 32) {
        printf("IOgetchar2: bad IOinputfd (%d)%s\n", IOinputfd,
            IOinputfd == -1 ? "Did you call IOsetup(fd)?" : "");
    }
    nfds = select(32, &readfds, 0, 0, &zero);
    if(nfds > 0) {
        switch(read(IOinputfd, &c, 1)) {
        case 0:
            return EOF;
        case 1:
            return c;
        default:
            printf("IOgetchar2: read failed!\n");
            return NOCHAR;
        }
    }
    else if(nfds < 0)
        printf("IOgetchar2: select failed!\n");
    return NOCHAR;
}
#endif

/*
 * IOwaitchar()
 *   Returns:
 *      A character off the input queue.  Waits if necessary.
 */

int IOwaitchar()
{
    char c;
    if (read(IOinputfd, &c, 1) == 1) return c;
    else return EOF;
}

