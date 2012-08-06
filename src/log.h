/*
 * media-service-upnp
 *
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Ludovic Ferrandis <ludovic.ferrandis@intel.com>
 *
 */

#ifndef MSU_LOG_H__
#define MSU_LOG_H__

#include <syslog.h>

#include <glib.h>

enum msu_log_type_t_ {
	MSU_LOG_TYPE_SYSLOG = 0,
	MSU_LOG_TYPE_GLIB,
	MSU_LOG_TYPE_FILE
};
typedef enum msu_log_type_t_ msu_log_type_t;

void msu_log_init(const char *program);

void msu_log_finalize(void);

void msu_log_update_type_level(msu_log_type_t log_type, int log_level);

void msu_log_trace(int priority, GLogLevelFlags flags, const char *format, ...)
			__attribute__((format(printf, 3, 4)));

/* Generic Logging macro
 */
#ifdef MSU_DEBUG_ENABLED
	#define MSU_LOG_HELPER(priority, flags, fmt, ...)    \
		do { \
			msu_log_trace(priority, flags, "%s : %s() --- " fmt, \
				      __FILE__, __func__, ## __VA_ARGS__); \
		} while (0)
#else
	#define MSU_LOG_HELPER(priority, flags, fmt, ...) \
		do { \
			msu_log_trace(priority, flags, fmt, ## __VA_ARGS__); \
		} while (0)
#endif


/* Logging macro for error messages
 */
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_ERROR
	#define MSU_LOG_ERROR(...) \
		MSU_LOG_HELPER(LOG_ERR, G_LOG_LEVEL_ERROR, __VA_ARGS__, 0)
#else
	#define MSU_LOG_ERROR(...)
#endif


/* Logging macro for critical messages
 */
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_CRITICAL
	#define MSU_LOG_CRITICAL(...) \
		MSU_LOG_HELPER(LOG_CRIT, G_LOG_LEVEL_CRITICAL, __VA_ARGS__, 0)
#else
	#define MSU_LOG_CRITICAL(...)
#endif


/* Logging macro for warning messages
 */
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_WARNING
	#define MSU_LOG_WARNING(...) \
		MSU_LOG_HELPER(LOG_WARNING, G_LOG_LEVEL_WARNING, __VA_ARGS__, 0)
#else
	#define MSU_LOG_WARNING(...)
#endif


/* Logging macro for messages
 */
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_MESSAGE
	#define MSU_LOG_MESSAGE(...) \
		MSU_LOG_HELPER(LOG_NOTICE, G_LOG_LEVEL_MESSAGE, __VA_ARGS__, 0)
#else
	#define MSU_LOG_MESSAGE(...)
#endif


/* Logging macro for informational messages
 */
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_INFO
	#define MSU_LOG_INFO(...) \
		MSU_LOG_HELPER(LOG_INFO, G_LOG_LEVEL_INFO, __VA_ARGS__, 0)
#else
	#define MSU_LOG_INFO(...)
#endif


/* Logging macro for debug messages
 */
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_DEBUG
	#define MSU_LOG_DEBUG(...) \
		MSU_LOG_HELPER(LOG_DEBUG, G_LOG_LEVEL_DEBUG, __VA_ARGS__, 0)
#else
	#define MSU_LOG_DEBUG(...)
#endif


/* Logging macro to display an empty line
 */
#if MSU_LOG_LEVEL & MSU_LOG_LEVEL_DEBUG
	#define MSU_LOG_DEBUG_NL() \
		do { \
			msu_log_trace(LOG_DEBUG, G_LOG_LEVEL_DEBUG, "\n"); \
		} while (0)
#else
	#define MSU_LOG_DEBUG_NL()
#endif

#endif /* MSU_LOG_H__ */
