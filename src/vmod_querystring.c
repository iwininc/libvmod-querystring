/*
 * libvmod-querystring - querystring manipulation module for Varnish
 *
 * Copyright (C) 2012-2016, Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <vdef.h>
#include <vcl.h>
#include <vrt.h>
#include <vre.h>
#include <vsb.h>
#include <vqueue.h>
#include <cache/cache.h>

#include "vcc_if.h"
#include "vmod_querystring.h"

/* End Of Query Parameter */
#define EOQP(c) (c == '\0' || c == '&')

/***********************************************************************
 * The static functions below contain the actual implementation of the
 * module with the least possible coupling to Varnish. This helps keep a
 * single code base for all Varnish versions.
 */

static const char *
qs_truncate(struct ws *ws, const char *url, const char *qs)
{
	size_t qs_pos;
	char *trunc;

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	AN(url);
	AN(qs);
	assert(url <= qs);

	qs_pos = qs - url;
	trunc = WS_Alloc(ws, qs_pos + 1);

	if (trunc == NULL)
		return (url);

	(void)memcpy(trunc, url, qs_pos);
	trunc[qs_pos] = '\0';

	return (trunc);
}

static int
qs_empty(struct ws *ws, const char *url, const char **res)
{
	const char *qs;

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	AN(res);

	*res = url;

	if (url == NULL)
		return (1);

	qs = strchr(url, '?');
	if (qs == NULL)
		return (1);

	if (qs[1] == '\0') {
		*res = qs_truncate(ws, url, qs);
		return (1);
	}

	*res = qs;
	return (0);
}

static const char *
qs_remove(struct ws *ws, const char *url)
{
	const char *res, *qs;

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);

	res = NULL;
	if (qs_empty(ws, url, &res))
		return (res);

	qs = res;
	return (qs_truncate(ws, url, qs));
}

static int
qs_cmp(const char *a, const char *b)
{

	while (*a == *b) {
		if (EOQP(*a) || EOQP(*b))
			return (0);
		a++;
		b++;
	}
	return (*a - *b);
}

static const char *
qs_sort(struct ws *ws, const char *url, const char *qs)
{
	struct query_param *end, *params;
	int count, head, i, last, prev, sorted, tail;
	char *pos, *res, sep;
	const char *c, *cur;
	unsigned available;
	size_t len;

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	AN(url);
	AN(qs);
	assert(url <= qs);

	/* reserve some memory */
	res = WS_Snapshot(ws);
	available = WS_Reserve(ws, 0);

	if (res == NULL) {
		WS_Release(ws, 0);
		return (url);
	}

	len = strlen(url);
	available -= len + 1;

	params = (void *)PRNDUP(res + len + 1);
	end = params + (available / sizeof *params);

	/* initialize the params array */
	head = 10;

	if (&params[head + 1] >= end)
		head = 0;

	if (&params[head + 1] >= end) {
		WS_Release(ws, 0);
		return (url);
	}

	tail = head;
	last = head;

	/* search and sort params */
	sorted = 1;
	c = qs + 1;
	params[head].val = c;

	for (; *c != '\0' && &params[tail+1] < end; c++) {
		if (*c != '&')
			continue;

		cur = c + 1;
		params[last].len = c - params[last].val;

		if (head > 0 && qs_cmp(params[head].val, cur) > -1) {
			sorted = 0;
			params[--head].val = cur;
			last = head;
			continue;
		}

		if (qs_cmp(params[tail].val, cur) < 1) {
			params[++tail].val = cur;
			last = tail;
			continue;
		}

		sorted = 0;

		i = tail++;
		params[tail] = params[i];

		prev = i - 1;
		while (i > head && qs_cmp(params[prev].val, cur) > -1)
			params[i--] = params[prev--];

		params[i].val = cur;
		last = i;
	}

	if (sorted || &params[tail + 1] >= end || tail - head < 1) {
		WS_Release(ws, 0);
		return (url);
	}

	params[last].len = c - params[last].val;

	/* copy the url parts */
	len = qs - url;
	(void)memcpy(res, url, len);
	pos = res + len;
	count = tail - head;
	sep = '?';

	for (;count >= 0; count--, ++head)
		if (params[head].len > 0) {
			*pos = sep;
			pos++;
			sep = '&';
			(void)memcpy(pos, params[head].val, params[head].len);
			pos += params[head].len;
		}

	*pos = '\0';

	WS_ReleaseP(ws, pos + 1);
	return (res);
}

static void
qs_append(char **begin, const char *end, const char *string, size_t len)
{

	if (*begin + len < end)
		(void)memcpy(*begin, string, len);
	*begin += len;
}

