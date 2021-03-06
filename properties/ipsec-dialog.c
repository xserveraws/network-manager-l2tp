/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/***************************************************************************
 *
 * Copyright (C) 2011 Geo Carncross, <geocar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#ifdef NM_VPN_OLD
#define NM_VPN_LIBNM_COMPAT
#include <nm-connection.h>
#include <nm-setting-vpn.h>

#else /* !NM_VPN_OLD */

#include <NetworkManager.h>
#endif

#include "ipsec-dialog.h"
#include "nm-default.h"
#include "nm-l2tp-editor.h"
#include "nm-service-defines.h"

#include "nm-utils/nm-shared-utils.h"

static const char *ipsec_keys[] = {
	NM_L2TP_KEY_IPSEC_ENABLE,
	NM_L2TP_KEY_IPSEC_GATEWAY_ID,
	NM_L2TP_KEY_IPSEC_GROUP_NAME,
	NM_L2TP_KEY_IPSEC_AUTH_TYPE,
	NM_L2TP_KEY_IPSEC_PSK,
	NM_L2TP_KEY_IPSEC_CA,
	NM_L2TP_KEY_IPSEC_CERT,
	NM_L2TP_KEY_IPSEC_KEY,
	NM_L2TP_KEY_IPSEC_CERTPASS,
	NM_L2TP_KEY_IPSEC_IKE,
	NM_L2TP_KEY_IPSEC_ESP,
	NM_L2TP_KEY_IPSEC_FORCEENCAPS,
	NULL
};

static void
hash_copy_value (const char *key, const char *value, gpointer user_data)
{
	GHashTable *hash = (GHashTable *) user_data;
	const char **i;

	for (i = &ipsec_keys[0]; *i; i++) {
		if (strcmp (key, *i))
			continue;
		g_hash_table_insert (hash, g_strdup (key), g_strdup (value));
	}
}

GHashTable *
ipsec_dialog_new_hash_from_connection (NMConnection *connection,
                                          GError **error)
{
	GHashTable *hash;
	NMSettingVpn *s_vpn;
	const char *secret, *flags;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	s_vpn = nm_connection_get_setting_vpn (connection);
	nm_setting_vpn_foreach_data_item (s_vpn, hash_copy_value, hash);

	/* IPsec certificate password is special */
	secret = nm_setting_vpn_get_secret (s_vpn, NM_L2TP_KEY_IPSEC_CERTPASS);
	if (secret) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_CERTPASS),
		                     g_strdup (secret));
	}

	flags = nm_setting_vpn_get_data_item (s_vpn, NM_L2TP_KEY_IPSEC_CERTPASS"-flags");
	if (flags)
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_CERTPASS"-flags"),
		                     g_strdup (flags));

	return hash;
}

static void
ipsec_auth_combo_changed_cb (GtkWidget *combo, gpointer user_data)
{
        GtkBuilder *builder = GTK_BUILDER (user_data);
        GtkWidget *widget;
        GtkTreeModel *model;
        GtkTreeIter iter;
        gint new_page = 0;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
        g_assert (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter));
        gtk_tree_model_get (model, &iter, COL_AUTH_PAGE, &new_page, -1);

        widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_auth_notebook"));
        gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), new_page);

		if (new_page == 0) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_tls_cert"));
			gtk_widget_hide (widget);
		} else {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_tls_cert"));
			gtk_widget_show (widget);
		}
}

static void
enable_toggled_cb (GtkWidget *check, gpointer user_data)
{
	GtkBuilder *builder = (GtkBuilder *) user_data;
	gboolean sensitive;
	GtkWidget *widget;
	guint32 i = 0;
	const char *widgets[] = {
		"general_label", "ipsec_gateway_id_label", "ipsec_gateway_id",
		"authentication_label", "ipsec_auth_type_label", "ipsec_auth_combo",
		"show_psk_checkbutton", "psk_label", "ipsec_psk_entry", "advanced_label",
		NULL
	};

	sensitive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

	while (widgets[i]) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, widgets[i++]));
		gtk_widget_set_sensitive (widget, sensitive);
	}

	if (!sensitive) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_auth_combo"));
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
		ipsec_auth_combo_changed_cb (widget, builder);

		widget = GTK_WIDGET (gtk_builder_get_object (builder, "show_psk_checkbutton"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);

		widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_psk_entry"));
		gtk_entry_set_visibility (GTK_ENTRY (widget), FALSE);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "advanced_expander"));
	if (!sensitive)
		gtk_expander_set_expanded (GTK_EXPANDER (widget), FALSE);
	gtk_widget_set_sensitive (widget, sensitive);
}

