/***********************************************************************************
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

#include <plog/Initializers/RollingFileInitializer.h>

#include "BrowserWindow.hpp"

#include <QToolTip>
#include <QStatusBar>

#include <QList>

#include <QAction>

#include <QClipboard>
#include <QDesktopWidget>

#include <QTimer>
#include <QMessageBox>

#include "Bookmarks/BookmarksUtils.hpp"
#include "Bookmarks/BookmarksToolbar.hpp"

#include "MaquetteGrid/MaquetteGridItem.hpp"

#include "Plugins/PluginProxy.hpp"

#include "Utils/DataPaths.hpp"
#include "Utils/RestoreManager.hpp"
#include "Utils/Settings.hpp"

#include "Web/LoadRequest.hpp"
#include "Web/WebPage.hpp"
#include "Web/Tab/WebTab.hpp"
#include "Web/Tab/TabbedWebView.hpp"

#include "Widgets/TitleBar.hpp"
#include "Widgets/CheckBoxDialog.hpp"
#include "Widgets/AddressBar/AddressBar.hpp"
#include "Widgets/Tab/TabWidget.hpp"
#include "Widgets/Tab/MainTabBar.hpp"


#ifdef Q_OS_WIN
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windows.h>

#if QT_VERSION == QT_VERSION_CHECK(5, 11, 0) || QT_VERSION == QT_VERSION_CHECK(5, 11, 1)
#error "The custom window don't work with Qt 5.11.0 or Qt 5.11.1"
#endif

#endif
QT_BEGIN_NAMESPACE
extern Q_WIDGETS_EXPORT void qt_blurImage(QPainter *p, QImage &blurImage, qreal radius, bool quality, bool alphaOnly, int transposed = 0);
QT_END_NAMESPACE

#ifdef Q_OS_WIN
bool DWMEnabled(void)
{
	if (QSysInfo::windowsVersion() < QSysInfo::WV_VISTA) return false;
	BOOL useDWM;

	if (DwmIsCompositionEnabled(&useDWM) < 0) return false;
	return useDWM == TRUE;
}

void adjust_maximized_client_rect(HWND window, RECT& rect)
{
	WINDOWPLACEMENT placement;
	if (!GetWindowPlacement(window, &placement) || placement.showCmd != SW_MAXIMIZE)
		return;

	auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
	if (!monitor)
		return;

	MONITORINFO monitor_info{};
	monitor_info.cbSize = sizeof(monitor_info);
	if (!::GetMonitorInfoW(monitor, &monitor_info))
		return;

	// when maximized, make the client area fill just the monitor (without task bar) rect,
	// not the whole window rect which extends beyond the monitor.
	rect = monitor_info.rcWork;
}
#endif

namespace Sn
{
BrowserWindow::SavedWindow::SavedWindow()
{
	// Empty
}

BrowserWindow::SavedWindow::SavedWindow(BrowserWindow* window)
{
	windowState = window->isFullScreen() ? QByteArray() : window->saveState();
	windowGeometry = window->saveGeometry();

	const int tabsSpaceCount{window->tabsSpaceSplitter()->count()};
	tabsSpaces.reserve(tabsSpaceCount);

	for (int i{0}; i < tabsSpaceCount; ++i) {
		TabsSpaceSplitter::SavedTabsSpace tabsSpace{window->tabsSpaceSplitter(), window->tabsSpaceSplitter()->tabWidget(i)};
		if (!tabsSpace.isValid())
			continue;

		tabsSpaces.append(tabsSpace);
	}
}

BrowserWindow::SavedWindow::SavedWindow(MaquetteGridItem* maquetteGridItem)
{
	windowState = QByteArray();
	windowGeometry = QByteArray();

	const int tabsSpaceCount{maquetteGridItem->tabsSpaces().count()};
	tabsSpaces.reserve(tabsSpaceCount);

	for (int i{0}; i < tabsSpaceCount; ++i) {
		TabsSpaceSplitter::SavedTabsSpace tabsSpace = maquetteGridItem->tabsSpaces()[i];
		if (!tabsSpace.isValid())
			continue;

		tabsSpaces.append(tabsSpace);
	}
}

bool BrowserWindow::SavedWindow::isValid() const
{
	for (const TabsSpaceSplitter::SavedTabsSpace& tabsSpace : tabsSpaces) {
		if (!tabsSpace.isValid())
			return false;
	}

	return true;
}

void BrowserWindow::SavedWindow::clear()
{
	windowState.clear();
	windowGeometry.clear();
	tabsSpaces.clear();
}

QDataStream &operator<<(QDataStream &stream, const BrowserWindow::SavedWindow &window)
{
	stream << 1;
	stream << window.windowState;
	stream << window.windowGeometry;
	stream << window.tabsSpaces.count();

	for (int i{0}; i < window.tabsSpaces.count(); ++i)
		stream << window.tabsSpaces[i];

	return stream;
}

QDataStream &operator>>(QDataStream &stream, BrowserWindow::SavedWindow &window)
{
	int version{0};
	stream >> version;

	if (version < 1)
		return stream;

	stream >> window.windowState;
	stream >> window.windowGeometry;

	int tabsSpacesCount{-1};
	stream >> tabsSpacesCount;
	window.tabsSpaces.reserve(tabsSpacesCount);

	for (int i{0}; i < tabsSpacesCount; ++i) {
		TabsSpaceSplitter::SavedTabsSpace tabsSpace{};
		stream >> tabsSpace;
		window.tabsSpaces.append(tabsSpace);
	}

	return stream;
}
BrowserWindow::BrowserWindow(Application::WindowType type, const QUrl& url) :
	QMainWindow(nullptr),
	m_startUrl(url),
	m_windowType(type),
	m_backgroundTimer(new QTimer())
{
    plog::init(plog::debug, "Sielo.log");
    PLOGD << "start run\n\n"; 

//#define DrawWidgetOnly 1

#ifdef DrawWidgetOnly
    hide();
#else 
    setAttribute(Qt::WA_AcceptTouchEvents); ///add
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAcceptDrops(true);
    setMouseTracking(true);

#ifdef Q_OS_WIN
    setWindowFlags(Qt::FramelessWindowHint);
    SetWindowLongPtrW(reinterpret_cast<HWND>(winId()), GWL_STYLE, WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    const MARGINS margins = { 1,1,1,1 };
    DwmExtendFrameIntoClientArea(reinterpret_cast<HWND>(winId()), &margins);
#endif


    setObjectName(QLatin1String("mainwindow"));
    setWindowTitle(tr("Sielo"));
    setProperty("private", Application::instance()->privateBrowsing());

    statusBar()->hide(); // Since we have a custom status bar, we hide the default one.

    setupUi();

    loadSettings();

    connect(m_backgroundTimer, &QTimer::timeout, this, &BrowserWindow::loadWallpaperSettings);
    m_backgroundTimer->start(1000);

    // Just wait some milli seconds before doing some post launch action
    QTimer::singleShot(10, this, &BrowserWindow::postLaunch);

    connect(this, SIGNAL(signalzoom(int)), this, SLOT(doZoom(int)));
    connect(this, SIGNAL(signalgoNewUrl(QString)), this, SLOT(goNewUrl(QString)));
#endif

 
    connect(this, SIGNAL(signalopenPanit(bool)), this, SLOT(openPanit(bool)));
    connect(this, SIGNAL(signalsetcolor(QString)), this, SLOT(setColor(QString)));
    connect(this, SIGNAL(signalclearPanit()), this, SLOT(clearPanit()));

    HttpServer::instance(0, this)->start();
}

BrowserWindow::~BrowserWindow()
{	
	if (DrawWidget::self) {
		DrawWidget::self->close();
		DrawWidget::self = nullptr;
	}
	Application::instance()->plugins()->emitMainWindowDeleted(this);
}

void BrowserWindow::loadSettings()
{
	Settings settings{};

	m_homePage = settings.value(QLatin1String("Web-Settings/homePage"), QUrl("https://item.jd.com/100010079898.html")).toUrl();

	m_blur_radius = settings.value(QLatin1String("Settings/backdropBlur"), 100).toInt();

	bool toolbarAtBottom{settings.value(QLatin1String("Settings/bottomToolBar"), false).toBool()};

	if (toolbarAtBottom) {
		m_layout->removeWidget(m_titleBar);
		m_layout->addWidget(m_titleBar);
	}
	else {
		m_layout->removeWidget(m_titleBar);
		m_layout->insertWidget(0, m_titleBar);
	}

	// There is two possibility: the user use the floating button or not. 
	// Despite the floating button belongs to the window, the navigation bar belongs to the tab widget
	if (m_tabsSpaceSplitter->count() > 0) {
		if (Application::instance()->showFloatingButton() && !m_fButton)
			setupFloatingButton();
		else if (!Application::instance()->showFloatingButton() && m_fButton) {
			delete m_fButton;
			m_fButton = nullptr;
		}
	}

	m_titleBar->loadSettings();
	m_bookmarksToolbar->loadSettings();
	m_tabsSpaceSplitter->loadSettings();

	loadWallpaperSettings();

	bool showBookmarksToolBar = settings.value(QLatin1String("ShowBookmarksToolBar"), false).toBool();
	m_bookmarksToolbar->setVisible(showBookmarksToolBar);
}

void BrowserWindow::loadWallpaperSettings()
{
	if (!m_backgroundTimer->isActive()) 
		m_backgroundTimer->start(100);

	Settings settings{};

	QString backgroundPath = settings.value(QLatin1String("Settings/backgroundPath"), QString()).toString();

#ifdef Q_OS_WIN 
	QSettings wallpaperSettings{"HKEY_CURRENT_USER\\Control Panel\\Desktop", QSettings::NativeFormat};
	QString wallpaper{wallpaperSettings.value("WallPaper", QString()).toString()};
	wallpaper.replace("\\", "/");

		if (backgroundPath.isEmpty())
			backgroundPath = wallpaper;
#endif


	QImage newBackground{backgroundPath};
	
	// Themes can have default backgound. If the user don't have custom background, we apply it.
	// However, if the user have a custom background we override the default one
	if (!backgroundPath.isEmpty() && newBackground != m_currentBackground) {
		QString sss{};
		sss += "QMainWindow {";
		sss += "border-image: url(" + backgroundPath + ") 0 0 0 0 stretch stretch;";
		sss += "border-width: 0px;";

		//if (settings.value(QLatin1String("Settings/repeatBackground"), false).toBool())
		//	sss += "background-repeat: repeat;";
		//else
		//	sss += "background-repeat: no-repeat;";

		sss += "}";

		setStyleSheet(sss);

		m_currentBackground = newBackground;
		m_upd_ss = true; // Citorva will explain this.
	}
}

void BrowserWindow::setStartTab(WebTab* tab)
{
	m_startTab = tab;
}

void BrowserWindow::setStartPage(WebPage* page)
{
	m_startPage = page;
}

void BrowserWindow::restoreWindowState(const SavedWindow& window)
{
	// Restore basics informations
	restoreState(window.windowState);
	restoreGeometry(window.windowGeometry);

	for (int i{0}; i < window.tabsSpaces.count(); ++i)
		m_tabsSpaceSplitter->restoreTabsSpace(window.tabsSpaces[i]);

	m_tabsSpaceSplitter->autoResize();

	loadSettings();
}

void BrowserWindow::currentTabChanged(WebTab* tab)
{
	if (!tab || !tab->webView())
		return;

	setWindowTitle(tr("%1 - Sielo").arg(tab->title()));

	tab->webView()->setFocus();
}

void BrowserWindow::postMouseEvent(ButtonType t, ButtonAction ac,int x,int y){

    auto wid = tabWidget()->webTab()->webView()->inputWidget();
    auto target = wid->childAt(wid->pos() += m_curPos);//?
    QMouseEvent * mEvent = NULL;

    if (ac == ButtonAction::BUTTON_ACTION_MOVE) 
    {
        m_curPos.setX(x);
        m_curPos.setY(y);
        QCursor::setPos(wid->pos() += m_curPos);//wid->pos() += m_curPos
        mEvent = new QMouseEvent(QEvent::MouseMove, QCursor::pos(), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::postEvent(target, (QEvent *)mEvent);
    }
    else 
    {
        switch (t)
        {
        case Sn::EBUTTON_LEFT: {
            mEvent = new QMouseEvent(
                ac == ButtonAction::BUTTON_ACTION_PRESS ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                QCursor::pos(), Qt::LeftButton, Qt::MouseButton::NoButton, Qt::NoModifier);
        }break;
       /* case Sn::EBUTTON_MIDDLE: {
            mEvent = new QMouseEvent(
                ac == ButtonAction::BUTTON_ACTION_PRESS ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                QCursor::pos(), Qt::MidButton, Qt::MidButton, Qt::NoModifier);
        }break;
        case Sn::EBUTTON_RIGHT: {
            mEvent = new QMouseEvent(
                ac == ButtonAction::BUTTON_ACTION_PRESS ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                QCursor::pos(), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        }break;
        case Sn::EBUTTON_X1: {
            mEvent = new QMouseEvent(
                ac == ButtonAction::BUTTON_ACTION_PRESS ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                QCursor::pos(), Qt::XButton1, Qt::XButton1, Qt::NoModifier);
        }break;
        case Sn::EBUTTON_X2: {
            mEvent = new QMouseEvent(
                ac == ButtonAction::BUTTON_ACTION_PRESS ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                QCursor::pos(), Qt::XButton2, Qt::XButton2, Qt::NoModifier);
        }break;*/
        default:
            break;
        }
        if (mEvent) {
            //QCursor::setPos(wid->pos()+m_curPos);
            //QCursor::setPos(mapToGlobal(wid->pos() += m_curPos));//wid->pos() += m_curPos
            QApplication::postEvent(target, (QEvent *)mEvent);
        }
    }
}

