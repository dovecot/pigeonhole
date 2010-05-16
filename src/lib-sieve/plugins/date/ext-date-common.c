/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "utc-offset.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-interpreter.h"
#include "sieve-message.h"

#include "ext-date-common.h"

#include <time.h>
#include <ctype.h>

struct ext_date_context {
	time_t current_date;
	int zone_offset;
};

/*
 * Runtime initialization
 */

static void ext_date_runtime_init
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv, 
	void *context ATTR_UNUSED)
{
	struct ext_date_context *dctx;
	pool_t pool;
	struct tm *tm;
	time_t current_date;
	int zone_offset;

	/* Get current time at instance main script is started */
	time(&current_date);	

	tm = localtime(&current_date);
	zone_offset = utc_offset(tm, current_date);

	/* Create context */
	pool = sieve_message_context_pool(renv->msgctx);
	dctx = p_new(pool, struct ext_date_context, 1);
	dctx->current_date = current_date;
	dctx->zone_offset = zone_offset;

	sieve_message_context_extension_set
		(renv->msgctx, ext, (void *) dctx);
}

static struct sieve_interpreter_extension date_interpreter_extension = {
	&date_extension,
	ext_date_runtime_init,
	NULL,
};

bool ext_date_interpreter_load
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv,
	sieve_size_t *address ATTR_UNUSED)
{	
	/* Register runtime hook to obtain stript start timestamp */
	if ( renv->msgctx == NULL ||
		sieve_message_context_extension_get(renv->msgctx, ext) == NULL ) {
		sieve_interpreter_extension_register
			(renv->interp, ext, &date_interpreter_extension, NULL);
	}

	return TRUE;
}

/*
 * Zone string
 */

bool ext_date_parse_timezone
(const char *zone, int *zone_offset_r)
{
	const unsigned char *str = (const unsigned char *) zone;
	size_t len = strlen(zone);

	if (len == 5 && (*str == '+' || *str == '-')) {
		int offset;

		if (!i_isdigit(str[1]) || !i_isdigit(str[2]) ||
		    !i_isdigit(str[3]) || !i_isdigit(str[4]))
			return FALSE;

		offset = ((str[1]-'0') * 10 + (str[2]-'0')) * 60  +
			(str[3]-'0') * 10 + (str[4]-'0');

		if ( zone_offset_r != NULL )		
			*zone_offset_r = *str == '+' ? offset : -offset;

		return TRUE;
	}

	return FALSE;
}

/*
 * Current date
 */

time_t ext_date_get_current_date
(const struct sieve_runtime_env *renv, int *zone_offset_r)
{	
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct ext_date_context *dctx = (struct ext_date_context *) 
		sieve_message_context_extension_get(renv->msgctx, this_ext);

	if ( dctx == NULL ) {
		ext_date_runtime_init(this_ext, renv, NULL);
		dctx = (struct ext_date_context *) 
			sieve_message_context_extension_get(renv->msgctx, this_ext);

		i_assert(dctx != NULL);
	}

	/* Read script start timestamp from message context */

	if ( zone_offset_r != NULL )
		*zone_offset_r = dctx->zone_offset;

	return dctx->current_date;
}

/* 
 * Date parts 
 */

/* "year"      => the year, "0000" .. "9999". 
 */

static const char *ext_date_year_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part year_date_part = {
	"year",
	ext_date_year_part_get
};

/* "month"     => the month, "01" .. "12".
 */

static const char *ext_date_month_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part month_date_part = {
	"month",
	ext_date_month_part_get
};

/* "day"       => the day, "01" .. "31".
 */

static const char *ext_date_day_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part day_date_part = {
	"day",
	ext_date_day_part_get
};

/* "date"      => the date in "yyyy-mm-dd" format.
 */

static const char *ext_date_date_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part date_date_part = {
	"date",
	ext_date_date_part_get
};

/* "julian"    => the Modified Julian Day, that is, the date
 *              expressed as an integer number of days since
 *              00:00 UTC on November 17, 1858 (using the Gregorian
 *              calendar).  This corresponds to the regular
 *              Julian Day minus 2400000.5.  Sample routines to
 *              convert to and from modified Julian dates are
 *              given in Appendix A.
 */ 

static const char *ext_date_julian_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part julian_date_part = {
	"julian",
	ext_date_julian_part_get
};

/* "hour"      => the hour, "00" .. "23". 
 */
static const char *ext_date_hour_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part hour_date_part = {
	"hour",
	ext_date_hour_part_get
};

/* "minute"    => the minute, "00" .. "59".
 */
static const char *ext_date_minute_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part minute_date_part = {
	"minute",
	ext_date_minute_part_get
};

/* "second"    => the second, "00" .. "60".
 */
static const char *ext_date_second_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part second_date_part = {
	"second",
	ext_date_second_part_get
};

/* "time"      => the time in "hh:mm:ss" format.
 */
static const char *ext_date_time_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part time_date_part = {
	"time",
	ext_date_time_part_get
};

/* "iso8601"   => the date and time in restricted ISO 8601 format.
 */
static const char *ext_date_iso8601_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part iso8601_date_part = {
	"iso8601",
	ext_date_iso8601_part_get
};

/* "std11"     => the date and time in a format appropriate
 *                for use in a Date: header field [RFC2822].
 */
static const char *ext_date_std11_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part std11_date_part = {
	"std11",
	ext_date_std11_part_get
};

/* "zone"      => the time zone in use.  If the user specified a
 *                time zone with ":zone", "zone" will
 *                contain that value.  If :originalzone is specified
 *                this value will be the original zone specified
 *                in the date-time value.  If neither argument is
 *                specified the value will be the server's default
 *                time zone in offset format "+hhmm" or "-hhmm".  An
 *                 offset of 0 (Zulu) always has a positive sign.
 */
