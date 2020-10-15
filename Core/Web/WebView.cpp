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

#include "Web/WebView.hpp"

#include <QHostInfo>
#include <QClipboard>
#include <QTimer>

#include <QUrl>
#include <QUrlQuery>

#include <QAction>

#include <QMenu>
#include <QLabel>

#include "Web/WebPage.hpp"
#include "Web/WebHitTestResult.hpp"
#include "Web/WebInspector.hpp"
#include "Web/Scripts.hpp"

#include "Utils/Settings.hpp"

#include "History/History.hpp"

#include "Plugins/PluginProxy.hpp"

#include "Utils/IconProvider.hpp"
#include "WebView.hpp"


namespace Sn {
    int g_zoom;
bool WebView::isUrlValide(const QUrl& url)
{
	auto x = url.host();
	return url.isValid() && !url.scheme().isEmpty()
		   && (!url.host().isEmpty() || !url.path().isEmpty() || url.hasQuery());
}

QUrl WebView::searchUrl(const QString& searchText)
{
/*
	QUrl url{QLatin1String("http://www.google.com/search")};
	QUrlQuery urlQuery{};

	urlQuery.addQueryItem(QLatin1String("q"), searchText);
	urlQuery.addQueryItem(QLatin1String("ie"), QLatin1String("UTF-8"));
	urlQuery.addQueryItem(QLatin1String("oe"), QLatin1String("UTF-8"));
	urlQuery.addQueryItem(QLatin1String("client"), QLatin1String("sielo"));

	url.setQuery(urlQuery);
*/

	QUrl url{QLatin1String("https://m.baidu.com/")};
	QUrlQuery urlQuery{};

	urlQuery.addQueryItem(QLatin1String("search"), searchText);

	url.setQuery(urlQuery);
	return url;
}

QList<int> WebView::zoomLevels()
{
	return QList<int>() << 10 << 20 << 30 << 40 << 50 << 60 << 80 << 90 << 100 << 110 << 120 << 130 << 140 << 150 << 160
						<< 170 << 180 << 190 << 200;
}

WebView::WebView(QWidget* parent) :
	Engine::WebView(parent),
	m_currentZoomLevel(zoomLevels().indexOf(300)),
	m_progress(100),
	m_firstLoad(false),
	m_page(nullptr)
{
	connect(this, &Engine::WebView::loadStarted, this, &WebView::sLoadStarted);
	connect(this, &Engine::WebView::loadProgress, this, &WebView::sLoadProgress);
	connect(this, &Engine::WebView::loadFinished, this, &WebView::sLoadFinished);
	connect(this, &Engine::WebView::urlChanged, this, &WebView::sUrlChanged);
	connect(this, &Engine::WebView::titleChanged, this, &WebView::sTitleChanged);
	connect(this, &Engine::WebView::iconChanged, this, &WebView::sIconChanged);

	Settings settings{};
	m_currentZoomLevel = settings.value("Web-Settings/defaultZoomLevel", WebView::zoomLevels().indexOf(300)).toInt();

	setAcceptDrops(true);

	m_zoomLabel = new QLabel(this);
	m_zoomLabel->setWindowFlags(m_zoomLabel->windowFlags() | Qt::WindowStaysOnTopHint);
	m_zoomLabel->hide();
	m_zoomTimer = new QTimer(this);
}

WebView::~WebView()
{
	Application::instance()->plugins()->emitWebPageDeleted(m_page);
	// Empty
}

QIcon WebView::icon(bool allowNull) const
{
	if (!Engine::WebView::icon().isNull())
		return Engine::WebView::icon();

	return allowNull ? QIcon() : Application::getAppIcon("webpage");

}

QString WebView::title() const
{
	QString title{Engine::WebView::title()};

	if (title.isEmpty())
		title = url().toString(QUrl::RemoveFragment);

	if (title.isEmpty() || title == QLatin1String("about:blank"))
		return tr("Empty page");

	return title;
}

bool WebView::isTitleEmpty() const
{
	return Engine::WebView::title().isEmpty();
}

WebPage* WebView::page() const
{
	return m_page;
}

void WebView::setPage(WebPage* page)
{
	if (m_page == page)
		return;

	if (m_page) {
		if (m_page->isLoading()) {
			emit m_page->loadProgress(100);
			emit m_page->loadFinished(true);
		}

		Application::instance()->plugins()->emitWebPageDeleted(m_page);
		m_page->setView(nullptr);
		m_page->deleteLater();
	}

	m_page = page;
	m_page->setParent(this);
	Engine::WebView::setPage(page);

	if (m_page->isLoading()) {
		emit loadStarted();
		emit loadProgress(m_page->m_loadProgress);
	}

	connect(m_page, &WebPage::privacyChanged, this, &WebView::privacyChanged);
	connect(m_page, &WebPage::pageRendering, this, &WebView::pageRendering);

	//zoomReset();
	initActions();

	emit pageChanged(m_page);
	Application::instance()->plugins()->emitWebPageCreated(m_page);
}

void WebView::load(const QUrl& url)
{
	if (m_page && !m_page->acceptNavigationRequest(url, Engine::WebPage::NavigationTypeTyped, true))
		return;

	Engine::WebView::load(url);

	if (!m_firstLoad) {
		m_firstLoad = true;
	}
}

void WebView::load(const LoadRequest& request)
{
	QUrl requestUrl{request.url()};

	if (requestUrl.isEmpty())
		return;

	if (requestUrl.scheme() == QLatin1String("javascript")) {
		const QString scriptSrc{requestUrl.toString().mid(11)};

		if (scriptSrc.contains(QLatin1Char('%')))
			m_page->runJavaScript(QUrl::fromPercentEncoding(scriptSrc.toUtf8()));
		else
			m_page->runJavaScript(scriptSrc);

		return;
	}

	/// 主页
    requestUrl = QUrl("http://wx.10086.cn/website/spa/main/home"); 

	if (isUrlValide(requestUrl)) {
		if (requestUrl.host() == "localhost")
			requestUrl.setScheme("http");
		loadRequest(requestUrl);
		return;
	}

	if (requestUrl == QUrl("localhost")) {
		requestUrl = QUrl("http://localhost/");
		loadRequest(requestUrl);
		return;
	}

	if (!requestUrl.isEmpty() && requestUrl.scheme().isEmpty() && !requestUrl.path().contains(QLatin1Char(' '))
		&& !requestUrl.path().contains(QLatin1Char('.'))) {
		QUrl url{QStringLiteral("https://") + requestUrl.path()};

		if (url.isValid()) {
			QHostInfo info{QHostInfo::fromName(url.path())};

			if (info.error() == QHostInfo::NoError) {
				LoadRequest req{request};
				req.setUrl(url);
				loadRequest(req);
				return;
			}
		}
	}

	const LoadRequest searchRequest{searchUrl(request.url().toString())};
	loadRequest(searchRequest);
}

bool WebView::isLoading() const
{
	return m_progress < 100;
}

int WebView::loadingProgress() const
{
	return m_progress;
}

int WebView::zoomLevel() const
{
	return m_currentZoomLevel;
}

void WebView::setZoomLevel(int level)
{
	m_currentZoomLevel = level;
	applyZoom();
}

QPointF WebView::mapToViewport(const QPointF& pos) const
{
	return m_page->mapToViewport(pos);
}

void WebView::restoreHistory(const QByteArray& data)
{
	QDataStream stream{data};
	stream >> *history();

	page()->setupWebChannel();
}

void WebView::addNotification(QWidget* notification)
{
	emit showNotification(notification);
}

bool WebView::isTransparent() const
{
	return (m_page->backgroundColor() == Qt::transparent);
}

void WebView::zoomIn()
{
	if (m_currentZoomLevel < zoomLevels().count() - 1) {
		++m_currentZoomLevel;
		applyZoom();
	}

	updateLabel(QString::number(zoomLevels()[m_currentZoomLevel]) + "%");
/*	QLabel* zoomLabel{new QLabel(QString::number(zoomLevels()[m_currentZoomLevel]) + "%", this)};
	zoomLabel->show();
	zoomLabel->raise();
	zoomLabel->move(0, 0);
 */
}

void WebView::zoomOut()
{
	if (m_currentZoomLevel > 0) {
		--m_currentZoomLevel;
		applyZoom();
	}
	updateLabel(QString::number(zoomLevels()[m_currentZoomLevel]) + "%");
}

void WebView::zoomReset()
{
	/*Settings settings{};
	int defaultZoomLevel{settings.value("Web-Settings/defaultZoomLevel", zoomLevels().indexOf(100)).toInt()};

	if (m_currentZoomLevel != defaultZoomLevel) {
		m_currentZoomLevel = defaultZoomLevel;
		applyZoom();
	}*/
	//updateLabel(QString::number(zoomLevels()[m_currentZoomLevel]) + "%");
}

void WebView::back()
{
	Engine::WebHistory* history = m_page->history();

	if (history->canGoBack()) {
		history->back();

		emit urlChanged(url());
	}
}

void WebView::forward()
{
	Engine::WebHistory* history = m_page->history();

	if (history->canGoForward()) {
		history->forward();

		emit urlChanged(url());
	}
}

void WebView::showSource()
{
	if (url().scheme() == QLatin1String("view-source") || url().scheme() == QLatin1String("sielo") || url().scheme() == QLatin1String("qrc"))
		return;

	triggerPageAction(Engine::WebPage::ViewSource);
}

void WebView::openUrlInNewTab(const QUrl& url, Application::NewTabType type)
{
	loadInNewTab(url, type);
}

void WebView::sLoadStarted()
{
	m_progress = 0;
}

void WebView::sLoadProgress(int progress)
{
	m_progress = progress;
}

void WebView::sLoadFinished(bool ok)
{
    applyZoom(g_zoom);
	m_progress = 100;

	if (ok) {
		Application::instance()->history()->addHistoryEntry(this);
#ifndef QT_DEBUG
		Application::instance()->piwikTraker()->sendEvent("navigation", "navigation", "page-loaded", "page loaded");
#endif
	}
}

void WebView::sUrlChanged(const QUrl& url)
{
	if (!url.isEmpty() && title().isEmpty()) {
		const bool oldActivity{m_backgroundActivity};
		m_backgroundActivity = true;
		emit titleChanged(title());
		m_backgroundActivity = oldActivity;
	}
	//TODO: Don't save blank page in history
}

void WebView::sTitleChanged(const QString& title)
{
	Q_UNUSED(title);

	if (!isVisible() && !isLoading() && !m_backgroundActivity) {
		m_backgroundActivity = true;
		emit backgroundActivityChanged(m_backgroundActivity);
	}
}

void WebView::sIconChanged()
{
	IconProvider::instance()->saveIcon(this);
}

void WebView::openUrlInNewWindow()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		Application::instance()->createWindow(Application::WT_NewWindow, action->data().toUrl());
}

