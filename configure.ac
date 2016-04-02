# libvmod-querystring - querystring manipulation module for Varnish
#
# Copyright (C) 2012-2016, Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above
#    copyright notice, this list of conditions and the following
#    disclaimer.
# 2. Redistributions in binary form must reproduce the above
#    copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials
#    provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.

AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2012-2016 Dridi Boukelmoune <dridi.boukelmoune@gmail.com>])
AC_INIT([libvmod-querystring],
	[0.4],
	[https://github.com/dridi/libvmod-querystring/issues])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR(src)
AC_CONFIG_HEADER(config.h)

AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE([foreign])
AM_SANITY_CHECK
AM_SILENT_RULES([yes])

# Check for rst utilities
AC_ARG_WITH([rst2man],
AS_HELP_STRING(
	[--with-rst2man=PATH],
	[Location of rst2man (auto)]),
	[RST2MAN="$withval"],
	AC_CHECK_PROGS(RST2MAN, [rst2man rst2man.py], [no]))
if test "$RST2MAN" = "no"; then
	AC_MSG_ERROR(
		[rst2man is needed to build the manual. To proceed without, specify --with-rst2man=true.])
fi

# Test suite
VTC_TESTS="$(cd $srcdir/src && echo tests/*.vtc)"
AC_SUBST(VTC_TESTS)

AX_VMOD_BUILD(querystring)
AX_CODE_COVERAGE

LT_INIT([dlopen])
AC_DISABLE_STATIC

AC_CONFIG_FILES([
	Makefile
	src/Makefile
])

AC_OUTPUT
