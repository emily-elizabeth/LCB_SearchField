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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include "libsearchfield.h"

/* -------------------------------------------------------------------------
 * Notes on approach
 *
 * Win32 has no dedicated native search field control, so we compose one:
 *
 *   - A container HWND (static/custom) that owns the layout.
 *   - A child Edit control with EM_SETCUEBANNER for placeholder text and
 *     EM_SETMARGINS to make room for the search icon and clear button.
 *   - The magnifying-glass icon is painted into the left margin of the Edit
 *     during WM_PAINT of the container.
 *   - A child button (×) on the right, shown/hidden on EN_CHANGE.
 *   - Enter → submitted callback; clear button / Escape → cancelled callback.
 *
 * The container HWND is returned as the native layer.
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */

struct MCSearchField
{
    HWND  container;
    HWND  edit;
    HWND  clear_btn;

    std::wstring text;
    std::wstring placeholder;
    bool         enabled;
    bool         show_cancel;

    MCSearchFieldTextChangedCallback      text_changed_cb;
    void                                 *text_changed_ctx;

    MCSearchFieldSearchSubmittedCallback  submitted_cb;
    void                                 *submitted_ctx;

    MCSearchFieldSearchCancelledCallback  cancelled_cb;
    void                                 *cancelled_ctx;

    /* Original Edit WndProc for subclassing */
    WNDPROC edit_proc;
};

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static LRESULT CALLBACK ContainerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static const wchar_t *kContainerClass = L"LCSFContainer";
static const int      kIconWidth      = 20;
static const int      kClearWidth     = 20;
static const int      kClearBtnID     = 1001;

static void RegisterContainerClass(HINSTANCE hInst)
{
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ContainerProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kContainerClass;
    RegisterClassExW(&wc);
    registered = true;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static std::string WideToUTF8(const std::wstring &ws)
{
    if (ws.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring UTF8ToWide(const char *s)
{
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring ws(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &ws[0], n);
    return ws;
}

static MCSearchField *FieldFromHwnd(HWND hwnd)
{
    return reinterpret_cast<MCSearchField *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

static void UpdateClearButton(MCSearchField *f)
{
    int len = GetWindowTextLengthW(f->edit);
    ShowWindow(f->clear_btn, (len > 0 && f->show_cancel) ? SW_SHOW : SW_HIDE);
}

static void FireTextChanged(MCSearchField *f)
{
    int len = GetWindowTextLengthW(f->edit);
    f->text.resize(len);
    GetWindowTextW(f->edit, &f->text[0], len + 1);

    UpdateClearButton(f);

    if (f->text_changed_cb)
    {
        auto u = WideToUTF8(f->text);
        f->text_changed_cb(f->text_changed_ctx, f, u.c_str());
    }
}

/* -------------------------------------------------------------------------
 * Container window procedure
 * ---------------------------------------------------------------------- */

static LRESULT CALLBACK ContainerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MCSearchField *f = FieldFromHwnd(hwnd);

    switch (msg)
    {
        case WM_SIZE:
        {
            if (!f) break;
            int w = LOWORD(lp), h = HIWORD(lp);
            /* Edit fills the container minus icon margin and clear button */
            SetWindowPos(f->edit, nullptr,
                         kIconWidth, 1,
                         w - kIconWidth - kClearWidth, h - 2,
                         SWP_NOZORDER);
            SetWindowPos(f->clear_btn, nullptr,
                         w - kClearWidth, 0,
                         kClearWidth, h,
                         SWP_NOZORDER);
            break;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            /* Draw a simple magnifying-glass placeholder using text.
             * A production build should use DrawIconEx with a proper icon
             * resource or render via Direct2D. */
            RECT rc = { 2, 0, kIconWidth, 0 };
            GetClientRect(hwnd, &rc);
            rc.right = kIconWidth;
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, L"\x2315", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_COMMAND:
        {
            if (!f) break;
            int id   = LOWORD(wp);
            int code = HIWORD(wp);

            if (id == kClearBtnID && code == BN_CLICKED)
            {
                SetWindowTextW(f->edit, L"");
                f->text.clear();
                UpdateClearButton(f);
                SetFocus(f->edit);
                if (f->cancelled_cb)
                    f->cancelled_cb(f->cancelled_ctx, f);
            }
            else if ((HWND)lp == f->edit && code == EN_CHANGE)
            {
                FireTextChanged(f);
            }
            break;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* -------------------------------------------------------------------------
 * Edit subclass procedure (intercepts Enter and Escape)
 * ---------------------------------------------------------------------- */

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MCSearchField *f = reinterpret_cast<MCSearchField *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_KEYDOWN)
    {
        if (wp == VK_RETURN && f && f->submitted_cb)
        {
            auto u = WideToUTF8(f->text);
            f->submitted_cb(f->submitted_ctx, f, u.c_str());
            return 0;
        }
        if (wp == VK_ESCAPE && f && f->cancelled_cb)
        {
            SetWindowTextW(hwnd, L"");
            f->text.clear();
            UpdateClearButton(f);
            f->cancelled_cb(f->cancelled_ctx, f);
            return 0;
        }
    }
    return CallWindowProcW(f ? f->edit_proc : DefWindowProcW, hwnd, msg, wp, lp);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool MCSearchFieldCreate(void *p_parent_view, MCSearchFieldRef *r_field)
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    RegisterContainerClass(hInst);

    HWND parent = reinterpret_cast<HWND>(p_parent_view);

    HWND container = CreateWindowExW(
        0, kContainerClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 200, 24,
        parent, nullptr, hInst, nullptr);

    if (!container) return false;

    /* Edit control */
    HWND edit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        kIconWidth, 1, 200 - kIconWidth - kClearWidth, 22,
        container, nullptr, hInst, nullptr);

    /* Clear button */
    HWND clear_btn = CreateWindowExW(
        0, L"BUTTON", L"×",
        WS_CHILD | BS_FLAT | BS_TEXT,
        200 - kClearWidth, 0, kClearWidth, 24,
        container, reinterpret_cast<HMENU>(kClearBtnID), hInst, nullptr);

    MCSearchField *f = new MCSearchField{};
    f->container    = container;
    f->edit         = edit;
    f->clear_btn    = clear_btn;
    f->enabled      = true;
    f->show_cancel  = true;

    SetWindowLongPtrW(container, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(f));

    /* Subclass the Edit control and store f in its USERDATA */
    f->edit_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(edit, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EditSubclassProc)));
    SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(f));

    /* Placeholder text */
    SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search"));

    /* Left margin to avoid overlapping the icon */
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN,
                 MAKELPARAM(kIconWidth, 0));

    ShowWindow(clear_btn, SW_HIDE);

    *r_field = f;
    return true;
}

