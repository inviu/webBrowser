#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QtCore>
#include <QtNetwork>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTimer>

class HttpServer : public QObject
{
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer();
    Q_DISABLE_COPY(HttpServer)

    void run(const QHostAddress &address = QHostAddress::Any, const quint16 &port = 80);
    void doSendWSMsg(QString message);
    void dealHttp(QByteArray message, QTcpSocket *socket);
    bool senddata(QByteArray response, QTcpSocket *socket);

public Q_SLOTS:
    void newConnection();
    void readyRead();

protected Q_SLOTS:
    void onNewWSConnection();
    void onWSclosed();
    void processWSTextMessage(QString message);
    void processWSBinaryMessage(QByteArray message);
    void socketWSDisconnected();

private:
    long m_lasttick = 0;
    bool m_screen = true;
    QTcpServer *m_httpServer;
    QWebSocketServer* m_pWebSocketServer;
    QList<QWebSocket *> m_WSclients;
};

#endif // HTTPSERVER_H