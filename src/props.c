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
 * Mark Ryan <mark.d.ryan@intel.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "path.h"
#include "props.h"

static const gchar gUPnPContainer[] = "object.container";
static const gchar gUPnPAudioItem[] = "object.item.audioItem";
static const gchar gUPnPVideoItem[] = "object.item.videoItem";
static const gchar gUPnPImageItem[] = "object.item.imageItem";

static const unsigned int gUPnPContainerLen =
	(sizeof(gUPnPContainer) / sizeof(gchar)) - 1;
static const unsigned int gUPnPAudioItemLen =
	(sizeof(gUPnPAudioItem) / sizeof(gchar)) - 1;
static const unsigned int gUPnPVideoItemLen =
	(sizeof(gUPnPVideoItem) / sizeof(gchar)) - 1;
static const unsigned int gUPnPImageItemLen =
	(sizeof(gUPnPImageItem) / sizeof(gchar)) - 1;

static const gchar gUPnPMusicTrack[] = "object.item.audioItem.musicTrack";
static const gchar gUPnPMovie[] = "object.item.videoItem.movie";
static const gchar gUPnPPhoto[] = "object.item.imageItem.photo";

static const gchar gMediaSpec2Container[] = "container";
static const gchar gMediaSpec2AudioMusic[] = "audio.music";
static const gchar gMediaSpec2Audio[] = "audio";
static const gchar gMediaSpec2VideoMovie[] = "video.movie";
static const gchar gMediaSpec2Video[] = "video";
static const gchar gMediaSpec2ImagePhoto[] = "image.photo";
static const gchar gMediaSpec2Image[] = "image";

static msu_prop_map_t *prv_msu_prop_map_new(const gchar *prop_name,
					    msu_upnp_prop_mask type,
					    gboolean filter,
					    gboolean searchable)
{
	msu_prop_map_t *retval = g_new(msu_prop_map_t, 1);
	retval->upnp_prop_name = prop_name;
	retval->type = type;
	retval->filter = filter;
	retval->searchable = searchable;
	return retval;
}

