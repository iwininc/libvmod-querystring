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
#include <string.h>

#include <vcl.h>
#include <vrt.h>
#include <cache/cache.h>

#include "vcc_if.h"

/* End Of Query Parameter */
#define EOQP(c) (c == '\0' || c == '&')

/***********************************************************************
 * Type definitions
 */

struct qs_param {
	const char	*val;
	size_t		len;
};

struct qs_filter;

typedef int qs_match_f(VRT_CTX, const struct qs_filter *, const char *,
    unsigned);

typedef void qs_free_f(void *);

struct qs_filter {
	unsigned		magic;
#define QS_FILTER_MAGIC		0xfc750864
	union {
		void		*ptr;
		const char	*str;
	};
	qs_match_f		*match;
	qs_free_f		*free;
	VTAILQ_ENTRY(qs_filter)	list;
};

struct vmod_querystring_filter {
	unsigned			magic;
#define VMOD_QUERYSTRING_FILTER_MAGIC	0xbe8ecdb4
	VTAILQ_HEAD(, qs_filter)	filters;
	unsigned			sort;
};

/***********************************************************************
 * Static data structures
 */

static struct vmod_querystring_filter qs_clean_filter = {
	.magic = VMOD_QUERYSTRING_FILTER_MAGIC,
	.sort = 0,
};

static struct vmod_querystring_filter qs_sort_filter = {
	.magic = VMOD_QUERYSTRING_FILTER_MAGIC,
	.sort = 1,
};

/***********************************************************************
 * VMOD implementation
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

static int __match_proto__(qs_match_f)
qs_match_name(VRT_CTX, const struct qs_filter *qsf, const char *s,
    unsigned keep)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	(void)keep;
	return (!strcmp(s, qsf->str));
}

static int __match_proto__(qs_match_f)
qs_match_regex(VRT_CTX, const struct qs_filter *qsf, const char *s,
    unsigned keep)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	(void)keep;
	return (VRT_re_match(ctx, s, qsf->ptr));
}

static int __match_proto__(qs_match_f)
qs_match_glob(VRT_CTX, const struct qs_filter *qsf, const char *s,
    unsigned keep)
{
	int match;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);

	match = fnmatch(qsf->str, s, 0);

	if (match == 0)
		return (1);

	if (match == FNM_NOMATCH)
		return (0);

	/* NB: If the fnmatch failed because of a wrong pattern, the error is
	 * logged but the query-string is kept intact.
	 */
	VSLb(ctx->vsl, SLT_Error, "querystring: failed to match glob `%s'",
	    qsf->str);
	return (keep);
}

int
qs_cmp(const void *v1, const void *v2)
{
	const struct qs_param *p1, *p2;
	size_t len;
	int cmp;

	AN(v1);
	AN(v2);
	p1 = v1;
	p2 = v2;

	len = p1->len < p2->len ? p1->len : p2->len;
	cmp = strncmp(p1->val, p2->val, len);

	if (cmp || p1->len == p2->len)
		return (cmp);
	return (p1->len - p2->len);
}

static unsigned
qs_match(VRT_CTX, const struct vmod_querystring_filter *obj,
    const char *name, size_t len, unsigned keep)
{
	struct qs_filter *qsf;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);

	if (len == 0)
		return (0);

	if (VTAILQ_EMPTY(&obj->filters))
		return (1);

	VTAILQ_FOREACH(qsf, &obj->filters, list) {
		CHECK_OBJ_NOTNULL(qsf, QS_FILTER_MAGIC);
		if (qsf->match(ctx, qsf, name, keep))
			return (keep);
	}

	return (!keep);
}

static char *
qs_append(char *cur, size_t cnt, struct qs_param *head, struct qs_param *tail)
{
	char sep;

	sep = '?';
	while (cnt > 0) {
		assert(head < tail);
		AZ(*cur);
		*cur = sep;
		cur++;
		(void)snprintf(cur, head->len + 1, "%s", head->val);
		sep = '&';
		cur += head->len;
		head++;
		cnt--;
	}

	assert(head == tail);
	return cur;
}

