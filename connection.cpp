#include "connection.h"
#include <netinet/in.h>
#include <QDir>

Connection::Connection(QObject *parent) : QObject(parent)
{

}

void Connection::slotConnectionExec()
{
    loop = new QEventLoop();
    loop->exec();
}

void Connection::slotStartServer(quint16 port)
{
    server = new QTcpServer();

    if (!server->listen(QHostAddress::Any, port))
    {
        qDebug() << "\nServer is not started";

        server->close();
        delete server;
        return;
    }

    qDebug() << "\nServer is started";

    connect(server, SIGNAL(newConnection()), this, SLOT(slotNewConnection()));
}

void Connection::slotStartTorServer(quint16 port)
{
    tor = new QProcess();
    tor->setProgram("tor");
    tor->setReadChannel(QProcess::StandardOutput);
    QDir tor_conf = QDir::current().absolutePath() + "/tor_config";
    if (!tor_conf.exists() && !QDir::current().mkdir("tor_config"))
    {
        qDebug() << "Can't create " << tor_conf.absolutePath() << "\n";
        delete tor;
        return;
    }

    tor->arguments() << "-f" << tor_conf.absolutePath() + "/torrc";
    QFile fout(tor_conf.absolutePath() + "/torrc");
    
    if (!fout.open(QIODevice::WriteOnly))
    {
        qDebug() << "Can't create torrc file\n";
        delete tor;
        return;
    }
    
    QString tmp = "SOCKSPort 9091\n";
    fout.write(tmp.toLatin1());
    qDebug() << "SOCKS port is 9091\n";

    tmp = "HiddenServiceDir " + tor_conf.absolutePath() + "/service\n";
    fout.write(tmp.toLatin1());
    qDebug() << "Hidden service directory is " << tor_conf.absolutePath() << "/service\n";

    QString strPort = QString::number(port);
    tmp = "HiddenServicePort " + strPort + " 127.0.0.1:" + strPort + "\n";
    fout.write(tmp.toLatin1());
    qDebug() << "Service listens to port " << strPort;

    fout.close();

    if (!tor->open(QIODevice::ReadOnly))
    {
        qDebug() << "Can't start tor\n";
        tor->close();
        delete tor;
        return;
    }

    tor->waitForFinished(3000);
    if (tor->state() == QProcess::NotRunning)
    {
        qDebug() << "Tor ran with error\n";
        tor->close();
        delete tor;
        return;
    }

    // while (true)
    // {
    //     /*if (!tor->canReadLine())
    //     {
    //         qDebug() << "Couldn't start tor\n";
    //         tor->close();
    //         delete tor;
    //         return;
    //     }*/
    //     tmp = QString::fromLocal8Bit(tor->read(20));
    //     if (tmp.indexOf("Bootstrapped 100") > 0 && tmp.indexOf(": Done") > 0)
    //     {
    //         qDebug() << "Tor works\n";
    //         break;
    //     }
    //     if (tmp.indexOf("[err]") >= 0)
    //     {
    //         qDebug() << "Couldn't start tor\n";
    //         tor->close();
    //         delete tor;
    //         return;
    //     }
    // }

    server = new QTcpServer();

    if (!server->listen(QHostAddress::Any, port))
    {
        qDebug() << "\nServer is not started";

        server->close();
        tor->close();
        delete tor;
        delete server;
        return;
    }

    qDebug() << "\nServer is started";

    connect(server, SIGNAL(newConnection()), this, SLOT(slotNewConnection()));
}

void Connection::slotNewConnection()
{
    QTcpSocket *tmp = server->nextPendingConnection();
    qint64 id = tmp->socketDescriptor();
    socketMap.insert(id, tmp);

    qDebug() << "\nNew Conncetion: " << id;

    connect(socketMap[id], SIGNAL(readyRead()), this, SLOT(slotRead()));
    connect(socketMap[id], SIGNAL(disconnected()), this, SLOT(slotDisconnectWarning()));
    connect(socketMap[id], SIGNAL(disconnected()), socketMap[id], SLOT(deleteLater()));

    tmp = nullptr;
}