GHashTable *msu_prop_maps_new()
{
	msu_prop_map_t *prop_map;
	GHashTable *filter_map =
		g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

	prop_map = prv_msu_prop_map_new("@parentID", MSU_UPNP_MASK_PROP_PARENT,
					FALSE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_PARENT, prop_map);

	prop_map = prv_msu_prop_map_new("upnp:class", MSU_UPNP_MASK_PROP_TYPE,
					FALSE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_TYPE, prop_map);

	prop_map = prv_msu_prop_map_new("@id", MSU_UPNP_MASK_PROP_PATH,
					FALSE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_PATH, prop_map);

	prop_map = prv_msu_prop_map_new("dc:title",
					MSU_UPNP_MASK_PROP_DISPLAY_NAME,
					FALSE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_DISPLAY_NAME,
			    prop_map);

	prop_map = prv_msu_prop_map_new("@childCount",
					MSU_UPNP_MASK_PROP_CHILD_COUNT,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_CHILD_COUNT,
			    prop_map);

	prop_map = prv_msu_prop_map_new("@searchable",
					MSU_UPNP_MASK_PROP_SEARCHABLE,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_SEARCHABLE, prop_map);

	prop_map = prv_msu_prop_map_new("res", MSU_UPNP_MASK_PROP_URLS,
					TRUE, FALSE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_URLS, prop_map);

	prop_map = prv_msu_prop_map_new("res@protocolInfo",
					MSU_UPNP_MASK_PROP_MIME_TYPE,
					TRUE, FALSE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_MIME_TYPE, prop_map);

	prop_map = prv_msu_prop_map_new("upnp:artist",
					MSU_UPNP_MASK_PROP_ARTIST, TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_ARTIST, prop_map);

	prop_map = prv_msu_prop_map_new("upnp:album", MSU_UPNP_MASK_PROP_ALBUM,
				       TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_ALBUM, prop_map);

	prop_map = prv_msu_prop_map_new("dc:date", MSU_UPNP_MASK_PROP_DATE,
				       TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_DATE, prop_map);

	prop_map = prv_msu_prop_map_new("upnp:genre", MSU_UPNP_MASK_PROP_GENRE,
				       TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_GENRE, prop_map);

	prop_map = prv_msu_prop_map_new("res@protocolInfo",
				       MSU_UPNP_MASK_PROP_DLNA_PROFILE,
				       TRUE, FALSE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_DLNA_PROFILE,
			    prop_map);

	prop_map = prv_msu_prop_map_new("upnp:originalTrackNumber",
					MSU_UPNP_MASK_PROP_TRACK_NUMBER,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_TRACK_NUMBER,
			    prop_map);

	prop_map = prv_msu_prop_map_new("res@size", MSU_UPNP_MASK_PROP_SIZE,
				       TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_SIZE, prop_map);

	prop_map = prv_msu_prop_map_new("res@duration",
					MSU_UPNP_MASK_PROP_DURATION,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_DURATION, prop_map);

	prop_map = prv_msu_prop_map_new("res@bitrate",
					MSU_UPNP_MASK_PROP_BITRATE,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_BITRATE, prop_map);

	prop_map = prv_msu_prop_map_new("res@sampleFrequency",
					MSU_UPNP_MASK_PROP_SAMPLE_RATE,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_SAMPLE_RATE,
			    prop_map);

	prop_map = prv_msu_prop_map_new("res@bitsPerSample",
					MSU_UPNP_MASK_PROP_BITS_PER_SAMPLE,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_BITS_PER_SAMPLE,
			    prop_map);

	prop_map = prv_msu_prop_map_new("res@resolution",
					MSU_UPNP_MASK_PROP_WIDTH, TRUE, FALSE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_WIDTH, prop_map);

	prop_map = prv_msu_prop_map_new("res@resolution",
					MSU_UPNP_MASK_PROP_HEIGHT, TRUE, FALSE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_HEIGHT, prop_map);

	prop_map = prv_msu_prop_map_new("res@colorDepth",
					MSU_UPNP_MASK_PROP_COLOR_DEPTH,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_COLOR_DEPTH,
			    prop_map);

	prop_map = prv_msu_prop_map_new("upnp:albumArtURI",
					MSU_UPNP_MASK_PROP_ALBUM_ART_URL,
					TRUE, TRUE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_ALBUM_ART_URL,
			    prop_map);

	prop_map = prv_msu_prop_map_new("res", MSU_UPNP_MASK_PROP_RESOURCES,
					TRUE, FALSE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_RESOURCES,
			    prop_map);

	prop_map = prv_msu_prop_map_new("res", MSU_UPNP_MASK_PROP_URL,
					TRUE, FALSE);
	g_hash_table_insert(filter_map,
			    (gpointer) MSU_INTERFACE_PROP_URL, prop_map);

	return filter_map;
}

static guint32 prv_parse_filter_list(GHashTable *filter_map, GVariant *filter,
				     gchar **upnp_filter)
{
	GVariantIter viter;
	const gchar *prop;
	msu_prop_map_t *prop_map;
	GHashTable *upnp_props;
	GHashTableIter iter;
	guint32 mask = 0;
	gpointer key;
	GString *str;

	upnp_props = g_hash_table_new_full(g_str_hash, g_str_equal,
					   NULL, NULL);
	(void) g_variant_iter_init(&viter, filter);

	while (g_variant_iter_next(&viter, "&s", &prop)) {
		prop_map = g_hash_table_lookup(filter_map, prop);
		if (!prop_map)
			continue;

		mask |= prop_map->type;

		if (!prop_map->filter)
			continue;

		g_hash_table_insert(upnp_props,
				    (gpointer) prop_map->upnp_prop_name, NULL);
	}

	str = g_string_new("");
	g_hash_table_iter_init(&iter, upnp_props);
	if (g_hash_table_iter_next(&iter, &key, NULL)) {
		g_string_append(str, (const gchar *) key);
		while (g_hash_table_iter_next(&iter, &key, NULL)) {
			g_string_append(str, ",");
			g_string_append(str, (const gchar *) key);
		}
	}
	*upnp_filter = g_string_free(str, FALSE);
	g_hash_table_unref(upnp_props);

	return mask;
}