void BrowserWindow::loadUrl(const QUrl& url)
{
	// We can't load url directly in pinned tabs
	if (tabWidget()->webTab()->isPinned()) {
		int index{tabWidget()->addView(url, Application::NTT_CleanSelectedTab)};
		tabWidget()->webTab(index)->webView()->setFocus();
	}
	else {
		tabWidget()->webTab()->webView()->setFocus();
		tabWidget()->webTab()->webView()->load(url);
	}
}

void BrowserWindow::loadUrlInNewTab(const QUrl& url)
{
	tabWidget()->addView(url);
}

TabWidget *BrowserWindow::tabWidget() const
{
	return m_tabsSpaceSplitter->tabWidget();
}

TabWidget *BrowserWindow::tabWidget(int index) const
{
	return m_tabsSpaceSplitter->tabWidget(index);
}

const QImage *BrowserWindow::background()
{
	return m_bg;
}

const QImage *BrowserWindow::processedBackground()
{
	return m_blur_bg;
}

void BrowserWindow::setWindowTitle(const QString& title)
{
	QString t{title};

	QMainWindow::setWindowTitle(t);
}

void BrowserWindow::enterHtmlFullScreen()
{
//	toggleFullScreen();
	/// Empty
}

void BrowserWindow::toggleFullScreen()
{
	if (isFullScreen())
		showNormal();
	else
		showFullScreen();
}

void BrowserWindow::bookmarkPage()
{
	TabbedWebView* view{tabWidget()->webTab()->webView()};

	BookmarksUtils::addBookmarkDialog(this, view->url(), view->title());
}

void BrowserWindow::bookmarkAllTabs()
{
	BookmarksUtils::bookmarkAllTabsDialog(this, tabWidget());
}

void BrowserWindow::addBookmark(const QUrl& url, const QString& title)
{
	BookmarksUtils::addBookmarkDialog(this, url, title);
}

void BrowserWindow::tabWidgetIndexChanged(TabWidget* tbWidget)
{
	if (tabWidget()->count() < 1 || tabWidget() == tbWidget)
		return;

	// Change the tabs space for the restore action
	if (m_restoreAction && m_tabsSpaceSplitter->tabWidget())
		disconnect(m_restoreAction, SIGNAL(triggered()), m_tabsSpaceSplitter->tabWidget(), SLOT(restoreClosedTab()));

	// Update the current tab widget
	m_tabsSpaceSplitter->currentTabWidgetChanged(tbWidget);

	emit tabWidgetChanged(tbWidget);

	connect(m_restoreAction, SIGNAL(triggered()), m_tabsSpaceSplitter->tabWidget(), SLOT(restoreClosedTab()));

// 	AddressBar* addressBar = tabWidget()->webTab()->addressBar();
// 	if (addressBar && m_titleBar->addressBars()->indexOf(addressBar) != -1)
// 		m_titleBar->addressBars()->setCurrentWidget(addressBar);

	// Move the floating button to the new focused tabs space if the user wants
	if (m_fButton) {
		QRect tabWidgetRect = tbWidget->geometry();

		if (!tabWidgetRect.contains(tbWidget->mapFromGlobal(mapToGlobal(m_fButton->pos())))
			&& Application::instance()->floatingButtonFoloweMouse()) {
			m_fButton->tabWidgetChanged(tbWidget);
		}
	}
}

