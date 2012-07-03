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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

#include <glib.h>

#include "log.h"

static msu_log_t *s_log_context;

static void prv_msu_log_set_flags(msu_log_t *log_context)
{
	int mask = 0;
	GLogLevelFlags flags = 0;

	if (MSU_LOG_LEVEL & LOG_LEVEL_ERROR) {
		mask |= LOG_MASK(LOG_ERR);
		flags |= G_LOG_LEVEL_ERROR;
	}

	if (MSU_LOG_LEVEL & LOG_LEVEL_CRITICAL) {
		mask |= LOG_MASK(LOG_CRIT);
		flags |= G_LOG_LEVEL_CRITICAL;
	}

	if (MSU_LOG_LEVEL & LOG_LEVEL_WARNING) {
		mask |= LOG_MASK(LOG_WARNING);
		flags |= G_LOG_LEVEL_WARNING;
	}

	if (MSU_LOG_LEVEL & LOG_LEVEL_MESSAGE) {
		mask |= LOG_MASK(LOG_NOTICE);
		flags |= G_LOG_LEVEL_MESSAGE;
	}

	if (MSU_LOG_LEVEL & LOG_LEVEL_INFO) {
		mask |= LOG_MASK(LOG_INFO);
		flags |= G_LOG_LEVEL_INFO;
	}

	if (MSU_LOG_LEVEL & LOG_LEVEL_DEBUG) {
		mask |= LOG_MASK(LOG_DEBUG);
		flags |= G_LOG_LEVEL_DEBUG;
	}

	if (flags)
		flags |= G_LOG_FLAG_RECURSION | G_LOG_FLAG_FATAL;

	log_context->mask = mask;
	log_context->flags = flags;
}

static void prv_msu_log_handler(const gchar *log_domain,
				GLogLevelFlags log_level,
				const gchar *message,
				gpointer data)
{
	msu_log_t *log_context = (msu_log_t *)(data);

	if (strcmp(log_domain, G_LOG_DOMAIN))
		return;

	if (log_context->flags & log_level)
		g_log_default_handler(log_domain, log_level, message, data);
}

void msu_log_init(const char *program, msu_log_t *log_context)
{
	int option = LOG_NDELAY | LOG_PID;
	int old;

#ifdef DEBUG
	option |= LOG_PERROR | LOG_CONS;
#endif

	memset(log_context, 0, sizeof(msu_log_t));
	prv_msu_log_set_flags(log_context);

	openlog(basename(program), option, LOG_DAEMON);

	old = setlogmask(LOG_MASK(LOG_INFO));
	syslog(LOG_INFO, "Media Service UPnP version %s", VERSION);
	(void) setlogmask(log_context->mask);

	log_context->old_mask = old;
	log_context->old_handler = g_log_set_default_handler(
					prv_msu_log_handler,
					log_context);

	s_log_context = log_context;

	if (log_context->log_type != MSU_LOG_TYPE_SYSLOG) {
		MSU_LOG_INFO("Media Service UPnP version %s", VERSION);
	}
}

void msu_log_finalize(msu_log_t *log_context)
{
	(void) setlogmask(LOG_MASK(LOG_INFO));
	syslog(LOG_INFO, "Media Service UPnP: Exit");

	if (log_context->log_type != MSU_LOG_TYPE_SYSLOG) {
		MSU_LOG_INFO("Media Service UPnP: Exit");
	}

	(void) g_log_set_default_handler(log_context->old_handler, NULL);

	(void) setlogmask(log_context->old_mask);
	closelog();

	s_log_context = NULL;
}

void msu_log_error(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch (s_log_context->log_type) {
	case MSU_LOG_TYPE_SYSLOG:
		vsyslog(LOG_ERR, format, args);
		break;
	case MSU_LOG_TYPE_GLIB:
		g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_ERROR, format, args);
		break;
	case MSU_LOG_TYPE_FILE:	break;
	default: break;
	}

	va_end(args);
}

void msu_log_critical(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch (s_log_context->log_type) {
	case MSU_LOG_TYPE_SYSLOG:
		vsyslog(LOG_CRIT, format, args);
		break;
	case MSU_LOG_TYPE_GLIB:
		g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, format, args);
		break;
	case MSU_LOG_TYPE_FILE:	break;
	default: break;
	}

	va_end(args);
}

void msu_log_warning(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch (s_log_context->log_type) {
	case MSU_LOG_TYPE_SYSLOG:
		vsyslog(LOG_WARNING, format, args);
		break;
	case MSU_LOG_TYPE_GLIB:
		g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, format, args);
		break;
	case MSU_LOG_TYPE_FILE:	break;
	default: break;
	}

	va_end(args);
}

void msu_log_message(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch (s_log_context->log_type) {
	case MSU_LOG_TYPE_SYSLOG:
		vsyslog(LOG_NOTICE, format, args);
		break;
	case MSU_LOG_TYPE_GLIB:
		g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, format, args);
		break;
	case MSU_LOG_TYPE_FILE:	break;
	default: break;
	}

	va_end(args);
}

void msu_log_info(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch (s_log_context->log_type) {
	case MSU_LOG_TYPE_SYSLOG:
		vsyslog(LOG_INFO, format, args);
		break;
	case MSU_LOG_TYPE_GLIB:
		g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, format, args);
		break;
	case MSU_LOG_TYPE_FILE:	break;
	default: break;
	}

	va_end(args);
}

void msu_log_debug(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch (s_log_context->log_type) {
	case MSU_LOG_TYPE_SYSLOG:
		vsyslog(LOG_DEBUG, format, args);
		break;
	case MSU_LOG_TYPE_GLIB:
		g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
		break;
	case MSU_LOG_TYPE_FILE:	break;
	default: break;
	}

	va_end(args);
}