guint32 msu_props_parse_filter(GHashTable *filter_map, GVariant *filter,
			       gchar **upnp_filter)
{
	gchar *str;
	gboolean parse_filter = TRUE;
	guint32 mask;

	if (g_variant_n_children(filter) == 1) {
		g_variant_get_child(filter, 0, "&s", &str);
		if (!strcmp(str, "*"))
			parse_filter = FALSE;
	}

	if (parse_filter) {
		mask = prv_parse_filter_list(filter_map, filter, upnp_filter);
	} else {
		mask = 0xffffffff;
		*upnp_filter = g_strdup("*");
	}

	return mask;
}

static void prv_add_string_prop(GVariantBuilder *vb, const gchar *key,
				const gchar *value)
{
	if (value)
		g_variant_builder_add(vb, "{sv}", key,
				      g_variant_new_string(value));
}

static void prv_add_strv_prop(GVariantBuilder *vb, const gchar *key,
			      const gchar **value, unsigned int len)
{
	if (len > 0)
		g_variant_builder_add(vb, "{sv}", key,
				      g_variant_new_strv(value, len));
}

static void prv_add_path_prop(GVariantBuilder *vb, const gchar *key,
			      const gchar *value)
{
	if (value)
		g_variant_builder_add(vb, "{sv}", key,
				      g_variant_new_object_path(value));
}

static void prv_add_uint_prop(GVariantBuilder *vb, const gchar *key,
			      unsigned int value)
{
	g_variant_builder_add(vb, "{sv}", key, g_variant_new_uint32(value));
}

static void prv_add_int_prop(GVariantBuilder *vb, const gchar *key,
			     int value)
{
	if (value != -1)
		g_variant_builder_add(vb, "{sv}", key,
				      g_variant_new_int32(value));
}

void msu_props_add_child_count(GVariantBuilder *item_vb, gint value)
{
	prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_CHILD_COUNT, value);
}

static void prv_add_bool_prop(GVariantBuilder *vb, const gchar *key,
			      gboolean value)
{
	g_variant_builder_add(vb, "{sv}", key, g_variant_new_boolean(value));
}

static void prv_add_int64_prop(GVariantBuilder *vb, const gchar *key,
			       gint64 value)
{
	if (value != -1)
		g_variant_builder_add(vb, "{sv}", key,
				      g_variant_new_int64(value));
}

void msu_props_add_device(GUPnPDeviceInfo *proxy, GVariantBuilder *vb)
{
	gchar *str;

	prv_add_string_prop(vb, MSU_INTERFACE_PROP_LOCATION,
			    gupnp_device_info_get_location(proxy));

	prv_add_string_prop(vb, MSU_INTERFACE_PROP_UDN,
			    gupnp_device_info_get_udn(proxy));

	prv_add_string_prop(vb, MSU_INTERFACE_PROP_DEVICE_TYPE,
			    gupnp_device_info_get_device_type(proxy));

	str = gupnp_device_info_get_friendly_name(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_FRIENDLY_NAME, str);
	g_free(str);

	str = gupnp_device_info_get_manufacturer(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_MANUFACTURER, str);
	g_free(str);

	str = gupnp_device_info_get_manufacturer_url(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_MANUFACTURER_URL, str);
	g_free(str);

	str = gupnp_device_info_get_model_description(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_MODEL_DESCRIPTION, str);
	g_free(str);

	str = gupnp_device_info_get_model_name(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_MODEL_NAME, str);
	g_free(str);

	str = gupnp_device_info_get_model_number(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_MODEL_NUMBER, str);
	g_free(str);

	str = gupnp_device_info_get_model_url(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_MODEL_URL, str);
	g_free(str);

	str = gupnp_device_info_get_serial_number(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_SERIAL_NUMBER, str);
	g_free(str);

	str = gupnp_device_info_get_presentation_url(proxy);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_PRESENTATION_URL, str);
	g_free(str);

	str = gupnp_device_info_get_icon_url(proxy, NULL, -1, -1, -1, FALSE,
					     NULL, NULL, NULL, NULL);
	prv_add_string_prop(vb, MSU_INTERFACE_PROP_ICON_URL, str);
	g_free(str);
}

