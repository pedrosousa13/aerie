// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_BUBBLE_LIFETIME_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_BUBBLE_LIFETIME_H_

namespace views {
class BubbleDialogDelegate;
class Widget;
}  // namespace views

namespace aerie_sidebar {

// Ends the life of a bubble delegate with its widget (issue #247).
//
// A BubbleDialogDelegate that was made with
// BubbleDialogDelegate::CreateBubbleDeprecated() and is not owned by its
// widget (the widget cannot own it outside Chromium's own list) is never
// deleted. It stays alive after its widget is gone, and so does the observer
// that it keeps on the anchor's window. The next theme change of that window
// calls Widget::ThemeChanged() on the delegate's null widget, and the browser
// process crashes.
//
// Call this once, right after CreateBubbleDeprecated(), with the delegate and
// the widget it returned. When the widget starts to go, the delegate lets go
// of its anchor, so that the window no longer tells it anything. When the
// widget is gone, the delegate is deleted in a task of its own, after the
// widget has finished with it. Nothing is left to the caller.
void DeleteDelegateWithWidget(views::BubbleDialogDelegate* delegate,
                              views::Widget* widget);

}  // namespace aerie_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_BUBBLE_LIFETIME_H_