static void
ipsec_tls_cert_changed_cb (NMACertChooser *this, gpointer user_data)
{
	NMACertChooser *other = user_data;
	NMSetting8021xCKScheme scheme;
	gs_free char *this_cert = NULL;
	gs_free char *other_cert = NULL;
	gs_free char *this_key = NULL;
	gs_free char *other_key = NULL;

	other_key = nma_cert_chooser_get_key (other, &scheme);
	this_key = nma_cert_chooser_get_key (this, &scheme);
	other_cert = nma_cert_chooser_get_cert (other, &scheme);
	this_cert = nma_cert_chooser_get_cert (this, &scheme);
	if (   scheme == NM_SETTING_802_1X_CK_SCHEME_PATH
	    && nm_utils_file_is_pkcs12(this_cert)) {
		if (!this_key)
			nma_cert_chooser_set_key (this, this_cert, NM_SETTING_802_1X_CK_SCHEME_PATH);
		if (!other_cert) {
			nma_cert_chooser_set_cert (other, this_cert, NM_SETTING_802_1X_CK_SCHEME_PATH);
			if (!other_key)
				nma_cert_chooser_set_key (other, this_cert, NM_SETTING_802_1X_CK_SCHEME_PATH);
		}
	}
}

static void
ipsec_tls_setup (GtkBuilder *builder, GHashTable *hash)
{
	NMACertChooser *ca_cert;
	NMACertChooser *cert;
	NMSettingSecretFlags pw_flags;
	const char *value;

	ca_cert = NMA_CERT_CHOOSER (gtk_builder_get_object (builder, "ipsec_tls_ca_cert"));
	cert = NMA_CERT_CHOOSER (gtk_builder_get_object (builder, "ipsec_tls_cert"));

	nma_cert_chooser_add_to_size_group (ca_cert, GTK_SIZE_GROUP (gtk_builder_get_object (builder, "ipsec_labels")));
	nma_cert_chooser_add_to_size_group (cert, GTK_SIZE_GROUP (gtk_builder_get_object (builder, "ipsec_labels")));

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_CA);
	if (value && value[0])
		nma_cert_chooser_set_cert (ca_cert, value, NM_SETTING_802_1X_CK_SCHEME_PATH);

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_CERT);
	if (value && value[0])
		nma_cert_chooser_set_cert (cert, value, NM_SETTING_802_1X_CK_SCHEME_PATH);

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_KEY);
	if (value && value[0])
		nma_cert_chooser_set_key (cert, value, NM_SETTING_802_1X_CK_SCHEME_PATH);

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_CERTPASS);
	if (value && value[0])
		nma_cert_chooser_set_key_password (cert, value);

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_CERTPASS"-flags");
	if (value) {
		G_STATIC_ASSERT_EXPR (((guint) (NMSettingSecretFlags) 0xFFFFu) == 0xFFFFu);
		pw_flags = _nm_utils_ascii_str_to_int64 (value, 10, 0, 0xFFFF, NM_SETTING_SECRET_FLAG_NONE);
	} else {
		pw_flags = NM_SETTING_SECRET_FLAG_NONE;
	}
	nma_cert_chooser_setup_key_password_storage (cert, pw_flags, NULL,
	                                             NM_L2TP_KEY_IPSEC_CERTPASS, TRUE, FALSE);

	/* Link to the PKCS#12 changer callback */
	g_signal_connect_object (ca_cert, "changed", G_CALLBACK (ipsec_tls_cert_changed_cb), cert, 0);
	g_signal_connect_object (cert, "changed", G_CALLBACK (ipsec_tls_cert_changed_cb), ca_cert, 0);
}

