/*********************************************************************
(C) Copyright (c) 1994-2003 ARC International;  Santa Cruz, CA 95060
*********************************************************************/
#pragma push_align_members(64);

#ifdef __CPLUSPLUS__
extern "C" {
#endif
#if 0


/*
 * Flag values accessible to open(2) and fcntl(2)
 */
    /* OSF/1 uses BSD fcntl. So does IBM WorkPlace */
    /* _IBMESA is AIX, but is implemented on OSF/1 and uses other fcntl.*/
#if _SUN || (_BSD && _I386) || _WPX || _IBMESA
    #define	O_RDONLY	0
    #define	O_WRONLY	1
    #define	O_RDWR		2
    #define	O_NDELAY	FNDELAY	/* Non-blocking I/O */
    #define	O_APPEND	FAPPEND	/* append (writes guaranteed at the end) */
    #define	O_CREAT		FCREAT	/* open with file create */
    #define	O_TRUNC		FTRUNC	/* open with truncation */
    #define	O_EXCL		FEXCL	/* error on create if file exists */
    /* flags for F_GETFL, F_SETFL-- needed by <sys/file.h> */
    #define	FNDELAY		00004	/* non-blocking reads */
    #define	FAPPEND		00010	/* append on each write */
    #define	FASYNC		00100	/* signal pgrp when data ready */
    #define	FCREAT		01000	/* create if nonexistant */
    #define	FTRUNC		02000	/* truncate to zero length */
    #define	FEXCL		04000	/* error if already created */

#elif _LINUX

    /* Use LINUX definitions directly */
    #include "sys/types.h"
    #include "_na.h"
    #include "linux/fcntl.h"
		           /* _ATTFCNTL: Allow ATT fcntl but with Sun's IOBs. */
#elif _AIX || _ATT || _XNX || _ATTFCNTL
    #define	O_RDONLY 0
    #define	O_WRONLY 1
    #define	O_RDWR	 2
    #define	O_NDELAY 04	/* Non-blocking I/O */
    #define	O_APPEND 010	/* append (writes guaranteed at the end) */
    #define O_SYNC	 020	/* synchronous write option */

    /* Flag values accessible only to open(2) */
    #define	O_CREAT	00400	/* open with file create (uses third open arg)*/
    #define	O_TRUNC	01000	/* open with truncation */
    #define	O_EXCL	02000	/* exclusive open */
    #if _AIX
	#define	O_COMMIT 040	/* do periodic commits of changes */
    /* The following flag is passed in by the opendir library(). Any programming
     * which opens a directory will be noticed by the absence of this flag */
	#define	O_OPENDIR 04000	/* open using opendir library routine */
    #endif
#elif _MSDOS || _MSNT || _OS2 || _NTDOS
    #define _O_RDONLY 0
    #define _O_WRONLY 1
    #define _O_RDWR   2
    #define _O_APPEND 8
    #define _O_NOINHERIT 128
    #define _O_CREAT 256
    #define _O_TRUNC 512
    #define _O_EXCL 1024
    #define _O_TEXT   0x4000
    #define _O_BINARY 0x8000
    #define _O_RAW    0x8000

    #if __HIGHC__
	#define O_RDONLY 	_O_RDONLY 
	#define O_WRONLY 	_O_WRONLY 
	#define O_RDWR 	_O_RDWR 
	#define O_APPEND 	_O_APPEND 
	#define O_NOINHERIT _O_NOINHERIT
	#define O_CREAT 	_O_CREAT
	#define O_TRUNC 	_O_TRUNC
	#define O_EXCL 	_O_EXCL 
	#define O_TEXT 	_O_TEXT
	#define O_BINARY 	_O_BINARY
	#define O_RAW 	_O_RAW
    #endif
#elif _AM29K || _HOB || _BEOS
    #define O_RDONLY		00
    #define O_WRONLY		01
    #define O_RDWR		02
    #define O_APPEND		010
    #define O_NDELAY		020
    #define O_TRUNC		02000
    #define O_EXCL		04000
    #define O_CREAT		01000
    #define O_FORM		040000
#elif _UPA || _ATT4 || _SOL
    #define	O_RDONLY	0
    #define	O_WRONLY	1
    #define	O_RDWR		2
    #define	O_NDELAY	0x04	/* non-blocking I/O */
    #define	O_APPEND	0x08 /* append (writes guaranteed at the end) */

    /* Flag values accessible only to open(2). */
    #define	O_CREAT		0x100	/* file create (uses third open arg) */
    #define	O_TRUNC		0x200	/* open with truncation */
    #define	O_EXCL		0x400	/* exclusive open */
    #if _UPA
	#define	O_SYNC		0x8000	/* synchronous write option */
	#define	O_NOCTTY	0x20000	/* don't allocate controlling tty */
    #elif _ATT4 || _SOL
	#define	O_SYNC		0x10    /* synchronous write option */
	#define	O_NOCTTY        0x800
    #endif
    #if _ARM
	#define O_TEXT   0x4000000	/* pass text/bin mode to host */
	#define O_BINARY 0x8000000
    #endif
#elif __OS_OPEN
	    /* access modes                 */
    #define         O_RDONLY        0x00000001
    #define         O_WRONLY        0x00000002
    #define         O_RDWR          0x00000004
	    /* access mode mask             */
    #define         O_ACCMODE       0x00000007      /* mask for access modes */
	    /* oflag values for open        */
    #define         O_CREAT         0x00000010
    #define         O_EXCL          0x00000020
    #define         O_TRUNC         0x00000040
    #define         O_NOCTTY        0x00000080
	    /* status flag values           */
    #define         O_APPEND        0x00000100
    #define         O_NONBLOCK      0x00000200
#elif _BSD || _ISIS
#    error "No allowance for BSD or ISIS fcntl! (__FILE__, __LINE__)"
#elif _NEWS
#    error "No allowance for Sony NEWS fcntl! (__FILE__, __LINE__)"
#elif _NEXT
#    error "No allowance for NEXT fcntl! (__FILE__, __LINE__)"
#else
#error What fcntl do you mean?  __FILE__, line __LINE__
#endif

/*-------------------------------------------------------------------------*/

/*
 * Other fcntl stuff besides open modes
 */
#if _AM29K
    /*
     * _access() file modes.
     */
    #define F_OK            0       /* does file exist */
    #define X_OK            1       /* is it executable by caller */
    #define W_OK            2       /* is it writable by caller */
    #define R_OK            4       /* is it readable by caller */
#elif _SUN || (_BSD && _I386) || _WPX || _IBMESA
    /* fcntl(2) requests */
    #define	F_DUPFD	0	/* Duplicate fildes */
    #define	F_GETFD	1	/* Get fildes flags */
    #define	F_SETFD	2	/* Set fildes flags */
    #define	F_GETFL	3	/* Get file flags */
    #define	F_SETFL	4	/* Set file flags */
    #define	F_GETOWN 5	/* Get owner */
    #define F_SETOWN 6	/* Set owner */
    #define F_GETLK  7      /* Get record-locking information */
    #define F_SETLK  8      /* Set or Clear a record-lock (Non-Blocking) */
    #define F_SETLKW 9      /* Set or Clear a record-lock (Blocking) */

    /* access(2) requests */
    #define	F_OK		0	/* does file exist */
    #define	X_OK		1	/* is it executable by caller */
    #define	W_OK		2	/* writable by caller */
    #define	R_OK		4	/* readable by caller */

    /* System-V record-locking options */
    /* lockf(2) requests */
    #define F_ULOCK 0       /* Unlock a previously locked region */
    #define F_LOCK  1       /* Lock a region for exclusive use */ 
    #define F_TLOCK 2       /* Test and lock a region for exclusive use */
    #define F_TEST  3       /* Test a region for other processes locks */

    /* fcntl(2) flags (l_type field of flock structure) */
    #define F_RDLCK 1       /* read lock */
    #define F_WRLCK 2       /* write lock */
    #define F_UNLCK 3       /* remove lock(s) */


    /* file segment locking set data type - information passed to system by user */
    struct flock {
	    short   l_type;		/* F_RDLCK, F_WRLCK, or F_UNLCK */
	    short   l_whence;	/* flag to choose starting offset */
	    long    l_start;	/* relative offset, in bytes */
	    long    l_len;          /* length, in bytes; 0 means lock to EOF */
	    short   l_pid;		/* returned with F_GETLK */
	    short   l_xxx;		/* reserved for future use */
    };
#elif _AIX || _ATT
    /* fcntl(2) requests */
    #define	F_DUPFD	0	/* Duplicate fildes */
    #define	F_GETFD	1	/* Get fildes flags */
    #define	F_SETFD	2	/* Set fildes flags */
    #define	F_GETFL	3	/* Get file flags */
    #define	F_SETFL	4	/* Set file flags */
    #define 	F_GETLK 5	/* Get first blocking lock */
    #define 	F_SETLK 6	/* Non-blocking lock */
    #define 	F_SETLKW 7	/* Blocking lock */

    /* file segment locking types used for System V fcntl() */
    #define F_RDLCK 1
    #define F_WRLCK 2
    #define F_UNLCK 3

    struct	flock {
		    short	l_type;
		    short	l_whence;
		    long	l_start;
		    long	l_len;
		    short	l_sysid;
		    short	l_pid;
    };
#elif _MSDOS || _MSNT || _OS2 || _HOBBIT || _NTDOS
    #ifndef _UNICHAR_DEFINED
	typedef unsigned short _unichar;
	#define _UNICHAR _unichar
	#define _UNICHAR_DEFINED;
    #endif

    extern int _creat(const char *,int);
    extern int _open(const char *,int,...);

    #include <_na.h>
    #if _NA_NAMES
	_NA(creat)
	_NA(open)
    #elif _HOBBIT && __HIGHC__
	extern int creat(const char *,int );
	extern int open(const char *,int ,...);
    #endif

    /* Unicode functions
     */
    extern int 	 _uopen(const _unichar *, int, ...);
    extern int 	 _ucreat(const _unichar *, int);

    #ifdef __UNICODE__
	#define Ucreat _ucreat
	#define Uopen  _uopen
    #elif _MSDOS || _OS2 || _MSNT || _NTDOS
	#define Ucreat _creat
	#define Uopen  _open
    #elif _HOBBIT && __HIGHC__
	#define Ucreat creat
	#define Uopen  open
    #endif
#elif __OS_OPEN
	    /* fcntl cmd values     */
    #define         F_DUPFD         1
    #define         F_GETFD         2
    #define         F_GETLK         3
    #define         F_SETFD         4
    #define         F_GETFL         5
    #define         F_SETFL         6
    #define         F_SETLK         7
    #define         F_SETLKW        8
	    /* file descriptor flags        */
    #define         FD_CLOEXEC      0x00000001
	    /* record locking values for fcntl      */
    #define         F_RDLCK         1
    #define         F_UNLCK         2
    #define         F_WRLCK         3
#endif
#endif

#ifdef __CPLUSPLUS__
}
#endif

#pragma pop_align_members();
