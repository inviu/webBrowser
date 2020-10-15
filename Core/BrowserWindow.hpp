#pragma once
#ifndef SIELOBROWSER_BROWSERWINDOW_HPP
#define SIELOBROWSER_BROWSERWINDOW_HPP

#include "SharedDefines.hpp"

#include <QMainWindow>

#include <QVBoxLayout>
#include <QSplitter>

#include <QVector>

#include <QUrl>

#include <QResizeEvent>
#include <QMutex>
#include <QWidget>
#include <QMenu>
#include <QPoint>
#include <QSize>
#include "Widgets/Tab/TabsSpaceSplitter.hpp"
#include "Widgets/FloatingButton.hpp"

#include "Application.hpp"


#include <QtNetwork>

#include "qhttpserver.hpp"
#include "qhttpserverconnection.hpp"
#include "qhttpserverrequest.hpp"
#include "qhttpserverresponse.hpp"
#include <QtCore/QCoreApplication>
#include <QDateTime>
#include <QLocale>

using namespace qhttp::server;

class HttpServer : public QThread
{
	Q_OBJECT
public:
	static HttpServer* HttpServer::instance(QObject *parent = nullptr, void*hand = 0)
	{
		static HttpServer obj(parent, hand);
		return &obj;
	}

	explicit HttpServer(QObject *parent = nullptr, void*hand=0);
	~HttpServer();
    Q_DISABLE_COPY(HttpServer)

    void run();
    void gDealHttp(int type, QByteArray data);

private:
	long m_lasttick = 0;
	bool m_screen = true;
	bool isrun = false;
	void* m_hand;
};

#include <plog/Log.h>

/** connection class for gathering incoming chunks of data from HTTP client.
    * @warning please note that the incoming request instance is the parent of
    * this QObject instance. Thus this class will be deleted automatically.
    * */
class ClientHandler : public QObject
{
public:
    explicit ClientHandler(quint64 id, QHttpRequest* req, QHttpResponse* res)
        : QObject(req /* as parent*/), iconnectionId(id) {

        // automatically collect http body(data) upto 1KB
        req->collectData(1024);

        // when all the incoming data are gathered, send some response to client.
        req->onEnd([this, req, res]() {
            qDebug("  connection (#%llu): request from %s:%d\n  method: %s url: %s",
                iconnectionId,
                req->remoteAddress().toUtf8().constData(),
                req->remotePort(),
                qhttp::Stringify::toString(req->method()),
                qPrintable(req->url().toString())
            );

            // 处理消息
            auto typesStr = req->url().toString();
            if (typesStr.contains("/type=")) {
                int type = typesStr.mid(typesStr.indexOf("/type=") + 6, 1).toInt();
                if (req->collectedData().size() > 0) {
                    QString url = req->collectedData().constData();
                    dealMsg(type, url);
                }
            }

            QString message = QString("ok");
            res->setStatusCode(qhttp::ESTATUS_OK);
            res->addHeaderValue("content-length", message.size());
            res->end(message.toUtf8());
        });
    }

    void dealMsg(int t, QString d) {
        HttpServer::instance()->gDealHttp(t, d.toLocal8Bit().data());
    }

    virtual ~ClientHandler() {
        qDebug("  ~ClientHandler(#%llu): I've being called automatically!",
            iconnectionId
        );
    }

protected:
    quint64    iconnectionId;
};

namespace Sn {
class WebPage;
class WebTab;
class TabbedWebView;

class TabWidget;

class RootFloatingButton;
class TitleBar;
class BookmarksToolbar;

class MaquetteGridItem;

enum ButtonType : int
{
    EBUTTON_LEFT = 0x01,
    EBUTTON_MIDDLE = 0x02,
    EBUTTON_RIGHT = 0x03,
    EBUTTON_X1 = 0x04,
    EBUTTON_X2 = 0x05
};

enum ButtonAction : char
{
    BUTTON_ACTION_PRESS = 0x08,
    BUTTON_ACTION_RELEASE = 0x09,
    BUTTON_ACTION_MOVE = 0x10
};

//! Represent a window of the browser.
/*!
 * This class provide various access to make operation in a window of Sielo.
 */
class SIELO_SHAREDLIB BrowserWindow: public QMainWindow {
Q_OBJECT

public:
	struct SavedWindow {
		QByteArray windowState{};
		QByteArray windowGeometry{};
		QVector<TabsSpaceSplitter::SavedTabsSpace> tabsSpaces{};

		SavedWindow();
		SavedWindow(BrowserWindow* window);
		SavedWindow(MaquetteGridItem* maquetteGridItem);

		bool isValid() const;
		void clear();

