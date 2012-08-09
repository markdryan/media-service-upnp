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

#include <string.h>

#include "settings.h"

#define MSU_SETTINGS_KEYFILE_NAME	"media-service-upnp.conf"

#define MSU_SETTINGS_GROUP_GENERAL	"general"
#define MSU_SETTINGS_KEY_NEVER_QUIT	"never-quit"

#define MSU_SETTINGS_GROUP_LOG		"log"
#define MSU_SETTINGS_KEY_LOG_TYPE	"log-type"
#define MSU_SETTINGS_KEY_LOG_LEVEL	"log-level"

#define MSU_SETTINGS_DEFAULT_NEVER_QUIT	FALSE
#define MSU_SETTINGS_DEFAULT_LOG_TYPE	MSU_LOG_TYPE
#define MSU_SETTINGS_DEFAULT_LOG_LEVEL	MSU_LOG_LEVEL

#define MSU_SETTINGS_LOG_KEYS(sys, loc, settings) \
do { \
	MSU_LOG_DEBUG_NL(); \
	MSU_LOG_INFO("Load file [%s]", loc ? loc : sys); \
	MSU_LOG_DEBUG_NL(); \
	MSU_LOG_DEBUG("[General settings]"); \
	MSU_LOG_DEBUG("Never Quit: %s", settings->never_quit ? "T" : "F"); \
	MSU_LOG_DEBUG_NL(); \
	MSU_LOG_DEBUG("[Logging settings]"); \
	MSU_LOG_DEBUG("Log Type : %d", settings->log_type); \
	MSU_LOG_DEBUG("Log Level: 0x%02X", settings->log_level); \
	MSU_LOG_DEBUG_NL(); \
} while (0)


static void prv_msu_settings_get_keyfile_path(gchar **sys_path,
					      gchar **loc_path)
{
	const gchar *loc_dir;

	if (sys_path != NULL) {
		*sys_path = NULL;

		if (SYS_CONFIG_DIR && *SYS_CONFIG_DIR)
			*sys_path = g_strdup_printf("%s/%s", SYS_CONFIG_DIR,
						    MSU_SETTINGS_KEYFILE_NAME);
	}

	if (loc_path != NULL) {
		loc_dir =  g_get_user_config_dir();
		*loc_path = NULL;

		if (loc_dir && *loc_dir)
			*loc_path = g_strdup_printf("%s/%s", loc_dir,
						    MSU_SETTINGS_KEYFILE_NAME);
	}
}

static void prv_msu_settings_check_local_keyfile(const gchar *sys_path,
						 const gchar *loc_path)
{
	GFile *sys_file = NULL;
	GFile *loc_file;

	loc_file = g_file_new_for_path(loc_path);

	if (g_file_query_exists(loc_file, NULL) || (sys_path == NULL))
		goto exit;

	sys_file = g_file_new_for_path(sys_path);

	(void) g_file_copy(sys_file, loc_file, G_FILE_COPY_TARGET_DEFAULT_PERMS,
			   NULL, NULL, NULL, NULL);

exit:
	if (loc_file)
		g_object_unref(loc_file);

	if (sys_file)
		g_object_unref(sys_file);
}

static GKeyFile *prv_msu_settings_load_keyfile(const gchar *filepath)
{
	GKeyFile *keyfile = NULL;

	if (filepath == NULL)
		goto exit;

	keyfile = g_key_file_new();

	if (!g_key_file_load_from_file(keyfile, filepath, G_KEY_FILE_NONE,
					NULL)) {
		g_key_file_free(keyfile);
		keyfile = NULL;
	}

exit:
	return keyfile;
}

static int prv_msu_settings_to_log_level(gint *int_list, gsize length)
{
	gsize i;
	int log_level_value;
	int level;
	int log_level_array[] = { MSU_LOG_LEVEL_DISABLED,
				  MSU_LOG_LEVEL_ERROR, MSU_LOG_LEVEL_CRITICAL,
				  MSU_LOG_LEVEL_WARNING, MSU_LOG_LEVEL_MESSAGE,
				  MSU_LOG_LEVEL_INFO, MSU_LOG_LEVEL_DEBUG,
				  MSU_LOG_LEVEL_DEFAULT, MSU_LOG_LEVEL_ALL };

	log_level_value = 0;

	/* Take all valid values, even duplicated ones, and skip all others.
	 * Priority is single value (0,7,8) over multi value (1..6)
	 * Priority for single values is first found */
	for (i = 0; i < length; ++i) {
		level = int_list[i];

		if (level > 0 && level < 7)
			log_level_value |= log_level_array[level];
		else if ((level == 0) || (level == 7) || (level == 8)) {
			log_level_value = log_level_array[level];
			break;
		}
	}

	return log_level_value;
}

static msu_log_type_t prv_msu_settings_to_log_type(int type)
{
	msu_log_type_t log_type = MSU_LOG_TYPE_SYSLOG;

	switch (type) {
	case 0:
		break;
	case 1:
		log_type = MSU_LOG_TYPE_GLIB;
		break;
	default:
		break;
	}

	return log_type;
}