void WebView::copyLinkToClipboard()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		QApplication::clipboard()->setText(action->data().toUrl().toEncoded());
}

void WebView::copyImageToClipboard()
{
	triggerPageAction(Engine::WebPage::CopyImageToClipboard);
}

void WebView::savePageAs()
{
	triggerPageAction(Engine::WebPage::SavePage);
}

void WebView::dlLinkToDisk()
{
	triggerPageAction(Engine::WebPage::DownloadLinkToDisk);
}

void WebView::dlImageToDisk()
{
	triggerPageAction(Engine::WebPage::DownloadImageToDisk);
}

void WebView::dlMediaToDisk()
{
	triggerPageAction(Engine::WebPage::DownloadMediaToDisk);
}

void WebView::openActionUrl()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		load(action->data().toUrl());
}

void WebView::showSiteInformation()
{
	//TODO: Make the site information widget (in utils)
}

void WebView::searchSelectedText()
{
	loadInNewTab(searchUrl(selectedText()), Application::NTT_SelectedTab);
}

void WebView::searchSelectedTextInBgTab()
{
	loadInNewTab(searchUrl(selectedText()), Application::NTT_NotSelectedTab);
}

void WebView::bookmarkLink()
{
	//TODO: Manage bookmarks
}

void WebView::openUrlInSelectedTab()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		openUrlInNewTab(action->data().toUrl(), Application::NTT_CleanSelectedTab);
}

