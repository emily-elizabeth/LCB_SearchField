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

#import <Cocoa/Cocoa.h>
#include <string>
#include "libsearchfield.h"

/* -------------------------------------------------------------------------
 * Delegate – receives NSSearchField notifications
 * ---------------------------------------------------------------------- */

@interface LCSFDelegate : NSObject <NSSearchFieldDelegate>
{
    MCSearchFieldRef m_field;
}
- (instancetype)initWithField:(MCSearchFieldRef)field;
@end

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */

struct MCSearchField
{
    NSSearchField  *view;
    LCSFDelegate   *delegate;

    std::string     text;
    std::string     placeholder;
    bool            enabled;
    bool            show_cancel;

    MCSearchFieldTextChangedCallback      text_changed_cb;
    void                                 *text_changed_ctx;

    MCSearchFieldSearchSubmittedCallback  submitted_cb;
    void                                 *submitted_ctx;

    MCSearchFieldSearchCancelledCallback  cancelled_cb;
    void                                 *cancelled_ctx;
};

/* -------------------------------------------------------------------------
 * Delegate implementation
 * ---------------------------------------------------------------------- */

@implementation LCSFDelegate

- (instancetype)initWithField:(MCSearchFieldRef)field
{
    self = [super init];
    if (self)
        m_field = field;
    return self;
}

- (void)controlTextDidChange:(NSNotification *)notification
{
    NSSearchField *sf = (NSSearchField *)notification.object;
    const char *text = sf.stringValue.UTF8String;
    m_field->text = text ? text : "";

    if (m_field->text_changed_cb)
        m_field->text_changed_cb(m_field->text_changed_ctx, m_field,
                                 m_field->text.c_str());
}

/* Return/Enter key → search submitted via doCommandBySelector, not target/action */
- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)commandSelector
{
    if (commandSelector == @selector(insertNewline:))
    {
        const char *text = m_field->view.stringValue.UTF8String;
        m_field->text = text ? text : "";
        if (m_field->submitted_cb)
            m_field->submitted_cb(m_field->submitted_ctx, m_field,
                                  m_field->text.c_str());
        return YES;
    }
    return NO;
}

/* Cancel button */
- (void)searchFieldCancelAction:(id)sender
{
    m_field->text = "";
    m_field->view.stringValue = @"";

    if (m_field->cancelled_cb)
        m_field->cancelled_cb(m_field->cancelled_ctx, m_field);
}

@end

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool MCSearchFieldCreate(void * /*p_parent_view*/, MCSearchFieldRef *r_field)
{
    MCSearchField *f = new MCSearchField{};
    f->enabled      = true;
    f->show_cancel  = true;

    NSSearchField *sf = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    sf.bezelStyle = NSTextFieldRoundedBezel;
    sf.sendsSearchStringImmediately = NO;

    LCSFDelegate *delegate = [[LCSFDelegate alloc] initWithField:f];
    sf.delegate = delegate;
    /* No target/action on the field itself — Return is handled via
     * control:textView:doCommandBySelector: in the delegate. */

    /* Wire up the cancel button if present */
    NSSearchFieldCell *cell = (NSSearchFieldCell *)sf.cell;
    [cell.cancelButtonCell setTarget:delegate];
    [cell.cancelButtonCell setAction:@selector(searchFieldCancelAction:)];

    /* Do NOT call addSubview: here. When the LCB widget uses
     * "set my native layer to", the engine handles view embedding.
     * Calling addSubview: on the parent pointer at this stage crashes
     * because it is not yet a valid NSView in the widget lifecycle. */

    f->view     = sf;
    f->delegate = delegate;
    *r_field    = f;
    return true;
}

void MCSearchFieldDestroy(MCSearchFieldRef p_field)
{
    if (!p_field) return;
    [p_field->view removeFromSuperview];
    p_field->view     = nil;
    p_field->delegate = nil;
    delete p_field;
}

void *MCSearchFieldGetNativeLayer(MCSearchFieldRef p_field)
{
    return (__bridge void *)p_field->view;
}

void MCSearchFieldSetFrame(MCSearchFieldRef p_field,
                           int32_t p_x, int32_t p_y,
                           int32_t p_width, int32_t p_height)
{
    p_field->view.frame = NSMakeRect(p_x, p_y, p_width, p_height);
}

const char *MCSearchFieldGetText(MCSearchFieldRef p_field)
{
    const char *s = p_field->view.stringValue.UTF8String;
    p_field->text = s ? s : "";
    return p_field->text.c_str();
}

void MCSearchFieldSetText(MCSearchFieldRef p_field, const char *p_text)
{
    p_field->text = p_text ? p_text : "";
    p_field->view.stringValue = [NSString stringWithUTF8String:p_field->text.c_str()];
}

const char *MCSearchFieldGetPlaceholderText(MCSearchFieldRef p_field)
{
    return p_field->placeholder.c_str();
}

void MCSearchFieldSetPlaceholderText(MCSearchFieldRef p_field,
                                     const char *p_placeholder)
{
    p_field->placeholder = p_placeholder ? p_placeholder : "";
    p_field->view.placeholderString =
        [NSString stringWithUTF8String:p_field->placeholder.c_str()];
}

bool MCSearchFieldGetEnabled(MCSearchFieldRef p_field)
{
    return p_field->enabled;
}

void MCSearchFieldSetEnabled(MCSearchFieldRef p_field, bool p_enabled)
{
    p_field->enabled   = p_enabled;
    p_field->view.enabled = p_enabled;
}

bool MCSearchFieldGetShowCancelButton(MCSearchFieldRef p_field)
{
    return p_field->show_cancel;
}

void MCSearchFieldSetShowCancelButton(MCSearchFieldRef p_field, bool p_show)
{
    p_field->show_cancel = p_show;
    NSSearchFieldCell *cell = (NSSearchFieldCell *)p_field->view.cell;
    cell.cancelButtonCell.transparent = !p_show;
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
