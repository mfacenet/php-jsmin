dnl $Id$
dnl config.m4 for extension jsmin

PHP_ARG_ENABLE(jsmin, whether to enable jsmin support,
[  --disable-jsmin           Enable jsmin support], yes)

if test "$PHP_JSMIN" != "no"; then
  AC_HEADER_STDC
  PHP_NEW_EXTENSION(jsmin, jsmin.c, $ext_shared)
  PHP_SUBST(JSMIN_SHARED_LIBADD)
fi
