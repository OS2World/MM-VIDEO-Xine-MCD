dnl Configure paths/version for ALSA
dnl
dnl Copyright (C) 2000 Daniel Caujolle-Bert <lobadia@club-internet.fr>
dnl  
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl  
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl  
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
dnl  
dnl  
dnl As a special exception to the GNU General Public License, if you
dnl distribute this file as part of a program that contains a configuration
dnl script generated by Autoconf, you may include it under the same
dnl distribution terms that you use for the rest of that program.
dnl  
dnl USAGE:
dnl AM_PATH_ALSA([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND ]]])
dnl Test for ALSA, then
dnl  AC_SUBST() for ALSA_CFLAGS, ALSA_LIBS and ALSA_STATIC_LIB,
dnl  AC_DEFINE() HAVE_GL,
dnl  $no_alsa is set to "yes" if alsa isn't found.
dnl  $have_alsa05 is set to "yes" if installed alsa version is <= 0.5
dnl  $have_alsa09 is set to "yes" if installed alsa version is >= 0.9
dnl
AC_DEFUN([AM_PATH_ALSA],
 [  
  AC_ARG_ENABLE(alsa, [  --disable-alsa          Do not build ALSA support],,)
  AC_ARG_WITH(alsa-prefix,[  --with-alsa-prefix=pfx  Prefix where alsa is installed (optional)],
            alsa_prefix="$withval", alsa_prefix="")
  AC_ARG_WITH(alsa-exec-prefix,[  --with-alsa-exec-prefix=pfx                                                                             Exec prefix where alsa is installed (optional)],
            alsa_exec_prefix="$withval", alsa_exec_prefix="")
  AC_ARG_ENABLE(alsatest, [  --disable-alsatest      Do not try to compile and run a test alsa program],, enable_alsatest=yes)

  no_alsa="yes"
  have_alsa05="no"
  have_alsa09="no"

if test x"$enable_alsa" != "xno"; then

  if test x$alsa_prefix != x ; then
    ALSA_LIBS="-L$alsa_prefix/lib"
    ALSA_STATIC_LIB="$alsa_prefix"
    ALSA_CFLAGS="-I$alsa_prefix/include"
  fi
  if test x$alsa_exec_prefix != x ; then
    ALSA_LIBS="-L$alsa_exec_prefix/lib"
    ALSA_STATIC_LIB="$alsa_exec_prefix"
    ALSA_CFLAGS="-I$alsa_exec_prefix/include"
  fi

  ALSA_LIBS="-lasound $ALSA_LIBS"
  if test x$ALSA_STATIC_LIB != x; then
    ALSA_STATIC_LIB="$ALSA_STATIC_LIB/lib/libasound.a"
  else
    ALSA_STATIC_LIB="/usr/lib/libasound.a"
  fi
  ALSA_CFLAGS="$ALSA_CFLAGS"

  ac_save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $ALSA_CFLAGS"
  AC_CHECK_HEADER(alsa/asoundlib.h,
         [
           asoundlib_h="alsa/asoundlib.h"
	   AC_DEFINE(HAVE_ALSA_ASOUNDLIB_H, 1, [Define this if your asoundlib.h is installed in alsa/]) 
	 ],[
	   AC_CHECK_HEADER(sys/asoundlib.h,
	     [
	       asoundlib_h="sys/asoundlib.h"
	       AC_DEFINE(HAVE_SYS_ASOUNDLIB_H, 1, [Define this if your asoundlib.h is installed in sys/]) 
	     ])
	 ])

  min_alsa_version=ifelse([$1], ,0.1.1,$1)
  AC_MSG_CHECKING([for ALSA version >= $min_alsa_version])
  if test "x$enable_alsatest" = "xyes" ; then
    no_alsa=""
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $ALSA_CFLAGS"
    LIBS="$ALSA_LIBS $LIBS"
dnl
dnl Now check if the installed ALSA is sufficiently new.
dnl

    AC_LANG_SAVE()
    AC_LANG_C()
    rm -f conf.alsatest
    AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <$asoundlib_h>

int main() {
  int major, minor, micro, extra;
  char *tmp_version;

  system("touch conf.alsatest");

  tmp_version = strdup("$min_alsa_version");
  if(sscanf(tmp_version, "%d.%d.%dpre%d", &major, &minor, &micro, &extra) != 4) {
    if(sscanf(tmp_version, "%d.%d.%dalpha%d", &major, &minor, &micro, &extra) != 4) {
      if(sscanf(tmp_version, "%d.%d.%dbeta%d", &major, &minor, &micro, &extra) != 4) {
        if(sscanf(tmp_version, "%d.%d.%drc%d", &major, &minor, &micro, &extra) != 4) {
          if(sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
            printf("%s, bad version string\n", "$min_alsa_version");
            exit(1);
          } else  /* final */
            extra = 1000000;
        } else  /* rc */
          extra += 100000;
      } else  /* beta */
        extra += 20000;
    } else  /* alpha */
      extra += 10000;
  }

  #if !defined(SND_LIB_MAJOR) && defined(SOUNDLIB_VERSION_MAJOR)
  #define SND_LIB_MAJOR SOUNDLIB_VERSION_MAJOR
  #endif
  #if !defined(SND_LIB_MINOR) && defined(SOUNDLIB_VERSION_MINOR)
  #define SND_LIB_MINOR SOUNDLIB_VERSION_MINOR
  #endif
  #if !defined(SND_LIB_SUBMINOR) && defined(SOUNDLIB_VERSION_SUBMINOR)
  #define SND_LIB_SUBMINOR SOUNDLIB_VERSION_SUBMINOR
  #endif
  #if !defined(SND_LIB_EXTRAVER) && defined(SOUNDLIB_VERSION_EXTRAVER)
  #define SND_LIB_EXTRAVER SOUNDLIB_VERSION_EXTRAVER
  #endif

  if((SND_LIB_MAJOR > major) ||
    ((SND_LIB_MAJOR == major) && (SND_LIB_MINOR > minor)) ||
    ((SND_LIB_MAJOR == major) && (SND_LIB_MINOR == minor) && (SND_LIB_SUBMINOR > micro)) ||
    ((SND_LIB_MAJOR == major) && (SND_LIB_MINOR == minor) && (SND_LIB_SUBMINOR == micro) && (SND_LIB_EXTRAVER >= extra))) {
    return 0;
  }
  else {
    printf("\n*** An old version of ALSA (%d.%d.%d) was found.\n",
           SND_LIB_MAJOR, SND_LIB_MINOR, SND_LIB_SUBMINOR);
    printf("*** You need a version of ALSA newer than %d.%d.%d. The latest version of\n", major, minor, micro);
    printf("*** ALSA is always available from:  http://www.alsa-project.org/\n");
    printf("***\n");
    printf("*** If you have already installed a sufficiently new version\n");
    printf("*** the easiest way to fix this is to remove the old version, and\n");
    printf("*** install a new one.\n");
  }
  return 1;
}
],, no_alsa=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"
  fi

  if test "x$no_alsa" = x ; then
    AC_MSG_RESULT(yes)

dnl
dnl now check for installed version.
dnl

dnl
dnl Check for alsa 0.5.x series
dnl
    AC_MSG_CHECKING([for ALSA <= 0.5 series])
    AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <$asoundlib_h>

int main() {

  #if !defined(SND_LIB_MAJOR) && defined(SOUNDLIB_VERSION_MAJOR)
  #define SND_LIB_MAJOR SOUNDLIB_VERSION_MAJOR
  #endif
  #if !defined(SND_LIB_MINOR) && defined(SOUNDLIB_VERSION_MINOR)
  #define SND_LIB_MINOR SOUNDLIB_VERSION_MINOR
  #endif

  if((SND_LIB_MAJOR == 0) && (SND_LIB_MINOR <= 5))
    return 0;

  return 1;
}
], [ AC_MSG_RESULT(yes)
     have_alsa05=yes ],
     AC_MSG_RESULT(no),[echo $ac_n "cross compiling; assumed OK... $ac_c"])

