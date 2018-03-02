#ifndef CLIENT_AUTHENTICATE_H
#define CLIENT_AUTHENTICATE_H

struct managesieve_arg;

const char *client_authenticate_get_capabilities
	(struct client *client);

void managesieve_client_auth_result
	(struct client *client, enum client_auth_result result,
		const struct client_auth_reply *reply, const char *text);

void managesieve_client_auth_send_challenge
	(struct client *client, const char *data);
void managesieve_client_auth_parse_response
	(struct client *client);

int cmd_authenticate
	(struct managesieve_client *client, const struct managesieve_arg *args);

#endif
