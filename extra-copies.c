/* Dovecot plugin for making additional copies of messages
   in other folders whenever a message is added (appended,
   posted, copied) to a given folder. */
/* Kim Vandry 2014-09 */
/* Licensed under the same terms as Dovecot itself */

#include <stdlib.h>
#include <time.h>
#include "lib.h"
#include "istream.h"
#include "array.h"
#include "module-context.h"
#include "mail-user.h"
#include "mail-storage-private.h"
#include "mailbox-list-private.h"
#include "mail-search-build.h"

#define EXTRA_COPIES_BOX_CONTEXT(obj) MODULE_CONTEXT((obj), extra_copies_box_module)

static MODULE_CONTEXT_DEFINE_INIT(extra_copies_box_module, &mail_storage_module_register);

struct extra_copies_destination {
	const char *mailbox;
	struct extra_copies_destination *next;
};

struct extra_copies_box {
	union mailbox_module_context module_ctx;
	struct extra_copies_destination *dest;
	ARRAY_TYPE(seq_range) new_uids;
};

static void
copy(struct mail *mail, const char *destbox_name, int debug)
{
struct mailbox *destbox;
struct mailbox_transaction_context *trans;
struct mail_save_context *save_ctx;
struct mail_namespace *dest_ns;

	dest_ns = mail_namespace_find(mail->box->list->ns->user->namespaces, destbox_name);
	if (!dest_ns) {
		i_error("extra copies: Namespace not found for mailbox: %s", destbox_name);
		return;
	}
	destbox = mailbox_alloc(dest_ns->list, destbox_name, 0);

	if (mailbox_open(destbox) < 0) {
		i_error("extra copies: cannot open destination mailbox \"%s\"", destbox_name);
	} else {
		trans = mailbox_transaction_begin(
			destbox, MAILBOX_TRANSACTION_FLAG_EXTERNAL
#if DOVECOT_PREREQ(2,3)
			, __func__
#endif
		);
		save_ctx = mailbox_save_alloc(trans);
		mailbox_save_copy_flags(save_ctx, mail);
		if (mailbox_copy(&save_ctx, mail) < 0) {
			i_error("extra copies: cannot copy mail to \"%s\"", destbox_name);
			mailbox_transaction_rollback(&trans);
		} else {
			mailbox_transaction_commit(&trans);
			if (debug) i_debug("extra copies: made an extra copy in %s", destbox_name);
		}
	}
	mailbox_free(&destbox);
	return;
}

static int
extra_copies_transaction_commit(
	struct mailbox_transaction_context *t,
	struct mail_transaction_commit_changes *changes
)
{
struct mailbox *box = t->box;
struct extra_copies_box *this_box = EXTRA_COPIES_BOX_CONTEXT(box);

	if ((this_box->module_ctx.super.transaction_commit(t, changes)) < 0)
		return -1;

	if (this_box && (this_box->dest)) {
		seq_range_array_merge(&(this_box->new_uids), &(changes->saved_uids));
	}

	return 0;
}

static void
extra_copies_close(struct mailbox *box)
{
struct extra_copies_box *this_box = EXTRA_COPIES_BOX_CONTEXT(box);
int debug = box->list->ns->user->mail_debug;
struct mailbox_transaction_context *trans;
struct mail_search_args *search_args;
struct mail_search_context *search_ctx;
struct mail *mail;
struct extra_copies_destination *dest;

	if (array_count(&(this_box->new_uids)) > 0) {
		mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_READ);

		search_args = mail_search_build_init();
		search_args->args = p_new(search_args->pool, struct mail_search_arg, 1);
		search_args->args->type = SEARCH_UIDSET;
		search_args->args->value.seqset = this_box->new_uids;

		trans = mailbox_transaction_begin(
			box, 0
#if DOVECOT_PREREQ(2,3)
			, __func__
#endif
		);
		search_ctx = mailbox_search_init(trans, search_args, NULL, 0, NULL);
		mail_search_args_unref(&search_args);

		while (mailbox_search_next(search_ctx, &mail)) {
			if (debug) i_debug("extra copies: will copy newly saved uid %u", mail->uid);
			for (dest = this_box->dest; dest; dest = dest->next) {
				copy(mail, dest->mailbox, debug);
			}
		}
		mailbox_search_deinit(&search_ctx);
		mailbox_transaction_commit(&trans);
	}
	array_free(&(this_box->new_uids));
	this_box->module_ctx.super.close(box);
}

