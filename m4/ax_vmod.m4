# libvmod-querystring - querystring manipulation module for Varnish
#
# Copyright (C) 2016, Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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

# AX_VMOD
# -------
AC_DEFUN([AX_VMOD], [

	dnl Check the Varnish installation
	m4_ifndef([VARNISH_VMOD_INCLUDES],
		AC_MSG_ERROR([Varnish development files are needed to build VMODs]))

	dnl Check the VMOD toolchain
	AC_REQUIRE([AC_LANG_C])
	AC_REQUIRE([AC_PROG_CC])
	AC_REQUIRE([AC_PROG_CC_C99])
	AC_REQUIRE([AC_PROG_CC_STDC])
	AC_REQUIRE([AC_PROG_CPP])
	AC_REQUIRE([AC_PROG_CPP_WERROR])
	AC_REQUIRE([AM_PROG_AR])
	AC_REQUIRE([AC_PROG_INSTALL])
	AC_REQUIRE([AC_PROG_LIBTOOL])
	AC_REQUIRE([AC_PROG_MAKE_SET])
	AC_REQUIRE([AC_HEADER_STDC])
	AC_REQUIRE([PKG_PROG_PKG_CONFIG])

	AM_PATH_PYTHON([2.6], [], [
		AC_MSG_ERROR([Python is needed to build VMODs.])
	])

	AS_IF([test -z "$RST2MAN"], [
		AC_MSG_ERROR([rst2man is needed to build VMOD manuals.])
	])

	dnl Read Varnish's pkg-config
	PKG_CHECK_MODULES([varnishapi], [varnishapi])
	PKG_CHECK_VAR([VARNISHAPI_PREFIX], [varnishapi], [prefix])
	PKG_CHECK_VAR([VARNISHAPI_DATAROOTDIR], [varnishapi], [datarootdir])
	PKG_CHECK_VAR([VARNISHAPI_BINDIR], [varnishapi], [bindir])
	PKG_CHECK_VAR([VARNISHAPI_SBINDIR], [varnishapi], [sbindir])

	AC_SUBST([VARNISHAPI_DATAROOTDIR])
	ac_default_prefix=$VARNISHAPI_PREFIX

	dnl Varnish development macros
	VARNISH_VMOD_INCLUDES
	VARNISH_VMOD_DIR
	VARNISH_VMODTOOL

	dnl Define the VMOD directory for libtool
	AC_SUBST([vmoddir], [$VMOD_DIR])

	dnl Define an automake silent execution for vmodtool
	[am__v_VMODTOOL_0='@echo "  VMODTOOL" $<;']
	[am__v_VMODTOOL_1='']
	[am__v_VMODTOOL_='$(am__v_VMODTOOL_$(AM_DEFAULT_VERBOSITY))']
	[AM_V_VMODTOOL='$(am__v_VMODTOOL_$(V))']
	AC_SUBST([am__v_VMODTOOL_0])
	AC_SUBST([am__v_VMODTOOL_1])
	AC_SUBST([am__v_VMODTOOL_])
	AC_SUBST([AM_V_VMODTOOL]) dnl It should eventually encompass $VMODTOOL

	dnl Define VMODs LDFLAGS
	AC_SUBST([VMOD_LDFLAGS],
		"-module -export-dynamic -avoid-version -shared")

	dnl Define the PATH for the test suite
	AC_SUBST([VMOD_TEST_PATH],
		[$VARNISHAPI_SBINDIR:$VARNISHAPI_BINDIR:$PATH])
])

# AX_VMOD_BUILD(NAME)
# -------------------
AC_DEFUN([AX_VMOD_BUILD], [

	AC_REQUIRE([AX_VMOD])

	VMOD_FILE="\$(abs_builddir)/.libs/libvmod_$1.so"
	AC_SUBST(m4_toupper(VMOD_$1_FILE), [$VMOD_FILE])

	VMOD_IMPORT="querystring from \\\"$VMOD_FILE\\\""
	AC_SUBST(m4_toupper(VMOD_$1), [$VMOD_IMPORT])

	VMOD_RULES="

vmod_$1.lo: vcc_$1_if.c vcc_$1_if.h

vcc_$1_if.c vcc_$1_if.h vmod_$1.rst vmod_$1.man.rst: .vcc_$1

.vcc_$1: vmod_$1.vcc
	\$(AM_V_VMODTOOL) $PYTHON $VMODTOOL -o vcc_$1_if \$(srcdir)/vmod_$1.vcc
	@touch .vcc_$1

vmod_$1.3: vmod_$1.man.rst
	$RST2MAN vmod_$1.man.rst vmod_$1.3

clean: clean-vmod-$1

distclean: clean-vmod-$1

clean-vmod-$1:
	rm -f vcc_$1_if.c vcc_$1_if.h
	rm -f vmod_$1.rst vmod_$1.man.rst vmod_$1.3
	rm -f .vcc_$1

install-exec-hook: clean-vmod-$1-la

clean-vmod-$1-la:
	rm -f \$(DESTDIR)\$(vmoddir)/libvmod_$1.la

"

	AC_SUBST(m4_toupper(BUILD_VMOD_$1), [$VMOD_RULES])
	m4_ifdef([_AM_SUBST_NOTMAKE],
		[_AM_SUBST_NOTMAKE(m4_toupper(BUILD_VMOD_$1))])
])
