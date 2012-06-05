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

#ifndef MSU_PROPS_H__
#define MSU_PROPS_H__

#include <libgupnp-av/gupnp-av.h>

#define MSU_INTERFACE_PROPERTIES "org.freedesktop.DBus.Properties"
#define MSU_INTERFACE_MEDIA_CONTAINER "org.gnome.UPnP.MediaContainer2"
#define MSU_INTERFACE_MEDIA_OBJECT "org.gnome.UPnP.MediaObject2"
#define MSU_INTERFACE_MEDIA_ITEM "org.gnome.UPnP.MediaItem2"

#define MSU_INTERFACE_PROP_PARENT "Parent"
#define MSU_INTERFACE_PROP_TYPE "Type"
#define MSU_INTERFACE_PROP_PATH "Path"
#define MSU_INTERFACE_PROP_DISPLAY_NAME "DisplayName"

#define MSU_INTERFACE_PROP_CHILD_COUNT "ChildCount"
#define MSU_INTERFACE_PROP_SEARCHABLE "Searchable"

#define MSU_INTERFACE_PROP_URLS "URLs"
#define MSU_INTERFACE_PROP_URL "URL"
#define MSU_INTERFACE_PROP_RESOURCES "Resources"
#define MSU_INTERFACE_PROP_MIME_TYPE "MIMEType"
#define MSU_INTERFACE_PROP_ARTIST "Artist"
#define MSU_INTERFACE_PROP_ALBUM "Album"
#define MSU_INTERFACE_PROP_DATE "Date"
#define MSU_INTERFACE_PROP_GENRE "Genre"
#define MSU_INTERFACE_PROP_DLNA_PROFILE "DLNAProfile"
#define MSU_INTERFACE_PROP_TRACK_NUMBER "TrackNumber"
#define MSU_INTERFACE_PROP_ALBUM_ART_URL "AlbumArtURL"
#define MSU_INTERFACE_PROP_ICON_URL "IconURL"

#define MSU_INTERFACE_PROP_SIZE "Size"
#define MSU_INTERFACE_PROP_DURATION "Duration"
#define MSU_INTERFACE_PROP_BITRATE "Bitrate"
#define MSU_INTERFACE_PROP_SAMPLE_RATE "SampleRate"
#define MSU_INTERFACE_PROP_BITS_PER_SAMPLE "BitsPerSample"
#define MSU_INTERFACE_PROP_WIDTH "Width"
#define MSU_INTERFACE_PROP_HEIGHT "Height"
#define MSU_INTERFACE_PROP_COLOR_DEPTH "ColorDepth"

#define MSU_INTERFACE_PROP_LOCATION "Location"
#define MSU_INTERFACE_PROP_UDN "UDN"
#define MSU_INTERFACE_PROP_DEVICE_TYPE "DeviceType"
#define MSU_INTERFACE_PROP_FRIENDLY_NAME "FriendlyName"
#define MSU_INTERFACE_PROP_MANUFACTURER "Manufacturer"
#define MSU_INTERFACE_PROP_MANUFACTURER_URL "ManufacturerUrl"
#define MSU_INTERFACE_PROP_MODEL_DESCRIPTION "ModelDescription"
#define MSU_INTERFACE_PROP_MODEL_NAME "ModelName"
#define MSU_INTERFACE_PROP_MODEL_NUMBER "ModelNumber"
#define MSU_INTERFACE_PROP_MODEL_URL "ModelURL"
#define MSU_INTERFACE_PROP_SERIAL_NUMBER "SerialNumber"
#define MSU_INTERFACE_PROP_PRESENTATION_URL "PresentationURL"