static void
extra_copies_mailbox_allocated(struct mailbox *box)
{
struct extra_copies_box *this_box;
struct mailbox_vfuncs *v = box->vlast;
int debug = box->list->ns->user->mail_debug;
const char *dir;
char *file;
struct extra_copies_destination *cur;
struct extra_copies_destination *tail = NULL;
int fd;
struct istream *input;
const char *line;

	if (!(box->name)) return;
#if DOVECOT_PREREQ(2,2)
	if (!mailbox_list_is_valid_name(box->list, box->name, &line)) return;
	if (mail_storage_is_mailbox_file(box->list->ns->storage)) {
		if (mailbox_list_get_path(box->list, box->name, MAILBOX_LIST_PATH_TYPE_CONTROL, &dir) != 1) return;
	} else {
		if (mailbox_list_get_path(box->list, box->name, MAILBOX_LIST_PATH_TYPE_MAILBOX, &dir) != 1) return;
	}
#else
	if (!mailbox_list_is_valid_existing_name(box->list, box->name)) return;
	if (mail_storage_is_mailbox_file(box->list->ns->storage)) {
		dir = mailbox_list_get_path(box->list, box->name, MAILBOX_LIST_PATH_TYPE_CONTROL);
	} else {
		dir = mailbox_list_get_path(box->list, box->name, MAILBOX_LIST_PATH_TYPE_MAILBOX);
	}
#endif
	if (!dir) return;
	file = i_strconcat(dir, "/extra-copies", NULL);

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		if ((errno == ENOENT) || (errno == ENOTDIR)) {
			if (debug) i_debug("extra copies: file \"%s\" not found", file);
			i_free(file);
			return;
		}
		i_error("open(%s) failed: %m", file);
		i_free(file);
		return;
	}
	if (debug) i_debug("extra copies: reading file %s", file);
	i_free(file);

	this_box = p_new(box->pool, struct extra_copies_box, 1);
	this_box->dest = NULL;
	i_array_init(&(this_box->new_uids), 128);

	input = i_stream_create_fd(
		fd, 4096
#if !DOVECOT_PREREQ(2,3)
		, FALSE
#endif
	);
	while ((line = i_stream_read_next_line(input)) != NULL) {
		if (line[0]) {
			cur = p_new(box->pool, struct extra_copies_destination, 1);
			cur->next = NULL;
			cur->mailbox = p_strdup(box->pool, line);
			if (tail) {
				tail->next = cur;
			} else {
				this_box->dest = cur;
			}
			tail = cur;
		}
	}

	i_stream_unref(&input);
	close(fd);

	this_box->module_ctx.super = *v;
	box->vlast = &this_box->module_ctx.super;

	v->transaction_commit = extra_copies_transaction_commit;
	v->close = extra_copies_close;

	MODULE_CONTEXT_SET(box, extra_copies_box_module, this_box);
}

static struct mail_storage_hooks extra_copies_mail_storage_hooks = {
	.mailbox_allocated = extra_copies_mailbox_allocated,
};

void
extra_copies_plugin_init(struct module *module)
{
	mail_storage_hooks_add(module, &extra_copies_mail_storage_hooks);
}

void
extra_copies_plugin_deinit(void)
{
	mail_storage_hooks_remove(&extra_copies_mail_storage_hooks);
}
