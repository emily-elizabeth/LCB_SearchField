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
#include <gtk/gtkx.h>   /* GtkPlug / GtkSocket (XEMBED support) */
#include <gdk/gdkx.h>   /* gdk_x11_display_get_xdisplay, gdk_x11_window_get_xid */
#include <X11/Xlib.h>   /* XSetInputFocus */
#include <string>
#include <cstring>
#include "libsearchfield.h"

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */

struct MCSearchField
{
    GtkWidget *plug;           /* GtkPlug — top-level XEMBED window; its XID is the native layer */
    GtkWidget *search_entry;   /* GtkSearchEntry — child of the plug */

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

static void on_icon_press(GtkEntry *entry, GtkEntryIconPosition pos,
                          GdkEvent * /*event*/, gpointer user_data)
{
    if (pos != GTK_ENTRY_ICON_SECONDARY) return;
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);
    gtk_entry_set_text(entry, "");
    f->text.clear();

    if (f->cancelled_cb)
        f->cancelled_cb(f->cancelled_ctx, f);
}

/* GDK's gdk_window_focus() uses _NET_ACTIVE_WINDOW on modern desktops, which
 * goes through the window manager and is ignored for XEMBED-embedded plug
 * windows.  We call XSetInputFocus directly so the plug gets X11 keyboard
 * focus.
 *
 * Additionally, gtk_entry_check_cursor_blink() (which sets cursor_visible=TRUE
 * and starts the blink timer) is only called from gtk_entry_focus_in(), which
 * is triggered by a GDK_FOCUS_CHANGE event on the ENTRY's GdkWindow.  Because
 * the entry's widget window IS the plug window (GtkEntry uses its parent's
 * window), FocusIn arrives at the GtkPlug, not at the entry — so the entry
 * never gets a GDK_FOCUS_CHANGE and the cursor stays invisible.  We synthesise
 * a focus-change event on the entry after XSetInputFocus to fix this. */
static gboolean on_entry_button_press(GtkWidget *widget, GdkEventButton *event,
                                      gpointer user_data)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);

    /* 1. Set X11 keyboard focus directly to the plug window */
    GdkDisplay *display = gtk_widget_get_display(f->plug);
    Display    *xdpy    = gdk_x11_display_get_xdisplay(display);
    Window      xwin    = gdk_x11_window_get_xid(gtk_widget_get_window(f->plug));
    XSetInputFocus(xdpy, xwin, RevertToParent, event->time);

    /* 2. Give GTK-internal focus to the entry */
    gtk_widget_grab_focus(widget);

    /* 3. Synthesise GDK_FOCUS_CHANGE on the entry so gtk_entry_focus_in()
     *    runs, starts the cursor blink timer, and makes the cursor visible. */
    GdkEvent *ev = gdk_event_new(GDK_FOCUS_CHANGE);
    ev->focus_change.in     = TRUE;
    ev->focus_change.window = gtk_widget_get_window(widget);
    g_object_ref(ev->focus_change.window);
    gtk_widget_send_focus_change(widget, ev);
    gdk_event_free(ev);

    return FALSE; /* let GtkEntry's default handler position the cursor */
}

/* Mirrors on_entry_button_press: when the plug loses X11 focus (FocusOut on
 * the plug's window), synthesise GDK_FOCUS_CHANGE(out) on the entry so
 * gtk_entry_focus_out() runs — stopping the blink timer and clearing the
 * cursor and focus-ring styling.  Without this the entry keeps its focused
 * appearance even after another window takes focus. */