enum msu_upnp_prop_mask_ {
	MSU_UPNP_MASK_PROP_PARENT = 1,
	MSU_UPNP_MASK_PROP_TYPE = 1 << 1,
	MSU_UPNP_MASK_PROP_PATH = 1 << 2,
	MSU_UPNP_MASK_PROP_DISPLAY_NAME = 1 << 3,
	MSU_UPNP_MASK_PROP_CHILD_COUNT = 1 << 4,
	MSU_UPNP_MASK_PROP_SEARCHABLE = 1 << 5,
	MSU_UPNP_MASK_PROP_URLS = 1 << 6,
	MSU_UPNP_MASK_PROP_MIME_TYPE = 1 << 7,
	MSU_UPNP_MASK_PROP_ARTIST = 1 << 8,
	MSU_UPNP_MASK_PROP_ALBUM = 1 << 9,
	MSU_UPNP_MASK_PROP_DATE = 1 << 10,
	MSU_UPNP_MASK_PROP_GENRE = 1 << 11,
	MSU_UPNP_MASK_PROP_DLNA_PROFILE = 1 << 12,
	MSU_UPNP_MASK_PROP_TRACK_NUMBER = 1 << 13,
	MSU_UPNP_MASK_PROP_SIZE = 1 << 14,
	MSU_UPNP_MASK_PROP_DURATION = 1 << 15,
	MSU_UPNP_MASK_PROP_BITRATE = 1 << 16,
	MSU_UPNP_MASK_PROP_SAMPLE_RATE = 1 << 17,
	MSU_UPNP_MASK_PROP_BITS_PER_SAMPLE = 1 << 18,
	MSU_UPNP_MASK_PROP_WIDTH = 1 << 19,
	MSU_UPNP_MASK_PROP_HEIGHT = 1 << 20,
	MSU_UPNP_MASK_PROP_COLOR_DEPTH = 1 << 21,
	MSU_UPNP_MASK_PROP_ALBUM_ART_URL = 1 << 22,
	MSU_UPNP_MASK_PROP_RESOURCES = 1 << 23,
	MSU_UPNP_MASK_PROP_URL = 1 << 24
};
typedef enum msu_upnp_prop_mask_ msu_upnp_prop_mask;

typedef struct msu_prop_map_t_ msu_prop_map_t;
struct msu_prop_map_t_ {
	const gchar *upnp_prop_name;
	msu_upnp_prop_mask type;
	gboolean filter;
	gboolean searchable;
};

GHashTable *msu_prop_maps_new(void);
guint32 msu_props_parse_filter(GHashTable *filter_map, GVariant *filter,
			       gchar **upnp_filter);
void msu_props_add_device(GUPnPDeviceInfo *proxy, GVariantBuilder *vb);
GVariant *msu_props_get_device_prop(GUPnPDeviceInfo *proxy, const gchar *prop);

gboolean msu_props_add_object(GVariantBuilder *item_vb,
			      GUPnPDIDLLiteObject *object,
			      const char *root_path,
			      const gchar *parent_path,
			      guint32 filter_mask);
GVariant *msu_props_get_object_prop(const gchar *prop, const gchar *root_path,
				    GUPnPDIDLLiteObject *object);

void msu_props_add_container(GVariantBuilder *item_vb,
			     GUPnPDIDLLiteContainer *object,
			     guint32 filter_mask,
			     gboolean *have_child_count);

void msu_props_add_child_count(GVariantBuilder *item_vb, gint value);
GVariant *msu_props_get_container_prop(const gchar *prop,
				       GUPnPDIDLLiteObject *object);

void msu_props_add_resource(GVariantBuilder *item_vb,
			    GUPnPDIDLLiteObject *object,
			    guint32 filter_mask,
			    const gchar *protocol_info);
void msu_props_add_item(GVariantBuilder *item_vb,
			GUPnPDIDLLiteObject *object,
			guint32 filter_mask,
			const gchar *protocol_info);
GVariant *msu_props_get_item_prop(const gchar *prop,
				  GUPnPDIDLLiteObject *object,
				  const gchar *protocol_info);

const gchar *msu_props_media_spec_to_upnp_class(const gchar *m2spec_class);
const gchar *msu_props_upnp_class_to_media_spec(const gchar *upnp_class);


#endif