void BrowserWindow::shotBackground()
{
	// Citorva will explain this
	m_tabsSpaceSplitter->hide();
	if (m_fButton) m_fButton->hide();
	m_titleBar->hide();

	QPixmap *bg = new QPixmap(size());
	render(bg, QPoint(), QRect(0, 0, width(), height()));
	m_bg = new QImage(bg->toImage());
	m_tabsSpaceSplitter->show();
	//m_titleBar->show(); 地址栏/关闭,最大,最小化窗口栏!

	if (m_fButton) {
//		auto pos = m_tabsSpaceSplitter->pos();
//		m_fButton->move(pos);
		m_fButton->hide();
	}
	m_blur_bg = new QImage(applyBlur(m_bg, m_blur_radius));
}

QImage BrowserWindow::applyBlur(const QImage *src, qreal radius, bool quality, bool alphaOnly, int transposed)
{
	QPixmap ret(src->size());
	QPainter painter(&ret);
	{
		QPixmap big(QSize(src->width() + 2 * radius, src->height() + 2 * radius));
		QPainter big_painter(&big);

		big_painter.drawImage(QPoint(radius, radius), src->copy());

		{
			QPixmap	left(QSize(1, big.height())),
				right(QSize(1, big.height()));

			QPainter painter_left(&left),
				painter_right(&right);

			painter_left.drawImage(QPoint(0, radius), src->copy());
			painter_right.drawImage(QPoint(1 - src->width(), radius), src->copy());

			for (int i = 0; i < radius; i++) {
				big_painter.drawImage(QPoint(i, 0), left.toImage());
				big_painter.drawImage(QPoint(radius + src->width() + i, 0), right.toImage());
			}
		}

		{
			QPixmap top(QSize(big.width(), 1)),
				bottom(QSize(big.width(), 1));

			QPainter painter_top(&top),
				painter_bottom(&bottom);

			painter_top.drawImage(QPoint(0, -radius), big.toImage());
			painter_bottom.drawImage(QPoint(0, 1 + radius - big.height()), big.toImage());

			for (int i = 0; i < radius; i++) {
				big_painter.drawImage(QPoint(0, i), top.toImage());
				big_painter.drawImage(QPoint(0, radius + src->height() + i), bottom.toImage());
			}
		}

		QImage bgImage{big.toImage()};
		qt_blurImage(&big_painter, bgImage, radius, quality, alphaOnly, transposed);
		painter.drawImage(QPoint(-radius, -radius), big.toImage());
	}
	return ret.toImage();
}

void BrowserWindow::paintEvent(QPaintEvent* event)
{
	// Citorva will explain this
	QMainWindow::paintEvent(event);
	if (m_upd_ss) {
		m_upd_ss = false;
		shotBackground();
	}
}

void BrowserWindow::resizeEvent(QResizeEvent* event)
{
	// Move the floating button if need
	QRect screeRect{QApplication::desktop()->availableGeometry(this)};

	if (event->oldSize() == screeRect.size() && event->size() != screeRect.size())
		emit maximizeChanged(false, event->oldSize());
	else if (event->oldSize() != screeRect.size() && event->size() == screeRect.size())
		emit maximizeChanged(true, event->oldSize());

	QMainWindow::resizeEvent(event);

	shotBackground();
}

void BrowserWindow::keyPressEvent(QKeyEvent* event)
{
	if (Application::instance()->plugins()->processKeyPress(Application::ON_BrowserWindow, this, event))
		return;

	QMainWindow::keyPressEvent(event);
}

void BrowserWindow::keyReleaseEvent(QKeyEvent* event)
{
	if (Application::instance()->plugins()->processKeyRelease(Application::ON_BrowserWindow, this, event))
		return;

	QMainWindow::keyReleaseEvent(event);
}

void BrowserWindow::mouseMoveEvent(QMouseEvent *e)
{
	if ((e->pos().x() >= pos().x() && e->pos().x() <= (pos().x() + width())) ||
		(e->pos().y() >= pos().y() && e->pos().y() <= (pos().y() + height())))
		emit mouseOver(true);
	else
		emit mouseOver(false);
	QMainWindow::mouseMoveEvent(e);
}

void BrowserWindow::addTab()
{
	tabWidget()->addView(QUrl(), Application::NTT_SelectedNewEmptyTab, true);
	tabWidget()->setCurrentTabFresh(true);
}

void BrowserWindow::postLaunch()
{
	bool addTab{true};
	QUrl startUrl{};

	Application::AfterLaunch launchAction = Application::instance()->afterLaunch();

// 	if (Application::instance()->isStartingAfterCrash()) {
// 		launchAction = Application::instance()->afterCrashLaunch();
// 	}

	switch (launchAction) {
	case Application::OpenBlankPage:
		startUrl = QUrl();
		break;
	case Application::OpenHomePage:
	case Application::OpenSavedSession:
	case Application::RestoreSession:
		startUrl = m_homePage;
		break;

	default:
		break;
	}

	switch (m_windowType) {
	case Application::WT_FirstAppWindow:
		// Check if we start after a crash or if we want to restore session
		if (Application::instance()->isStartingAfterCrash()) {
			if (Application::instance()->afterCrashLaunch() == Application::AfterLaunch::RestoreSession)
				addTab = m_tabsSpaceSplitter->count() <= 0;
		}
		else if (Application::instance()->afterLaunch() == Application::AfterLaunch::RestoreSession ||
				 Application::instance()->afterLaunch() == Application::AfterLaunch::OpenSavedSession) {
			addTab = m_tabsSpaceSplitter->count() <= 0;
		}
		break;
	case Application::WT_NewWindow:
		addTab = true;
		break;
	case Application::WT_OtherRestoredWindow:
		addTab = false;
		break;

	}

	showFullScreen();
	
	qDebug() << "pos "<<pos().rx() << "," << pos().ry();
	qDebug() << "size " << size().width() << "," << size().height();
	auto pos = this->pos();
	pos.setX(pos.rx()-7);
	pos.setY(pos.ry()-7);
	move(pos);

	auto x = size();
	x.setWidth(size().width() + 14);
	x.setHeight(size().height() + 14);
	resize(x);

	if (!m_startUrl.isEmpty()) {
		startUrl = m_startUrl;
		addTab = true;
	}
	if (m_startTab) {
		addTab = false;
		m_tabsSpaceSplitter->createNewTabsSpace();
		tabWidget()->addView(m_startTab, Application::NTT_SelectedTabAtEnd);
	}

	if (m_startPage) {
		addTab = false;
		tabWidget()->addView(QUrl());
		tabWidget()->webTab()->webView()->setPage(m_startPage);
	}

	if (addTab) {
		m_tabsSpaceSplitter->createNewTabsSpace();
		tabWidget()->addView(startUrl, Application::NTT_CleanSelectedTabAtEnd);
	}

	if (tabWidget()->tabBar()->normalTabsCount() <= 0)
		tabWidget()->addView(m_homePage, Application::NTT_SelectedTabAtEnd);

	//TODO: emit main window created to plugins
	m_restoreAction = new QAction(tr("Restore Closed Tab"), this);
	m_restoreAction->setShortcut(QKeySequence("Ctrl+Shift+T"));
	this->addAction(m_restoreAction);
	connect(m_restoreAction, SIGNAL(triggered()), tabWidget(), SLOT(restoreClosedTab()));

	//tabWidget()->tabBar()->ensureVisible();
	if (Application::instance()->showFloatingButton() && !m_fButton)
		setupFloatingButton();
	else if (!Application::instance()->showFloatingButton() && m_fButton) {
		delete m_fButton;
		m_fButton = nullptr;
	}

// 	if (m_fButton)
// 		m_fButton->tabWidgetChanged(tabWidget());

    if (m_fButton)
        m_fButton->hide();

// 	m_fButton->expandAroundA(m_fButton->pos());
// 	m_fButton->closeButton();

	Application::instance()->plugins()->emitMainWindowCreated(this);

	Settings settings{};

	// Show the "getting started" page if it's the first time Sielo is launch
	if (!settings.value("installed", false).toBool()) {
#ifndef QT_DEBUG
		Application::instance()->piwikTraker()->sendEvent("installation", "installation", "installation", "new installation");
#endif
// 		CheckBoxDialog dialog(QMessageBox::Ok, this);
// 		dialog.setWindowTitle(tr("Floating Button"));
// 		dialog.setText(tr("Do you want to enable floating button? This button can always be enable/disable later in settings."));
// 		dialog.setCheckBoxText(tr("Enable"));
// 		dialog.setIcon(QMessageBox::Information);
// 		dialog.exec();
// 
 		settings.setValue("Settings/showFloatingButton", true);
 		settings.setValue("installed", true);

		Application::instance()->loadSettings();
	}
}


