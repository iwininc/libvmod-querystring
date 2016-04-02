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

AM_CPPFLAGS = $(VMOD_INCLUDES) $(CODE_COVERAGE_CPPFLAGS)
AM_CFLAGS = -Wall -Werror -Wextra $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(VMOD_LDFLAGS) $(CODE_COVERAGE_LDFLAGS)

if CODE_COVERAGE_ENABLED
AM_CPPFLAGS += -DWITHOUT_ASSERTS
endif

# VMOD library

vmod_LTLIBRARIES = libvmod_querystring.la

dist_man_MANS = vmod_querystring.3

libvmod_querystring_la_SOURCES = \
	vmod_querystring.c

nodist_libvmod_querystring_la_SOURCES = \
	vcc_querystring_if.c \
	vcc_querystring_if.h

@BUILD_VMOD_QUERYSTRING@

# Test suite

VMOD_FILE="$(abs_builddir)/.libs/libvmod_querystring.so"

VTC_LOG_COMPILER = \
	env PATH="$(VMOD_TEST_PATH)" \
	varnishtest -v \
	-Dvmod_querystring="querystring from \"$(VMOD_FILE)\""

TEST_EXTENSIONS = .vtc

TESTS = $(VTC_TESTS)

# misc

EXTRA_DIST = \
	vmod_querystring.vcc \
	$(VTC_TESTS)
