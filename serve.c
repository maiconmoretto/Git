#include "cache.h"
#include "repository.h"
#include "config.h"
#include "pkt-line.h"
#include "version.h"
#include "argv-array.h"
#include "ls-refs.h"
#include "serve.h"
#include "upload-pack.h"

static int always_advertise(struct repository *r,
			    struct strbuf *value)
{
	return 1;
}

static int agent_advertise(struct repository *r,
			   struct strbuf *value)
{
	if (value)
		strbuf_addstr(value, git_user_agent_sanitized());
	return 1;
}

struct protocol_capability {
	/*
	 * The name of the capability.  The server uses this name when
	 * advertising this capability, and the client uses this name to
	 * specify this capability.
	 */
	const char *name;

	/*
	 * Function queried to see if a capability should be advertised.
	 * Optionally a value can be specified by adding it to 'value'.
	 * If a value is added to 'value', the server will advertise this
	 * capability as "<name>=<value>" instead of "<name>".
	 */
	int (*advertise)(struct repository *r, struct strbuf *value);

	/*
	 * Function called when a client requests the capability as a command.
	 * The command request will be provided to the function via 'keys', the
	 * capabilities requested, and 'args', the command specific parameters.
	 *
	 * This field should be NULL for capabilities which are not commands.
	 */
	int (*command)(struct repository *r,
		       struct argv_array *keys,
		       struct argv_array *args);
};

static struct protocol_capability capabilities[] = {
	{ "agent", agent_advertise, NULL },
	{ "ls-refs", always_advertise, ls_refs },
	{ "fetch", upload_pack_advertise, upload_pack_v2 },
};

static void advertise_capabilities(void)
{
	struct strbuf capability = STRBUF_INIT;
	struct strbuf value = STRBUF_INIT;
	int i;

	for (i = 0; i < ARRAY_SIZE(capabilities); i++) {
		struct protocol_capability *c = &capabilities[i];

		if (c->advertise(the_repository, &value)) {
			strbuf_addstr(&capability, c->name);

			if (value.len) {
				strbuf_addch(&capability, '=');
				strbuf_addbuf(&capability, &value);
			}

			strbuf_addch(&capability, '\n');
			packet_write(1, capability.buf, capability.len);
		}

		strbuf_reset(&capability);
		strbuf_reset(&value);
	}

	packet_flush(1);
	strbuf_release(&capability);
	strbuf_release(&value);
}

static struct protocol_capability *get_capability(const char *key)
{
	int i;

	if (!key)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(capabilities); i++) {
		struct protocol_capability *c = &capabilities[i];
		const char *out;
		if (skip_prefix(key, c->name, &out) && (!*out || *out == '='))
			return c;
	}

	return NULL;
}

static int is_valid_capability(const char *key)
{
	const struct protocol_capability *c = get_capability(key);

	return c && c->advertise(the_repository, NULL);
}

static int is_command(const char *key, struct protocol_capability **command)
{
	const char *out;

	if (skip_prefix(key, "command=", &out)) {
		struct protocol_capability *cmd = get_capability(out);

		if (!cmd || !cmd->advertise(the_repository, NULL) || !cmd->command)
			die("invalid command '%s'", out);
		if (*command)
			die("command already requested");

		*command = cmd;
		return 1;
	}

	return 0;
}

int has_capability(const struct argv_array *keys, const char *capability,
		   const char **value)
{
	int i;
	for (i = 0; i < keys->argc; i++) {
		const char *out;
		if (skip_prefix(keys->argv[i], capability, &out) &&
		    (!*out || *out == '=')) {
			if (value) {
				if (*out == '=')
					out++;
				*value = out;
			}
			return 1;
		}
	}

	return 0;
}

enum request_state {
	PROCESS_REQUEST_KEYS = 0,
	PROCESS_REQUEST_ARGS,
	PROCESS_REQUEST_DONE,
};

static int process_request(void)
{
	enum request_state state = PROCESS_REQUEST_KEYS;
	struct packet_reader reader;
	struct argv_array keys = ARGV_ARRAY_INIT;
	struct argv_array args = ARGV_ARRAY_INIT;
	struct protocol_capability *command = NULL;

	packet_reader_init(&reader, 0, NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF);

	/*
	 * Check to see if the client closed their end before sending another
	 * request.  If so we can terminate the connection.
	 */
	if (packet_reader_peek(&reader) == PACKET_READ_EOF)
		return 1;
	reader.options = PACKET_READ_CHOMP_NEWLINE;

	while (state != PROCESS_REQUEST_DONE) {
		switch (packet_reader_read(&reader)) {
		case PACKET_READ_EOF:
			BUG("Should have already died when seeing EOF");
		case PACKET_READ_NORMAL:
			break;
		case PACKET_READ_FLUSH:
			state = PROCESS_REQUEST_DONE;
			continue;
		case PACKET_READ_DELIM:
			if (state != PROCESS_REQUEST_KEYS)
				die("protocol error");
			state = PROCESS_REQUEST_ARGS;
			/*
			 * maybe include a check to make sure that a
			 * command/capabilities were given.
			 */
			continue;
		}

		switch (state) {
		case PROCESS_REQUEST_KEYS:
			/* collect request; a sequence of keys and values */
			if (is_command(reader.line, &command) ||
			    is_valid_capability(reader.line))
				argv_array_push(&keys, reader.line);
			else
				die("unknown capability '%s'", reader.line);
			break;
		case PROCESS_REQUEST_ARGS:
			/* collect arguments for the requested command */
			argv_array_push(&args, reader.line);
			break;
		case PROCESS_REQUEST_DONE:
			continue;
		}
	}

	/*
	 * If no command and no keys were given then the client wanted to
	 * terminate the connection.
	 */
	if (!keys.argc && !args.argc)
		return 1;

	if (!command)
		die("no command requested");

	command->command(the_repository, &keys, &args);

	argv_array_clear(&keys);
	argv_array_clear(&args);
	return 0;
}

/* Main serve loop for protocol version 2 */
void serve(struct serve_options *options)
{
	if (options->advertise_capabilities || !options->stateless_rpc) {
		/* serve by default supports v2 */
		packet_write_fmt(1, "version 2\n");

		advertise_capabilities();
		/*
		 * If only the list of capabilities was requested exit
		 * immediately after advertising capabilities
		 */
		if (options->advertise_capabilities)
			return;
	}

	/*
	 * If stateless-rpc was requested then exit after
	 * a single request/response exchange
	 */
	if (options->stateless_rpc) {
		process_request();
	} else {
		for (;;)
			if (process_request())
				break;
	}
}