static const char *ext_date_zone_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part zone_date_part = {
	"zone",
	ext_date_zone_part_get
};
 
/* "weekday"   => the day of the week expressed as an integer between
 *                "0" and "6". "0" is Sunday, "1" is Monday, etc.
 */
static const char *ext_date_weekday_part_get(struct tm *tm, int zone_offset);

static const struct ext_date_part weekday_date_part = {
	"weekday",
	ext_date_weekday_part_get
};

/*
 * Date part extraction
 */

static const struct ext_date_part *date_parts[] = {
	&year_date_part, &month_date_part, &day_date_part, &date_date_part,
	&julian_date_part, &hour_date_part, &minute_date_part, &second_date_part,
	&time_date_part, &iso8601_date_part, &std11_date_part, &zone_date_part, 
	&weekday_date_part 
};

unsigned int date_parts_count = N_ELEMENTS(date_parts);

const char *ext_date_part_extract
(const char *part, struct tm *tm, int zone_offset)
{
	unsigned int i;

	for ( i = 0; i < date_parts_count; i++ ) {
		if ( strcasecmp(date_parts[i]->identifier, part) == 0 ) {
			if ( date_parts[i]->get_string != NULL )
				return date_parts[i]->get_string(tm, zone_offset);

			return NULL;
		}
	}
	
	return NULL;
}

/*
 * Date part implementations
 */

static const char *month_names[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *weekday_names[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *ext_date_year_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%04d", tm->tm_year + 1900);
}

static const char *ext_date_month_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%02d", tm->tm_mon + 1);
}

static const char *ext_date_day_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%02d", tm->tm_mday);
}

static const char *ext_date_date_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%04d-%02d-%02d", 
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

static const char *ext_date_julian_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	int year = tm->tm_year+1900;
	int month = tm->tm_mon+1;
	int day = tm->tm_mday;
	int c, ya, jd;
	
	/* Modified from RFC 5260 Appendix A */	

	if ( month > 2 )
		month -= 3;
	else {
		month += 9;
		year--;
	}

	c = year / 100;
	ya = year - c * 100;

	jd = c * 146097 / 4 + ya * 1461 / 4 + (month * 153 + 2) / 5 + day + 1721119;
	
	return t_strdup_printf("%d", jd - 2400001);
}

static const char *ext_date_hour_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%02d", tm->tm_hour);
}

static const char *ext_date_minute_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%02d", tm->tm_min);
}

static const char *ext_date_second_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%02d", tm->tm_sec);
}

static const char *ext_date_time_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static const char *ext_date_iso8601_part_get
(struct tm *tm, int zone_offset)
{
	const char *time_offset;

	/* 
	 * RFC 3339: 5.6. Internet Date/Time Format
	 * 
	 * The following profile of ISO 8601 [ISO8601] dates SHOULD be used in
	 * new protocols on the Internet.  This is specified using the syntax
	 * description notation defined in [ABNF].
	 * 
	 * date-fullyear   = 4DIGIT
	 * date-month      = 2DIGIT  ; 01-12
	 * date-mday       = 2DIGIT  ; 01-28, 01-29, 01-30, 01-31 based on
	 * 		                     ; month/year
	 * time-hour       = 2DIGIT  ; 00-23
	 * time-minute     = 2DIGIT  ; 00-59
	 * time-second     = 2DIGIT  ; 00-58, 00-59, 00-60 based on leap second
	 * 		                     ; rules
	 * time-secfrac    = "." 1*DIGIT
	 * time-numoffset  = ("+" / "-") time-hour ":" time-minute
	 * time-offset     = "Z" / time-numoffset
	 * 
	 * partial-time    = time-hour ":" time-minute ":" time-second
	 * 		             [time-secfrac]
	 * full-date       = date-fullyear "-" date-month "-" date-mday
	 * full-time       = partial-time time-offset
	 * 
	 * date-time       = full-date "T" full-time
	 * 
	 */

	if ( zone_offset == 0 )
		time_offset = "Z";
	else {
		int offset = zone_offset > 0 ? zone_offset : -zone_offset;

		time_offset = t_strdup_printf
			("%c%02d:%02d", (zone_offset > 0 ? '+' : '-'), offset / 60, offset % 60);
	}

	return t_strdup_printf("%04d-%02d-%02dT%02d:%02d:%02d%s",
		tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, 
		tm->tm_sec, time_offset); 
}

static const char *ext_date_std11_part_get
(struct tm *tm, int zone_offset)
{
	return t_strdup_printf("%s, %02d %s %04d %02d:%02d:%02d %s",
		weekday_names[tm->tm_wday],
		tm->tm_mday,
		month_names[tm->tm_mon],
		tm->tm_year+1900,
		tm->tm_hour, tm->tm_min, tm->tm_sec, 
		ext_date_zone_part_get(tm, zone_offset));
}

static const char *ext_date_zone_part_get
(struct tm *tm ATTR_UNUSED, int zone_offset)
{
	bool negative;
	int offset = zone_offset;

	if (zone_offset >= 0)
		negative = FALSE;
	else {
		negative = TRUE;
		offset = -offset;
	}

	return t_strdup_printf
		("%c%02d%02d", negative ? '-' : '+', offset / 60, offset % 60);
}

static const char *ext_date_weekday_part_get
(struct tm *tm, int zone_offset ATTR_UNUSED)
{
	return t_strdup_printf("%d", tm->tm_wday);
}