static void
show_psk_toggled_cb (GtkCheckButton *button, GtkEntry *psk_entry)
{
	gboolean visible;

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (psk_entry), visible);
}

static void
ipsec_psk_setup (GtkBuilder *builder, GHashTable *hash)
{
	GtkWidget *psk_entry_widget;
	GtkWidget *checkbutton_widget;
	const char *value;

	checkbutton_widget = GTK_WIDGET (gtk_builder_get_object (builder,  "show_psk_checkbutton"));
	psk_entry_widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_psk_entry"));

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_PSK);
	if (value && value[0])
		gtk_entry_set_text (GTK_ENTRY (psk_entry_widget), value);

	g_signal_connect (checkbutton_widget, "toggled", G_CALLBACK (show_psk_toggled_cb), psk_entry_widget);
}

GtkWidget *
ipsec_dialog_new (GHashTable *hash)
{
	GtkBuilder *builder;
	GtkWidget *dialog = NULL;
	GtkWidget *widget;
	GtkListStore *store;
	GtkTreeIter iter;
	int active = -1;
	const char *value;
	GError *error = NULL;
	const char *authtype = NM_L2TP_AUTHTYPE_PASSWORD;

	g_return_val_if_fail (hash != NULL, NULL);

	builder = gtk_builder_new ();

	if (!gtk_builder_add_from_resource (builder, "/org/freedesktop/network-manager-l2tp/nm-l2tp-dialog.ui", &error)) {
		g_warning ("Couldn't load builder file: %s", error ? error->message
		           : "(unknown)");
		g_clear_error (&error);
		g_object_unref(G_OBJECT(builder));
		return NULL;
	}
	gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);

	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "l2tp-ipsec-dialog"));
	if (!dialog) {
		g_object_unref (G_OBJECT (builder));
		return NULL;
	}
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	g_object_set_data_full (G_OBJECT (dialog), "gtkbuilder-xml",
			builder, (GDestroyNotify) g_object_unref);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_gateway_id"));
	if((value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_GATEWAY_ID)))
		gtk_entry_set_text (GTK_ENTRY(widget), value);

	authtype = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_AUTH_TYPE);
	if (authtype) {
		if (   strcmp (authtype, NM_L2TP_AUTHTYPE_TLS)
		    && strcmp (authtype, NM_L2TP_AUTHTYPE_PSK))
			authtype = NM_L2TP_AUTHTYPE_PSK;
	} else {
		authtype = NM_L2TP_AUTHTYPE_PSK;
	}
	g_object_set_data (G_OBJECT (dialog), "auth-type", GINT_TO_POINTER (authtype));

	store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);

	/* PSK auth widget */
	ipsec_psk_setup (builder, hash);
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
	                    COL_AUTH_NAME, _("Pre-shared key (PSK)"),
	                    COL_AUTH_PAGE, 0,
	                    COL_AUTH_TYPE, NM_L2TP_AUTHTYPE_PSK,
	                    -1);

	/* TLS auth widget */
	ipsec_tls_setup (builder, hash);
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
	                    COL_AUTH_NAME, _("Certificates (TLS)"),
	                    COL_AUTH_PAGE, 1,
	                    COL_AUTH_TYPE, NM_L2TP_AUTHTYPE_TLS,
	                    -1);

	if ((active < 0) && !strcmp (authtype, NM_L2TP_AUTHTYPE_TLS))
		active = 1;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_auth_combo"));
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (store));
	g_object_unref (store);

	g_signal_connect (widget, "changed", G_CALLBACK (ipsec_auth_combo_changed_cb), builder);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), active < 0 ? 0 : active);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_psk_entry"));
	if((value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_PSK)))
		gtk_entry_set_text(GTK_ENTRY(widget), value);

	/* Phase 1 Algorithms: IKE */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_phase1"));
	if((value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_IKE))) {
		gtk_entry_set_text (GTK_ENTRY(widget), value);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "advanced_expander"));
		gtk_expander_set_expanded (GTK_EXPANDER (widget), TRUE);
	}

	/* Phase 2 Algorithms: ESP */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_phase2"));
	if((value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_ESP)))
		gtk_entry_set_text (GTK_ENTRY(widget), value);

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_FORCEENCAPS);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "forceencaps_enable"));
	if (value && !strcmp (value, "yes")) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
	} else {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
	}

	value = g_hash_table_lookup (hash, NM_L2TP_KEY_IPSEC_ENABLE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_enable"));
	if (value && !strcmp (value, "yes")) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
	}
	enable_toggled_cb (widget, builder);
	g_signal_connect (G_OBJECT (widget), "toggled", G_CALLBACK (enable_toggled_cb), builder);

	return dialog;
}