static int __match_proto__(qs_match_f)
qs_match_list(VRT_CTX, const char *s, size_t len, const struct qs_filter *qsf,
    unsigned keep)
{
	const struct qs_list *names;
	struct qs_name *n;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	names = &qsf->names;
	AZ(VSTAILQ_EMPTY(names));

	VSTAILQ_FOREACH(n, names, list)
		if (strlen(n->name) == len && !strncmp(s, n->name, len))
			return (!keep);

	return (keep);
}

static int __match_proto__(qs_match_f)
qs_match_name(VRT_CTX, const char *s, size_t len, const struct qs_filter *qsf,
    unsigned keep)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	return (!strncmp(s, qsf->str, len) ^ keep);
}

static int __match_proto__(qs_match_f)
qs_match_regex(VRT_CTX, const char *s, size_t len, const struct qs_filter *qsf,
    unsigned keep)
{
	int match;
	char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	/* NB: It is not possible to allocate from the workspace because it
	 * will be reserved. Allocating from the stack is not recommended
	 * because of the way Varnish uses the stack, and because we can't
	 * predict the size of a URL. The stack is also a concern because of
	 * the regex match right after the allocation.
	 *
	 * According to PHK in this case we're probably better off using plain
	 * malloc but it may not be a good idea to crash the child process if
	 * the allocation failed so instead we make the assumption that we
	 * couldn't allocate a parameter's LARGE name and deem it malicious,
	 * so we make sure not to keep it.
	 */
	p = strndup(s, len);
	if (p == NULL)
		return (!keep);

	match = VRT_re_match(ctx, p, qsf->regex);
	free(p);
	return (match ^ keep);
}

static int __match_proto__(qs_match_f)
qs_match_glob(VRT_CTX, const char *s, size_t len, const struct qs_filter *qsf,
    unsigned keep)
{
	int match;
	char *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	/* See qs_match_regex for the explanation */
	p = strndup(s, len);
	if (p == NULL)
		return (!keep);

	match = fnmatch(qsf->str, p, 0);
	free(p);

	switch (match) {
	case FNM_NOMATCH:
		return (keep);
	case 0:
		return (!keep);
	}

	/* NB: If the fnmatch failed because of a wrong pattern, the error is
	 * logged but the query-string is kept intact.
	 */
	VSLb(ctx->vsl, SLT_Error, "querystring.globfilter: wrong pattern `%s'",
	    qsf->str);
	return (keep);
}

static void *
qs_re_init(VRT_CTX, const char *regex)
{
	void *re;
	const char *error;
	int error_offset;

	(void)ctx;

	re = VRE_compile(regex, 0, &error, &error_offset);
	VSL(SLT_Error, 0, "Regex error (%s): '%s' pos %d", error,
	    regex, error_offset);
	return (re);
}

static const char*
qs_apply_(VRT_CTX, const char *url, const char *qs, const struct qs_filter *qsf,
    unsigned keep)
{
	const char *cursor, *param_pos, *equal_pos;
	char *begin, *end;
	unsigned available;
	int name_len, match;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	AN(url);
	AN(qs);
	assert(url <= qs);

	available = WS_Reserve(ctx->ws, 0);
	begin = ctx->ws->f;
	end = &begin[available];
	cursor = qs;

	qs_append(&begin, end, url, cursor - url + 1);

	while (*cursor != '\0' && begin < end) {
		param_pos = ++cursor;
		equal_pos = NULL;

		for (; !EOQP(*cursor); cursor++)
			if (equal_pos == NULL && *cursor == '=')
				equal_pos = cursor;

		name_len = (equal_pos ? equal_pos : cursor) - param_pos;
		match = name_len == 0;
		if (!match && qsf->match != NULL)
			match = qsf->match(ctx, param_pos, name_len, qsf, keep);

		if (!match) {
			qs_append(&begin, end, param_pos, cursor - param_pos);
			if (*cursor == '&') {
				*begin = '&';
				begin++;
			}
		}
	}

	if (begin < end) {
		begin -= (begin[-1] == '&');
		begin -= (begin[-1] == '?');
		*begin = '\0';
	}

	begin++;

	if (begin > ctx->ws->e) {
		WS_Release(ctx->ws, 0);
		return (url);
	}

	end = begin;
	begin = ctx->ws->f;
	WS_Release(ctx->ws, end - begin);
	return (begin);
}

static const char *
qs_filter(VRT_CTX, const char *url, const struct qs_filter *qsf, unsigned keep)
{
	const char *qs, *res;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	res = NULL;
	if (qs_empty(ctx->ws, url, &res))
		return (res);

	qs = res;
	return (qs_apply_(ctx, url, qs, qsf, keep));
}

