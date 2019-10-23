#ifndef MANAGESIEVE_QUOTA_H
#define MANAGESIEVE_QUOTA_H

uint64_t managesieve_quota_max_script_size(struct client *client);

bool managesieve_quota_check_validsize(struct client_command_context *cmd,
				       size_t size);
bool managesieve_quota_check_all(struct client_command_context *cmd,
				 const char *scriptname, size_t size);

#endif