static const char *
qs_apply(VRT_CTX, const char *url, const char *qs, unsigned keep,
    const struct vmod_querystring_filter *obj)
{
	struct qs_param *params, *p;
	const char *nm, *eq;
	char *res, *cur, *tmp;
	size_t len, nm_len, cnt;
	ssize_t ws_len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(url);
	AN(qs);
	assert(url <= qs);
	assert(*qs == '?');

	len = strlen(url);
	res = WS_Alloc(ctx->ws, len + 1);
	if (res == NULL)
		return (url);

	params = (void *)WS_Snapshot(ctx->ws);
	ws_len = (ssize_t)WS_Reserve(ctx->ws, 0);

	p = params;

	len = qs - url;
	(void)snprintf(res, len + 1, "%s", url);
	cur = res + len;
	AZ(*cur);

	cnt = 0;
	qs++;
	AN(*qs);

	/* NB: during the matching phase we can use the preallocated space for
	 * the result's query-string in order to copy the current parameter in
	 * the loop. This saves an allocation in matchers that require a null-
	 * terminated string.
	 */
	tmp = cur + 1;

	while (*qs != '\0') {
		nm = qs;
		eq = NULL;

		while (!EOQP(*qs)) {
			if (eq == NULL && *qs == '=')
				eq = qs;
			qs++;
		}

		nm_len = (eq != NULL ? eq : qs) - nm;

		(void)snprintf(tmp, nm_len + 1, "%s", nm);

		if (qs_match(ctx, obj, tmp, nm_len, keep)) {
			AN(nm_len);
			if (ws_len < (ssize_t)sizeof *p) {
				ws_len = -1;
				break;
			}
			p->val = nm;
			p->len = qs - nm;
			p++;
			ws_len -= sizeof *p;
			cnt++;
		}

		if (*qs == '&')
			qs++;
	}

	if (ws_len < 0) {
		WS_Release(ctx->ws, 0);
		WS_Reset(ctx->ws, res);
		return (url);
	}

	if (obj->sort)
		qsort(params, cnt, sizeof *params, qs_cmp);

	if (cnt > 0)
		cur = qs_append(cur, cnt, params, p);

	AZ(*cur);
	cur = (char *)PRNDUP(cur + 1);
	assert((void *)cur <= (void *)params);

	WS_Release(ctx->ws, 0);
	WS_Reset(ctx->ws, cur);

	return (res);
}

/***********************************************************************
 * VMOD interfaces
 */

VCL_STRING
vmod_remove(VRT_CTX, VCL_STRING url)
{
	const char *res, *qs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);

	res = NULL;
	if (qs_empty(ctx->ws, url, &res))
		return (res);

	qs = res;
	return (qs_truncate(ctx->ws, url, qs));
}

VCL_VOID
vmod_filter__init(VRT_CTX, struct vmod_querystring_filter **objp,
    const char *vcl_name, VCL_BOOL sort)
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
	obj->sort = sort;
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
	const char *error;
	int error_offset;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(regex);

	ALLOC_OBJ(qsf, QS_FILTER_MAGIC);
	AN(qsf);

	qsf->ptr = VRE_compile(regex, 0, &error, &error_offset);
	if (qsf->ptr == NULL) {
		AN(ctx->msg);
		FREE_OBJ(qsf);
		VSB_printf(ctx->msg,
		    "vmod-querystring: regex error (%s): '%s' pos %d\n",
		    error, regex, error_offset);
		VRT_handling(ctx, VCL_RET_FAIL);
		return;
	}

	qsf->match = qs_match_regex;
	qsf->free = VRT_re_fini;
	VTAILQ_INSERT_TAIL(&obj->filters, qsf, list);
}

VCL_STRING
vmod_filter_apply(VRT_CTX, struct vmod_querystring_filter *obj,
    VCL_STRING url, VCL_ENUM mode)
{
	const char *tmp, *qs;
	unsigned keep;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(mode);

	tmp = NULL;
	if (qs_empty(ctx->ws, url, &tmp))
		return (tmp);

	qs = tmp;
	keep = 0;

	if (!strcmp(mode, "keep"))
		keep = 1;
	else if (strcmp(mode, "drop"))
		WRONG("Unknown filtering mode");

	return (qs_apply(ctx, url, qs, keep, obj));
}

VCL_STRING
vmod_filter_extract(VRT_CTX, struct vmod_querystring_filter *obj,
    VCL_STRING url, VCL_ENUM mode)
{
	const char *res, *qs;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(obj, VMOD_QUERYSTRING_FILTER_MAGIC);
	AN(mode);

	if (url == NULL)
		return (NULL);

	qs = strchr(url, '?');
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

VCL_STRING
vmod_clean(VRT_CTX, VCL_STRING url)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (vmod_filter_apply(ctx, &qs_clean_filter, url, "keep"));
}

VCL_STRING
vmod_sort(VRT_CTX, VCL_STRING url)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (vmod_filter_apply(ctx, &qs_sort_filter, url, "keep"));
}