static void prv_msu_settings_read_keys(msu_settings_context_t *settings)
{
	GError *error = NULL;
	GKeyFile *keyfile = settings->keyfile;
	gboolean b_val;
	gint int_val;
	gint *int_star;
	gsize length;

	b_val = g_key_file_get_boolean(keyfile, MSU_SETTINGS_GROUP_GENERAL,
						MSU_SETTINGS_KEY_NEVER_QUIT,
						&error);

	if (error == NULL)
		settings->never_quit = b_val;
	else {
		g_error_free(error);
		error = NULL;
	}

	int_val = g_key_file_get_integer(keyfile, MSU_SETTINGS_GROUP_LOG,
						  MSU_SETTINGS_KEY_LOG_TYPE,
						  &error);

	if (error == NULL)
		settings->log_type = prv_msu_settings_to_log_type(int_val);
	else {
		g_error_free(error);
		error = NULL;
	}

	g_key_file_set_list_separator(keyfile, ',');

	int_star = g_key_file_get_integer_list(keyfile, MSU_SETTINGS_GROUP_LOG,
						   MSU_SETTINGS_KEY_LOG_LEVEL,
						   &length,
						   &error);

	if (error == NULL) {
		settings->log_level = prv_msu_settings_to_log_level(int_star,
								     length);
		g_free(int_star);
	} else {
		g_error_free(error);
		error = NULL;
	}
}

static void prv_msu_settings_init_default(msu_settings_context_t *settings)
{
	settings->never_quit = MSU_SETTINGS_DEFAULT_NEVER_QUIT;

	settings->log_type = MSU_SETTINGS_DEFAULT_LOG_TYPE;
	settings->log_level = MSU_SETTINGS_DEFAULT_LOG_LEVEL;
}

static void prv_msu_settings_keyfile_init(msu_settings_context_t *settings,
					  const gchar *sys_path,
					  const gchar *loc_path)
{
	settings->keyfile = prv_msu_settings_load_keyfile(loc_path);

	if (settings->keyfile == NULL)
		settings->keyfile = prv_msu_settings_load_keyfile(sys_path);

	if (settings->keyfile != NULL) {
		prv_msu_settings_read_keys(settings);
		msu_log_update_type_level(settings->log_type,
					  settings->log_level);
	}
}

static void prv_msu_settings_keyfile_finalize(msu_settings_context_t *settings)
{
	if (settings->keyfile != NULL) {
		g_key_file_free(settings->keyfile);
		settings->keyfile = NULL;
	}
}

static void prv_msu_settings_reload(msu_settings_context_t *settings)
{
	gchar *sys_path = NULL;
	gchar *loc_path = NULL;

	MSU_LOG_INFO("Reload local configuration file");

	prv_msu_settings_keyfile_finalize(settings);
	prv_msu_settings_init_default(settings);
	prv_msu_settings_get_keyfile_path(&sys_path, &loc_path);

	if (sys_path || loc_path)
		prv_msu_settings_keyfile_init(settings, sys_path, loc_path);

	MSU_SETTINGS_LOG_KEYS(sys_path, loc_path, settings);

	g_free(sys_path);
	g_free(loc_path);
}

static gboolean prv_msu_settings_monitor_timout_cb(gpointer user_data)
{
	msu_settings_context_t *data = (msu_settings_context_t *) user_data;

	MSU_LOG_INFO("Change in local settings file: Reload");

	prv_msu_settings_reload(data);

	data->ev_id = 0;
	return FALSE;
}

static void prv_msu_settings_monitor_keyfile_cb(GFileMonitor *monitor,
						GFile *file,
						GFile *other_file,
						GFileMonitorEvent event_type,
						gpointer user_data)
{
	msu_settings_context_t *data = (msu_settings_context_t *) user_data;

	switch (event_type) {
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
	case G_FILE_MONITOR_EVENT_DELETED:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_MOVED:
		/* Reset the timer to prevent running the cb if monitoring
		 * event are raised 1 sec after the timer has been set */
		if (data->ev_id != 0)
			(void) g_source_remove(data->ev_id);

		data->ev_id = g_timeout_add_seconds(1,
				prv_msu_settings_monitor_timout_cb, user_data);
		break;
	default:
		break;
	}
}

static void prv_msu_settings_monitor_local_keyfile(
		msu_settings_context_t *settings, const gchar *loc_path)
{
	GFile *loc_file;
	GFileMonitor *monitor = NULL;
	gulong handler_id;

	loc_file = g_file_new_for_path(loc_path);
	monitor = g_file_monitor_file(loc_file, G_FILE_MONITOR_SEND_MOVED, NULL,
					NULL);
	g_object_unref(loc_file);

	if (monitor != NULL) {
		handler_id = g_signal_connect(monitor, "changed",
				G_CALLBACK(prv_msu_settings_monitor_keyfile_cb),
				settings);

		settings->monitor = monitor;
		settings->handler_id = handler_id;
	}
}

gboolean msu_settings_is_never_quit(msu_settings_context_t *settings)
{
	return settings->never_quit;
}

void msu_settings_init(msu_settings_context_t *settings)
{
	gchar *sys_path = NULL;
	gchar *loc_path = NULL;

	memset(settings, 0, sizeof(*settings));
	prv_msu_settings_init_default(settings);

	prv_msu_settings_get_keyfile_path(&sys_path, &loc_path);

	if (loc_path) {
		prv_msu_settings_check_local_keyfile(sys_path, loc_path);
		prv_msu_settings_monitor_local_keyfile(settings, loc_path);
	}

	if (sys_path || loc_path)
		prv_msu_settings_keyfile_init(settings, sys_path, loc_path);

	MSU_SETTINGS_LOG_KEYS(sys_path, loc_path, settings);

	g_free(sys_path);
	g_free(loc_path);
}

void msu_settings_finalize(msu_settings_context_t *settings)
{
	if (settings->monitor) {
		if (settings->handler_id)
			g_signal_handler_disconnect(settings->monitor,
						    settings->handler_id);

		g_file_monitor_cancel(settings->monitor);
		g_object_unref(settings->monitor);
	}

	prv_msu_settings_keyfile_finalize(settings);
}