void BrowserWindow::floatingButtonPatternChange(RootFloatingButton::Pattern pattern)
{
	if (pattern != RootFloatingButton::Pattern::Floating) {
		m_fButton->tabWidgetChanged(tabWidget());
	}
}


void BrowserWindow::clearPanit() {
    PLOGD << "DO clearPanit";
    DrawWidget::Instance()->doClener();
}

//缩放动作
void BrowserWindow::doZoom(int n) {
    //m_zoom;
    auto view = m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->webView();
    if(view)
        view->applyZoom(n);
}

void BrowserWindow::openPanit(bool r)
{
    if (r) {
        DrawWidget::Instance()->show();
    }
    else {
        DrawWidget::Instance()->doClener();
        DrawWidget::Instance()->doClener();
        DrawWidget::Instance()->hide();
        delete DrawWidget::self;
        DrawWidget::self = nullptr;
    }
}

void BrowserWindow::closePanit()
{
     DrawWidget::Instance()->close();
}

///蒙层+标记
void  BrowserWindow::on_mark_window()
{
	qDebug() << "on_mark_window";
	if (DrawWidget::self){
		DrawWidget::self->close();
		DrawWidget::self = nullptr;
	}
	else {
		auto tab = m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab();
		auto tabsize = tab->size();
		tabsize.setWidth(tabsize.width());
		auto pos = this->pos();
		pos.setY(pos.y() );
		pos.setX(pos.x() );
		DrawWidget::Instance()->move(pos);
		DrawWidget::Instance()->show();
	}
}

void BrowserWindow::newWindow()
{
	m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->sNewWindow();
}

void BrowserWindow::goNewUrl(QString r)
{
    m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->webView()->load(QUrl(r));
	//m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->loadNewUrl(QUrl(r));

    QList<QTouchEvent::TouchPoint> touchPoints;
    touchPoints.append(QTouchEvent::TouchPoint(0));

    /*state
        TouchPointPressed    = 0x01,
        TouchPointMoved      = 0x02,
        TouchPointStationary = 0x04,
        TouchPointReleased   = 0x08
    */
    touchPoints[0].setState(Qt::TouchPointPressed); 
    touchPoints[0].setRect(QRect(11, 11, 0, 0));
    /*event
        TouchBegin = 194,
        TouchUpdate = 195,
        TouchEnd = 196,
    */
    QTouchEvent touchEvent(QEvent::TouchBegin,
        0 /* device */,
        Qt::NoModifier,
        Qt::TouchPointPressed,
        touchPoints);

}

void BrowserWindow::setColor(QString r)
{
    PLOGD << "do setColor :" << r.toStdString();
    DrawWidget::Instance()->setColor(r);
}


///转到主页
void BrowserWindow::goHome()
{
	m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->sGoHome();
}

void BrowserWindow::forward()
{
	m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->webView()->forward();
}

void BrowserWindow::back()
{
	m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->webView()->back();
}

void BrowserWindow::newTab()
{
	LoadRequest request{};
	request.setUrl(m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->urlOnNewTab());
	m_tabsSpaceSplitter->tabWidget(m_fButton->pos())->webTab()->webView()->loadInNewTab(request, Application::NTT_CleanSelectedTabAtEnd);
}

void BrowserWindow::setupUi()
{
	QWidget* centralWidget{new QWidget(this)};
	m_layout = new QVBoxLayout(centralWidget);

	m_layout->setSpacing(0);
	m_layout->setContentsMargins(0, 0, 0, 0);

	m_titleBar = new TitleBar(this);
	m_bookmarksToolbar = new BookmarksToolbar(this, this);
	m_tabsSpaceSplitter = new TabsSpaceSplitter(this);

	m_titleBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	m_bookmarksToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	m_tabsSpaceSplitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	QPalette palette{QToolTip::palette()};
	QColor color{palette.window().color()};

	color.setAlpha(0);
	palette.setColor(QPalette::Window, color);
	QToolTip::setPalette(palette);

	setMinimumWidth(300);
	///TODO: delete this line when settings will be implements

    //设置全屏
	QApplication::desktop()->screenGeometry().size();
	resize(QApplication::desktop()->size());
	//resize(1200, 1200);

// 	m_layout->addWidget(m_titleBar);
// 	m_layout->addWidget(m_bookmarksToolbar);
	m_layout->addWidget(m_tabsSpaceSplitter);

// 	m_layout->setStretchFactor(m_titleBar, 1);
// 	m_layout->setStretchFactor(m_bookmarksToolbar, 1);
	m_layout->setStretchFactor(m_tabsSpaceSplitter, 100);

	setCentralWidget(centralWidget);
	move(0, 0);
}

void BrowserWindow::setupFloatingButton()
{
	m_fButton = new RootFloatingButton(this, this);

	// Get floating button state
	QFile fButtonDataFile{DataPaths::currentProfilePath() + QLatin1String("/fbutton.dat")};

	if (fButtonDataFile.exists()) {
		fButtonDataFile.open(QIODevice::ReadOnly);

		QDataStream fButtonData{&fButtonDataFile};
		int version{0};
		int pattern;
		QPoint lastPosition{};

		fButtonData >> version;

		if (version <= 0x0003) {
			int buttonCount{0};

			fButtonData >> buttonCount;
			buttonCount = 1;
			QString button = "fbutton-new-window";
			QString toolTip{ "Open New Window" };

			for (int i{0}; i < buttonCount; ++i) {

				//fButtonData >> button;

				if (version >= 0x0003) {
					fButtonData >> toolTip;
				}
				else {
					if (button == "fbutton-next")
						toolTip = tr("Go Forward");
					else if (button == "fbutton-back")
						toolTip = tr("Go Back");
					else if (button == "fbutton-home")
						toolTip = tr("Go Home");
					else if (button == "fbutton-add-bookmark")
						toolTip = tr("Add Bookmark");
					else if (button == "fbutton-view-bookmarks")
						toolTip = tr("Show all Bookmarks");
					else if (button == "fbutton-view-history")
						toolTip = tr("Show History");
					else if (button == "fbutton-new-window") {
						toolTip = tr("Open New Window");
					}
					else if (button == "fbutton-new-tab")
						toolTip = tr("Open New Tab");
				}

				m_fButton->addButton(button, toolTip);
			}

			if (version >= 0x0002) {
				fButtonData >> pattern;
				fButtonData >> lastPosition;
				m_fButton->setPattern(static_cast<RootFloatingButton::Pattern>(pattern));
			}
		}
	}
	else { // Load default floating button
		m_fButton->addButton("fbutton-next", tr("Go Forward"));
		m_fButton->addButton("fbutton-back", tr("Go Back"));
		m_fButton->addButton("fbutton-home", tr("Go Home"));
		m_fButton->addButton("fbutton-add-bookmark", tr("Add Bookmark"));
		m_fButton->addButton("fbutton-view-bookmarks", tr("Show all Bookmarks"));
		m_fButton->addButton("fbutton-view-history", tr("Show History"));
		m_fButton->addButton("fbutton-new-window", tr("Open New Window"));
		m_fButton->addButton("fbutton-new-tab", tr("Open New Tab"));
	}

	connect(m_fButton, &RootFloatingButton::statusChanged, this, &BrowserWindow::saveButtonState);
	connect(m_fButton, &RootFloatingButton::patternChanged, this, &BrowserWindow::floatingButtonPatternChange);

	connect(m_fButton->button("fbutton-view-bookmarks"), &FloatingButton::isClicked, tabWidget(),
			&TabWidget::openBookmarksDialog);
	connect(m_fButton->button("fbutton-view-history"), &FloatingButton::isClicked, tabWidget(),
			&TabWidget::openHistoryDialog);
	connect(m_fButton->button("fbutton-add-bookmark"), &FloatingButton::isClicked, this,
			&BrowserWindow::bookmarkPage);
	connect(m_fButton->button("fbutton-new-window"), &FloatingButton::isClicked, this, &BrowserWindow::newTab);
	/// 这个按钮改成,标记按钮
	connect(m_fButton->button("fbutton-new-tab"), &FloatingButton::isClicked, this, &BrowserWindow::newTab);
	connect(m_fButton->button("fbutton-home"), &FloatingButton::isClicked, this, &BrowserWindow::goHome);
	connect(m_fButton->button("fbutton-next"), &FloatingButton::isClicked, this, &BrowserWindow::forward);
	connect(m_fButton->button("fbutton-back"), &FloatingButton::isClicked, this, &BrowserWindow::back);

// 	m_fButton->button("fbutton-view-bookmarks")->hide();
// 	m_fButton->button("fbutton-view-history")->hide();
// 	m_fButton->button("fbutton-view-bookmark")->hide();
}