GVariant *msu_props_get_device_prop(GUPnPDeviceInfo *proxy, const gchar *prop)
{
	GVariant *retval = NULL;
	const gchar *str = NULL;
	gchar *copy = NULL;

	if (!strcmp(MSU_INTERFACE_PROP_LOCATION, prop)) {
		str = gupnp_device_info_get_location(proxy);
	} else if (!strcmp(MSU_INTERFACE_PROP_UDN, prop)) {
		str = gupnp_device_info_get_udn(proxy);
	} else if (!strcmp(MSU_INTERFACE_PROP_DEVICE_TYPE, prop)) {
		str = gupnp_device_info_get_device_type(proxy);
	} else if (!strcmp(MSU_INTERFACE_PROP_FRIENDLY_NAME, prop)) {
		copy = gupnp_device_info_get_friendly_name(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_MANUFACTURER, prop)) {
		copy = gupnp_device_info_get_manufacturer(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_MANUFACTURER_URL, prop)) {
		copy = gupnp_device_info_get_manufacturer_url(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_MODEL_DESCRIPTION, prop)) {
		copy = gupnp_device_info_get_model_description(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_MODEL_NAME, prop)) {
		copy = gupnp_device_info_get_model_name(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_MODEL_NUMBER, prop)) {
		copy = gupnp_device_info_get_model_number(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_MODEL_URL, prop)) {
		copy = gupnp_device_info_get_model_url(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_SERIAL_NUMBER, prop)) {
		copy = gupnp_device_info_get_serial_number(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_PRESENTATION_URL, prop)) {
		copy = gupnp_device_info_get_presentation_url(proxy);
		str = copy;
	} else if (!strcmp(MSU_INTERFACE_PROP_ICON_URL, prop)) {
		copy = gupnp_device_info_get_icon_url(proxy, NULL,
						      -1, -1, -1, FALSE,
						      NULL, NULL, NULL, NULL);
		str = copy;
	}

	if (str)
		retval = g_variant_ref_sink(g_variant_new_string(str));

	g_free(copy);

	return retval;
}

static GUPnPDIDLLiteResource *prv_match_resource(GUPnPDIDLLiteResource *res,
						 gchar **pi_str_array)
{
	GUPnPDIDLLiteResource *retval = NULL;
	GUPnPProtocolInfo *res_pi;
	GUPnPProtocolInfo *pi;
	unsigned int i;
	gboolean match;

	if (!pi_str_array) {
		retval = res;
		goto done;
	}

	res_pi = gupnp_didl_lite_resource_get_protocol_info(res);
	if (!res_pi)
		goto done;

	for (i = 0; pi_str_array[i]; ++i) {
		pi = gupnp_protocol_info_new_from_string(pi_str_array[i],
			NULL);
		if (!pi)
			continue;
		match = gupnp_protocol_info_is_compatible(pi, res_pi);
		g_object_unref(pi);
		if (match) {
			retval = res;
			break;
		}
	}
done:

	return retval;
}

static GUPnPDIDLLiteResource *prv_get_matching_resource(
	GUPnPDIDLLiteObject *object, const gchar *protocol_info)
{
	GUPnPDIDLLiteResource *retval = NULL;
	GUPnPDIDLLiteResource *res;
	GList *resources;
	GList *ptr;
	gchar **pi_str_array = NULL;

	if (protocol_info)
		pi_str_array = g_strsplit(protocol_info, ",", 0);

	ptr = resources = gupnp_didl_lite_object_get_resources(object);
	while (ptr) {
		res = ptr->data;
		if (!retval) {
			retval = prv_match_resource(res, pi_str_array);
			if (!retval)
				g_object_unref(res);
		} else
			g_object_unref(res);
		ptr = ptr->next;
	}

	g_list_free(resources);
	if (pi_str_array)
		g_strfreev(pi_str_array);

	return retval;
}

