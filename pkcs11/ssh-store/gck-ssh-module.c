/* 
 * gnome-keyring
 * 
 * Copyright (C) 2008 Stefan Walter
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *  
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#include "config.h"

#include "gck-ssh-module.h"
#include "gck-ssh-private-key.h"
#include "gck-ssh-public-key.h"

#include "gck/gck-file-tracker.h"

#include <string.h>

struct _GckSshModule {
	GckModule parent;
	GckFileTracker *tracker;
	gchar *directory;
	GHashTable *keys_by_path;
};

static const CK_SLOT_INFO gck_ssh_module_slot_info = {
	"SSH Keys",
	"Gnome Keyring",
	CKF_TOKEN_PRESENT,
	{ 0, 0 },
	{ 0, 0 }
};

static const CK_TOKEN_INFO gck_ssh_module_token_info = {
	"SSH Keys",
	"Gnome Keyring",
	"1.0",
	"1", /* Unique serial number for manufacturer */
	CKF_TOKEN_INITIALIZED | CKF_WRITE_PROTECTED | CKF_USER_PIN_INITIALIZED,
	CK_EFFECTIVELY_INFINITE,
	CK_EFFECTIVELY_INFINITE,
	CK_EFFECTIVELY_INFINITE,
	CK_EFFECTIVELY_INFINITE,
	1024,
	1,
	CK_UNAVAILABLE_INFORMATION,
	CK_UNAVAILABLE_INFORMATION,
	CK_UNAVAILABLE_INFORMATION,
	CK_UNAVAILABLE_INFORMATION,
	{ 0, 0 },
	{ 0, 0 },
	""
};


G_DEFINE_TYPE (GckSshModule, gck_ssh_module, GCK_TYPE_MODULE);

/* -----------------------------------------------------------------------------
 * ACTUAL PKCS#11 Module Implementation 
 */

/* Include all the module entry points */
#include "gck/gck-module-ep.h"
GCK_DEFINE_MODULE (gck_ssh_module, GCK_TYPE_SSH_MODULE);

/* -----------------------------------------------------------------------------
 * INTERNAL 
 */

static gchar*
private_path_for_public (const gchar *public_path)
{
	gsize length;
	
	g_assert (public_path);
	
	length = strlen (public_path);
	if (length > 4 && g_str_equal (public_path + (length - 4), ".pub"))
		return g_strndup (public_path, length - 4);
	
	return NULL;
}

static void
file_load (GckFileTracker *tracker, const gchar *path, GckSshModule *self)
{
	GckSshPrivateKey *key;
	GckSshPublicKey *pubkey;
	gchar *private_path;
	GckManager *manager;
	GError *error = NULL;
	gchar *unique;
	
	g_return_if_fail (path);
	g_return_if_fail (GCK_IS_SSH_MODULE (self));
	
	private_path = private_path_for_public (path);
	if (!private_path || !g_file_test (private_path, G_FILE_TEST_IS_REGULAR)) {
		g_message ("no private key present for public key: %s", path);
		g_free (private_path);
		return;
	}
	
	/* Create a key if necessary */
	key = g_hash_table_lookup (self->keys_by_path, path);
	if (key == NULL) {
		unique = g_strdup_printf ("ssh-store:%s", private_path);
		key = gck_ssh_private_key_new (unique);
		g_free (unique);
		
		g_hash_table_replace (self->keys_by_path, g_strdup (path), key);
	}
	
	/* Parse the data into the key */
	if (!gck_ssh_private_key_parse (key, path, private_path, &error)) {
		g_message ("couldn't parse data: %s: %s", path,
		           error && error->message ? error->message : "");
		g_clear_error (&error);
		
	/* When successful register with the object manager */
	} else {
		manager = gck_module_get_manager (GCK_MODULE (self));
		
		/* Make sure the private key has the right manager */
		if (!gck_object_get_manager (GCK_OBJECT (key))) 
			gck_manager_register_object (manager, GCK_OBJECT (key));
		
		pubkey = gck_ssh_private_key_get_public_key (key);
		if (!gck_object_get_manager (GCK_OBJECT (pubkey)))
			gck_manager_register_object (manager, GCK_OBJECT (pubkey));
	}
}

static void
file_remove (GckFileTracker *tracker, const gchar *path, GckSshModule *self)
{
	g_return_if_fail (path);
	g_return_if_fail (GCK_IS_SSH_MODULE (self));
	g_hash_table_remove (self->keys_by_path, path);
}


/* -----------------------------------------------------------------------------
 * OBJECT 
 */

static void 
gck_ssh_module_real_parse_argument (GckModule *base, const gchar *name, const gchar *value)
{
	GckSshModule *self = GCK_SSH_MODULE (base);
	if (g_str_equal (name, "directory")) {
		g_free (self->directory);
		self->directory = g_strdup (value);
	}
}

static CK_RV
gck_ssh_module_real_refresh_token (GckModule *base)
{
	GckSshModule *self = GCK_SSH_MODULE (base);
	gck_file_tracker_refresh (self->tracker, FALSE);
	return CKR_OK;
}

static GObject* 
gck_ssh_module_constructor (GType type, guint n_props, GObjectConstructParam *props) 
{
	GckSshModule *self = GCK_SSH_MODULE (G_OBJECT_CLASS (gck_ssh_module_parent_class)->constructor(type, n_props, props));
	g_return_val_if_fail (self, NULL);	

	if (!self->directory)
		self->directory = g_strdup ("~/.ssh");
	self->tracker = gck_file_tracker_new (self->directory, "*.pub", NULL);
	g_signal_connect (self->tracker, "file-added", G_CALLBACK (file_load), self);
	g_signal_connect (self->tracker, "file-changed", G_CALLBACK (file_load), self);
	g_signal_connect (self->tracker, "file-removed", G_CALLBACK (file_remove), self);
	
	return G_OBJECT (self);
}

static void
gck_ssh_module_init (GckSshModule *self)
{
	self->keys_by_path = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
gck_ssh_module_dispose (GObject *obj)
{
	GckSshModule *self = GCK_SSH_MODULE (obj);
	
	if (self->tracker)
		g_object_unref (self->tracker);
	self->tracker = NULL;
	
	g_hash_table_remove_all (self->keys_by_path);
    
	G_OBJECT_CLASS (gck_ssh_module_parent_class)->dispose (obj);
}

static void
gck_ssh_module_finalize (GObject *obj)
{
	GckSshModule *self = GCK_SSH_MODULE (obj);
	
	g_assert (self->tracker == NULL);
	
	g_hash_table_destroy (self->keys_by_path);
	self->keys_by_path = NULL;
	
	g_free (self->directory);
	self->directory = NULL;

	G_OBJECT_CLASS (gck_ssh_module_parent_class)->finalize (obj);
}

static void
gck_ssh_module_class_init (GckSshModuleClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GckModuleClass *module_class = GCK_MODULE_CLASS (klass);
	
	gobject_class->constructor = gck_ssh_module_constructor;
	gobject_class->dispose = gck_ssh_module_dispose;
	gobject_class->finalize = gck_ssh_module_finalize;
	
	module_class->parse_argument = gck_ssh_module_real_parse_argument;
	module_class->refresh_token = gck_ssh_module_real_refresh_token;
	
	module_class->slot_info = &gck_ssh_module_slot_info;
	module_class->token_info = &gck_ssh_module_token_info;
}

/* ----------------------------------------------------------------------------
 * PUBLIC
 */

CK_FUNCTION_LIST_PTR
gck_ssh_store_get_functions (void)
{
	gck_crypto_initialize ();
	return gck_ssh_module_function_list;
}