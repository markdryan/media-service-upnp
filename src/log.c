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

#define _GNU_SOURCE
#include <stdarg.h>
#include <string.h>

#include "log.h"

static msu_log_t s_log_context;


static void prv_msu_log_set_flags_from_param(msu_log_t *log_context)
{
	int mask = 0;
	GLogLevelFlags flags = 0;

	if (MSU_LOG_LEVEL & MSU_LOG_LEVEL_ERROR) {
		mask |= LOG_MASK(LOG_ERR);
		flags |= G_LOG_LEVEL_ERROR;
	}

	if (MSU_LOG_LEVEL & MSU_LOG_LEVEL_CRITICAL) {
		mask |= LOG_MASK(LOG_CRIT);
		flags |= G_LOG_LEVEL_CRITICAL;
	}

	if (MSU_LOG_LEVEL & MSU_LOG_LEVEL_WARNING) {
		mask |= LOG_MASK(LOG_WARNING);
		flags |= G_LOG_LEVEL_WARNING;
	}

	if (MSU_LOG_LEVEL & MSU_LOG_LEVEL_MESSAGE) {
		mask |= LOG_MASK(LOG_NOTICE);
		flags |= G_LOG_LEVEL_MESSAGE;
	}

	if (MSU_LOG_LEVEL & MSU_LOG_LEVEL_INFO) {
		mask |= LOG_MASK(LOG_INFO);
		flags |= G_LOG_LEVEL_INFO;
	}

	if (MSU_LOG_LEVEL & MSU_LOG_LEVEL_DEBUG) {
		mask |= LOG_MASK(LOG_DEBUG);
		flags |= G_LOG_LEVEL_DEBUG;
	}

	if (flags)
		flags |= G_LOG_FLAG_RECURSION | G_LOG_FLAG_FATAL;

	log_context->mask = mask;
	log_context->flags = flags;

	log_context->log_type = MSU_LOG_TYPE;
}

static void prv_msu_log_handler(const gchar *log_domain,
				GLogLevelFlags log_level,
				const gchar *message,
				gpointer data)
{
	msu_log_t *log_context = (msu_log_t *)(data);

	if (g_strcmp0(log_domain, G_LOG_DOMAIN))
		return;

	if (log_context->flags & log_level)
		g_log_default_handler(log_domain, log_level, message, data);
}

void msu_log_init(const char *program)
{
	int option = LOG_NDELAY | LOG_PID;
	int old;

#ifdef MSU_DEBUG_ENABLED
	option |= LOG_PERROR | LOG_CONS;
#endif

	memset(&s_log_context, 0, sizeof(s_log_context));
	prv_msu_log_set_flags_from_param(&s_log_context);

	openlog(basename(program), option, LOG_DAEMON);

	old = setlogmask(LOG_MASK(LOG_INFO));
	syslog(LOG_INFO, "Media Service UPnP version %s", VERSION);
	(void) setlogmask(s_log_context.mask);

	s_log_context.old_mask = old;
	s_log_context.old_handler = g_log_set_default_handler(
					prv_msu_log_handler,
					&s_log_context);

	if (s_log_context.log_type != MSU_LOG_TYPE_SYSLOG) {
		MSU_LOG_INFO("Media Service UPnP version %s", VERSION);
	}
}

void msu_log_finalize(void)
{
	(void) setlogmask(LOG_MASK(LOG_INFO));
	syslog(LOG_INFO, "Media Service UPnP: Exit");

	if (s_log_context.log_type != MSU_LOG_TYPE_SYSLOG) {
		MSU_LOG_INFO("Media Service UPnP: Exit");
	}

	(void) g_log_set_default_handler(s_log_context.old_handler, NULL);

	(void) setlogmask(s_log_context.old_mask);
	closelog();

	memset(&s_log_context, 0, sizeof(s_log_context));
}

void msu_log_trace(int priority, GLogLevelFlags flags, const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch (s_log_context.log_type) {
	case MSU_LOG_TYPE_SYSLOG:
		vsyslog(priority, format, args);
		break;
	case MSU_LOG_TYPE_GLIB:
		g_logv(G_LOG_DOMAIN, flags, format, args);
		break;
	case MSU_LOG_TYPE_FILE:
		break;
	default:
		break;
	}

	va_end(args);
}
