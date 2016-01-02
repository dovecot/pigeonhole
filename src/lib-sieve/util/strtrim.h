#ifndef STRTRIM_H
#define STRTRIM_H

/* Trim matching chars from either side of the string */
const char *ph_t_str_trim(const char *str, const char *chars);
const char *ph_p_str_trim(pool_t pool, const char *str, const char *chars);
const char *ph_str_ltrim(const char *str, const char *chars);
const char *ph_t_str_ltrim(const char *str, const char *chars);
const char *ph_p_str_ltrim(pool_t pool, const char *str, const char *chars);
const char *ph_t_str_rtrim(const char *str, const char *chars);
const char *ph_p_str_rtrim(pool_t pool, const char *str, const char *chars);

#endif