static void prv_parse_resources(GVariantBuilder *item_vb,
				GUPnPDIDLLiteResource *res,
				guint32 filter_mask)
{
	GUPnPProtocolInfo *protocol_info;
	int int_val;
	gint64 int64_val;
	const char *str_val;

	if (filter_mask & MSU_UPNP_MASK_PROP_SIZE) {
		int64_val = gupnp_didl_lite_resource_get_size64(res);
		prv_add_int64_prop(item_vb, MSU_INTERFACE_PROP_SIZE, int64_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_BITRATE) {
		int_val = gupnp_didl_lite_resource_get_bitrate(res);
		prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_BITRATE, int_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_SAMPLE_RATE) {
		int_val = gupnp_didl_lite_resource_get_sample_freq(res);
		prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_SAMPLE_RATE,
				 int_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_BITS_PER_SAMPLE) {
		int_val = gupnp_didl_lite_resource_get_bits_per_sample(res);
		prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_BITS_PER_SAMPLE,
				 int_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_DURATION) {
		int_val = (int) gupnp_didl_lite_resource_get_duration(res);
		prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_DURATION, int_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_WIDTH) {
		int_val = (int) gupnp_didl_lite_resource_get_width(res);
		prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_WIDTH, int_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_HEIGHT) {
		int_val = (int) gupnp_didl_lite_resource_get_height(res);
		prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_HEIGHT, int_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_COLOR_DEPTH) {
		int_val = (int) gupnp_didl_lite_resource_get_color_depth(res);
		prv_add_int_prop(item_vb, MSU_INTERFACE_PROP_COLOR_DEPTH,
				 int_val);
	}

	protocol_info = gupnp_didl_lite_resource_get_protocol_info(res);

	if (filter_mask & MSU_UPNP_MASK_PROP_DLNA_PROFILE) {
		str_val = gupnp_protocol_info_get_dlna_profile(protocol_info);
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_DLNA_PROFILE,
				    str_val);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_MIME_TYPE) {
		str_val = gupnp_protocol_info_get_mime_type(protocol_info);
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_MIME_TYPE,
				    str_val);
	}
}

const gchar *msu_props_media_spec_to_upnp_class(const gchar *m2spec_class)
{
	const gchar *retval = NULL;

	if (!strcmp(m2spec_class, gMediaSpec2Container))
		retval = gUPnPContainer;
	else if (!strcmp(m2spec_class, gMediaSpec2AudioMusic))
		retval = gUPnPMusicTrack;
	else if (!strcmp(m2spec_class, gMediaSpec2Audio))
		retval = gUPnPAudioItem;
	else if (!strcmp(m2spec_class, gMediaSpec2VideoMovie))
		retval = gUPnPMovie;
	else if (!strcmp(m2spec_class, gMediaSpec2Video))
		retval = gUPnPVideoItem;
	else if (!strcmp(m2spec_class, gMediaSpec2ImagePhoto))
		retval = gUPnPPhoto;
	else if (!strcmp(m2spec_class, gMediaSpec2Image))
		retval = gUPnPImageItem;

	return retval;
}

const gchar *msu_props_upnp_class_to_media_spec(const gchar *upnp_class)
{
	const gchar *retval = NULL;
	const gchar *ptr;

	if (!strncmp(upnp_class, gUPnPContainer, gUPnPContainerLen)) {
		ptr = upnp_class + gUPnPContainerLen;
		if (!*ptr || *ptr == '.')
			retval = gMediaSpec2Container;
	} else if (!strncmp(upnp_class, gUPnPAudioItem, gUPnPAudioItemLen)) {
		ptr = upnp_class + gUPnPAudioItemLen;
		if (!strcmp(ptr, ".musicTrack"))
			retval = gMediaSpec2AudioMusic;
		else if (!*ptr)
			retval = gMediaSpec2Audio;
	} else if (!strncmp(upnp_class, gUPnPVideoItem, gUPnPVideoItemLen)) {
		ptr = upnp_class + gUPnPVideoItemLen;
		if (!strcmp(ptr, ".movie"))
			retval = gMediaSpec2VideoMovie;
		else if (!*ptr)
			retval = gMediaSpec2Video;
	}  else if (!strncmp(upnp_class, gUPnPImageItem, gUPnPImageItemLen)) {
		ptr = upnp_class + gUPnPImageItemLen;
		if (!strcmp(ptr, ".photo"))
			retval = gMediaSpec2ImagePhoto;
		else if (!*ptr)
			retval = gMediaSpec2Image;
	}

	return retval;
}

