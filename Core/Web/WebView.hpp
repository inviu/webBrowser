﻿/***********************************************************************************
** MIT License                                                                    **
**                                                                                **
** Copyright (c) 2018 Victor DENIS (victordenis01@gmail.com)                      **
**                                                                                **
** Permission is hereby granted, free of charge, to any person obtaining a copy   **
** of this software and associated documentation files (the "Software"), to deal  **
** in the Software without restriction, including without limitation the rights   **
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell      **
** copies of the Software, and to permit persons to whom the Software is          **
** furnished to do so, subject to the following conditions:                       **
**                                                                                **
** The above copyright notice and this permission notice shall be included in all **
** copies or substantial portions of the Software.                                **
**                                                                                **
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     **
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       **
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    **
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         **
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  **
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE  **
** SOFTWARE.                                                                      **
***********************************************************************************/

#pragma once
#ifndef CORE_WEBVIEW_HPP
#define CORE_WEBVIEW_HPP

#include "SharedDefines.hpp"

#include <QApplication>
#include <QPointer>

#include <QEvent>
#include <QKeyEvent>

#include <QOpenGLWidget>
#include <QWidget>
#include <QLabel>

#include <QWebEngine/WebView.hpp>
#include <QWebEngine/WebPage.hpp>

#include "Web/LoadRequest.hpp"

#include "Application.hpp"

namespace Sn {

class WebPage;
class WebHitTestResult;

class SIELO_SHAREDLIB WebView: public Engine::WebView {
Q_OBJECT

public:
	static bool isUrlValide(const QUrl& url);
	static QUrl searchUrl(const QString& searchText);
	static QList<int> zoomLevels();

	WebView(QWidget* parent = nullptr);
	~WebView();

	QIcon icon(bool allowNull = false) const;

	QString title() const;
	bool isTitleEmpty() const;

	WebPage* page() const;
	void setPage(WebPage* page);

	void load(const QUrl& url);
	void load(const LoadRequest& request);
	bool isLoading() const;
	bool wasLoaded() const { return m_firstLoad; }

	int loadingProgress() const;

	bool backgroundActivity() const { return m_backgroundActivity; }

	int zoomLevel() const;
	void setZoomLevel(int level);

	QPointF mapToViewport(const QPointF& pos) const;

	void restoreHistory(const QByteArray& data);

	void addNotification(QWidget* notification);

	virtual QWidget* overlayWidget() = 0;

	bool isTransparent() const;
    void applyZoom(int zoom);

signals:
	void pageChanged(WebPage* page);
	void pageRendering();
	void viewportResized(QSize);
	void privacyChanged(bool);
	void zoomLevelChanged(int);
	void showNotification(QWidget*);
	void backgroundActivityChanged(bool);

public slots:
	void zoomIn();
	void zoomOut();
	void zoomReset();

	void editUndo() { triggerPageAction(Engine::WebPage::Undo); }
	void editRedo() { triggerPageAction(Engine::WebPage::Redo); }
	void editCopy() { triggerPageAction(Engine::WebPage::Copy); }
	void editCut() { triggerPageAction(Engine::WebPage::Cut); }
	void editPast() { triggerPageAction(Engine::WebPage::Paste); }
	void editSelectAll() { triggerPageAction(Engine::WebPage::SelectAll); }
	void editDelet()
	{
		QKeyEvent event{QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier};
		QApplication::sendEvent(this, &event);
	}
	void reloadBypassCache() { triggerPageAction(Engine::WebPage::ReloadAndBypassCache); }

	void back();
	void forward();

	void showSource();
	virtual void openUrlInNewTab(const QUrl& url, Application::NewTabType position);

	virtual void closeView() = 0;
	virtual void loadInNewTab(const LoadRequest& request, Application::NewTabType position) = 0;

	virtual bool isFullScreen() = 0;
	virtual void requestFullScreen(bool enable) = 0;

protected slots:
	void sLoadStarted();
	void sLoadProgress(int progress);
	void sLoadFinished(bool ok);
	void sUrlChanged(const QUrl& url);
	void sTitleChanged(const QString& title);
	void sIconChanged();

	void openUrlInNewWindow();
	void copyLinkToClipboard();
	void copyImageToClipboard();
	void savePageAs();
	void dlLinkToDisk();
	void dlImageToDisk();
	void dlMediaToDisk();
	void openActionUrl();
	void showSiteInformation();
	void searchSelectedText();
	void searchSelectedTextInBgTab();
	void bookmarkLink();
	void openUrlInSelectedTab();
	void openUrlInBgTab();

protected:
	void showEvent(QShowEvent* event) override;
	void resizeEvent(QResizeEvent* event);
	void contextMenuEvent(QContextMenuEvent* event);

	virtual void newWheelEvent(QWheelEvent* event);
	virtual void newMousePressEvent(QMouseEvent* event);
	virtual void newMouseReleaseEvent(QMouseEvent* event);
	virtual void newMouseMoveEvent(QMouseEvent* event);
	virtual void newKeyPressEvent(QKeyEvent* event);
	virtual void newKeyReleaseEvent(QKeyEvent* event);
	virtual void newContextMenuEvent(QContextMenuEvent* event);

	void loadRequest(const LoadRequest& request);
    void applyZoom();

	void createContextMenu(QMenu* menu, WebHitTestResult& hitTest);
	void createPageContextMenu(QMenu* menu);
	void createLinkContextMenu(QMenu* menu, const WebHitTestResult& hitTest);
	void createImageContextMenu(QMenu* menu, const WebHitTestResult& hitTest);
	void createSelectedTextContextMenu(QMenu* menu, const WebHitTestResult& hitTest);
	void createMediaContextMenu(QMenu* menu, const WebHitTestResult& hitTest);

private slots:
	//TODO: Manage forms (for password for example)

	void toggleMediaPause() { triggerPageAction(Engine::WebPage::ToggleMediaPlayPause); }
	void toggleMediaMute() { triggerPageAction(Engine::WebPage::ToggleMediaMute); }

private:
	// Actually, this class is more for add shortcuts
	void initActions();

	void updateLabel(const QString& zoomLevel);

	int m_currentZoomLevel{};
	int m_progress{100};
	bool m_backgroundActivity{false};
	bool m_firstLoad{false};

	QUrl m_clickedUrl{};
	QPointF m_clickedPos{};

	QLabel* m_zoomLabel{nullptr};
	QTimer* m_zoomTimer{nullptr};

	WebPage* m_page{nullptr};
};
extern int g_zoom;
}

#endif //CORE_WEBVIEW_HPP