void WebView::openUrlInBgTab()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		openUrlInNewTab(action->data().toUrl(), Application::NTT_CleanNotSelectedTab);
}

void WebView::showEvent(QShowEvent* event)
{
	Engine::WebView::showEvent(event);

	if (m_backgroundActivity) {
		m_backgroundActivity = false;
		emit backgroundActivityChanged(m_backgroundActivity);
	}
}

void WebView::resizeEvent(QResizeEvent* event)
{
	Engine::WebView::resizeEvent(event);
	emit viewportResized(size());
}

void WebView::contextMenuEvent(QContextMenuEvent* event)
{
	const QPoint position{event->pos()};
	const QContextMenuEvent::Reason reason{event->reason()};

	// My thanks to the duck for asking about lambda
	QTimer::singleShot(0, this, [this, position, reason]()
	{
		QContextMenuEvent ev(reason, position);
		newContextMenuEvent(&ev);
	});
}

void WebView::newWheelEvent(QWheelEvent* event)
{
	if (Application::instance()->plugins()->processWheelEvent(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	if (event->modifiers() & Qt::ControlModifier) {
		event->delta() > 0 ? zoomIn() : zoomOut();
		event->accept();
		return;
	}

	if (event->spontaneous()) {
		const qreal multiplier{Application::wheelScrollLines() / 3.0};

		if (multiplier != 1.0) {
			QWheelEvent newEvent
				{event->pos(), event->globalPos(), event->pixelDelta(), event->angleDelta() * multiplier, 0,
				 Qt::Horizontal, event->buttons(), event->modifiers(), event->phase(), event->source(),
				 event->inverted()};
			Application::sendEvent(inputWidget(), &newEvent);
			event->accept();
		}
	}
}

void WebView::newMousePressEvent(QMouseEvent* event)
{
	m_clickedUrl = QUrl();
	m_clickedPos = QPointF();

	if (Application::instance()->plugins()->processMousePress(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->button()) {
	case Qt::XButton1:
		back();
		event->accept();
		break;
	case Qt::XButton2:
		forward();
		event->accept();
		break;
	case Qt::MiddleButton:
		m_clickedUrl = m_page->hitTestContent(event->pos()).linkUrl();
		if (!m_clickedUrl.isEmpty())
			event->accept();
		break;
	case Qt::LeftButton:
		m_clickedUrl = m_page->hitTestContent(event->pos()).linkUrl();
		break;
	default:
		break;
	}
}

void WebView::newMouseReleaseEvent(QMouseEvent* event)
{
	if (Application::instance()->plugins()->processMouseRelease(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->button()) {
	case Qt::MiddleButton:
		if (!m_clickedUrl.isEmpty()) {
			const QUrl newUrl{m_page->hitTestContent(event->pos()).linkUrl()};

			if (m_clickedUrl == newUrl && isUrlValide(newUrl)) {
				if (event->modifiers() & Qt::ShiftModifier)
					openUrlInNewTab(newUrl, Application::NTT_SelectedTab);
				else
					openUrlInNewTab(newUrl, Application::NTT_NotSelectedTab);

				event->accept();
			}
		}
		break;
	case Qt::LeftButton:
		if (!m_clickedUrl.isEmpty()) {
			const QUrl newUrl{m_page->hitTestContent(event->pos()).linkUrl()};

			if ((m_clickedUrl == newUrl && isUrlValide(newUrl)) && event->modifiers() & Qt::ControlModifier) {
				if (event->modifiers() & Qt::ShiftModifier)
					openUrlInNewTab(newUrl, Application::NTT_SelectedTab);
				else
					openUrlInNewTab(newUrl, Application::NTT_NotSelectedTab);

				event->accept();
			}
		}
	default:
		break;
	}
}

void WebView::newMouseMoveEvent(QMouseEvent* event)
{
	if (Application::instance()->plugins()->processMouseMove(Application::ON_WebView, this, event))
		event->accept();
}

void WebView::newKeyPressEvent(QKeyEvent* event)
{
	if (Application::instance()->plugins()->processKeyPress(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->key()) {
		case Qt::Key_ZoomIn:
			zoomIn();
			event->accept();
			break;
		case Qt::Key_ZoomOut:
			zoomOut();
			event->accept();
			break;
	}
	if (event->modifiers() & Qt::ControlModifier) 
	{
		switch (event->key()) 
		{
			case Qt::Key_Plus:
				zoomIn();
				event->accept();
				break;
			case Qt::Key_Minus:
				zoomOut();
				event->accept();
				break;
			case Qt::Key_0:
				zoomReset();
				event->accept();
				break;
			case Qt::Key_M:
				m_page->setAudioMuted(!m_page->isAudioMuted());
				event->accept();
				break;
			case Qt::Key_Down:
				m_page->scroll(0, height());
				break;
			case Qt::Key_Up:
				m_page->scroll(0, -height());
				break;
			default:
				break;
		}
	}
}

void WebView::newKeyReleaseEvent(QKeyEvent* event)
{
	if (Application::instance()->plugins()->processKeyRelease(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->key()) {
	case Qt::Key_Escape:
		if (isFullScreen()) {
			triggerPageAction(Engine::WebPage::ExitFullScreen);
			event->accept();
		}
		break;

	default:
		break;
	}
}

void WebView::newContextMenuEvent(QContextMenuEvent* event)
{
	Q_UNUSED(event);
}

void WebView::loadRequest(const LoadRequest& request)
{
	if (request.operation() == LoadRequest::GetOp)
		load(request.url());
	else
		m_page->runJavaScript(Scripts::sendPostData(request.url(), request.data()), Engine::WebProfile::ScriptWorldId::ApplicationWorld);
}

void WebView::applyZoom(int zoom) {
    if (zoomFactor()!= zoom) {
        g_zoom = zoom;
        setZoomFactor(qreal(zoom) / 100.0);
    }
}

void WebView::applyZoom()
{
	setZoomFactor(qreal(zoomLevels()[m_currentZoomLevel]) / 100.0);
	//emit zoomLevelChanged(m_currentZoomLevel);
}

void WebView::createContextMenu(QMenu* menu, WebHitTestResult& hitTest)
{
	int spellCheckAction{0};

	const Engine::ContextMenuData& contextMenuData{page()->contextMenuData()};
	hitTest.updateWithContextMenuData(contextMenuData);
//
//#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
//	if (!contextMenuData.misspelledWord().isEmpty()) {
//		QFont boldFont{menu->font()};
//		boldFont.setBold(true);
//
//		for (const QString& suggestion : contextMenuData.spellCheckerSuggestions()) {
//			QAction* action{menu->addAction(suggestion)};
//			action->setFont(boldFont);
//
//			connect(action, &QAction::triggered, this, [=]()
//			{
//				page()->replaceMisspelledWord(suggestion);
//			});
//		}
//
//		if (menu->actions().isEmpty())
//			menu->addAction(tr("No suggestions"))->setEnabled(false);
//
//		menu->addSeparator();
//		spellCheckAction = menu->actions().count();
//	}
//#endif
//
//	if (!hitTest.linkUrl().isEmpty() && hitTest.linkUrl().scheme() != QLatin1String("javascript"))
//		createLinkContextMenu(menu, hitTest);
//	if (!hitTest.imageUrl().isEmpty())
//		createImageContextMenu(menu, hitTest);
//	if (!hitTest.mediaUrl().isEmpty())
//		createMediaContextMenu(menu, hitTest);
//	if (hitTest.isContentEditable()) {
//		if (menu->actions().count() == spellCheckAction) {
//			menu->addAction(pageAction(Engine::WebPage::Undo));
//			menu->addAction(pageAction(Engine::WebPage::Redo));
//			menu->addSeparator();
//			menu->addAction(pageAction(Engine::WebPage::Cut));
//			menu->addAction(pageAction(Engine::WebPage::Copy));
//			menu->addAction(pageAction(Engine::WebPage::Paste));
//		}
//
//	}
//	if (!selectedText().isEmpty())
//		createSelectedTextContextMenu(menu, hitTest);
//
	if (menu->isEmpty())
		createPageContextMenu(menu);

	menu->addSeparator();
	Application::instance()->plugins()->populateWebViewMenu(menu, this, hitTest);

}

void WebView::createPageContextMenu(QMenu* menu)
{
	QAction* action{menu->addAction(tr(u8"&后退"), this, &WebView::back)};
	action->setEnabled(history()->canGoBack());

	action = menu->addAction(tr(u8"&前进"), this, &WebView::forward);
	action->setEnabled(history()->canGoForward());

	//menu->addAction(pageAction(Engine::WebPage::Reload));

	//menu->addAction(pageAction(Engine::WebPage::Stop));

	//menu->addSeparator();

	//menu->addAction(tr("Book&mark page"), this, &WebView::bookmarkLink);
	//menu->addAction(tr("&Save page as..."), this, &WebView::savePageAs);
	//menu->addAction(tr("&Copy page link"), this, &WebView::copyLinkToClipboard)->setData(url());

	//menu->addSeparator();

	//menu->addAction(tr("Select &all"), this, &WebView::editSelectAll);

	//menu->addSeparator();

	//if (url().scheme() == QLatin1String("http") || url().scheme() == QLatin1String("https")) {
	//	//TODO: translate action
	//}

}

void WebView::createLinkContextMenu(QMenu* menu, const WebHitTestResult& hitTest)
{
	menu->addSeparator();

	menu->addAction(pageAction(Engine::WebPage::OpenLinkInNewTab));
	menu->addAction(tr("Open link in new &window"), this, &WebView::openUrlInNewWindow)->setData(hitTest.linkUrl());

	menu->addSeparator();

	QVariantList bookmarkData{};
	bookmarkData << hitTest.linkUrl() << hitTest.linkTitle();

	menu->addAction(tr("B&ookmark link"), this, &WebView::bookmarkLink)->setData(bookmarkData);
	menu->addAction(tr("&Save link as..."), this, &WebView::dlLinkToDisk);
	menu->addAction(tr("&Copy link address"), this, &WebView::copyLinkToClipboard)->setData(hitTest.linkUrl());

	if (!selectedText().isEmpty())
		menu->addAction(pageAction(Engine::WebPage::Copy));

}

void WebView::createImageContextMenu(QMenu* menu, const WebHitTestResult& hitTest)
{
	menu->addSeparator();

	menu->addAction(tr("Show i&mage"), this, &WebView::openActionUrl)->setData(hitTest.imageUrl());
	menu->addAction(tr("Copy image"), this, &WebView::copyImageToClipboard);
	menu->addAction(tr("Copy image ad&dress"), this, &WebView::copyLinkToClipboard)->setData(hitTest.imageUrl());

	menu->addSeparator();

	menu->addAction(tr("&Save image as..."), this, &WebView::dlImageToDisk);

	menu->addSeparator();

	if (!selectedText().isEmpty())
		menu->addAction(pageAction(Engine::WebPage::Copy));
}

void WebView::createSelectedTextContextMenu(QMenu* menu, const WebHitTestResult& hitTest)
{
	Q_UNUSED(hitTest)

	QString selectedText{page()->selectedText()};

	menu->addSeparator();

	if (!menu->actions().contains(pageAction(Engine::WebPage::Copy)))
		menu->addAction(pageAction(Engine::WebPage::Copy));

	menu->addSeparator();

	QAction* actionDictionary{new QAction(Application::getAppIcon("disctionary"), tr("Dictionary"))};
	actionDictionary->setData(QUrl("https://wiktionary.org/wiki/Special:Search?search=" + selectedText));
	connect(actionDictionary, &QAction::triggered, this, &WebView::openUrlInSelectedTab);
	menu->addAction(actionDictionary);

	QString selectedString{selectedText.trimmed().remove(QLatin1Char('\n'))};

	if (!selectedString.contains(QLatin1Char('.')))
		selectedString.append(QLatin1String(".com"));

	QUrl guessedUrl{QUrl::fromUserInput(selectedString)};

	if (isUrlValide(guessedUrl)) {
		QAction* actionGoToWebAddress{new QAction(Application::getAppIcon("open-remote"), tr("Go to &web address"))};
		actionGoToWebAddress->setData(guessedUrl);

		connect(actionGoToWebAddress, &QAction::triggered, this, &WebView::openUrlInSelectedTab);
		menu->addAction(actionGoToWebAddress);
	}

	// TODO: action to search selected text on google
}

void WebView::createMediaContextMenu(QMenu* menu, const WebHitTestResult& hitTest)
{
	bool paused{hitTest.mediaPaused()};
	bool muted{hitTest.mediaMuted()};

	menu->addSeparator();

	menu->addAction(paused ? tr("&Play") : tr("&Pause"), this, &WebView::toggleMediaPause);
	menu->addAction(muted ? tr("Un&mute") : tr("&Mute"), this, &WebView::toggleMediaMute);

	menu->addSeparator();

	menu->addAction(tr("&Copy media address"), this, &WebView::copyLinkToClipboard)->setData(hitTest.mediaUrl());
	menu->addAction(tr("Save media to &disk"), this, &WebView::dlMediaToDisk);
}

void WebView::initActions()
{
	QAction* a_undo{pageAction(Engine::WebPage::Undo)};
	a_undo->setText(tr("&Undo"));
	a_undo->setShortcut(QKeySequence("Ctrl+Z"));
	a_undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_undo->setIcon(Application::getAppIcon("edit-undo", "edit"));

	QAction* a_redo{pageAction(Engine::WebPage::Redo)};
	a_redo->setText(tr("&Redo"));
	a_redo->setShortcut(QKeySequence("Ctrl+Shift+Z"));
	a_redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_redo->setIcon(Application::getAppIcon("edit-redo", "edit"));

	QAction* a_cut{pageAction(Engine::WebPage::Cut)};
	a_cut->setText(tr("&Cut"));
	a_cut->setShortcut(QKeySequence("Ctrl+X"));
	a_cut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_cut->setIcon(Application::getAppIcon("edit-cut", "edit"));

	QAction* a_copy{pageAction(Engine::WebPage::Copy)};
	a_copy->setText(tr("&Copy"));
	a_copy->setShortcut(QKeySequence("Ctrl+C"));
	a_copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_copy->setIcon(Application::getAppIcon("edit-copy", "edit"));

	QAction* a_past{pageAction(Engine::WebPage::Paste)};
	a_past->setText(tr("&Paste"));
	a_past->setShortcut(QKeySequence("Ctrl+V"));
	a_past->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_past->setIcon(Application::getAppIcon("edit-past", "edit"));

	QAction* a_selectAll{pageAction(Engine::WebPage::SelectAll)};
	a_selectAll->setText(tr("Select All"));
	a_selectAll->setShortcut(QKeySequence("Ctrl+A"));
	a_selectAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_selectAll->setIcon(Application::getAppIcon("edit-select-all", "edit"));

	QAction* reloadAction{pageAction(Engine::WebPage::Reload)};
	reloadAction->setText(tr("&Reload"));
	reloadAction->setIcon(Application::getAppIcon("reload"));

	QAction* stopAction{pageAction(Engine::WebPage::Stop)};
	stopAction->setText(tr("S&top"));
	stopAction->setIcon(Application::getAppIcon("stop"));

	addAction(a_undo);
	addAction(a_redo);
	addAction(a_cut);
	addAction(a_copy);
	addAction(a_past);
	addAction(a_selectAll);
}

void WebView::updateLabel(const QString& zoomLevel)
{
    return;
	m_zoomTimer->stop();
	if (m_zoomLabel)
		disconnect(m_zoomTimer, &QTimer::timeout, m_zoomLabel, &QLabel::hide);

	QPalette palette{};
	QBrush textBrush{QColor(0, 0, 0, 255)};
	QBrush backgroundBrush{QColor(255, 255, 255, 255)};

	textBrush.setStyle(Qt::SolidPattern);
	backgroundBrush.setStyle(Qt::SolidPattern);

	palette.setBrush(QPalette::Active, QPalette::WindowText, textBrush);
	palette.setBrush(QPalette::Inactive, QPalette::WindowText, textBrush);
	palette.setBrush(QPalette::Active, QPalette::Window, backgroundBrush);
	palette.setBrush(QPalette::Inactive, QPalette::Window, backgroundBrush);

	QFont font;
	font.setPointSize(24);
	font.setBold(false);

	m_zoomLabel->setFont(font);
	m_zoomLabel->setPalette(palette);
	m_zoomLabel->setAlignment(Qt::AlignCenter);
	m_zoomLabel->setAutoFillBackground(true);
	m_zoomLabel->setFocusPolicy(Qt::NoFocus);
	m_zoomLabel->setText(zoomLevel);
	m_zoomLabel->move(width() / 2 - m_zoomLabel->width() / 2, height() / 2 - m_zoomLabel->height() / 2);
	m_zoomLabel->raise();
	m_zoomLabel->show();

	m_zoomTimer->start(3 * 1000);
	connect(m_zoomTimer, &QTimer::timeout, m_zoomLabel, &QLabel::hide);
}
}