gboolean msu_props_add_object(GVariantBuilder *item_vb,
			      GUPnPDIDLLiteObject *object,
			      const char *root_path,
			      const gchar *parent_path,
			      guint32 filter_mask)
{
	gchar *path = NULL;
	const char *id;
	const char *title;
	const char *upnp_class;
	const char *media_spec_type;
	gboolean retval = FALSE;

	id = gupnp_didl_lite_object_get_id(object);
	if (!id)
		goto on_error;

	upnp_class = gupnp_didl_lite_object_get_upnp_class(object);
	media_spec_type = msu_props_upnp_class_to_media_spec(upnp_class);

	if (!media_spec_type)
		goto on_error;

	title = gupnp_didl_lite_object_get_title(object);
	path = msu_path_from_id(root_path, id);

	if (filter_mask & MSU_UPNP_MASK_PROP_DISPLAY_NAME)
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_DISPLAY_NAME,
				    title);

	if (filter_mask & MSU_UPNP_MASK_PROP_PATH)
		prv_add_path_prop(item_vb, MSU_INTERFACE_PROP_PATH, path);

	if (filter_mask & MSU_UPNP_MASK_PROP_PARENT)
		prv_add_path_prop(item_vb, MSU_INTERFACE_PROP_PARENT,
				  parent_path);

	if (filter_mask & MSU_UPNP_MASK_PROP_TYPE)
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_TYPE,
				    media_spec_type);

	retval = TRUE;

on_error:

	g_free(path);

	return retval;
}

void msu_props_add_container(GVariantBuilder *item_vb,
			     GUPnPDIDLLiteContainer *object,
			     guint32 filter_mask,
			     gboolean *have_child_count)
{
	int child_count;
	gboolean searchable;

	*have_child_count = FALSE;
	if (filter_mask & MSU_UPNP_MASK_PROP_CHILD_COUNT) {
		child_count = gupnp_didl_lite_container_get_child_count(object);
		if (child_count >= 0) {
			prv_add_uint_prop(item_vb,
					  MSU_INTERFACE_PROP_CHILD_COUNT,
					  (unsigned int) child_count);
			*have_child_count = TRUE;
		}
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_SEARCHABLE) {
		searchable = gupnp_didl_lite_container_get_searchable(object);
		prv_add_bool_prop(item_vb, MSU_INTERFACE_PROP_SEARCHABLE,
				  searchable);
	}
}

static GVariant *prv_compute_resources(GUPnPDIDLLiteObject *object,
				       guint32 filter_mask)
{
	GUPnPDIDLLiteResource *res = NULL;
	GList *resources;
	GList *ptr;
	GVariantBuilder *res_array_vb;
	GVariantBuilder *res_vb;
	const char *str_val;
	GVariant *retval;

	res_array_vb = g_variant_builder_new(G_VARIANT_TYPE("aa{sv}"));

	ptr = resources = gupnp_didl_lite_object_get_resources(object);
	while (ptr) {
		res = ptr->data;
		res_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
		if (filter_mask & MSU_UPNP_MASK_PROP_URL) {
			str_val = gupnp_didl_lite_resource_get_uri(res);
			if (str_val)
				prv_add_string_prop(res_vb,
						    MSU_INTERFACE_PROP_URL,
						    str_val);
		}
		prv_parse_resources(res_vb, res, filter_mask);
		g_variant_builder_add(res_array_vb, "@a{sv}",
				      g_variant_builder_end(res_vb));
		g_variant_builder_unref(res_vb);
		ptr = g_list_next(ptr);
	}
	retval = g_variant_builder_end(res_array_vb);
	g_variant_builder_unref(res_array_vb);

	ptr = resources;
	while (ptr) {
		g_object_unref(ptr->data);
		ptr = ptr->next;
	}
	g_list_free(resources);

	return retval;
}

static void prv_add_resources(GVariantBuilder *item_vb,
			      GUPnPDIDLLiteObject *object,
			      guint32 filter_mask)
{
	GVariant *val;

	val = prv_compute_resources(object, filter_mask);
	g_variant_builder_add(item_vb, "{sv}", MSU_INTERFACE_PROP_RESOURCES,
			      val);
}

