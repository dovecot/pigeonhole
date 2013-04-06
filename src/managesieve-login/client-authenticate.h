/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __CLIENT_AUTHENTICATE_H
#define __CLIENT_AUTHENTICATE_H

struct managesieve_arg;

const char *client_authenticate_get_capabilities
	(struct client *client);

bool managesieve_client_auth_handle_reply(struct client *client,
				   const struct client_auth_reply *reply);

void managesieve_client_auth_send_challenge
	(struct client *client, const char *data);
int managesieve_client_auth_parse_response
	(struct client *client);

int cmd_authenticate
	(struct managesieve_client *client, const struct managesieve_arg *args);

#endif /* __CLIENT_AUTHENTICATE_H */
