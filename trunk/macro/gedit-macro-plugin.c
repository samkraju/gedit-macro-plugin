/*
 * gedit-macro-plugin.c
 * 
 * Copyright (C) 2009-2010 - Sam K. Raju
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gedit-macro-plugin.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include <gedit/gedit-debug.h>

#define WINDOW_DATA_KEY "GeditMacroPluginWindowData"

GEDIT_PLUGIN_REGISTER_TYPE(GeditMacroPlugin, gedit_macro_plugin)

typedef enum {
	START_RECORD_MACRO,
	STOP_RECORD_MACRO,
	PLAYBACK_MACRO,
} MacroChoice;

static GSList *macro_list = NULL;
static gboolean is_macro_recording = FALSE;
static GeditPlugin *macro_plugin;

static gboolean snooper (GtkWidget *grab_widget, GdkEventKey *event_key, gpointer func_data)
{
	GdkEvent *event = gdk_event_copy ((GdkEvent *)event_key);
	macro_list = g_slist_append (macro_list, event);
	return FALSE;
}

static guint result;

static void
do_start_record_macro ()
{
	guint list_size = g_slist_length (macro_list);
	if (list_size > 0)
	{
		//TODO: Free existing events
		macro_list = NULL;
	}
	result = gtk_key_snooper_install ((GtkKeySnoopFunc)snooper, 0);
	
	is_macro_recording = TRUE;
}

static void
do_stop_record_macro ()
{
	gtk_key_snooper_remove (result);
	is_macro_recording = FALSE;
}

static void
do_playback_macro ()
{
	int i;
	int list_size = g_slist_length (macro_list);
	for (i=0;i<list_size;i++)
	{
		GdkEvent *event = g_slist_nth_data (macro_list, i);
		gdk_event_put (event);
	}
}

static void
macro (GeditWindow      *window,
             MacroChoice  choice)
{
	GeditDocument *doc;

	gedit_debug (DEBUG_PLUGINS);

	doc = gedit_window_get_active_document (window);
	g_return_if_fail (doc != NULL);

	gtk_text_buffer_begin_user_action (GTK_TEXT_BUFFER (doc));

	switch (choice)
	{
	case START_RECORD_MACRO:
		do_start_record_macro ();
		gedit_plugin_update_ui (macro_plugin, window);
		break;
	case STOP_RECORD_MACRO:
		do_stop_record_macro ();
		gedit_plugin_update_ui (macro_plugin, window);
		break;
	case PLAYBACK_MACRO:
		do_playback_macro ();
		break;
	default:
		g_return_if_reached ();
	}

	gtk_text_buffer_end_user_action (GTK_TEXT_BUFFER (doc));
}

static void
start_record_macro_cb (GtkAction   *action,
               GeditWindow *window)
{
	macro (window, START_RECORD_MACRO);
}

static void
stop_record_macro_cb (GtkAction   *action,
               GeditWindow *window)
{
	macro (window, STOP_RECORD_MACRO);
}

static void
playback_macro_cb (GtkAction   *action,
                GeditWindow *window)
{
	macro (window, PLAYBACK_MACRO);
}

static const GtkActionEntry action_entries[] =
{
	{ "Macro", NULL, N_("_Macro") },
	{ "StartRecordMacro", NULL, N_("Start _Recording"), NULL,
	  N_("Start recording of macro"),
	  G_CALLBACK (start_record_macro_cb) },
	{ "StopRecordMacro", NULL, N_("_Stop Recording"), NULL,
	  N_("Stop recording of macro"),
	  G_CALLBACK (stop_record_macro_cb) },
	{ "PlaybackMacro", NULL, N_("_Playback Macro"), "<Ctrl>m",
	  N_("Playback recorded macro"),
	  G_CALLBACK (playback_macro_cb) }
};

const gchar submenu[] =
"<ui>"
"  <menubar name='MenuBar'>"
"    <menu name='ToolsMenu' action='Tools'>"
"      <placeholder name='ToolsOps_3'>"
"        <menu action='Macro'>"
"          <menuitem action='StartRecordMacro'/>"
"          <menuitem action='StopRecordMacro'/>"
"          <menuitem action='PlaybackMacro'/>"
"        </menu>"
"      </placeholder>"
"    </menu>"
"  </menubar>"
"</ui>";

static void
gedit_macro_plugin_init (GeditMacroPlugin *plugin)
{
	gedit_debug_message (DEBUG_PLUGINS, "GeditMacroPlugin initializing");
}

static void
gedit_macro_plugin_finalize (GObject *object)
{
	G_OBJECT_CLASS (gedit_macro_plugin_parent_class)->finalize (object);

	gedit_debug_message (DEBUG_PLUGINS, "GeditMacroPlugin finalizing");
}

typedef struct
{
	GtkActionGroup *action_group;
	guint           ui_id;
} WindowData;

static void
free_window_data (WindowData *data)
{
	g_return_if_fail (data != NULL);

	g_slice_free (WindowData, data);
}

static void
update_ui_real (GeditWindow  *window,
		WindowData   *data)
{
	GtkAction *record_macro_action, *stop_macro_action, *play_macro_action;

	gedit_debug (DEBUG_PLUGINS);

	record_macro_action = gtk_action_group_get_action (data->action_group, "StartRecordMacro");
	stop_macro_action = gtk_action_group_get_action (data->action_group, "StopRecordMacro");
	play_macro_action = gtk_action_group_get_action (data->action_group, "PlaybackMacro");

	gtk_action_set_sensitive (record_macro_action, !(is_macro_recording));
	gtk_action_set_sensitive (stop_macro_action, is_macro_recording);
	if (!(is_macro_recording) && g_slist_length (macro_list) > 0)
		gtk_action_set_sensitive (play_macro_action, TRUE);
	else
		gtk_action_set_sensitive (play_macro_action, FALSE);
}

static void
impl_activate (GeditPlugin *plugin,
	       GeditWindow *window)
{
	macro_plugin = plugin;
	GtkUIManager *manager;
	WindowData *data;
	GError *error = NULL;

	gedit_debug (DEBUG_PLUGINS);

	data = g_slice_new (WindowData);

	manager = gedit_window_get_ui_manager (window);

	data->action_group = gtk_action_group_new ("GeditMacroPluginActions");
	gtk_action_group_set_translation_domain (data->action_group, 
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (data->action_group,
				      action_entries,
				      G_N_ELEMENTS (action_entries), 
				      window);

	gtk_ui_manager_insert_action_group (manager, data->action_group, -1);

	data->ui_id = gtk_ui_manager_add_ui_from_string (manager,
							 submenu,
							 -1,
							 &error);
	if (data->ui_id == 0)
	{
		g_warning ("%s", error->message);
		free_window_data (data);
		return;
	}

	g_object_set_data_full (G_OBJECT (window), 
				WINDOW_DATA_KEY, 
				data,
				(GDestroyNotify) free_window_data);

	update_ui_real (window, data);
}

static void
impl_deactivate	(GeditPlugin *plugin,
		 GeditWindow *window)
{
	GtkUIManager *manager;
	WindowData *data;

	gedit_debug (DEBUG_PLUGINS);

	manager = gedit_window_get_ui_manager (window);

	data = (WindowData *) g_object_get_data (G_OBJECT (window), WINDOW_DATA_KEY);
	g_return_if_fail (data != NULL);

	gtk_ui_manager_remove_ui (manager, data->ui_id);
	gtk_ui_manager_remove_action_group (manager, data->action_group);

	g_object_set_data (G_OBJECT (window), WINDOW_DATA_KEY, NULL);	
}

static void
impl_update_ui (GeditPlugin *plugin,
		GeditWindow *window)
{
	WindowData *data;

	gedit_debug (DEBUG_PLUGINS);

	data = (WindowData *) g_object_get_data (G_OBJECT (window), WINDOW_DATA_KEY);
	g_return_if_fail (data != NULL);

	update_ui_real (window, data);
}

static void
gedit_macro_plugin_class_init (GeditMacroPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GeditPluginClass *plugin_class = GEDIT_PLUGIN_CLASS (klass);

	object_class->finalize = gedit_macro_plugin_finalize;
	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
	plugin_class->update_ui = impl_update_ui;
}