void BrowserWindow::saveButtonState()
{
	QByteArray data{};
	QDataStream stream{&data, QIODevice::WriteOnly};

	stream << 0x0003;
	stream << m_fButton->buttons().size();

	for (int i{0}; i < m_fButton->buttons().size(); ++i) {
		foreach(FloatingButton* button, m_fButton->buttons())
		{
			if (button->index() == i) {
				stream << button->objectName();
				stream << button->toolTip();
				break;
			}
		}
	}

	stream << m_fButton->pattern();

	QFile fButtonFile{DataPaths::currentProfilePath() + QLatin1String("/fbutton.dat")};

	fButtonFile.open(QIODevice::WriteOnly);
	fButtonFile.write(data);
	fButtonFile.close();
}

void BrowserWindow::addCaption(const QWidget* widget)
{
	if (!isCaption(widget)) {
		m_captionWidgets.push_back(widget);
	}
}

void BrowserWindow::removeCaption(const QWidget* widget)
{
	for (int i = 0; i < m_captionWidgets.length(); i++) {
		if (m_captionWidgets[i] == widget) {
			m_captionWidgets.removeAt(i);
			return;
		}
	}
}

void BrowserWindow::dosignalopenPanit(bool r){
    Q_EMIT signalopenPanit(r);
}

void BrowserWindow::dosignalclearPanit() {
    Q_EMIT signalclearPanit();
}

void BrowserWindow::dosignalzoom(int n) {
    Q_EMIT signalzoom(n);
}

void BrowserWindow::dosignalsetcolor(QString r) {
    Q_EMIT signalsetcolor(r);
}
void BrowserWindow::dosignalgoNewUrl(QString r) {
    Q_EMIT signalgoNewUrl(r);
}

bool BrowserWindow::isCaption(const QWidget* widget)
{
	for (int i = 0; i < m_captionWidgets.length(); i++) {
		if (m_captionWidgets[i] == widget)
			return true;
	}
	return false;
}

#ifdef Q_OS_WIN
bool BrowserWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
	Q_UNUSED(eventType);
	const MSG* wMsg = reinterpret_cast<MSG*>(message);
	const UINT wMessage = wMsg->message;
	bool hasHandled = false;
	long res = 0;

	if (DWMEnabled())
		hasHandled = DwmDefWindowProc(wMsg->hwnd, wMessage, wMsg->wParam,
									  wMsg->lParam, reinterpret_cast<LRESULT*>(&res));

	if (wMessage == WM_NCCALCSIZE && wMsg->wParam == TRUE) {
		NCCALCSIZE_PARAMS& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(wMsg->lParam);
		adjust_maximized_client_rect(wMsg->hwnd, params.rgrc[0]);
		res = 0;
		hasHandled = true;
	}
	else if (wMessage == WM_NCHITTEST && res == 0) {
		res = ncHitTest(wMsg);

		if (res != HTNOWHERE)
			hasHandled = true;
	}
	//else if (wMessage == WM_NCPAINT)
	//	hasHandled = true;

	if (hasHandled)
		*result = res;
	return hasHandled;
}

long BrowserWindow::ncHitTest(const MSG* wMsg) const
{
	const long ncHitZone[3][4] = {
		{HTTOPLEFT, HTLEFT, HTLEFT, HTBOTTOMLEFT},
		{HTTOP, HTCAPTION, HTNOWHERE, HTBOTTOM},
		{HTTOPRIGHT, HTRIGHT, HTRIGHT, HTBOTTOMRIGHT}
	};

	const QPoint cursor(GET_X_LPARAM(wMsg->lParam), GET_Y_LPARAM(wMsg->lParam));
	const UINT8 borderSize = 2;
	UINT8 xPos = 1;
	UINT8 yPos = 2;

	RECT rcWin;
	GetWindowRect(wMsg->hwnd, &rcWin);

	for (const auto &e : m_captionWidgets)
		if (e == QApplication::widgetAt(QCursor::pos()))
			return HTCAPTION;

	RECT rcFrame = {0};
	AdjustWindowRectEx(&rcFrame, WS_OVERLAPPEDWINDOW & ~WS_CAPTION, FALSE, NULL);

	if (cursor.y() >= rcWin.top && cursor.y() <= rcWin.top + borderSize)
		yPos = 0;
	else if (cursor.y() >= rcWin.top + borderSize && cursor.y() <= rcWin.top + borderSize)
		yPos = 1;
	else if (cursor.y() >= rcWin.bottom - borderSize && cursor.y() < rcWin.bottom)
		yPos = 3;

	if (cursor.x() >= rcWin.left && cursor.x() < rcWin.left + borderSize)
		xPos = 0;
	else if (cursor.x() >= rcWin.right - borderSize && cursor.x() < rcWin.right)
		xPos = 2;

	return ncHitZone[xPos][yPos];
}

#endif
}


#include "qfiledialog.h"
#if (QT_VERSION > QT_VERSION_CHECK(5,0,0))
#include "qscreen.h"
#endif

#if _MSC_VER >= 1600
#pragma execution_character_set("utf-8")
#endif

#define AppPath qApp->applicationDirPath()
#define STRDATETIME qPrintable (QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss"))

Screen::Screen(QSize size)
{
	maxWidth = size.width();
	maxHeight = size.height();

	startPos = QPoint(-1, -1);
	endPos = startPos;
	leftUpPos = startPos;
	rightDownPos = startPos;
	status = SELECT;
}

int Screen::width()
{
	return maxWidth;
}

int Screen::height()
{
	return maxHeight;
}

Screen::STATUS Screen::getStatus()
{
	return status;
}

void Screen::setStatus(STATUS status)
{
	this->status = status;
}

void Screen::setEnd(QPoint pos)
{
	endPos = pos;
	leftUpPos = startPos;
	rightDownPos = endPos;
	cmpPoint(leftUpPos, rightDownPos);
}

void Screen::setStart(QPoint pos)
{
	startPos = pos;
}

QPoint Screen::getEnd()
{
	return endPos;
}

QPoint Screen::getStart()
{
	return startPos;
}

QPoint Screen::getLeftUp()
{
	return leftUpPos;
}

QPoint Screen::getRightDown()
{
	return rightDownPos;
}

bool Screen::isInArea(QPoint pos)
{
	if (pos.x() > leftUpPos.x() && pos.x() < rightDownPos.x() && pos.y() > leftUpPos.y() && pos.y() < rightDownPos.y()) {
		return true;
	}

	return false;
}

void Screen::move(QPoint p)
{
	int lx = leftUpPos.x() + p.x();
	int ly = leftUpPos.y() + p.y();
	int rx = rightDownPos.x() + p.x();
	int ry = rightDownPos.y() + p.y();

	if (lx < 0) {
		lx = 0;
		rx -= p.x();
	}

	if (ly < 0) {
		ly = 0;
		ry -= p.y();
	}

	if (rx > maxWidth) {
		rx = maxWidth;
		lx -= p.x();
	}

	if (ry > maxHeight) {
		ry = maxHeight;
		ly -= p.y();
	}

	leftUpPos = QPoint(lx, ly);
	rightDownPos = QPoint(rx, ry);
	startPos = leftUpPos;
	endPos = rightDownPos;
}

void Screen::cmpPoint(QPoint &leftTop, QPoint &rightDown)
{
	QPoint l = leftTop;
	QPoint r = rightDown;

	if (l.x() <= r.x()) {
		if (l.y() <= r.y()) {
			;
		}
		else {
			leftTop.setY(r.y());
			rightDown.setY(l.y());
		}
	}
	else {
		if (l.y() < r.y()) {
			leftTop.setX(r.x());
			rightDown.setX(l.x());
		}
		else {
			QPoint tmp;
			tmp = leftTop;
			leftTop = rightDown;
			rightDown = tmp;
		}
	}
}

ScreenWidget *ScreenWidget::self = 0;
ScreenWidget::ScreenWidget(QWidget *parent) : QWidget(parent)
{
	this->initForm();
	showFullScreen();
}

void ScreenWidget::initForm()
{
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

	menu = new QMenu(this);
	menu->addAction("保存截图", this, SLOT(saveScreen()));
	menu->addAction("截图另存为", this, SLOT(saveScreenOther()));
	menu->addAction("全屏截图", this, SLOT(saveFullScreen()));
	menu->addAction("退出截图", this, SLOT(hide()));

	//取得屏幕大小
	screen = new Screen(QApplication::desktop()->size());
	//保存全屏图像
	fullScreen = new QPixmap();
}

