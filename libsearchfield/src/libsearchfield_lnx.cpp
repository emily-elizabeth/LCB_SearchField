/*
Copyright (C) 2024 LiveCode Ltd.

This file is part of LCB_SearchField.

LCB_SearchField is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License v3 as
published by the Free Software Foundation.

LCB_SearchField is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with LCB_SearchField. If not, see <http://www.gnu.org/licenses/>.
*/

#include <gtk/gtk.h>
#include <string>
#include <cstring>
#include "libsearchfield.h"

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */

struct MCSearchField
{
    GtkWidget *search_entry;   /* GtkSearchEntry */
    GtkWidget *search_bar;     /* GtkSearchBar container */

    std::string text;
    std::string placeholder;
    bool        enabled;
    bool        show_cancel;

    MCSearchFieldTextChangedCallback      text_changed_cb;
    void                                 *text_changed_ctx;

    MCSearchFieldSearchSubmittedCallback  submitted_cb;
    void                                 *submitted_ctx;

    MCSearchFieldSearchCancelledCallback  cancelled_cb;
    void                                 *cancelled_ctx;
};

/* -------------------------------------------------------------------------
 * Signal handlers
 * ---------------------------------------------------------------------- */

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    f->text = text ? text : "";

    if (f->text_changed_cb)
        f->text_changed_cb(f->text_changed_ctx, f, f->text.c_str());
}

static void on_activate(GtkEntry *entry, gpointer user_data)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);
    const char *text = gtk_entry_get_text(entry);
    f->text = text ? text : "";

    if (f->submitted_cb)
        f->submitted_cb(f->submitted_ctx, f, f->text.c_str());
}

static void on_stop_search(GtkSearchEntry *entry, gpointer user_data)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    f->text.clear();

    if (f->cancelled_cb)
        f->cancelled_cb(f->cancelled_ctx, f);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool MCSearchFieldCreate(void *p_parent_view, MCSearchFieldRef *r_field)
{
    MCSearchField *f = new MCSearchField{};
    f->enabled      = true;
    f->show_cancel  = true;

    GtkWidget *search_entry = gtk_search_entry_new();
    GtkWidget *search_bar   = gtk_search_bar_new();

    gtk_search_bar_connect_entry(GTK_SEARCH_BAR(search_bar),
                                 GTK_ENTRY(search_entry));
    gtk_container_add(GTK_CONTAINER(search_bar), search_entry);
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(search_bar), TRUE);
    gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(search_bar), TRUE);

    g_signal_connect(search_entry, "search-changed",
                     G_CALLBACK(on_search_changed), f);
    g_signal_connect(search_entry, "activate",
                     G_CALLBACK(on_activate), f);
    g_signal_connect(search_entry, "stop-search",
                     G_CALLBACK(on_stop_search), f);

    /* Do NOT add to p_parent_view here. The engine embeds the widget when the
     * LCB widget calls "set my native layer to MCSearchFieldGetNativeLayer(...)".
     * Calling gtk_container_add on the parent pointer at this stage crashes
     * because it is not yet a valid GtkContainer in the widget lifecycle. */
    gtk_widget_show_all(search_bar);

    f->search_entry = search_entry;
    f->search_bar   = search_bar;
    *r_field        = f;
    return true;
}

void MCSearchFieldDestroy(MCSearchFieldRef p_field)
{
    if (!p_field) return;
    gtk_widget_destroy(p_field->search_bar);
    delete p_field;
}

void *MCSearchFieldGetNativeLayer(MCSearchFieldRef p_field)
{
    return reinterpret_cast<void *>(p_field->search_bar);
}

void MCSearchFieldSetFrame(MCSearchFieldRef p_field,
                           int32_t p_x, int32_t p_y,
                           int32_t p_width, int32_t p_height)
{
    /* GTK layout is typically managed by a container; for fixed containers: */
    GtkWidget *parent = gtk_widget_get_parent(p_field->search_bar);
    if (parent && GTK_IS_FIXED(parent))
    {
        gtk_fixed_move(GTK_FIXED(parent), p_field->search_bar, p_x, p_y);
    }
    gtk_widget_set_size_request(p_field->search_bar, p_width, p_height);
}

const char *MCSearchFieldGetText(MCSearchFieldRef p_field)
{
    const char *t = gtk_entry_get_text(GTK_ENTRY(p_field->search_entry));
    p_field->text = t ? t : "";
    return p_field->text.c_str();
}

void MCSearchFieldSetText(MCSearchFieldRef p_field, const char *p_text)
{
    p_field->text = p_text ? p_text : "";
    gtk_entry_set_text(GTK_ENTRY(p_field->search_entry), p_field->text.c_str());
}

const char *MCSearchFieldGetPlaceholderText(MCSearchFieldRef p_field)
{
    return p_field->placeholder.c_str();
}

void MCSearchFieldSetPlaceholderText(MCSearchFieldRef p_field,
                                     const char *p_placeholder)
{
    p_field->placeholder = p_placeholder ? p_placeholder : "";
    gtk_entry_set_placeholder_text(GTK_ENTRY(p_field->search_entry),
                                   p_field->placeholder.c_str());
}

bool MCSearchFieldGetEnabled(MCSearchFieldRef p_field)
{
    return p_field->enabled;
}

void MCSearchFieldSetEnabled(MCSearchFieldRef p_field, bool p_enabled)
{
    p_field->enabled = p_enabled;
    gtk_widget_set_sensitive(p_field->search_bar, p_enabled);
}

bool MCSearchFieldGetShowCancelButton(MCSearchFieldRef p_field)
{
    return p_field->show_cancel;
}

void MCSearchFieldSetShowCancelButton(MCSearchFieldRef p_field, bool p_show)
{
    p_field->show_cancel = p_show;
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(p_field->search_bar),
                                         p_show ? TRUE : FALSE);
}

void MCSearchFieldSetTextChangedCallback(MCSearchFieldRef p_field,
                                         MCSearchFieldTextChangedCallback p_cb,
                                         void *p_ctx)
{
    p_field->text_changed_cb  = p_cb;
    p_field->text_changed_ctx = p_ctx;
}

void MCSearchFieldSetSearchSubmittedCallback(MCSearchFieldRef p_field,
                                             MCSearchFieldSearchSubmittedCallback p_cb,
                                             void *p_ctx)
{
    p_field->submitted_cb  = p_cb;
    p_field->submitted_ctx = p_ctx;
}

void MCSearchFieldSetSearchCancelledCallback(MCSearchFieldRef p_field,
                                             MCSearchFieldSearchCancelledCallback p_cb,
                                             void *p_ctx)
{
    p_field->cancelled_cb  = p_cb;
    p_field->cancelled_ctx = p_ctx;
}
