/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-settings.h"
#include "sieve-extensions.h"

#include "ext-vacation-common.h"

bool ext_vacation_load(const struct sieve_extension *ext, void **context)
{
	struct sieve_instance *svinst = ext->svinst;
	struct ext_vacation_config *config;
	sieve_number_t min_period, max_period, default_period;
	bool use_original_recipient, dont_check_recipient, send_from_recipient,
		to_header_ignore_envelope;
	const char *default_subject, *default_subject_template;

	if (*context != NULL)
		ext_vacation_unload(ext);

	if (!sieve_setting_get_duration_value(
		svinst, "sieve_vacation_min_period", &min_period))
		min_period = EXT_VACATION_DEFAULT_MIN_PERIOD;
	if (!sieve_setting_get_duration_value(
		svinst, "sieve_vacation_max_period", &max_period))
		max_period = EXT_VACATION_DEFAULT_MAX_PERIOD;
	if (!sieve_setting_get_duration_value(
		svinst, "sieve_vacation_default_period", &default_period))
		default_period = EXT_VACATION_DEFAULT_PERIOD;

	if (max_period > 0 &&
	    (min_period > max_period || default_period < min_period ||
	     default_period > max_period)) {
		min_period = EXT_VACATION_DEFAULT_MIN_PERIOD;
		max_period = EXT_VACATION_DEFAULT_MAX_PERIOD;
		default_period = EXT_VACATION_DEFAULT_PERIOD;

		e_warning(svinst->event, "vacation extension: "
			  "invalid settings: violated "
			  "sieve_vacation_min_period < "
			  "sieve_vacation_default_period < "
			  "sieve_vacation_max_period");
	}

	default_subject = sieve_setting_get(
		svinst, "sieve_vacation_default_subject");
	default_subject_template = sieve_setting_get(
		svinst, "sieve_vacation_default_subject_template");

	if (!sieve_setting_get_bool_value(
		svinst, "sieve_vacation_use_original_recipient",
		&use_original_recipient))
		use_original_recipient = FALSE;
	if (!sieve_setting_get_bool_value(
		svinst, "sieve_vacation_dont_check_recipient",
		&dont_check_recipient))
		dont_check_recipient = FALSE;
	if (!sieve_setting_get_bool_value(
		svinst, "sieve_vacation_send_from_recipient",
		&send_from_recipient))
		send_from_recipient = FALSE;
	if (!sieve_setting_get_bool_value(
		svinst, "sieve_vacation_to_header_ignore_envelope",
		&to_header_ignore_envelope) )
		to_header_ignore_envelope = FALSE;

	config = i_new(struct ext_vacation_config, 1);
	config->min_period = min_period;
	config->max_period = max_period;
	config->default_period = default_period;
	config->default_subject = i_strdup_empty(default_subject);
	config->default_subject_template =
		i_strdup_empty(default_subject_template);
	config->use_original_recipient = use_original_recipient;
	config->dont_check_recipient = dont_check_recipient;
	config->send_from_recipient = send_from_recipient;
	config->to_header_ignore_envelope = to_header_ignore_envelope;

	*context = (void *)config;
	return TRUE;
}

void ext_vacation_unload(const struct sieve_extension *ext)
{
	struct ext_vacation_config *config =
		(struct ext_vacation_config *)ext->context;

	i_free(config->default_subject);
	i_free(config->default_subject_template);
	i_free(config);
}