void ScreenWidget::paintEvent(QPaintEvent *)
{
	int x = screen->getLeftUp().x();
	int y = screen->getLeftUp().y();
	int w = screen->getRightDown().x() - x;
	int h = screen->getRightDown().y() - y;

	QPainter painter(this);

	QPen pen;
	pen.setColor(Qt::green);
	pen.setWidth(2);
	pen.setStyle(Qt::DotLine);
	painter.setPen(pen);

	QFont font;
	font.setFamily("Microsoft YaHei");
	font.setPointSize(10);
	painter.setFont(font);

	painter.drawPixmap(0, 0, *bgScreen);

	if (w != 0 && h != 0) {
		painter.drawPixmap(x, y, fullScreen->copy(x, y, w, h));
	}

	painter.drawRect(x, y, w, h);

	pen.setColor(Qt::yellow);
	painter.setPen(pen);
	painter.drawText(x + 2, y - 8, tr("截图范围：( %1 x %2 ) - ( %3 x %4 )  图片大小：( %5 x %6 )")
		.arg(x).arg(y).arg(x + w).arg(y + h).arg(w).arg(h));
}

void ScreenWidget::showEvent(QShowEvent *)
{
	QPoint point(-1, -1);
	screen->setStart(point);
	screen->setEnd(point);

#if (QT_VERSION <= QT_VERSION_CHECK(5,0,0))
	*fullScreen = fullScreen->grabWindow(QApplication::desktop()->winId(), 0, 0, screen->width(), screen->height());
#else
	::QScreen *pscreen = QApplication::primaryScreen();
	*fullScreen = pscreen->grabWindow(QApplication::desktop()->winId(), 0, 0, screen->width(), screen->height());
#endif

	//设置透明度实现模糊背景
	::QPixmap pix(screen->width(), screen->height());
	pix.fill((QColor(160, 160, 160, 200)));
	bgScreen = new ::QPixmap(*fullScreen);
	QPainter p(bgScreen);
	p.drawPixmap(0, 0, pix);
}

void ScreenWidget::saveScreenOther()
{
	QString fileName = QFileDialog::getSaveFileName(this, "保存图片", STRDATETIME, "JPEG Files (*.jpg)");

	if (fileName.length() > 0) {
		int x = screen->getLeftUp().x();
		int y = screen->getLeftUp().y();
		int w = screen->getRightDown().x() - x;
		int h = screen->getRightDown().y() - y;

		fullScreen->copy(x, y, w, h).save(fileName, "jpg");

		close();
	}
}

void ScreenWidget::saveScreen()
{
	int x = screen->getLeftUp().x();
	int y = screen->getLeftUp().y();
	int w = screen->getRightDown().x() - x;
	int h = screen->getRightDown().y() - y;

	QString fileName = QString("%1/screen_%2.jpg").arg(AppPath).arg(STRDATETIME);
	fullScreen->copy(x, y, w, h).save(fileName, "jpg");

	close();
}

void ScreenWidget::saveFullScreen()
{
	QString fileName = QString("%1/full_%2.jpg").arg(AppPath).arg(STRDATETIME);
	fullScreen->save(fileName, "jpg");
	close();
}

void ScreenWidget::mouseMoveEvent(QMouseEvent *e)
{
	if (screen->getStatus() == Screen::SELECT) {
		screen->setEnd(e->pos());
	}
	else if (screen->getStatus() == Screen::MOV) {
		QPoint p(e->x() - movPos.x(), e->y() - movPos.y());
		screen->move(p);
		movPos = e->pos();
	}

	update();
}

void ScreenWidget::mousePressEvent(QMouseEvent *e)
{
	int status = screen->getStatus();

	if (status == Screen::SELECT) {
		screen->setStart(e->pos());
	}
	else if (status == Screen::MOV) {
		if (screen->isInArea(e->pos()) == false) {
			screen->setStart(e->pos());
			screen->setStatus(Screen::SELECT);
		}
		else {
			movPos = e->pos();
			this->setCursor(Qt::SizeAllCursor);
		}
	}

	update();
}

void ScreenWidget::mouseReleaseEvent(QMouseEvent *)
{
	if (screen->getStatus() == Screen::SELECT) {
		screen->setStatus(Screen::MOV);
	}
	else if (screen->getStatus() == Screen::MOV) {
		this->setCursor(Qt::ArrowCursor);
	}
}

void ScreenWidget::contextMenuEvent(QContextMenuEvent *)
{
	this->setCursor(Qt::ArrowCursor);
	menu->exec(cursor().pos());
}

#include <QColorDialog>
DrawWidget *DrawWidget::self = 0;
DrawWidget::DrawWidget(QWidget *parent) :
	QWidget(parent)
{
	setupUi();

	this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    this->resize(QApplication::desktop()->size());
    this->resize(QSize(720,1280));

    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_PaintOnScreen);

    hide();
    frame->hide();

    QPixmap pix(this->size().width(), this->size().height());
    pix.fill((QColor(0, 0, 0, 1)));
    image = pix.toImage();

	tempImage = image;  //将临时画布设置成与主画布相同的状态
	drawing = false;    //默认未绘图
	nshape = 0;			//默认涂鸦,画笔,形状

	//绘画数据初始化
	shape->setChecked(true);
	width = 0;
	heigh = 0;
	for (int i = 0; i < 3; i++) {
		pointPolygon[i].setX(0);
		pointPolygon[i].setY(0);
	}
	lineEdit.setParent(this);
	lineEdit.resize(70, 20);
	lineEdit.setText(" ");
	lineEdit.setVisible(false);

	penWidth->setValue(3);
	penColor.setNamedColor("red");
}

DrawWidget::~DrawWidget()
{
}

void DrawWidget::paintEvent(QPaintEvent *event) //重写窗口重绘事件
{
	/*
	 *采用双缓冲绘图，以矩形为例，当绘制矩形时，
	 * 鼠标左键未松开（移动），将图形绘制在临时画布（即预览）
	 * 在鼠标松开后，将临时画布上的图形copy到主画布上，
	 * 显示主画布
	 * */
	QPainter painter(this);
	if (drawing == true){
		painter.drawImage(0, 0, tempImage);		//鼠标按住但在拖动时在临时画布上画
	}
	else {
		painter.drawImage(0, 0, image);			//在image上绘画
	}
}

void drawArrow(QPoint startPoint, QPoint endPoint, QPainter &p, QColor penColor)
{
	double par = 27.0;//箭头部分三角形的腰长
	double slopy = atan2((endPoint.y() - startPoint.y()), (endPoint.x() - startPoint.x()));
	double cosy = cos(slopy);
	double siny = sin(slopy);
	QPoint point1 = QPoint(endPoint.x() + int(-par * cosy - (par / 2.0*siny)), endPoint.y() + int(-par * siny + (par / 2.0*cosy)));
	QPoint point2 = QPoint(endPoint.x() + int(-par * cosy + (par / 2.0*siny)), endPoint.y() - int(par / 2.0*cosy + par * siny));
	QPoint points[3] = { endPoint, point1, point2 };
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	QPen drawTrianglePen;//创建画笔
	drawTrianglePen.setColor(penColor);
	drawTrianglePen.setWidth(1);
	p.setPen(drawTrianglePen);
	p.drawPolygon(points, 3);// 绘制箭头部分 还需要填充颜色

	QPolygonF pts;
	QPainterPath path;
	pts.push_back(endPoint);
	pts.push_back(point1);
	pts.push_back(point2);
	path.addPolygon(pts);
	p.fillPath(path, QBrush(penColor));

	int offsetX = int(par*siny / 4);
	int offsetY = int(par*cosy / 4);
	QPoint point3, point4;
	point3 = QPoint(endPoint.x() + int(-par * cosy - (par / 2.0*siny)) + offsetX, endPoint.y() + int(-par * siny + (par / 2.0*cosy)) - offsetY);
	point4 = QPoint(endPoint.x() + int(-par * cosy + (par / 2.0*siny) - offsetX), endPoint.y() - int(par / 2.0*cosy + par * siny) + offsetY);
	QPoint arrowBodyPoints[3] = { startPoint, point3, point4 };
	p.drawPolygon(arrowBodyPoints, 3);	//绘制箭身部分
	pts.clear();
	pts.push_back(startPoint);
	pts.push_back(point3);
	pts.push_back(point4);
	path.addPolygon(pts);
	p.fillPath(path, QBrush(penColor));
}