void msu_props_add_item(GVariantBuilder *item_vb,
			GUPnPDIDLLiteObject *object,
			guint32 filter_mask,
			const gchar *protocol_info)
{
	int track_number;
	GUPnPDIDLLiteResource *res;
	const char *str_val;

	if (filter_mask & MSU_UPNP_MASK_PROP_ARTIST)
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_ARTIST,
				    gupnp_didl_lite_object_get_artist(object));

	if (filter_mask & MSU_UPNP_MASK_PROP_ALBUM)
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_ALBUM,
				    gupnp_didl_lite_object_get_album(object));

	if (filter_mask & MSU_UPNP_MASK_PROP_DATE)
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_DATE,
				    gupnp_didl_lite_object_get_date(object));

	if (filter_mask & MSU_UPNP_MASK_PROP_GENRE)
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_GENRE,
				    gupnp_didl_lite_object_get_genre(object));

	if (filter_mask & MSU_UPNP_MASK_PROP_TRACK_NUMBER) {
		track_number = gupnp_didl_lite_object_get_track_number(object);
		if (track_number >= 0)
			prv_add_int_prop(item_vb,
					 MSU_INTERFACE_PROP_TRACK_NUMBER,
					 track_number);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_ALBUM_ART_URL)
		prv_add_string_prop(item_vb, MSU_INTERFACE_PROP_ALBUM_ART_URL,
				    gupnp_didl_lite_object_get_album_art(
					    object));

	res = prv_get_matching_resource(object, protocol_info);
	if (res) {
		if (filter_mask & MSU_UPNP_MASK_PROP_URLS) {
			str_val = gupnp_didl_lite_resource_get_uri(res);
			if (str_val)
				prv_add_strv_prop(item_vb,
						  MSU_INTERFACE_PROP_URLS,
						  &str_val, 1);
		}
		prv_parse_resources(item_vb, res, filter_mask);
		g_object_unref(res);
	}

	if (filter_mask & MSU_UPNP_MASK_PROP_RESOURCES)
		prv_add_resources(item_vb, object, filter_mask);
}

void msu_props_add_resource(GVariantBuilder *item_vb,
			    GUPnPDIDLLiteObject *object,
			    guint32 filter_mask,
			    const gchar *protocol_info)
{
	GUPnPDIDLLiteResource *res;
	const char *str_val;

	res = prv_get_matching_resource(object, protocol_info);
	if (res) {
		if (filter_mask & MSU_UPNP_MASK_PROP_URL) {
			str_val = gupnp_didl_lite_resource_get_uri(res);
			if (str_val)
				prv_add_string_prop(item_vb,
						    MSU_INTERFACE_PROP_URL,
						    str_val);
		}
		prv_parse_resources(item_vb, res, filter_mask);
		g_object_unref(res);
	}
}


