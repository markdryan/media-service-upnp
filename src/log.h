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

#include <glib.h>

enum msu_log_type_t_ {
	MSU_LOG_TYPE_SYSLOG = 0,
	MSU_LOG_TYPE_GLIB,
	MSU_LOG_TYPE_FILE
};
typedef enum msu_log_type_t_ msu_log_type_t;

typedef struct msu_log_t_ msu_log_t;
struct msu_log_t_ {
	int old_mask;
	int mask;
	msu_log_type_t log_type;
	GLogLevelFlags flags;
	GLogFunc old_handler;
};

void msu_log_init(const char *program, msu_log_t *log_context);

void msu_log_finialize(msu_log_t *log_context);

void msu_log_error(const char *format, ...)
			__attribute__((format(printf, 1, 2)));

void msu_log_critical(const char *format, ...)
			__attribute__((format(printf, 1, 2)));

void msu_log_warning(const char *format, ...)
			__attribute__((format(printf, 1, 2)));

void msu_log_message(const char *format, ...)
			__attribute__((format(printf, 1, 2)));

void msu_log_info(const char *format, ...)
			__attribute__((format(printf, 1, 2)));

void msu_log_debug(const char *format, ...)
			__attribute__((format(printf, 1, 2)));



/* Logging macro for error messages
 */
#if MSU_LOG_LEVEL & LOG_LEVEL_ERROR
	#ifdef DEBUG
		#define MSU_LOG_ERROR(fmt, args...) \
			do { \
				msu_log_error("%s:%s() " fmt, __FILE__, \
						__FUNCTION__, ## args); \
			} while (0)
	#else
		#define MSU_LOG_ERROR(fmt, args...) \
			do { \
				msu_log_error(fmt, ## args); \
			} while (0)
	#endif
#else
	#define MSU_LOG_ERROR(fmt, ...)
#endif


/* Logging macro for critical messages
 */
#if MSU_LOG_LEVEL & LOG_LEVEL_CRITICAL
	#ifdef DEBUG
		#define MSU_LOG_CRITICAL(fmt, args...) \
			do { \
				msu_log_critical("%s:%s() " fmt, __FILE__, \
						__FUNCTION__, ## args); \
			} while (0)
	#else
		#define MSU_LOG_CRITICAL(fmt, args...) \
			do { \
				msu_log_critical(fmt, ## args); \
			} while (0)
	#endif
#else
	#define MSU_LOG_CRITICAL(fmt, ...)
#endif


/* Logging macro for warning messages
 */
#if MSU_LOG_LEVEL & LOG_LEVEL_WARNING
	#ifdef DEBUG
		#define MSU_LOG_WARNING(fmt, args...) \
			do { \
				msu_log_warning("%s:%s() " fmt, __FILE__, \
						__FUNCTION__, ## args); \
			} while (0)
	#else
		#define MSU_LOG_WARNING(fmt, args...) \
			do { \
				msu_log_warning(fmt, ## args); \
			} while (0)
	#endif
#else
	#define MSU_LOG_WARNING(fmt, ...)
#endif


/* Logging macro for messages
 */
#if MSU_LOG_LEVEL & LOG_LEVEL_MESSAGE
	#ifdef DEBUG
		#define MSU_LOG_MESSAGE(fmt, args...) \
			do { \
				msu_log_message("%s:%s() " fmt, __FILE__, \
						__FUNCTION__, ## args); \
			} while (0)
	#else
		#define MSU_LOG_MESSAGE(fmt, args...) \
			do { \
				msu_log_message(fmt, ## args); \
			} while (0)
	#endif
#else
	#define MSU_LOG_MESSAGE(fmt, ...)
#endif


/* Logging macro for informational messages
 */
#if MSU_LOG_LEVEL & LOG_LEVEL_INFO
	#ifdef DEBUG
		#define MSU_LOG_INFO(fmt, args...) \
			do { \
				msu_log_info("%s:%s() " fmt, __FILE__, \
						__FUNCTION__, ## args); \
			} while (0)
	#else
		#define MSU_LOG_INFO(fmt, args...) \
			do { \
				msu_log_info(fmt, ## args); \
			} while (0)
	#endif
#else
	#define MSU_LOG_INFO(fmt, ...)
#endif


/* Logging macro for debug messages
 */
#if MSU_LOG_LEVEL & LOG_LEVEL_DEBUG
	#ifdef DEBUG
		#define MSU_LOG_DEBUG(fmt, args...) \
			do { \
				msu_log_debug("%s:%s() " fmt, __FILE__, \
						__FUNCTION__, ## args); \
			} while (0)
	#else
		#define MSU_LOG_DEBUG(fmt, args...) \
			do { \
				msu_log_debug(fmt, ## args); \
			} while (0)
	#endif
#else
	#define MSU_LOG_DEBUG(fmt, ...)
#endif

#endif /* MSU_LOG_H__ */
