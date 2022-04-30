/* vms configuration - it was easier to separate it from configur.h */
#if !defined(_VMS_H_INCLUDED)
# define _VMS_H_INCLUDED

# include <unixlib.h>
# define fork() vfork()      /* necessary */
# include <processes.h>      /* should pull in vfork() */
# if defined(__DECC)
#   define HAVE_DLFCN_H
#   define HAVE_FCNTL_H
#   define HAVE_GRP_H
#   define HAVE_INTTYPES_H
#   define HAVE_LIMITS_H
#   define HAVE_MALLOC_H
#   define HAVE_MATH_H
#   define HAVE_MEMORY_H
#   define HAVE_NETDB_H
#   define HAVE_POLL_H
#   define HAVE_PWD_H
#   define HAVE_SETJMP_H
#   define HAVE_SIGNAL_H
#   define HAVE_STDINT_H
#   define HAVE_STDIO_H
#   define HAVE_STDLIB_H
#   define HAVE_STRING_H
#   define HAVE_UNISTD_H
#   define HAVE_ARPA_INET_H
#   define HAVE_NETINET_IN_H
#   define HAVE_SYS_IOCTL_H
#   define HAVE_SYS_MMAN_H
#   define HAVE_SYS_RESOURCE_H
#   define HAVE_SYS_SOCKET_H
#   define HAVE_SYS_STATVFS_H
#   define HAVE_SYS_TIME_H
#   define HAVE_SYS_UTSNAME_H
#   define HAVE_SYS_WAIT_H
#   if __CRTL_VER >= 70000000   /* OpenVMS V7.0 and higher */
#     include <fcntl.h>      /* necessary for posix_do_command */
#     include <sys/utsname.h>
#     define STDC_HEADERS
#     define HAVE_FTRUNCATE
#     define HAVE_ICONV
#     define HAVE_ICONV_H
#     define HAVE_MKSTEMP
#     define HAVE_MMAP
#     define HAVE_PUTENV
#     define HAVE_RANDOM
#     define HAVE_SETENV
#     define HAVE_SIGACTION
#     define HAVE_SYSCONF
#     define HAVE_USLEEP
#     if !defined _VMS_V6_SOURCE
#       define HAVE_GETTIMEOFDAY
#       define HAVE_GMTIME_R
#       define HAVE_LOCALTIME_R
#     endif
#     if !defined(__VAX)        /* 64-bit systems only */
#       define HAVE_ATOLL
#       define HAVE_GETGRGID_R_RETURNS_INT_5_PARAMS
#       define HAVE_GETHOSTBYNAME_R_RETURNS_INT_6_PARAMS
#       define HAVE_GETPWUID_R_RETURNS_INT
#       define HAVE_STRERROR_R
#     endif
#   else
#     include "utsname.h"    /* for unxfuncs */
#   endif
#   if __CRTL_VER >= 70301000   /* OpenVMS V7.3-1 and higher */
#       define HAVE_FSEEKO
#       define HAVE_FTELLO
#   endif
#   if __CRTL_VER >= 70302000   /* OpenVMS V7.3-2 and higher */
#       define HAVE_POLL
#   endif
#   if __CRTL_VER >= 80300000   /* OpenVMS V8.3 and higher */
#       define HAVE_CRYPT
#       define HAVE_LSTAT
#       define HAVE_READLINK
#       define HAVE_REALPATH    /* use the UNIX version of my_fullpath() */
#   endif
#   define HAVE_DIV
#   define HAVE_FTIME
#   define HAVE_MBSTOWCS
#   define HAVE_MEMCPY
#   define HAVE_MEMMOVE
#   define HAVE_RAISE
#   define HAVE_SOCKLEN_T
#   define HAVE_STRERROR
#   define HAVE_VSPRINTF
#   define TIME_WITH_SYS_TIME
#   define _XOPEN_SOURCE_EXTENDED
# endif
# ifndef _MAX_PATH
#   define _MAX_PATH PATH_MAX
# endif
# ifdef DYNAMIC
#   define DYNAMIC_VMS
# endif

#endif
