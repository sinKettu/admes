#ifndef CONTROL_H
#define CONTROL_H

#include <QObject>
#include "connection.h"
#include "chat.h"

class Control : public QObject
{
    Q_OBJECT
public:
    explicit Control(QObject *parent = nullptr);
    void StartServer(quint16 port);
    void StartTorServer(quint16 port);
    void ReadAll();
    void Send(qint64 id, QString message);
    void ConnectTo(QString adr, quint16 port);
    void Disconnect(qint64 id);
    void ConnectThroughSOCKS5(QString addr, quint16 port);
    bool OutputChat(qint64 id);

signals:
    void sigStartServer(quint16);
    void sigStartTorServer(quint16 port);
    void sigReadAll();
    void sigWrite(qint64, QString);
    void sigConnect(QString, quint16);
    void sigConnectSOCKS5(QString, quint16);
    void sigDisconnect(qint64);

private:
    Connection *connection = nullptr;
    Chat *chat = nullptr;

public slots:
};

#endif // CONTROL_H