static gboolean on_plug_focus_out(GtkWidget * /*widget*/, GdkEventFocus * /*event*/,
                                  gpointer user_data)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);

    GdkEvent *ev = gdk_event_new(GDK_FOCUS_CHANGE);
    ev->focus_change.in     = FALSE;
    ev->focus_change.window = gtk_widget_get_window(f->search_entry);
    g_object_ref(ev->focus_change.window);
    gtk_widget_send_focus_change(f->search_entry, ev);
    gdk_event_free(ev);

    return FALSE;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool MCSearchFieldCreate(void * /*p_parent_view*/, MCSearchFieldRef *r_field)
{
    MCSearchField *f = new MCSearchField{};
    f->enabled     = true;
    f->show_cancel = true;

    /* The HyperXTalk Linux engine embeds native layers via X11 XEMBED
     * (GtkSocket + GtkPlug). It expects the native layer value to be an XID.
     * We create a GtkPlug (socket_id=0 → standalone until the engine embeds
     * it), put the GtkSearchEntry inside it, and return the plug's XID from
     * MCSearchFieldGetNativeLayer. The engine's GtkSocket then calls
     * gtk_socket_add_id(socket, xid) to embed it. */
    GtkWidget *plug  = gtk_plug_new(0);
    GtkWidget *entry = gtk_search_entry_new();

    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry),
                                      GTK_ENTRY_ICON_SECONDARY,
                                      "edit-clear-symbolic");

    gtk_container_add(GTK_CONTAINER(plug), entry);

    g_signal_connect(entry, "search-changed",    G_CALLBACK(on_search_changed),     f);
    g_signal_connect(entry, "activate",          G_CALLBACK(on_activate),           f);
    g_signal_connect(entry, "stop-search",       G_CALLBACK(on_stop_search),        f);
    g_signal_connect(entry, "icon-press",        G_CALLBACK(on_icon_press),         f);
    g_signal_connect(entry, "button-press-event",G_CALLBACK(on_entry_button_press), f);
    g_signal_connect(plug,  "focus-out-event",   G_CALLBACK(on_plug_focus_out),     f);

    /* Show the plug (and all its children) before the engine's GtkSocket embeds
     * it. gtk_widget_show sets XEMBED_MAPPED in the plug's _XEMBED_INFO X
     * property. gtk_socket_add_window reads that property to decide whether to
     * map the plug after reparenting it into the socket. Without XEMBED_MAPPED
     * the socket embeds but never maps the plug, so it stays invisible.
     *
     * The plug is briefly visible as a standalone window here, but
     * gtk_socket_add_id (called synchronously by the engine's doAttach) hides
     * it, reparents it into the socket, then re-shows it there — so in
     * practice no visual artifact is produced. */
    gtk_widget_show_all(plug);

    guint64 xid = (guint64)gtk_plug_get_id(GTK_PLUG(plug));
    fprintf(stderr, "[LCSF] MCSearchFieldCreate: plug=%p realized=%d XID=%lu gdk_window=%p\n",
            (void*)plug,
            (int)gtk_widget_get_realized(plug),
            (unsigned long)xid,
            (void*)gtk_widget_get_window(plug));

    f->plug         = plug;
    f->search_entry = entry;
    *r_field        = f;
    return true;
}

void MCSearchFieldDestroy(MCSearchFieldRef p_field)
{
    if (!p_field) return;
    gtk_widget_destroy(p_field->plug);
    delete p_field;
}

void *MCSearchFieldGetNativeLayer(MCSearchFieldRef p_field)
{
    /* Return the XID of the GtkPlug — this is what the engine passes to
     * gtk_socket_add_id() when embedding via XEMBED. */
    guint64 xid = (guint64)gtk_plug_get_id(GTK_PLUG(p_field->plug));
    fprintf(stderr, "[LCSF] MCSearchFieldGetNativeLayer: XID=%lu realized=%d\n",
            (unsigned long)xid,
            (int)gtk_widget_get_realized(p_field->plug));
    return reinterpret_cast<void *>(xid);
}

void MCSearchFieldSetFrame(MCSearchFieldRef p_field,
                           int32_t /*p_x*/, int32_t /*p_y*/,
                           int32_t p_width, int32_t p_height)
{
    /* Positioning is managed by the engine's GtkSocket. We only set the
     * requested size so GTK knows how large to render the entry. */
    gtk_widget_set_size_request(p_field->search_entry, p_width, p_height);
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
    gtk_widget_set_sensitive(p_field->plug, p_enabled);
}

bool MCSearchFieldGetShowCancelButton(MCSearchFieldRef p_field)
{
    return p_field->show_cancel;
}

void MCSearchFieldSetShowCancelButton(MCSearchFieldRef p_field, bool p_show)
{
    p_field->show_cancel = p_show;
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(p_field->search_entry),
                                      GTK_ENTRY_ICON_SECONDARY,
                                      p_show ? "edit-clear-symbolic" : nullptr);
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
