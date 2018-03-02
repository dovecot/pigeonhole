#ifndef MANAGESIEVE_PROXY_H
#define MANAGESIEVE_PROXY_H

void managesieve_proxy_reset(struct client *client);
int managesieve_proxy_parse_line(struct client *client, const char *line);

void managesieve_proxy_error(struct client *client, const char *text);
const char *managesieve_proxy_get_state(struct client *client);

#endif