static GVariant *prv_get_resource_property(const gchar *prop,
					   GUPnPDIDLLiteResource *res)
{
	int int_val;
	gint64 int64_val;
	const char *str_val;
	GVariant *retval = NULL;
	GUPnPProtocolInfo *protocol_info;

	if (!strcmp(prop, MSU_INTERFACE_PROP_DLNA_PROFILE)) {
		protocol_info = gupnp_didl_lite_resource_get_protocol_info(res);
		if (!protocol_info)
			goto on_error;
		str_val = gupnp_protocol_info_get_dlna_profile(protocol_info);
		if (!str_val)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_string(str_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_MIME_TYPE)) {
		protocol_info = gupnp_didl_lite_resource_get_protocol_info(res);
		if (!protocol_info)
			goto on_error;
		str_val = gupnp_protocol_info_get_mime_type(protocol_info);
		if (!str_val)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_string(str_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_SIZE)) {
		int64_val = gupnp_didl_lite_resource_get_size64(res);
		if (int64_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int64(int64_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_DURATION)) {
		int_val = (int) gupnp_didl_lite_resource_get_duration(res);
		if (int_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int32(int_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_BITRATE)) {
		int_val = gupnp_didl_lite_resource_get_bitrate(res);
		if (int_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int32(int_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_SAMPLE_RATE)) {
		int_val = gupnp_didl_lite_resource_get_sample_freq(res);
		if (int_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int32(int_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_BITS_PER_SAMPLE)) {
		int_val = gupnp_didl_lite_resource_get_bits_per_sample(res);
		if (int_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int32(int_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_WIDTH)) {
		int_val = (int) gupnp_didl_lite_resource_get_width(res);
		if (int_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int32(int_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_HEIGHT)) {
		int_val = (int) gupnp_didl_lite_resource_get_height(res);
		if (int_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int32(int_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_COLOR_DEPTH)) {
		int_val = (int) gupnp_didl_lite_resource_get_color_depth(res);
		if (int_val == -1)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_int32(int_val));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_URLS)) {
		str_val = gupnp_didl_lite_resource_get_uri(res);
		if (str_val)
			retval = g_variant_new_strv(&str_val, 1);
	}

on_error:

	return retval;
}

GVariant *msu_props_get_object_prop(const gchar *prop, const gchar *root_path,
				    GUPnPDIDLLiteObject *object)
{
	const char *id;
	gchar *path;
	const char *upnp_class;
	const char *media_spec_type;
	const char *title;
	GVariant *retval = NULL;

	if (!strcmp(prop, MSU_INTERFACE_PROP_PARENT)) {
		id = gupnp_didl_lite_object_get_parent_id(object);
		if (!id || !strcmp(id, "-1")) {
			retval = g_variant_ref_sink(g_variant_new_string(
							    root_path));
		} else {
			path = msu_path_from_id(root_path, id);
			retval = g_variant_ref_sink(g_variant_new_string(
							    path));
			g_free(path);
		}
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_PATH)) {
		id = gupnp_didl_lite_object_get_id(object);
		if (!id)
			goto on_error;

		path = msu_path_from_id(root_path, id);
		retval = g_variant_ref_sink(g_variant_new_string(path));
		g_free(path);
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_TYPE)) {
		upnp_class = gupnp_didl_lite_object_get_upnp_class(object);
		media_spec_type =
			msu_props_upnp_class_to_media_spec(upnp_class);
		if (!media_spec_type)
			goto on_error;

		retval = g_variant_ref_sink(g_variant_new_string(
						    media_spec_type));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_DISPLAY_NAME)) {
		title = gupnp_didl_lite_object_get_title(object);
		if (!title)
			goto on_error;

		retval = g_variant_ref_sink(g_variant_new_string(title));
	}

on_error:

	return retval;
}

GVariant *msu_props_get_item_prop(const gchar *prop,
				  GUPnPDIDLLiteObject *object,
				  const gchar *protocol_info)
{
	const gchar *str;
	gint track_number;
	GUPnPDIDLLiteResource *res;
	GVariant *retval = NULL;

	if (GUPNP_IS_DIDL_LITE_CONTAINER(object))
		goto on_error;

	if (!strcmp(prop, MSU_INTERFACE_PROP_ARTIST)) {
		str = gupnp_didl_lite_object_get_artist(object);
		if (!str)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_string(str));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_ALBUM)) {
		str = gupnp_didl_lite_object_get_album(object);
		if (!str)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_string(str));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_DATE)) {
		str = gupnp_didl_lite_object_get_date(object);
		if (!str)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_string(str));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_GENRE)) {
		str = gupnp_didl_lite_object_get_genre(object);
		if (!str)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_string(str));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_TRACK_NUMBER)) {
		track_number = gupnp_didl_lite_object_get_track_number(object);
		if (track_number < 0)
			goto on_error;

		retval = g_variant_ref_sink(
			g_variant_new_int32(track_number));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_ALBUM_ART_URL)) {
		str = gupnp_didl_lite_object_get_album_art(object);
		if (!str)
			goto on_error;
		retval = g_variant_ref_sink(g_variant_new_string(str));
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_RESOURCES)) {
		retval = g_variant_ref_sink(
			prv_compute_resources(object, 0xffffffff));
	} else {
		res = prv_get_matching_resource(object, protocol_info);
		if (!res)
			goto on_error;

		retval = prv_get_resource_property(prop, res);

		g_object_unref(res);
	}

on_error:

	return retval;
}

GVariant *msu_props_get_container_prop(const gchar *prop,
				       GUPnPDIDLLiteObject *object)
{
	gint child_count;
	gboolean searchable;
	GUPnPDIDLLiteContainer *container;
	GVariant *retval = NULL;

	if (!GUPNP_IS_DIDL_LITE_CONTAINER(object))
		goto on_error;

	container = (GUPnPDIDLLiteContainer *) object;
	if (!strcmp(prop, MSU_INTERFACE_PROP_CHILD_COUNT)) {
		child_count =
			gupnp_didl_lite_container_get_child_count(container);
		if (child_count >= 0) {
			retval = g_variant_new_uint32((guint) child_count);
			retval = g_variant_ref_sink(retval);
		}
	} else if (!strcmp(prop, MSU_INTERFACE_PROP_SEARCHABLE)) {
		searchable =
			gupnp_didl_lite_container_get_searchable(container);
		retval = g_variant_ref_sink(
			g_variant_new_boolean(searchable));
	}

on_error:

	return retval;
}
