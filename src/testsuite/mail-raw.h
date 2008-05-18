#ifndef __MAIL_RAW_H
#define __MAIL_RAW_H

#include "lib.h"
#include "str.h"

struct mail_raw {
    pool_t pool;
    struct mail *mail;

    struct istream *input;
    struct mailbox *box;
    struct mailbox_transaction_context *trans;
};

void mail_raw_init(pool_t namespaces_pool, const char *user);
struct mail_raw *mail_raw_open(string_t *mail_data);
void mail_raw_close(struct mail_raw *mailr);
void mail_raw_deinit(void);

#endif /* __MAIL_RAW_H */
