#ifndef MANAGESIEVE_PROXY_H
#define MANAGESIEVE_PROXY_H

void managesieve_proxy_reset(struct client *client);
int managesieve_proxy_parse_line(struct client *client, const char *line);

void managesieve_proxy_failed(struct client *client,
			      enum login_proxy_failure_type type,
			      const char *reason, bool reconnecting);
const char *managesieve_proxy_get_state(struct client *client);

#endif
