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

/* Send a synthetic GDK_FOCUS_CHANGE event to the entry.  This triggers the
 * GTK entry's focus_in / focus_out handler, which starts/stops the cursor
 * blink timer and updates the focus-ring state.  We need it because the entry
 * shares the plug's GdkWindow (GtkEntry calls gtk_widget_set_window with its
 * parent's window), so GDK only dispatches GDK_FOCUS_CHANGE to the GtkPlug,
 * never to the entry directly. */
static void send_entry_focus_change(MCSearchField *f, gboolean focus_in)
{
    GdkEvent *ev = gdk_event_new(GDK_FOCUS_CHANGE);
    ev->focus_change.in     = focus_in;
    ev->focus_change.window = gtk_widget_get_window(f->search_entry);
    g_object_ref(ev->focus_change.window);
    gtk_widget_send_focus_change(f->search_entry, ev);
    gdk_event_free(ev);
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

    /* 1. Set X11 keyboard focus to the plug window.
     *
     * The plug lives inside the engine's X window hierarchy (the GtkSocket
     * container is reparented under the engine's stack window).  A direct
     * XSetInputFocus(plug) therefore sends FocusOut with detail=NotifyInferior
     * to the engine's window.  GDK ignores NotifyInferior FocusOut events
     * (it means "focus is still within my subtree"), so the engine's controls
     * never see the focus leave and keep their focused styling.
     *
     * Fix: set focus to None first.  That generates FocusOut(NotifyNonlinear)
     * on the engine's window — which GDK *does* honour — then immediately set
     * focus to the plug.  The "no focus" window between the two requests is
     * processed atomically by the X server before any key event can slip in. */
    GdkDisplay *display = gtk_widget_get_display(f->plug);
    Display    *xdpy    = gdk_x11_display_get_xdisplay(display);
    Window      xwin    = gdk_x11_window_get_xid(gtk_widget_get_window(f->plug));
    XSetInputFocus(xdpy, None, RevertToNone, event->time);
    XSetInputFocus(xdpy, xwin, RevertToParent, event->time);

    /* 2. Give GTK-internal focus to the entry */
    gtk_widget_grab_focus(widget);

    /* 3. Synthesise GDK_FOCUS_CHANGE(in) on the entry immediately so the cursor
     *    appears without waiting for the async FocusIn X event on the plug.
     *    on_plug_focus_in will also fire when FocusIn arrives; the second call
     *    to gtk_entry_focus_in just resets the blink timer, which is harmless. */
    send_entry_focus_change(f, TRUE);

    return FALSE; /* let GtkEntry's default handler position the cursor */
}

/* Called when the plug's window gains X11 focus — either because the user
 * clicked inside the entry (XSetInputFocus called from on_entry_button_press)
 * or because the XEMBED host (GtkSocket) forwarded tab focus to the plug.
 * Give GTK focus to the entry and synthesise focus-in so the cursor appears. */
static gboolean on_plug_focus_in(GtkWidget * /*widget*/, GdkEventFocus * /*event*/,
                                 gpointer user_data)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);
    gtk_widget_grab_focus(f->search_entry);
    send_entry_focus_change(f, TRUE);
    return FALSE;
}

/* Called when the plug's window loses X11 focus.  Synthesise focus-out on the
 * entry so gtk_entry_focus_out() stops the blink timer and clears the cursor
 * and focus-ring styling. */
static gboolean on_plug_focus_out(GtkWidget * /*widget*/, GdkEventFocus * /*event*/,
                                  gpointer user_data)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);
    send_entry_focus_change(f, FALSE);
    return FALSE;
}

/* When Tab / Shift-Tab is pressed, move X11 keyboard focus back to the
 * GtkSocket's window so the XEMBED host can advance its own tab order.
 * Without this the plug retains X11 focus indefinitely after a Tab press:
 * XEMBED_FOCUS_NEXT is sent to the socket but FocusOut never arrives on the
 * plug, so on_plug_focus_out never runs and the entry keeps its focused
 * appearance. */
static gboolean on_entry_key_press(GtkWidget * /*widget*/, GdkEventKey *event,
                                   gpointer user_data)
{
    if (event->keyval != GDK_KEY_Tab && event->keyval != GDK_KEY_ISO_Left_Tab)
        return FALSE;

    MCSearchField *f = reinterpret_cast<MCSearchField *>(user_data);
    GdkWindow *socket_win = gtk_plug_get_socket_window(GTK_PLUG(f->plug));
    if (socket_win != NULL)
    {
        GdkDisplay *display = gtk_widget_get_display(f->plug);
        Display    *xdpy    = gdk_x11_display_get_xdisplay(display);
        Window      xwin    = gdk_x11_window_get_xid(socket_win);
        /* Same NotifyInferior issue in reverse: the socket is inside the engine's
         * window hierarchy, so XSetInputFocus(socket) sends FocusIn(NotifyInferior)
         * to the engine's window, which GDK ignores.  Route through None first so
         * the engine sees FocusOut(NotifyNonlinear) from the plug and clears our
         * focus styling, then give focus to the socket so the engine's tab order
         * takes over. */
        XSetInputFocus(xdpy, None, RevertToNone, event->time);
        XSetInputFocus(xdpy, xwin, RevertToParent, event->time);
    }
    return FALSE; /* let normal Tab handling proceed (sends XEMBED_FOCUS_NEXT) */
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
    g_signal_connect(entry, "key-press-event",   G_CALLBACK(on_entry_key_press),    f);
    g_signal_connect(plug,  "focus-in-event",    G_CALLBACK(on_plug_focus_in),      f);
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
