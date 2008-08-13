#ifndef __MAIL_RAW_H
#define __MAIL_RAW_H

struct mail_raw {
    pool_t pool;
    struct mail *mail;

    struct istream *input;
    struct mailbox *box;
    struct mailbox_transaction_context *trans;
};

void mail_raw_init(const char *user);
struct mail_raw *mail_raw_open(int fd);
void mail_raw_close(struct mail_raw *mailr);
void mail_raw_deinit(void);

#endif /* __MAIL_RAW_H */