static int
qs_build_list(struct ws *ws, struct qs_list *names, const char *p, va_list ap)
{
	struct qs_name *n;
	const char *q;

	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);
	AN(ws);
	AN(names);
	AN(p);
	AN(VSTAILQ_EMPTY(names));

	while (p != vrt_magic_string_end) {
		q = p;
		p = va_arg(ap, const char*);

		if (q == NULL || *q == '\0')
			continue;

		n = (struct qs_name *)WS_Alloc(ws, sizeof *n);
		if (n == NULL)
			return (-1);
		n->name = TRUST_ME(q);
		VSTAILQ_INSERT_TAIL(names, n, list);
	}

	if (VSTAILQ_EMPTY(names))
		return (-1);

	return (0);
}

/***********************************************************************
 * Below are the functions that will actually be linked by Varnish.
 */

const char *
vmod_clean(VRT_CTX, const char *url)
{
	struct qs_filter qsf;
	const char *res;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\"", url);

	INIT_OBJ(&qsf, QS_FILTER_MAGIC);

	res = qs_filter(ctx, url, &qsf, 0);

	QS_LOG_RETURN(ctx, res);
	return (res);
}

const char *
vmod_remove(VRT_CTX, const char *url)
{
	const char *cleaned_url;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\"", url);

	cleaned_url = qs_remove(ctx->ws, url);

	QS_LOG_RETURN(ctx, cleaned_url);
	return (cleaned_url);
}

const char *
vmod_sort(VRT_CTX, const char *url)
{
	const char *res, *qs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\"", url);

	res = NULL;
	if (qs_empty(ctx->ws, url, &res))
		return (res);

	qs = res;
	res = qs_sort(ctx->ws, url, qs);

	QS_LOG_RETURN(ctx, res);
	return (res);
}

const char *
vmod_filtersep(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

const char *
vmod_filter_(VRT_CTX, const char *url, const char *params, ...)
{
	struct qs_filter qsf;
	const char *res;
	char *snap;
	va_list ap;
	int retval;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\", \"%s\", ...", url, params);

	INIT_OBJ(&qsf, QS_FILTER_MAGIC);
	qsf.match = &qs_match_list;

	VSTAILQ_INIT(&qsf.names);

	snap = WS_Snapshot(ctx->ws);
	AN(snap);

	va_start(ap, params);
	retval = qs_build_list(ctx->ws, &qsf.names, params, ap);
	va_end(ap);

	res = retval == 0 ? qs_filter(ctx, url, &qsf, 0) : url;
	WS_Reset(ctx->ws, snap);

	QS_LOG_RETURN(ctx, res);
	return (res);
}

const char *
vmod_filter_except(VRT_CTX, const char *url, const char *params, ...)
{
	struct qs_filter qsf;
	const char *res;
	char *snap;
	va_list ap;
	int retval;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\", \"%s\", ...", url, params);

	INIT_OBJ(&qsf, QS_FILTER_MAGIC);
	qsf.match = &qs_match_list;

	VSTAILQ_INIT(&qsf.names);

	snap = WS_Snapshot(ctx->ws);
	AN(snap);

	va_start(ap, params);
	retval = qs_build_list(ctx->ws, &qsf.names, params, ap);
	va_end(ap);

	res = retval == 0 ? qs_filter(ctx, url, &qsf, 1) : url;
	WS_Reset(ctx->ws, snap);

	QS_LOG_RETURN(ctx, res);
	return (res);
}

const char *
vmod_regfilter(VRT_CTX, const char *url, const char *regex)
{
	struct qs_filter qsf;
	const char *res;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\", \"%s\"", url, regex);

	INIT_OBJ(&qsf, QS_FILTER_MAGIC);
	qsf.match = &qs_match_regex;
	qsf.regex = qs_re_init(ctx, regex);

	if (qsf.regex == NULL)
		return (url);

	res = qs_filter(ctx, url, &qsf, 0);

	VRT_re_fini(qsf.regex);
	QS_LOG_RETURN(ctx, res);
	return (res);
}

const char *
vmod_regfilter_except(VRT_CTX, const char *url, const char *regex)
{
	struct qs_filter qsf;
	const char *res;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\", \"%s\"", url, regex);

	INIT_OBJ(&qsf, QS_FILTER_MAGIC);
	qsf.match = &qs_match_regex;
	qsf.regex = qs_re_init(ctx, regex);

	if (qsf.regex == NULL)
		return (url);

	res = qs_filter(ctx, url, &qsf, 1);

	VRT_re_fini(qsf.regex);
	QS_LOG_RETURN(ctx, res);
	return (res);
}