void DrawWidget::paint(QImage &theImage)        //绘图
{
    QPen pen(penColor, penWidth->value(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    QPainter thePainter(&theImage);
    thePainter.setPen(pen);
    thePainter.setRenderHint(QPainter::HighQualityAntialiasing, true);
    thePainter.setCompositionMode(QPainter::CompositionMode_Source);

	//绘图
	switch (nshape) {
	case 0:thePainter.drawLine(change, point);
		change = point;
		break;
	case 1: 
		drawArrow(from, point, thePainter, penColor);
		break;   
	case 2:thePainter.drawRect(from.x(), from.y(), width, heigh);
		break;
// 	case 3:thePainter.eraseRect(point.x(), point.y(), penWidth->value() + 5, penWidth->value() + 5);
// 		break;
	case 4:thePainter.drawEllipse(from.x(), from.y(), width, heigh);
		break;
	case 5:
		lineEdit.move(point.x(), point.y());
		lineEdit.setVisible(true);
		thePainter.drawText(change, lineEdit.text());
		lineEdit.clear();
		if (lineEdit.text() != ""){
			lineEdit.setVisible(false);
		}
		break;
	default:break;
	}
	thePainter.end(); //结束绘图
	update();   //窗口重绘
}

void DrawWidget::setupUi()
{
	this->setObjectName(QString::fromUtf8("DrawWidget"));
	frame = new QFrame(this);
	frame->setObjectName(QString::fromUtf8("frame"));
	frame->setFrameShape(QFrame::StyledPanel);
	frame->setFrameShadow(QFrame::Raised);
	verticalLayout = new QVBoxLayout(frame);
	verticalLayout->setSpacing(6);
	verticalLayout->setContentsMargins(11, 11, 11, 11);
	verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
	widget_2 = new QWidget(frame);
	widget_2->setObjectName(QString::fromUtf8("widget_2"));
	gridLayout_3 = new QGridLayout(widget_2);
	gridLayout_3->setSpacing(6);
	gridLayout_3->setContentsMargins(11, 11, 11, 11);
	gridLayout_3->setObjectName(QString::fromUtf8("gridLayout_3"));
	label = new QLabel(widget_2);
	label->setObjectName(QString::fromUtf8("label"));

	gridLayout_3->addWidget(label, 0, 0, 1, 1);

	penWidth = new QSlider(widget_2);
	penWidth->setObjectName(QString::fromUtf8("penWidth"));
	penWidth->setMinimum(1);
	penWidth->setMaximum(100);
	penWidth->setOrientation(Qt::Horizontal);

	gridLayout_3->addWidget(penWidth, 0, 1, 1, 1);

	label_6 = new QLabel(widget_2);
	label_6->setObjectName(QString::fromUtf8("label_6"));

	gridLayout_3->addWidget(label_6, 1, 0, 1, 1);

	pushButton = new QPushButton(widget_2);
	pushButton->setObjectName(QString::fromUtf8("pushButton"));
	QIcon icon;
	icon.addFile(QString::fromUtf8(":/Core/image/rgb.png"), QSize(), QIcon::Normal, QIcon::Off);
	pushButton->setIcon(icon);
	pushButton->setIconSize(QSize(200, 200));

	gridLayout_3->addWidget(pushButton, 1, 1, 1, 1);
	verticalLayout->addWidget(widget_2);

	widget_5 = new QWidget(frame);
	widget_5->setObjectName(QString::fromUtf8("widget_5"));
	verticalLayout_2 = new QVBoxLayout(widget_5);
	verticalLayout_2->setSpacing(6);
	verticalLayout_2->setContentsMargins(11, 11, 11, 11);
	verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
	label_2 = new QLabel(widget_5);
	label_2->setObjectName(QString::fromUtf8("label_2"));

	verticalLayout_2->addWidget(label_2);

	widget_3 = new QWidget(widget_5);
	widget_3->setObjectName(QString::fromUtf8("widget_3"));
	gridLayout_2 = new QGridLayout(widget_3);
	gridLayout_2->setSpacing(6);
	gridLayout_2->setContentsMargins(11, 11, 11, 11);
	gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
	label_3 = new QLabel(widget_3);
	label_3->setObjectName(QString::fromUtf8("label_3"));
	label_3->setPixmap(QPixmap(QString::fromUtf8(":/Core/image/line.png")));

	gridLayout_2->addWidget(label_3, 1, 0, 1, 1);

	label_9 = new QLabel(widget_3);
	label_9->setObjectName(QString::fromUtf8("label_9"));
	label_9->setPixmap(QPixmap(QString::fromUtf8(":/Core/image/edit.png")));

	gridLayout_2->addWidget(label_9, 5, 2, 1, 1);

	label_5 = new QLabel(widget_3);
	label_5->setObjectName(QString::fromUtf8("label_5"));
	label_5->setPixmap(QPixmap(QString::fromUtf8(":/Core/image/quxian.png")));

	gridLayout_2->addWidget(label_5, 4, 0, 1, 1);

	radioButton_4 = new QRadioButton(widget_3);
	radioButton_4->setObjectName(QString::fromUtf8("radioButton_4"));

	gridLayout_2->addWidget(radioButton_4, 5, 1, 1, 1);

	radioButton_5 = new QRadioButton(widget_3);
	radioButton_5->setObjectName(QString::fromUtf8("radioButton_5"));

	gridLayout_2->addWidget(radioButton_5, 5, 3, 1, 1);

	shape = new QRadioButton(widget_3);
	shape->setObjectName(QString::fromUtf8("shape"));

	gridLayout_2->addWidget(shape, 4, 1, 1, 1);

	radioButton_3 = new QRadioButton(widget_3);
	radioButton_3->setObjectName(QString::fromUtf8("radioButton_3"));

	gridLayout_2->addWidget(radioButton_3, 4, 3, 1, 1);

	label_4 = new QLabel(widget_3);
	label_4->setObjectName(QString::fromUtf8("label_4"));
	label_4->setPixmap(QPixmap(QString::fromUtf8(":/Core/image/rect.png")));

	gridLayout_2->addWidget(label_4, 1, 2, 1, 1);

	label_8 = new QLabel(widget_3);
	label_8->setObjectName(QString::fromUtf8("label_8"));
	label_8->setPixmap(QPixmap(QString::fromUtf8(":/Core/image/3jiaoxing.png")));

	gridLayout_2->addWidget(label_8, 5, 0, 1, 1);

	radioButton = new QRadioButton(widget_3);
	radioButton->setObjectName(QString::fromUtf8("radioButton"));

	gridLayout_2->addWidget(radioButton, 1, 1, 1, 1);

	radioButton_2 = new QRadioButton(widget_3);
	radioButton_2->setObjectName(QString::fromUtf8("radioButton_2"));

	gridLayout_2->addWidget(radioButton_2, 1, 3, 1, 1);

	label_7 = new QLabel(widget_3);
	label_7->setObjectName(QString::fromUtf8("label_7"));
	label_7->setPixmap(QPixmap(QString::fromUtf8(":/Core/image/eraser.png")));

	gridLayout_2->addWidget(label_7, 4, 2, 1, 1);
	verticalLayout_2->addWidget(widget_3);
	verticalLayout->addWidget(widget_5);

	widget_4 = new QWidget(frame);
	widget_4->setObjectName(QString::fromUtf8("widget_4"));
	horizontalLayout = new QHBoxLayout(widget_4);
	horizontalLayout->setSpacing(6);
	horizontalLayout->setContentsMargins(11, 11, 11, 11);
	horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
	pushButton_2 = new QPushButton(widget_4);
	pushButton_2->setObjectName(QString::fromUtf8("pushButton_2"));

	horizontalLayout->addWidget(pushButton_2);

	pushButton_3 = new QPushButton(widget_4);
	pushButton_3->setObjectName(QString::fromUtf8("pushButton_3"));

	horizontalLayout->addWidget(pushButton_3);
	verticalLayout->addWidget(widget_4);

	pushButton->setDefault(false);

	label->setText(QApplication::translate("DrawWidget", "\347\224\273\347\254\224\347\262\227\347\273\206", nullptr));
	label_6->setText(QApplication::translate("DrawWidget", "\347\224\273\347\254\224\351\242\234\350\211\262", nullptr));
	pushButton->setText(QString());
	label_2->setText(QApplication::translate("DrawWidget", "<html><head/><body><p align=\"center\"><span style=\" font-size:16pt;\">\345\275\242\347\212\266\351\200\211\346\213\251</span></p></body></html>", nullptr));
	label_3->setText(QString());
	label_9->setText(QString());
	label_5->setText(QString());
	radioButton_4->setText(QString());
	radioButton_5->setText(QString());
	shape->setText(QString());
	radioButton_3->setText(QString());
	label_4->setText(QString());
	label_8->setText(QString());
	radioButton->setText(QString());
	radioButton_2->setText(QString());
	label_7->setText(QString());
	pushButton_2->setText(QApplication::translate("DrawWidget", "\344\277\235\345\255\230", nullptr));
	pushButton_3->setText(QApplication::translate("DrawWidget", "\346\270\205\347\251\272", nullptr));


	connect(radioButton, SIGNAL(clicked()), this, SLOT(on_radioButton_clicked()));
	connect(radioButton_2, SIGNAL(clicked()), this, SLOT(on_radioButton_2_clicked()));
	connect(pushButton, SIGNAL(clicked()), this, SLOT(on_pushButton_clicked()));
	connect(shape, SIGNAL(clicked()), this, SLOT(on_shape_clicked()));
	connect(radioButton_3, SIGNAL(clicked()), this, SLOT(on_radioButton_3_clicked()));
	connect(pushButton_2, SIGNAL(clicked()), this, SLOT(on_pushButton_2_clicked()));
	connect(radioButton_4, SIGNAL(clicked()), this, SLOT(on_radioButton_4_clicked()));
	connect(pushButton_3, SIGNAL(clicked()), this, SLOT(on_pushButton_3_clicked()));
	connect(radioButton_5, SIGNAL(clicked()), this, SLOT(on_radioButton_5_clicked()));
	
	QString style = "QRadioButton::indicator{\
width:15px;\
height:15px;\
}\
QRadioButton::indicator::unchecked{\
image:url(:/icons/lightblue/radiobutton_unchecked.png);\
}\
QRadioButton::indicator::unchecked:disabled{\
image:url(:/icons/lightblue/radiobutton_unchecked_disable.png);\
}\
QRadioButton::indicator::checked{\
image:url(:/icons/lightblue/radiobutton_checked.png);\
}\
QRadioButton::indicator::checked:disabled{\
image:url(:/icons/lightblue/radiobutton_checked_disable.png);\
}";
	radioButton->setStyleSheet(style);
	radioButton_2->setStyleSheet(style);
	radioButton_3->setStyleSheet(style);
	radioButton_4->setStyleSheet(style);
	radioButton_5->setStyleSheet(style);
}

void DrawWidget::setShape(int n)
{
	if (DrawWidget::self) {
		nshape = n;
        PLOGD << "do cmd set nshape  " << n;
    }
    else {
        PLOGD << "do cmd set nshape err !   no self!";
    }
}

void DrawWidget::doClener()
{
	if (DrawWidget::self) {
		on_pushButton_3_clicked();
	}
}
void DrawWidget::setWidth(int n)
{
	if (DrawWidget::self) {
		penWidth->setValue(n);
	}
}
void DrawWidget::setColor(QString n)
{
    if (DrawWidget::self) {
        penColor = QColor(n);
    }
}

void DrawWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		qDebug() << "mouse LeftButton" << event->pos().rx() << ":" << event->pos().ry();
		drawing = true;
		point = event->pos();
		from = event->pos();
		change = event->pos();
		width = 0; heigh = 0;
		pointPolygon[0] = point;
		pointPolygon[1].setX(point.x());
	}
}