void MCSearchFieldDestroy(MCSearchFieldRef p_field)
{
    if (!p_field) return;
    DestroyWindow(p_field->container);
    delete p_field;
}

void *MCSearchFieldGetNativeLayer(MCSearchFieldRef p_field)
{
    return reinterpret_cast<void *>(p_field->container);
}

void MCSearchFieldSetFrame(MCSearchFieldRef p_field,
                           int32_t p_x, int32_t p_y,
                           int32_t p_width, int32_t p_height)
{
    SetWindowPos(p_field->container, nullptr,
                 p_x, p_y, p_width, p_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

const char *MCSearchFieldGetText(MCSearchFieldRef p_field)
{
    int len = GetWindowTextLengthW(p_field->edit);
    p_field->text.resize(len);
    GetWindowTextW(p_field->edit, &p_field->text[0], len + 1);
    static std::string utf8;
    utf8 = WideToUTF8(p_field->text);
    return utf8.c_str();
}

void MCSearchFieldSetText(MCSearchFieldRef p_field, const char *p_text)
{
    p_field->text = UTF8ToWide(p_text);
    SetWindowTextW(p_field->edit, p_field->text.c_str());
    UpdateClearButton(p_field);
}

const char *MCSearchFieldGetPlaceholderText(MCSearchFieldRef p_field)
{
    static std::string utf8;
    utf8 = WideToUTF8(p_field->placeholder);
    return utf8.c_str();
}

void MCSearchFieldSetPlaceholderText(MCSearchFieldRef p_field,
                                     const char *p_placeholder)
{
    p_field->placeholder = UTF8ToWide(p_placeholder);
    SendMessageW(p_field->edit, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(p_field->placeholder.c_str()));
}

bool MCSearchFieldGetEnabled(MCSearchFieldRef p_field)
{
    return p_field->enabled;
}

void MCSearchFieldSetEnabled(MCSearchFieldRef p_field, bool p_enabled)
{
    p_field->enabled = p_enabled;
    EnableWindow(p_field->edit,      p_enabled);
    EnableWindow(p_field->clear_btn, p_enabled);
}

bool MCSearchFieldGetShowCancelButton(MCSearchFieldRef p_field)
{
    return p_field->show_cancel;
}

void MCSearchFieldSetShowCancelButton(MCSearchFieldRef p_field, bool p_show)
{
    p_field->show_cancel = p_show;
    UpdateClearButton(p_field);
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