GHashTable *
ipsec_dialog_new_hash_from_dialog (GtkWidget *dialog, GError **error)
{
	NMSetting8021xCKScheme scheme;
	GHashTable *hash;
	GtkWidget *widget;
	GtkBuilder *builder;
	const gchar *value;
	GtkTreeModel *model;
	GtkTreeIter iter;
	guint32 pw_flags;

	g_return_val_if_fail (dialog != NULL, NULL);
	if (error)
		g_return_val_if_fail (*error == NULL, NULL);

	builder = g_object_get_data (G_OBJECT (dialog), "gtkbuilder-xml");
	g_return_val_if_fail (builder != NULL, NULL);

	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_enable"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		g_hash_table_insert(hash, g_strdup(NM_L2TP_KEY_IPSEC_ENABLE), g_strdup("yes"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_gateway_id"));
	value = gtk_entry_get_text(GTK_ENTRY(widget));
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_GATEWAY_ID),
		                     g_strdup (value));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_auth_combo"));
	model = gtk_combo_box_get_model (GTK_COMBO_BOX(widget));
	value = NULL;
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX(widget), &iter)) {
		gtk_tree_model_get (model, &iter, COL_AUTH_TYPE, &value, -1);
	}
	if (value) {
		g_hash_table_insert(hash, g_strdup(NM_L2TP_KEY_IPSEC_AUTH_TYPE), g_strdup(value));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_psk_entry"));
	value = gtk_entry_get_text(GTK_ENTRY(widget));
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_PSK),
		                     g_strdup (value));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_tls_ca_cert"));
	value = nma_cert_chooser_get_cert (NMA_CERT_CHOOSER (widget), &scheme);
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_CA),
		                     g_strdup (value));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_tls_cert"));
	value = nma_cert_chooser_get_cert (NMA_CERT_CHOOSER (widget), &scheme);
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_CERT),
		                     g_strdup (value));
	}

	value = nma_cert_chooser_get_key (NMA_CERT_CHOOSER (widget), &scheme);
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_KEY),
		                     g_strdup (value));
	}

	value = nma_cert_chooser_get_key_password (NMA_CERT_CHOOSER (widget));
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_CERTPASS),
		                     g_strdup (value));
	}

	pw_flags = nma_cert_chooser_get_key_password_flags (NMA_CERT_CHOOSER (widget));
	if (pw_flags != NM_SETTING_SECRET_FLAG_NONE) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_CERTPASS"-flags"),
		                     g_strdup_printf ("%d", pw_flags));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_phase1"));
	value = gtk_entry_get_text(GTK_ENTRY(widget));
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_IKE),
		                     g_strdup (value));
	}


	widget = GTK_WIDGET (gtk_builder_get_object (builder, "ipsec_phase2"));
	value = gtk_entry_get_text(GTK_ENTRY(widget));
	if (value && *value) {
		g_hash_table_insert (hash,
		                     g_strdup (NM_L2TP_KEY_IPSEC_ESP),
		                     g_strdup (value));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "forceencaps_enable"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		g_hash_table_insert(hash, g_strdup(NM_L2TP_KEY_IPSEC_FORCEENCAPS), g_strdup("yes"));


	return hash;
}

