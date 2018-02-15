#include "cache.h"
#include "repository.h"
#include "refs.h"
#include "remote.h"
#include "argv-array.h"
#include "ls-refs.h"
#include "pkt-line.h"

struct ls_refs_data {
	unsigned peel;
	unsigned symrefs;
	struct argv_array patterns;
};

/*
 * Check if one of the patterns matches the tail part of the ref.
 * If no patterns were provided, all refs match.
 */
static int ref_match(const struct argv_array *patterns, const char *refname)
{
	char *pathbuf;
	int i;

	if (!patterns->argc)
		return 1; /* no restriction */

	pathbuf = xstrfmt("/%s", refname);
	for (i = 0; i < patterns->argc; i++) {
		if (!wildmatch(patterns->argv[i], pathbuf, 0)) {
			free(pathbuf);
			return 1;
		}
	}
	free(pathbuf);
	return 0;
}

static int send_ref(const char *refname, const struct object_id *oid,
		    int flag, void *cb_data)
{
	struct ls_refs_data *data = cb_data;
	const char *refname_nons = strip_namespace(refname);
	struct strbuf refline = STRBUF_INIT;

	if (!ref_match(&data->patterns, refname))
		return 0;

	strbuf_addf(&refline, "%s %s", oid_to_hex(oid), refname_nons);
	if (data->symrefs && flag & REF_ISSYMREF) {
		struct object_id unused;
		const char *symref_target = resolve_ref_unsafe(refname, 0,
							       &unused,
							       &flag);

		if (!symref_target)
			die("'%s' is a symref but it is not?", refname);

		strbuf_addf(&refline, " symref-target:%s", symref_target);
	}

	if (data->peel) {
		struct object_id peeled;
		if (!peel_ref(refname, &peeled))
			strbuf_addf(&refline, " peeled:%s", oid_to_hex(&peeled));
	}

	strbuf_addch(&refline, '\n');
	packet_write(1, refline.buf, refline.len);

	strbuf_release(&refline);
	return 0;
}

int ls_refs(struct repository *r, struct argv_array *keys, struct argv_array *args)
{
	int i;
	struct ls_refs_data data = { 0, 0, ARGV_ARRAY_INIT };

	for (i = 0; i < args->argc; i++) {
		const char *arg = args->argv[i];
		const char *out;

		if (!strcmp("peel", arg))
			data.peel = 1;
		else if (!strcmp("symrefs", arg))
			data.symrefs = 1;
		else if (skip_prefix(arg, "ref-pattern ", &out))
			argv_array_pushf(&data.patterns, "*/%s", out);
	}

	head_ref_namespaced(send_ref, &data);
	for_each_namespaced_ref(send_ref, &data);
	packet_flush(1);
	argv_array_clear(&data.patterns);
	return 0;
}