void DrawWidget::mouseMoveEvent(QMouseEvent *event)
{
	point = event->pos();
	width = point.x() - from.x();
	heigh = point.y() - from.y();
	pointPolygon[1].setY(point.y());
	pointPolygon[2] = point;
	tempImage = image;

	if (nshape == 0 || nshape == 3) {
		paint(image);
	}
	else {
		paint(tempImage);
	}
}

void DrawWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
	{
		qDebug() << "mouse LeftButton "<< event->pos().rx()<<":"<< event->pos().ry();
		to = event->pos();
		point = event->pos();
		width = to.x() - from.x();
		heigh = to.y() - from.y();
		pointPolygon[2] = point;
		drawing = false;

		paint(image);
	}
}

void DrawWidget::on_radioButton_clicked()
{
	nshape = 1;
}

void DrawWidget::on_radioButton_2_clicked()
{
	nshape = 2;
}

void DrawWidget::on_pushButton_clicked()
{
	QColorDialog color;//调出颜色选择器对话框
	penColor = color.getRgba(); //设置画笔颜色
}

void DrawWidget::on_shape_clicked()
{
	nshape = 0;
}

void DrawWidget::on_radioButton_3_clicked()
{
	nshape = 3;
}

void DrawWidget::on_pushButton_2_clicked()  //将画布内容保存
{
	QString filename = QFileDialog::getSaveFileName(this,
		tr("Save Image"),
		"",
		tr("*.bmp;; *.png;; *.jpg;; *.tif;; *.GIF")); //选择路径
	if (filename.isEmpty()) {
		return;
	}
	else{
		if (!(image.save(filename))){
			QMessageBox::information(this,
				tr("Failed to save the image"),
				tr("Failed to save the image!"));
			return;
		}
	}
}

void DrawWidget::on_radioButton_4_clicked()
{
	nshape = 4;
}

void DrawWidget::on_pushButton_3_clicked()
{
    QPixmap pix(this->size().width(), this->size().height());
    pix.fill((QColor(0, 0, 0, 1)));
    image = pix.toImage();
    tempImage = pix.toImage();
	update();
    PLOGD << "do signalclearPanit end";
}

void DrawWidget::on_radioButton_5_clicked()
{
	nshape = 5;
}


HttpServer::HttpServer(QObject *parent, void*hand) : QThread(parent),m_hand(hand)
{
    moveToThread(this);
}

HttpServer::~HttpServer()
{
    quit();
}

void HttpServer::run() {
    QHttpServer server(this);
    quint64 iconnectionCounter = 0;
    QString portOrUnixSocket("8010");
    server.listen(portOrUnixSocket, [&](QHttpRequest* req, QHttpResponse* res) {
        new ClientHandler(++iconnectionCounter, req, res);
    });

    if (!server.isListening()) {
        fprintf(stderr, "can not listen on %s!\n", qPrintable(portOrUnixSocket));
    }

    PLOGD << "start listen 8010";
    exec();
}

struct MouseData {
    int type;
    int action;
    int x;
    int y;
};

std::string typeToString(int t) {
    switch (t)
    {
    case 0:return "close panit";
    case 1:return "open panit";
    case 2:return "set shape";
    case 3:return "set width";
    case 4:return "set url";
    case 6:return "set color";
    case 7:return "clear paint";
    case 8:return "zoom";
    default:
        break;
    }
}

void HttpServer::gDealHttp(int type, QByteArray data)
{
    PLOGD << "do cmd type :" << typeToString(type) <<" data :"<< data.toStdString() <<" hand:"<< m_hand;
    if (type == 1) {	//打开画板
        if (data.toInt() == 1 && m_hand) {
            ((Sn::BrowserWindow*)m_hand)->dosignalopenPanit(true);
        }
        else if (data.toInt() == 0 && m_hand) {
            ((Sn::BrowserWindow*)m_hand)->dosignalopenPanit(false);
        }
    }
    else if (type == 2) {	//形状
        int i = 0;
        do{
            if (!DrawWidget::self) {
                Sleep(100);
            }
            else {
                DrawWidget::self->setShape(data.toInt());
            }
        } while (i++ < 3);

        if (!DrawWidget::self){
            PLOGD << "do cmd set shape err:no widget  " << data.toInt();
        }
    }
    else if (type == 3) {	//设置粗细
        int i = 0;
        do {
            if (!DrawWidget::self) {
                Sleep(100);
            }
            else {
                DrawWidget::self->setWidth(data.toInt());
            }
        } while (i++ < 3);

        if (!DrawWidget::self) {
            PLOGD << "do cmd set width err:no widget  " << data.toInt();
        }
    }
    else if (type == 4 && m_hand) {	//跳转网址
        if (data.size()) {
            ((Sn::BrowserWindow*)m_hand)->dosignalgoNewUrl(data.data());
        }
    }
   /* else if (type == 5) {
        QJsonDocument json = QJsonDocument::fromJson(data.data(), 0);
        QJsonObject obj = json.object();

        int x = (obj["x"].toInt() & 0xFFFF) / (65535 / float(1920));
        int y = (obj["y"].toInt() & 0xFFFF) / (65535 / float(1080));

        ((Sn::BrowserWindow*)m_hand)->postMouseEvent(
            (Sn::ButtonType)obj["type"].toInt(),
            (Sn::ButtonAction)obj["action"].toInt(),
            x,
            y);
        PLOGD << "do cmd set mouse  " << " x:"<<x<<" y:"<<y;
    }*/
    else if (type == 6) { //颜色
        if (data.size()) {
            ((Sn::BrowserWindow*)m_hand)->dosignalsetcolor(data.data());
        }
    }
    else if (type == 7 && m_hand) {
        ((Sn::BrowserWindow*)m_hand)->dosignalclearPanit();
    }
    else if (type == 8 && m_hand) {
        ((Sn::BrowserWindow*)m_hand)->dosignalzoom(data.toInt());
    }
    else {
        PLOGE << "unknow cmd type  " << type;
    }
}