		friend QDataStream &operator<<(QDataStream &stream, const SavedWindow &window);
		friend QDataStream &operator>>(QDataStream &stream, SavedWindow &window);
	};

	/*!
	 * This constructor build the window.
	 * 
	 * This construct **should never be called manually**. Prefere using the `createWindow` methode from the Application class. 
	 */
	BrowserWindow(Application::WindowType type, const QUrl& url = QUrl());
	~BrowserWindow();

	/*! 
	 * Load settings.
	 * 
	 *  - Home page.
	 *  - Padding between tabs space.
	 *  - Background.
	 *  - Show bookmarks toolbar.
	 */
	void loadSettings();
	void loadWallpaperSettings();

	void setStartTab(WebTab* tab);
	void setStartPage(WebPage* page);

	void restoreWindowState(const SavedWindow& window);

	void currentTabChanged(WebTab* tab);

	/*!
	 * Load an url in the current tab.
	 */
	void loadUrl(const QUrl& url);

    void postMouseEvent(ButtonType, ButtonAction,int ,int);

	/*!
	 * Load an url in a new tab
	 */
	void loadUrlInNewTab(const QUrl& url);

	/*!
	 * This return the URL for the home page (doosearch.sielo.app at default).
	 * @return The URL for the home page.
	 */
	QUrl homePageUrl() const { return m_homePage; }

	TabWidget* tabWidget() const;
	TabWidget* tabWidget(int index) const;
	TabsSpaceSplitter* tabsSpaceSplitter() const { return m_tabsSpaceSplitter; }

	TitleBar* titleBar() const { return m_titleBar; }
	BookmarksToolbar* bookmarksToolBar() const { return m_bookmarksToolbar; }

	const QImage* background();
	const QImage* processedBackground();

	void addCaption(const QWidget* widget);
	void removeCaption(const QWidget* widget);
	bool isCaption(const QWidget* widget);  

    void dosignalopenPanit(bool);
    void dosignalgoNewUrl(QString);
    void dosignalsetcolor(QString);
    void dosignalclearPanit();
    void dosignalzoom(int);

Q_SIGNALS:
    void signalopenPanit(bool);
    void signalgoNewUrl(QString);
    void signalsetcolor(QString);
    void signalclearPanit();
    void signalzoom(int);


signals:
	void mouseOver(bool state);
	void tabWidgetChanged(TabWidget* tabWidget);

	void maximizeChanged(bool maximized, QSize oldSize);

public slots:
    void doZoom(int);
    void openPanit(bool);
    void clearPanit();
    void closePanit();
    void goNewUrl(QString r);
    void setColor(QString r);


	void setWindowTitle(const QString& title);

	void enterHtmlFullScreen();
	void toggleFullScreen();

	void bookmarkPage();
	void bookmarkAllTabs();
	void addBookmark(const QUrl& url, const QString& title);

	void tabWidgetIndexChanged(TabWidget* tbWidget);

protected:
	void shotBackground();
	QImage applyBlur(const QImage *src, qreal radius, bool quality = true, bool alphaOnly = false, int transposed = 0);
	void paintEvent(QPaintEvent* event);
	void resizeEvent(QResizeEvent* event);
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
	void mouseMoveEvent(QMouseEvent *e);

#ifdef Q_OS_WIN
	bool nativeEvent(const QByteArray &eventType, void *message, long *result);
#endif

public slots :
	void addTab();
	void postLaunch();

	void floatingButtonPatternChange(RootFloatingButton::Pattern pattern);

	void on_mark_window();
	void newWindow();
	void goHome();
	void forward();
	void back();
	void newTab();

private:
	void setupUi();
	void setupFloatingButton();

	void saveButtonState();

#ifdef Q_OS_WIN
	long ncHitTest(const MSG* wMsg) const;
#endif
    int m_zoom =100;
	QVector<const QWidget*> m_captionWidgets;
    QPoint m_curPos;

	QAction* m_restoreAction{nullptr};

	QUrl m_startUrl{};
	QUrl m_homePage{};
	Application::WindowType m_windowType{};
	WebTab* m_startTab{nullptr};
	WebPage* m_startPage{nullptr};

	QVBoxLayout* m_layout{nullptr};

	TabsSpaceSplitter* m_tabsSpaceSplitter{nullptr};
	TitleBar* m_titleBar{nullptr};
	BookmarksToolbar* m_bookmarksToolbar{nullptr};

	qreal m_blur_radius{ 0 };

	RootFloatingButton* m_fButton{nullptr};

	QTimer* m_backgroundTimer{nullptr};
	QImage m_currentBackground{};
	QImage* m_bg{ nullptr };
	QImage* m_blur_bg{ nullptr };
	bool m_upd_ss{ false };
	HttpServer* m_srv;
};

}