const char *
vmod_globfilter(VRT_CTX, const char *url, const char *glob)
{
	struct qs_filter qsf;
	const char *res;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\", \"%s\"", url, glob);

	INIT_OBJ(&qsf, QS_FILTER_MAGIC);
	qsf.match = &qs_match_glob;
	qsf.str = glob;

	res = qs_filter(ctx, url, &qsf, 0);

	QS_LOG_RETURN(ctx, res);
	return (res);
}

const char *
vmod_globfilter_except(VRT_CTX, const char *url, const char *glob)
{
	struct qs_filter qsf;
	const char *res;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	QS_LOG_CALL(ctx, "\"%s\", \"%s\"", url, glob);

	INIT_OBJ(&qsf, QS_FILTER_MAGIC);
	qsf.match = &qs_match_glob;
	qsf.str = glob;

	res = qs_filter(ctx, url, &qsf, 1);

	QS_LOG_RETURN(ctx, res);
	return (res);
}

/* -------------------------------------------------------------------- */

VCL_VOID
vmod_filter__init(VRT_CTX, struct vmod_querystring_filter **objp,
    const char *vcl_name)
{
	struct vmod_querystring_filter *obj;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(objp);
	AZ(*objp);
	AN(vcl_name);

	ALLOC_OBJ(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(obj);

	VTAILQ_INIT(&obj->filters);
	*objp = obj;
}

VCL_VOID
vmod_filter__fini(struct vmod_querystring_filter **objp)
{
	struct vmod_querystring_filter *obj;
	struct qs_filter *qsf, *tmp;

	ASSERT_CLI();
	AN(objp);
	obj = *objp;
	*objp = NULL;
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	// XXX: TAKE_OBJ_NOTNULL(obj, objp, VMOD_QUERYSTRING_FILTER_MAGIC);

	VTAILQ_FOREACH_SAFE(qsf, &obj->filters, list, tmp) {
		CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);
		if (qsf->free != NULL)
			qsf->free(qsf->ptr);
		VTAILQ_REMOVE(&obj->filters, qsf, list);
		FREE_OBJ(qsf);
	}

	FREE_OBJ(obj);
}

VCL_VOID
vmod_filter_add_name(VRT_CTX, struct vmod_querystring_filter *obj,
    VCL_STRING name)
{
	struct qs_filter *qsf;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(name);

	ALLOC_OBJ(qsf, QS_FILTER_MAGIC);
	AN(qsf);

	qsf->str = name;
	qsf->match = qs_match_name;
	VTAILQ_INSERT_TAIL(&obj->filters, qsf, list);
}

VCL_VOID
vmod_filter_add_glob(VRT_CTX, struct vmod_querystring_filter *obj,
    VCL_STRING glob)
{
	struct qs_filter *qsf;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(glob);

	ALLOC_OBJ(qsf, QS_FILTER_MAGIC);
	AN(qsf);

	qsf->str = glob;
	qsf->match = qs_match_glob;
	VTAILQ_INSERT_TAIL(&obj->filters, qsf, list);
}


VCL_VOID
vmod_filter_add_regex(VRT_CTX, struct vmod_querystring_filter *obj,
    VCL_STRING regex)
{
	struct qs_filter *qsf;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(regex);

	ALLOC_OBJ(qsf, QS_FILTER_MAGIC);
	AN(qsf);

	qsf->ptr = qs_re_init(ctx, regex);
	if (qsf->ptr == NULL) {
		AN(ctx->msg);
		VSB_printf(ctx->msg, "vmod-querystring: invalid regex: %s\n",
		    regex);
		VRT_handling(ctx, VCL_RET_FAIL);
		return;
	}

	qsf->match = qs_match_regex;
	qsf->free = VRT_re_fini;
	VTAILQ_INSERT_TAIL(&obj->filters, qsf, list);
}

VCL_STRING
vmod_filter_apply(VRT_CTX, struct vmod_querystring_filter *obj,
    VCL_STRING src, VCL_ENUM mode)
{

	(void)ctx;
	(void)obj;
	(void)src;
	(void)mode;
	INCOMPL();
	return (NULL);
}

VCL_STRING
vmod_filter_extract(VRT_CTX, struct vmod_querystring_filter *obj,
    VCL_STRING src, VCL_ENUM mode)
{
	const char *res, *qs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(mode);

	if (src == NULL)
		return (NULL);

	qs = strchr(src, '?');
	if (qs == NULL || qs[1] == '\0')
		return (NULL);

	res = vmod_filter_apply(ctx, obj, qs, mode);
	AN(res);
	if (*res == '?')
		res++;
	else
		AZ(*res);
	return (res);
}
