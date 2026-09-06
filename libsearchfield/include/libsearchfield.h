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

#ifndef LIBSEARCHFIELD_H
#define LIBSEARCHFIELD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to a native search field instance. */
typedef struct MCSearchField *MCSearchFieldRef;

/* -------------------------------------------------------------------------
 * Callback types
 * ---------------------------------------------------------------------- */

/* Called when the search text changes (on each keystroke). */
typedef void (*MCSearchFieldTextChangedCallback)(void *p_context,
                                                 MCSearchFieldRef p_field,
                                                 const char *p_text);

/* Called when the user submits a search (Return key / search button). */
typedef void (*MCSearchFieldSearchSubmittedCallback)(void *p_context,
                                                     MCSearchFieldRef p_field,
                                                     const char *p_text);

/* Called when the user cancels / clears the search field. */
typedef void (*MCSearchFieldSearchCancelledCallback)(void *p_context,
                                                     MCSearchFieldRef p_field);

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/* Create a native search field as a child of p_parent_view.
 * p_parent_view is a platform-native view/window handle:
 *   Mac     – NSView *
 *   Windows – HWND
 *   Linux   – GtkWidget * (a GtkBox or GtkWindow)
 *
 * Returns true on success; r_field is set to the new instance. */
bool MCSearchFieldCreate(void *p_parent_view, MCSearchFieldRef *r_field);

/* Destroy a search field and release all associated resources. */
void MCSearchFieldDestroy(MCSearchFieldRef p_field);

/* -------------------------------------------------------------------------
 * Native layer access
 * ---------------------------------------------------------------------- */

/* Returns the platform-native view handle for use with
 * set my native layer to in LCB:
 *   Mac     – NSView *
 *   Windows – HWND
 *   Linux   – GtkWidget *                                               */
void *MCSearchFieldGetNativeLayer(MCSearchFieldRef p_field);

/* -------------------------------------------------------------------------
 * Geometry
 * ---------------------------------------------------------------------- */

/* Set the position and size of the search field in its parent's coordinate
 * space (top-left origin, pixels). */
void MCSearchFieldSetFrame(MCSearchFieldRef p_field,
                           int32_t p_x, int32_t p_y,
                           int32_t p_width, int32_t p_height);

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */

/* Get/set the current text content of the field.
 * The returned string from Get is valid until the next call into
 * libsearchfield on this field; callers that need to keep it must copy it. */
const char *MCSearchFieldGetText(MCSearchFieldRef p_field);
void        MCSearchFieldSetText(MCSearchFieldRef p_field, const char *p_text);

/* Placeholder text shown when the field is empty. */
const char *MCSearchFieldGetPlaceholderText(MCSearchFieldRef p_field);
void        MCSearchFieldSetPlaceholderText(MCSearchFieldRef p_field,
                                            const char *p_placeholder);

/* Whether the field and its controls are interactive. */
bool MCSearchFieldGetEnabled(MCSearchFieldRef p_field);
void MCSearchFieldSetEnabled(MCSearchFieldRef p_field, bool p_enabled);

/* Whether the platform cancel/clear button is visible.
 * On platforms where this is always shown or always hidden, the setter
 * is a no-op and the getter reflects actual behaviour. */
bool MCSearchFieldGetShowCancelButton(MCSearchFieldRef p_field);
void MCSearchFieldSetShowCancelButton(MCSearchFieldRef p_field, bool p_show);

/* -------------------------------------------------------------------------
 * Callbacks
 * ---------------------------------------------------------------------- */

void MCSearchFieldSetTextChangedCallback(
        MCSearchFieldRef p_field,
        MCSearchFieldTextChangedCallback p_callback,
        void *p_context);

void MCSearchFieldSetSearchSubmittedCallback(
        MCSearchFieldRef p_field,
        MCSearchFieldSearchSubmittedCallback p_callback,
        void *p_context);

void MCSearchFieldSetSearchCancelledCallback(
        MCSearchFieldRef p_field,
        MCSearchFieldSearchCancelledCallback p_callback,
        void *p_context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBSEARCHFIELD_H */