///全屏截图类
class Screen
{
public:
	enum STATUS { SELECT, MOV, SET_W_H };
	Screen() {}
	Screen(QSize size);

	void setStart(QPoint pos);
	void setEnd(QPoint pos);
	QPoint getStart();
	QPoint getEnd();
	QPoint getLeftUp();
	QPoint getRightDown();
	STATUS getStatus();
	void setStatus(STATUS status);

	int width();
	int height();
	bool isInArea(QPoint pos);          // 检测pos是否在截图区域内
	void move(QPoint p);                // 按 p 移动截图区域

private:
	QPoint leftUpPos, rightDownPos;     //记录 截图区域 左上角、右下角
	QPoint startPos, endPos;            //记录 鼠标开始位置、结束位置
	int maxWidth, maxHeight;            //记录屏幕大小
	STATUS status;                      //三个状态: 选择区域、移动区域、设置width height

	void cmpPoint(QPoint &s, QPoint &e);//比较两位置，判断左上角、右下角
};

class ScreenWidget : public QWidget
{
	Q_OBJECT
public:
	explicit ScreenWidget(QWidget *parent = 0);
	static ScreenWidget *Instance()
	{
		static QMutex mutex;

		if (!self) {
			QMutexLocker locker(&mutex);

			if (!self) {
				self = new ScreenWidget;
			}
		}

		return self;
	}

private:
	static ScreenWidget *self;
	QMenu *menu;            //右键菜单对象
	Screen *screen;         //截屏对象
	QPixmap *fullScreen;    //保存全屏图像
	QPixmap *bgScreen;      //模糊背景图
	QPoint movPos;          //坐标

protected:
	void contextMenuEvent(QContextMenuEvent *);
	void mousePressEvent(QMouseEvent *);
	void mouseMoveEvent(QMouseEvent *);
	void mouseReleaseEvent(QMouseEvent *);
	void paintEvent(QPaintEvent *);
	void showEvent(QShowEvent *);

private slots:
	void initForm();
	void saveScreen();
	void saveScreenOther();
	void saveFullScreen();
};

#include <QRadioButton>
#include <QLineEdit>
#include <QLabel>
#include <QSlider>

class DrawWidget : public QWidget
{
	Q_OBJECT

public:
	explicit DrawWidget(QWidget *parent);
	static DrawWidget *Instance()
	{
		static QMutex mutex;

		if (!self) {
			QMutexLocker locker(&mutex);

			if (!self) {
				self = new DrawWidget(0);
			}
		}
		return self;
	}
	~DrawWidget();
	void paint(QImage &theImage);
	void setupUi();

	void doClener();
	void setShape(int n);
    void setWidth(int n);
    void setColor(QString n);

	static DrawWidget *self;
private:
	//绘画变量
	QImage image;			//画布
	QImage tempImage;		//临时画布（双缓冲绘图时应用）
	QColor setting_color;	//背景色
	QColor penColor;		//画笔颜色
	bool drawing;			//绘图状态
	int nshape;				//绘制类型

	//绘图需要的鼠标数据
	QPoint point;
	QPoint from;
	QPoint to;
	QPoint change;
	QPoint pointPolygon[3];
	int width, heigh;
	QLineEdit lineEdit;

protected:
	void paintEvent(QPaintEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);

private slots:
	void on_radioButton_clicked();
	void on_radioButton_2_clicked();
	void on_pushButton_clicked();
	void on_shape_clicked();
	void on_radioButton_3_clicked();
	void on_pushButton_2_clicked();
	void on_radioButton_4_clicked();
	void on_pushButton_3_clicked();
	void on_radioButton_5_clicked();

public:
	QFrame *frame;
	QVBoxLayout *verticalLayout;
	QWidget *widget_2;
	QGridLayout *gridLayout_3;
	QLabel *label;
	QSlider *penWidth;
	QLabel *label_6;
	QPushButton *pushButton;
	QWidget *widget_5;
	QVBoxLayout *verticalLayout_2;
	QLabel *label_2;
	QWidget *widget_3;
	QGridLayout *gridLayout_2;
	QLabel *label_3;
	QLabel *label_9;
	QLabel *label_5;
	QRadioButton *radioButton_4;
	QRadioButton *radioButton_5;
	QRadioButton *shape;
	QRadioButton *radioButton_3;
	QLabel *label_4;
	QLabel *label_8;
	QRadioButton *radioButton;
	QRadioButton *radioButton_2;
	QLabel *label_7;
	QWidget *widget_4;
	QHBoxLayout *horizontalLayout;
	QPushButton *pushButton_2;
	QPushButton *pushButton_3;
};


#endif //SIELOBROWSER_BROWSERWINDOW_HPP
