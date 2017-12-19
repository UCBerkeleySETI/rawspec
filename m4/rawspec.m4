# serial 1 rawspec.m4
AC_DEFUN([AX_CHECK_RAWSPEC],
[AC_PREREQ([2.65])dnl
AC_ARG_WITH([rawspec],
            AC_HELP_STRING([--with-rawspec=DIR],
                           [Location of RAWSPEC files (/usr/local)]),
            [RAWSPECDIR="$withval"],
            [RAWSPECDIR=/usr/local])

orig_LDFLAGS="${LDFLAGS}"
LDFLAGS="${orig_LDFLAGS} -L${RAWSPECDIR}/lib"
AC_CHECK_LIB([rawspec], [fb_fd_read_int],
             # Found
             AC_SUBST(RAWSPEC_LIBDIR,${RAWSPECDIR}/lib),
             # Not found there, check RAWSPECDIR
             AS_UNSET(ac_cv_lib_rawspec_fb_fd_read_int)
             LDFLAGS="${orig_LDFLAGS} -L${RAWSPECDIR}"
             AC_CHECK_LIB([rawspec], [fb_fd_read_int],
                          # Found
                          AC_SUBST(RAWSPEC_LIBDIR,${RAWSPECDIR}),
                          # Not found there, error
                          AC_MSG_ERROR([RAWSPEC library not found])))
LDFLAGS="${orig_LDFLAGS}"

AC_CHECK_FILE([${RAWSPECDIR}/include/rawspec_fbutils.h],
              # Found
              AC_SUBST(RAWSPEC_INCDIR,${RAWSPECDIR}/include),
              # Not found there, check RAWSPECDIR
              AC_CHECK_FILE([${RAWSPECDIR}/rawspec_fbutils.h],
                            # Found
                            AC_SUBST(RAWSPEC_INCDIR,${RAWSPECDIR}),
                            # Not found there, error
                            AC_MSG_ERROR([rawspec_fbutils.h header file not found])))

])