dnl
dnl Check for alsa >= 0.9.x
dnl
    AC_MSG_CHECKING([for ALSA >= 0.9 series])
    AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <$asoundlib_h>

int main() {

  #if !defined(SND_LIB_MAJOR) && defined(SOUNDLIB_VERSION_MAJOR)
  #define SND_LIB_MAJOR SOUNDLIB_VERSION_MAJOR
  #endif
  #if !defined(SND_LIB_MINOR) && defined(SOUNDLIB_VERSION_MINOR)
  #define SND_LIB_MINOR SOUNDLIB_VERSION_MINOR
  #endif

  if((SND_LIB_MAJOR >= 0) && (SND_LIB_MINOR >= 9))
    return 0;

  return 1;
}
], [ AC_MSG_RESULT(yes)
     have_alsa09=yes ],
     AC_MSG_RESULT(no),[echo $ac_n "cross compiling; assumed OK... $ac_c"])
dnl
dnl Version checking done.
dnl
    ifelse([$2], , :, [$2])
  else
    AC_MSG_RESULT(no)
    if test -f conf.alsatest ; then
     :
    else
      echo "*** Could not run ALSA test program, checking why..."
      CFLAGS="$CFLAGS $ALSA_CFLAGS"
      LIBS="$LIBS $ALSA_LIBS"
      AC_TRY_LINK([
#include <$asoundlib_h>
#include <stdio.h>
], 
      [return ((SND_LIB_MAJOR) || (SND_LIB_MINOR) || (SND_LIB_SUBMINOR));],
      [ echo "*** The test program compiled, but did not run. This usually means"
        echo "*** that the run-time linker is not finding ALSA or finding the wrong"
        echo "*** version of ALSA. If it is not finding ALSA, you'll need to set your"
        echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
        echo "*** to the installed location  Also, make sure you have run ldconfig if that"
        echo "*** is required on your system"
        echo "***"
        echo "*** If you have an old version installed, it is best to remove it, although"
        echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"
        echo "***"],
      [ echo "*** The test program failed to compile or link. See the file config.log for the"
        echo "*** exact error that occured. This usually means ALSA was incorrectly installed."])
      CFLAGS="$ac_save_CFLAGS"
      LIBS="$ac_save_LIBS"
    fi

    ALSA_CFLAGS=""
    ALSA_STATIC_LIB=""
    ALSA_LIBS=""
    ifelse([$3], , :, [$3])
  fi

  CPPFLAGS="$ac_save_CPPFLAGS"
fi

  AC_SUBST(ALSA_CFLAGS)
  AC_SUBST(ALSA_STATIC_LIB)
  AC_SUBST(ALSA_LIBS)
  AC_LANG_RESTORE()
  rm -f conf.alsatest
])