void Connection::slotRead()
{
    QTcpSocket *soc = (QTcpSocket *)QObject::sender();
    qint64 id = soc->socketDescriptor();

    qDebug() << "\nREAD:" << id;

    storage.push_back(soc->readAll());
    storage.push_back(QString::number(id));

    soc = nullptr;
}

void Connection::slotConnect(QString adr, quint16 port)
{
    qDebug() << "\nConnecting";

    QTcpSocket *soc = new QTcpSocket();
    soc->connectToHost(adr, port);

    connect(soc, SIGNAL(connected()), this, SLOT(slotConnectSuccess()));

    soc = nullptr;
}

void Connection::slotConnectSOCKS5(QString addr, quint16 port)
{
    qDebug() << "\nConnecting to SOCKS server\n";
    QTcpSocket *soc = new QTcpSocket();
    soc->connectToHost("127.0.0.1", (quint16)9050);

    if (soc->waitForConnected(5000))
    {
        char *request = const_cast<char*>("\05\01\00");
        soc->write(request, 3);
        char response[10];
        soc->read(response, 2);

        if (response[1] != 0x00)
        {
            qDebug() << "SOCKS5 error authentificating\n";
            soc->close();
            delete soc;
            return;
        }

        port = htons(port);
        request = new char[4 + 1 + addr.length() + 2];
        memcpy(request, "\05\01\00\03", 4);
        request[4] = static_cast<unsigned char>(addr.length());
        memcpy(request + 5, addr.toStdString().c_str(), addr.length());
        memcpy(request + 5 + addr.length(), &port, 2);

        soc->write(request, 4 + 1+ addr.length() + 2);
        delete[] request;

        soc->read(response, 10);
        if (response[1] != 0x00)
        {
            qDebug() << "SOCKS5 error: " << (int)response[1] << "\n";
            soc->close();
            delete soc;
            return;
        }

        qint64 id = soc->socketDescriptor();
        qDebug() << "SOCKS5 connected: " << id << "\n";

        socketMap.insert(id, soc);
        connect(socketMap[id], SIGNAL(readyRead()), this, SLOT(slotRead()));
        connect(socketMap[id], SIGNAL(disconnected()), this, SLOT(slotDisconnectWarning()));
        connect(socketMap[id], SIGNAL(disconnected()), socketMap[id], SLOT(deleteLater()));
    }
    else
    {
        qDebug() << "SOCKS5 does not answer\n";
        soc->close();
        delete soc;
   }
}

void Connection::slotConnectSuccess()
{
    QTcpSocket *soc = (QTcpSocket *)QObject::sender();
    qint64 id = soc->socketDescriptor();

    qDebug() << "\nConnected: " << id;

    socketMap.insert(id, soc);
    connect(socketMap[id], SIGNAL(readyRead()), this, SLOT(slotRead()));
    connect(socketMap[id], SIGNAL(disconnected()), this, SLOT(slotDisconnectWarning()));
    connect(socketMap[id], SIGNAL(disconnected()), socketMap[id], SLOT(deleteLater()));

    soc = nullptr;
}

void Connection::slotWrite(qint64 id, QString message)
{
    if (socketMap.contains(id))
        socketMap[id]->write(message.toStdString().c_str());
    else
        qDebug() << "No socket" << id << "exists";
}

void Connection::slotReadIncomming()
{
    if (storage.isEmpty())
    {
        qDebug() << "\nNothing to read";
    }
    else
    {
        while (!storage.isEmpty())
        {
            printf("[%s]:\n", storage.last().toStdString().c_str());
            storage.pop_back();
            printf("%s\n", storage.last().toStdString().c_str());
            storage.pop_back();
        }
    }
}

void Connection::slotDisconnect(qint64 id)
{
    socketMap[id]->disconnectFromHost();
    socketMap.remove(id);
}


void Connection::slotDisconnectWarning()
{
    qDebug() << "Disconnected: " << ((QTcpSocket *)QObject::sender())->socketDescriptor();
}