#if defined(_WIN32) || defined(_WIN64)
    #include <winsock.h>
#else
    #include <netinet/in.h>
#endif

#include "connection.h"
#include "common.h"

QString connectReq = "admesconnectrequest";
QString connectResp = "admesconnectresponse";

Connection::Connection(QObject *parent) : QObject(parent) {}

void Connection::slotConnectionExec()
{
    chat = new Chat();
}

Connection::~Connection()
{
   /* if (server != nullptr)
    {
        server->close();
        delete server;
    }*/
    if (tor != nullptr && tor->state() == QProcess::Running)
    {
        tor->close();
    }
    QMap<qint64, QTcpSocket*>::iterator iter;
    for (iter = WaitingForConfirmation.begin(); iter != WaitingForConfirmation.end(); iter++)
        iter.value()->disconnect();
    for (iter = socketMap.begin(); iter != socketMap.end(); iter++)
        iter.value()->disconnect();
    WaitingForConfirmation.clear();
    socketMap.clear();
    delete chat;
}

void Connection::slotStopAll()
{
    if (server != nullptr)
    {
        server->close();
        delete server;
    }

    if (tor != nullptr && tor->state() == QProcess::Running)
    {
        tor->close();
    }

    QMap<qint64, QTcpSocket*>::iterator iter;
    for (iter = WaitingForConfirmation.begin(); iter != WaitingForConfirmation.end(); iter++)
        iter.value()->disconnect();

    for (iter = socketMap.begin(); iter != socketMap.end(); iter++)
        iter.value()->disconnect();

    WaitingForConfirmation.clear();
    socketMap.clear();
    delete chat;

    emit sigTerminateThread();
}

void Connection::slotStartServer()
{
    server = new QTcpServer();

    if (!server->listen(QHostAddress::Any, serverPort))
    {
        std::cout << prefix << "Server is not started\n";

        server->close();
        delete server;
        return;
    }

    std::cout << prefix << "Server is started at port " << serverPort << "\n";

    connect(server, SIGNAL(newConnection()), this, SLOT(slotNewConnection()));
}

void Connection::slotRunTor()
{
    tor = new QProcess();
    connect(tor, SIGNAL(finished(int, QProcess::ExitStatus)), tor, SLOT(deleteLater()));
    connect(tor, SIGNAL(readyReadStandardOutput()), this, SLOT(slotReadTorOutput()));
    connect(tor, SIGNAL(readyReadStandardError()), this, SLOT(slotReadTorOutput()));
    tor->setProgram(tor_path);
    QDir tor_conf("./config");
    if (!tor_conf.exists() && !QDir().mkdir(tor_conf.path())) // !!!
    {
        std::cout << prefix << "Can't create " << tor_conf.absolutePath().toStdString() << "\n";
        return;
    }

    QStringList args;
    args << "-f" << tor_conf.absolutePath() + "/torrc";
    tor->setArguments(args);
    QFile fout(tor_conf.absolutePath() + "/torrc");
    
    if (!fout.open(QIODevice::WriteOnly))
    {
        std::cout << prefix << "Can't create torrc file\n";
        return;
    }
    
    QString tmp = "SOCKSPort " + QString::number(socks5Port) + "\n";
    fout.write(tmp.toLatin1());

    tmp = "HiddenServiceDir " + tor_conf.absolutePath() + "/service\n";
    fout.write(tmp.toLatin1());

    QString strPort = QString::number(serverPort);
    tmp = "HiddenServicePort " + strPort + " 127.0.0.1:" + strPort + "\n";
    fout.write(tmp.toLatin1());

    fout.close();
    if (!tor->open(QIODevice::ReadOnly))
    {
        std::cout << prefix << "Can't start tor\n";
        tor->close();
        return;
    }

    tor->waitForFinished(3000);
    if (tor->state() == QProcess::NotRunning)
    {
        std::cout << prefix << "Tor ran with error\n";
        QStringList torOutput = QString().fromLocal8Bit(tor->readAll()).split('\n');
        for (uint32_t i = 0; i < torOutput.length(); i++)
            std::cout << "[TOR] " << torOutput[i].toStdString() << "\n";
            
        tor->close();
        return;
    }

    QFile fin(tor_conf.absolutePath() + "/service/hostname", this);
    if (!fin.open(QIODevice::ReadOnly))
    {
        std::cout << prefix << "Couldn't read tor service hostname\n";
        tor->close();
        return;
    }
    else
    {
        tmp = QString().fromLocal8Bit(fin.readLine());
        fin.close();
        std::cout << prefix << "Service will listen to port " << strPort.toStdString() << "\n";
        std::cout << prefix << "Hidden service directory is " << tor_conf.absolutePath().toStdString() + "/service\n";
        std::cout << prefix << "SOCKS5 port is " + QString::number(socks5Port).toStdString() + "\n";
        std::cout << prefix << "Tor service hostname: " << tmp.mid(0, tmp.length() - 1).toStdString() << "\n";
    }
}

void Connection::slotNewConnection()
{
    QTcpSocket *tmp = server->nextPendingConnection();
    qint64 id = tmp->socketDescriptor();
    WaitingForConfirmation.insert(id, tmp);

    std::cout << prefix << "New conncetion at socket " << id << "\n";
    connectionStage.insert(id, 0);
    connect(tmp, SIGNAL(readyRead()), this, SLOT(slotRead()));
    connect(tmp, SIGNAL(disconnected()), this, SLOT(slotDisconnectWarning()));
    connect(tmp, SIGNAL(disconnected()), tmp, SLOT(deleteLater()));
}

QByteArray Connection::MessagePackaging(QString message, qint64 id)
{
    QByteArray result;
    if (!EPNG_inited())
    {
        std::cout << prefix << "EC PRNG wasn't initialized\n";
        return result;
    }

    if (!socketMap.contains(id))
    {
        std::cout << prefix << "Couldn't find connection '" << id << "'\n";
        return result;
    }

    // Get random IV
    mpz_t mp_iv;
    mpz_init(mp_iv);
    EPNG_get(mp_iv);
    unsigned char *char_iv;
    unsigned int iv_len;
    ecc_mpz_to_cstr(mp_iv, &char_iv, iv_len);
    QByteArray iv = QByteArray(reinterpret_cast<char*>(char_iv), iv_len);
    
    // Encrypt message with AES-CBC(session key, iv)
    QByteArray encryptedMessage = AES_CBC_Encrypt(message.toLocal8Bit(), 
                                                  peersSessionKeys[id].data(), 
                                                  peersSessionKeys[id].length(),
                                                  char_iv);
    if (encryptedMessage.isEmpty())
    {
        std::cout << prefix << "Unknown error\n";
        mpz_clear(mp_iv);
        delete[] char_iv;
        return result;
    }
    
    // Encrypt IV with AES-ECB(session key)
    QByteArray encryptedIV = AES_ECB_Encrypt(iv, peersSessionKeys[id].data(),
                                             peersSessionKeys[id].length());
    if (encryptedIV.isEmpty())
    {
        std::cout << prefix << "Unknown error\n";
        mpz_clear(mp_iv);
        delete[] char_iv;
        return result;
    }
    
    // (encrypted-message-last-16-bytes) XOR iv - Integrity control
    mpz_t tail;
    mpz_init(tail);
    int pos = encryptedMessage.length() - 16;
    ecc_cstr_to_mpz(reinterpret_cast<unsigned char *>(encryptedMessage.data() + pos), 
                    16, tail);
    mpz_xor(tail, mp_iv, tail);
    delete[] char_iv;
    ecc_mpz_to_cstr(tail, &char_iv, iv_len);
    
    QByteArray integrity = QByteArray(reinterpret_cast<char*>(char_iv), iv_len);
    mpz_clears(mp_iv, tail, NULL);
    delete[] char_iv;

    // EC signature based on integrity control
    EllipticCurve *ec = GetEC();
    Keychain *kc = GetKeys();
    QByteArray signature = ECC_Sign(ec, kc->PrivateKey, integrity);
    if (signature.isEmpty())
    {
        std::cout << prefix << "Unknown error\n";
        return result;
    }

    pos = signature.length();
    result.append(encryptedIV);
    result.append(reinterpret_cast<char*>(&pos), 4);
    result.append(signature);
    result.append(encryptedMessage);

    return result;
}

QString Connection::MessageExtract(QByteArray data, qint64 id, bool &signature)
{
    QString result;
    signature = false;
    if (!socketMap.contains(id))
    {
        std::cout << prefix << "Couldn't find connection '" << id << "'\n";
        return result;
    }

    QByteArray encIVBuf = data.mid(0, 32);
    int tmp = *reinterpret_cast<int*>(data.data() + 32);
    QByteArray SignBuf = data.mid(36, tmp);
    QByteArray encMesBuf = data.mid(36 + tmp);
    if (encIVBuf.length() != 32 || 
        SignBuf.length() != tmp || 
        encMesBuf.length() != data.length() - 36 - tmp ||
        encMesBuf.length() < 16)
    {
        std::cout << prefix << "Couldn't parse message\n";
        return result;
    }

    // preparations to check signature
    QByteArray IVBuf = AES_ECB_Decrypt(encIVBuf, peersSessionKeys[id].data(), peersSessionKeys[id].length());
    tmp = encMesBuf.length() - 16;
    mpz_t a, b;
    mpz_inits(a, b, NULL);
    ecc_cstr_to_mpz(reinterpret_cast<unsigned char *>(IVBuf.data()), 16, a);
    ecc_cstr_to_mpz(reinterpret_cast<unsigned char *>(encMesBuf.data() + tmp), 16, b);
    mpz_xor(a, a, b);
    unsigned char *integrity;
    unsigned int l;
    ecc_mpz_to_cstr(a, &integrity, l);
    QByteArray qinteg = QByteArray(reinterpret_cast<char*>(integrity), l);
    mpz_clears(a, b, NULL);
    delete[] integrity;

    EllipticCurve *ec = GetEC();
    tmp = ECC_Check(ec, pukMap[id], qinteg, SignBuf);
    if (tmp == -1)
    {
        std::cout << "Unknown error\n";
        return result;
    }

    signature = static_cast<bool>(tmp);
    if (!signature)
    {
        std::cout << prefix << "Signature failed verification\n";
        return result;
    }

    QByteArray message = AES_CBC_Decrypt(encMesBuf, peersSessionKeys[id].data(),
                                         peersSessionKeys[id].length(),
                                         reinterpret_cast<unsigned char*>(IVBuf.data()));
    
    if (message.isEmpty())
    {
        std::cout << prefix << "Unknown error\n";
        return result;
    }

    result = QString::fromLocal8Bit(message);
    return result;
}

void Connection::BadConnection(qint64 id)
{
    std::cout << prefix << "Connection establishment was failed\n";

    if (WaitingForConfirmation.contains(id))
    {
        WaitingForConfirmation[id]->disconnect();
        WaitingForConfirmation.remove(id);
    }
    if (connectionStage.contains(id))
    {
        connectionStage.remove(id);
    }
    if (pukMap.contains(id))
    {
        pukMap.remove(id);
    }
    if (peersSessionKeys.contains(id))
    {
        peersSessionKeys.remove(id);
    }
}

bool Connection::SendPuk(QTcpSocket *soc)
{
    QByteArray puk = GetBytePuk();
    if (puk.isEmpty())
        return false;
    
    puk.push_front('K');
    puk.push_front('U');
    puk.push_front('P');
    soc->write(puk);
    return true;
}

void Connection::SendLogin(QTcpSocket *soc, qint64 id)
{
    QString login = GetLogin();
    EllipticCurve *ec = GetEC();
    Keychain *kc = GetKeys();

    QByteArray mes = "LOG";
    QByteArray enc = ECC_Encrypt(ec, pukMap[id], login.toLocal8Bit());
    QByteArray sign = ECC_Sign(ec, kc->PrivateKey, login.toLocal8Bit());
    int tmp = enc.length();
    mes.append(reinterpret_cast<char*>(&tmp), 4);
    mes.append(enc);
    tmp = sign.length();
    mes.append(reinterpret_cast<char*>(&tmp), 4);
    mes.append(sign);

    soc->write(mes);
}

// -1 => Ключ не прошел подтверждение
//  0 => Ключ подтверджен и пир известен
//  1 => Незнакомый пир
//  2 => Пир известен, ключ подтверджен, но не совпал с имеющимся
int Connection::CheckPeer(qint64 id, QByteArray data, QString &strLogin)
{
    int enc_l = *reinterpret_cast<int*>(data.data());
    QByteArray enc = data.mid(4, enc_l);
    int sign_l = *reinterpret_cast<int*>(data.data() + 4 + enc_l);
    QByteArray sign = data.mid(8 + enc_l, sign_l);

    EllipticCurve *ec = ec_init(SECP521R1);
    Keychain *kc = GetKeys();
    QByteArray login = ECC_Decrypt(ec, kc->PrivateKey, enc);
    if (ECC_Check(ec, pukMap[id], login, sign) == 0)
    {
        ec_deinit(ec);
        return -1;
    }

    ec_deinit(ec);
    strLogin = QString::fromLocal8Bit(login);
    if (!IsUserKnown(strLogin))
    {
        return 1;
    }
    else if (CheckKey(strLogin, pukMap[id]))
    {
        return 0;
    }
    else
    {
        strLogin = "";
        return 2;
    }
}

bool Connection::SendNumber(qint64 id)
{
    if (!EPNG_inited())
        return false;

    if (!WaitingForConfirmation.contains(id))
        return false;

    mpz_t num;
    mpz_init(num);
    EPNG_get(num);
    unsigned char *buf;
    unsigned int l;
    ecc_mpz_to_cstr(num, &buf, l);
    QByteArray data = QByteArray(reinterpret_cast<char*>(buf), l);

    delete[] buf; 
    mpz_clear(num);

    WaitingForConfirmation[id]->write(data);
    peersSessionKeys.insert(id, data);

    return true;
}

void Connection::OpenSession(qint64 id, QTcpSocket *soc, QByteArray message)
{
    // Установить сессионный ключ
    unsigned char *b = reinterpret_cast<unsigned char*>(peersSessionKeys[id].data());
    unsigned char *a = reinterpret_cast<unsigned char*>(message.data());

    mpz_t ma, mb;
    mpz_inits(ma, mb, NULL);
    ecc_cstr_to_mpz(a, message.length(), ma);
    ecc_cstr_to_mpz(b, peersSessionKeys[id].length(), mb);
    mpz_xor(ma, ma, mb);

    a = nullptr;
    unsigned int l;
    ecc_mpz_to_cstr(ma, &a, l);
    QByteArray sessionKey = QByteArray(reinterpret_cast<char *>(a), l);
    delete[] a;
    
    if (sessionKey.length() > 16)
    {
        sessionKey = sessionKey.mid(0, 16);
    }
    if (sessionKey.length() < 16)
    {
        while (sessionKey.length() != 16)
            sessionKey.append('\00');
    }
    peersSessionKeys[id] = sessionKey;

    // Открыть окончательное соединение
    socketMap.insert(id, soc);
    WaitingForConfirmation.remove(id);
    connectionStage.remove(id);
    chat->AddNewOne(id);
}

void Connection::slotRead()
{
    QTcpSocket *soc = (QTcpSocket *)QObject::sender();
    qint64 id = soc->socketDescriptor();
    QByteArray message = soc->readAll();

    // Connection establishing
    // Need to make this a part of a protocol
    // and move to another scoupe
    if (socketMap.contains(id))
    {
        // Message: |IV|sign_len|sign|message|
        // IV - AES-ECB with session key
        // message - AES-CBC with session key and IV
        // sign - EC signature
        bool signature;
        QString mes = MessageExtract(message, id, signature);
        if (!signature || mes.isEmpty()) 
            return;
        else
        {
            chat->AddToChat(id, IDPeer[id], mes);
            return;
        }
    }

    bool a = connectionStage.contains(id);
    int b = connectionStage[id];
    if (connectionStage.contains(id) && connectionStage[id] == 0 && message == connectReq)
    {
        if (soc->write(connectResp.toLocal8Bit()) == connectResp.length() ||
            soc->write(connectResp.toLocal8Bit()) == connectResp.length() ||
            soc->write(connectResp.toLocal8Bit()) == connectResp.length() )
        {
            std::cout << prefix << "The connection (" << id << ") is established\n";
            connectionStage[id]++; // become 1
        }
        else
        {
            BadConnection(id);
            return;
        }
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 0 && message != connectReq)
    {
        return;
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 100 && message == connectResp)
    {
        std::cout << prefix << "The connection (" << id << ") is established\n";
        if (!SendPuk(soc))
        {
            BadConnection(id);
            return;
        }

        connectionStage[id]++; // become 101
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 100 && message != connectResp)
    {
        return;
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 1)
    {
        if (message.mid(0, 3) != "PUK")
        {
            return;
        }

        message = message.mid(3);
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(1));
        Keychain *kc = qba_to_ecc_keys(message);
        if (kc == nullptr)
        {
            BadConnection(id);
            return;
        }

        Point puk = pnt_init();
        pntcpy(kc->PublicKey, puk);
        pukMap.insert(id, puk);
        delete_keys(kc);

        if (!SendPuk(soc))
        {
            BadConnection(id);
            return;
        }

        connectionStage[id]++; // become 2
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 101)
    {
        // отправить зашифрованный и подписанный логин
        if (message.mid(0, 3) != "PUK")
        {
            return;
        }

        message = message.mid(3);
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(0));
        message.push_front(static_cast<char>(1));
        Keychain *kc = qba_to_ecc_keys(message);
        if (kc == nullptr)
        {
            BadConnection(id);
            return;
        }

        Point puk = pnt_init();
        pntcpy(kc->PublicKey, puk);
        pukMap.insert(id, puk);
        delete_keys(kc);

        SendLogin(soc, id);
        connectionStage[id]++; // become 102
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 2)
    {
        // принять и проверить логин
        if (message.mid(0,3) != "LOG")
        {
            return;
        }

        QString login;
        int res = CheckPeer(id, message.mid(3), login);
        if (res == -1)
        {
            std::cout << prefix << "The key is not authorized\n";
            BadConnection(id);
            return;
        }
        else if (res == 0)
        {
            if (peerID.contains(login))
            {
                std::cout << prefix << "The peer is already connected\n";
                std::cout << prefix << "New connection will be refused\n";
                BadConnection(id);
                return;
            }
            else
            {
                std::cout << prefix << "Known peer " + login.toStdString() + "\n";
                IDPeer.insert(id, login);
                peerID.insert(login, id);
            }
        }
        else if (res == 1)
        {
            IDPeer.insert(id, login);
            peerID.insert(login, id);
            QString tmp = IDPeer[id];
            std::cout << prefix << "Unknown peer " + login.toStdString() + "\n";
            std::cout << prefix << "If you trust this peer, type '/accept ";
            std::cout << QString::number(id).toStdString() << "'\n";
            std::cout << prefix << "In another case type '/refuse ";
            std::cout << QString::number(id).toStdString() << "'\n";
            return;
        }
        else if (res == 2)
        {
            std::cout << prefix << "Known peer, but keys did not match\n";
            std::cout << prefix << "MitM-attack is possible\n";
            std::cout << prefix << "Connection will be refused\n";
            std::cout << prefix << "If you want to connect, please, remove old data manually and reconnect\n";
            BadConnection(id);
            return;
        }

        // отправить зашифрованный и подписанный логин
        SendLogin(soc, id);
        connectionStage[id]++; // become 3
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 102)
    {
        // Проверить логин
        if (message.mid(0,3) != "LOG")
        {
            return;
        }

        QString login;
        int res = CheckPeer(id, message.mid(3), login);
        if (res == -1)
        {
            std::cout << prefix << "The key is not authorized\n";
            BadConnection(id);
            return;
        }
        else if (res == 0)
        {
            if (peerID.contains(login))
            {
                std::cout << prefix << "The peer is already connected\n";
                std::cout << prefix << "New connection will be refused\n";
                BadConnection(id);
                return;
            }
            else
            {
                std::cout << prefix << "Known peer " + login.toStdString() + "\n";
                IDPeer.insert(id, login);
                peerID.insert(login, id);
            }
        }
        else if (res == 1)
        {
            IDPeer.insert(id, login);
            peerID.insert(login, id);
            QString tmp = IDPeer[id];
            std::cout << prefix << "Unknown peer " + login.toStdString() + "\n";
            std::cout << prefix << "If you trust this peer, type '/accept ";
            std::cout << QString::number(id).toStdString() << "'\n";
            std::cout << prefix << "In another case type '/refuse ";
            std::cout << QString::number(id).toStdString() << "'\n";
            return;
        }
        else if (res == 2)
        {
            std::cout << prefix << "Known peer, but keys did not match\n";
            std::cout << prefix << "MitM-attack is possible\n";
            std::cout << prefix << "Connection will be refused\n";
            std::cout << prefix << "If you want to connect, please, remove old data manually and reconnect\n";
            BadConnection(id);
            return;
        }

        // Отправить зашифрованное и подписанное случайное число А
        if (!SendNumber(id))
        {
            BadConnection(id);
            return;
        }
        connectionStage[id]++; // become 103
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 3)
    {
        // Сгенерировать случайное число B и отправить
        if (!SendNumber(id))
        {
            BadConnection(id);
            return;
        }
        OpenSession(id, soc, message);
        std::cout << prefix << "New session is opened\n";
    }
    else if (connectionStage.contains(id) && connectionStage[id] == 103)
    {
        OpenSession(id, soc, message);
        std::cout << prefix << "New session is opened\n";
    }
    else
    {
        BadConnection(id);
    }
}

void Connection::slotConnect(QString adr, quint16 port)
{
    QTcpSocket *soc = new QTcpSocket();
    soc->connectToHost(adr, port);

    connect(soc, SIGNAL(connected()), this, SLOT(slotConnectSuccess()));
}

void Connection::AdmesConnectionRequest(QTcpSocket *soc)
{
    qint64 id = soc->socketDescriptor();
    // Three attempts to send
    if (soc->write(connectReq.toLocal8Bit()) != connectReq.length() &&
        soc->write(connectReq.toLocal8Bit()) != connectReq.length() &&
        soc->write(connectReq.toLocal8Bit()) != connectReq.length())
    {
        std::cout << "Connection failure\n";
    }
    else
    {
        std::cout << prefix << "New conncetion at socket " << id << "\n";

        connectionStage.insert(id, 100);
        WaitingForConfirmation.insert(id, soc);
        connect(soc, SIGNAL(readyRead()), this, SLOT(slotRead()));
        connect(soc, SIGNAL(disconnected()), this, SLOT(slotDisconnectWarning()));
        connect(soc, SIGNAL(disconnected()), soc, SLOT(deleteLater()));
    }
}

void Connection::slotConnectSOCKS5(QString addr, quint16 port)
{
    std::cout << prefix << "Connecting to SOCKS server\n";
    QTcpSocket *soc = new QTcpSocket();
    soc->connectToHost("127.0.0.1", socks5Port);

    if (soc->waitForConnected(5000))
    {
        char *request = const_cast<char*>("\05\01\00");
        soc->write(request, 3);
        char response[10];
        soc->read(response, 2);

        if (response[1] != 0x00)
        {
            std::cout << prefix << "SOCKS5 error authentificating\n";
            std::cout << prefix << "SOCKS5 response: ";
            HexOutput(QByteArray(response), ' ', 2);
            std::cout << '\n';

            soc->close();
            delete soc;
            return;
        }

        port = htons(static_cast<unsigned short>(port));
        request = new char[4 + 1 + addr.length() + 2];
        memcpy(request, "\05\01\00\03", 4);
        request[4] = static_cast<char>(addr.length());
        memcpy(request + 5, addr.toStdString().c_str(), static_cast<size_t>(addr.length()));
        memcpy(request + 5 + addr.length(), &port, 2);

        soc->write(request, 4 + 1 + addr.length() + 2);
        delete[] request;

        soc->read(response, 10);
        if (response[1] != 0x00)
        {
            std::cout << prefix << "SOCKS5 error: " << (int)response[1] << "\n";
            std::cout << prefix << "SOCKS5 response: ";
            HexOutput(QByteArray(response), ' ', 10);
            soc->close();
            delete soc;
            return;
        }

        qint64 id = soc->socketDescriptor();
        std::cout << prefix << "SOCKS5 connected at socket " << id << "\n";

        AdmesConnectionRequest(soc);
    }
    else
    {
        std::cout << prefix << "SOCKS5 server does not answer\n";
        soc->close();
        delete soc;
   }
}

void Connection::slotConnectSuccess()
{
    QTcpSocket *soc = (QTcpSocket *)QObject::sender();
    AdmesConnectionRequest(soc);
}

void Connection::slotWrite(QString peerName, QString message)
{
    qint64 id;
    if (peerID.contains(peerName))
        id = peerID[peerName];
    else
    {
        std::cout << prefix << "No connected peer '" << peerName.toStdString() << "' exists\n";
        return;
    }

    if (socketMap.contains(id))
    {
        QByteArray toWrite = MessagePackaging(message, id);
        if (toWrite.isEmpty())
        {
            std::cout << prefix << "Packaging failure\n";
            return;
        }
        // Three attempts to send
        if (socketMap[id]->write(toWrite) == toWrite.length() ||
            socketMap[id]->write(toWrite) == toWrite.length() ||
            socketMap[id]->write(toWrite) == toWrite.length())
        {
            chat->AddToChat(id, GetLogin(), message);
        }
        else
            std::cout << prefix << "Sending failure\n";
    }
    else
        std::cout << prefix << "No connected peer '" << peerName.toStdString() << "' exists\n";
}

void Connection::slotDisconnect(QString peer)
{
    qint64 id;
    if (peerID.contains(peer))
        id = peerID[peer];
    else
    {
        std::cout << prefix << "No such peer connected\n";
        return;
    }
    
    if (socketMap.contains(id))
    {
        socketMap[id]->disconnectFromHost();
        if (pukMap.contains(id))
        {
            pukMap.remove(id);
        }
        if (peersSessionKeys.contains(id))
        {
            peersSessionKeys.remove(id);
        }
        if (IDPeer.contains(id))
        {
            if (peerID.contains(IDPeer[id]))
                peerID.remove(IDPeer[id]);
            IDPeer.remove(id);
        }
    }
    else if (WaitingForConfirmation.contains(id))
    {
        WaitingForConfirmation[id]->disconnectFromHost();
        WaitingForConfirmation.remove(id);

        if (connectionStage.contains(id))
        {
            connectionStage.remove(id);
        }
        if (pukMap.contains(id))
        {
            pukMap.remove(id);
        }
        if (peersSessionKeys.contains(id))
        {
            peersSessionKeys.remove(id);
        }
        if (IDPeer.contains(id))
        {
            if (peerID.contains(IDPeer[id]))
                peerID.remove(IDPeer[id]);
            IDPeer.remove(id);
        }
    }
    else
        std::cout << prefix << "No such peer connected\n";
}

// FIX DISCONNECTING
void Connection::slotDisconnectWarning()
{
    CheckUp(); 
}

void Connection::slotOutputDialog(QString peer)
{
    if (peerID.contains(peer))
    {
        qint64 id = peerID[peer];
        chat->Output(id);
    }
}

void Connection::slotCloseDialog()
{
    chat->Close();
}

void Connection::CheckUp()
{
    QMap<qint64, QTcpSocket *>::iterator soc;
    QVector<qint64> disconnectedSockets;
    for (soc = socketMap.begin(); soc != socketMap.end(); soc++)
    {
        if (soc.value()->state() == QAbstractSocket::UnconnectedState)
        {
            std::cout << prefix << "Socket " << soc.key() << " disconnected\n";
            disconnectedSockets.push_back(soc.key());
        }
    }
    for (soc = WaitingForConfirmation.begin(); soc != WaitingForConfirmation.end(); soc++)
    {
        if (soc.value()->state() == QAbstractSocket::UnconnectedState)
        {
            std::cout << prefix << "Socket " << soc.key() << " disconnected\n";
            disconnectedSockets.push_back(soc.key());
        }
    }
    for (quint32 index = 0; index < disconnectedSockets.length(); index++)
    {
        chat->Remove(disconnectedSockets.at(index));
        if (socketMap.contains(disconnectedSockets.at(index)))
            socketMap.remove(disconnectedSockets.at(index));

        if (WaitingForConfirmation.contains(disconnectedSockets.at(index)))
            WaitingForConfirmation.remove(disconnectedSockets.at(index));
        
        if (IDPeer.contains(disconnectedSockets.at(index)))
        {
            if (peerID.contains(IDPeer[disconnectedSockets.at(index)]))
                peerID.remove(IDPeer[disconnectedSockets.at(index)]);
            IDPeer.remove(disconnectedSockets.at(index));
        }

        if (connectionStage.contains(disconnectedSockets.at(index)))
            connectionStage.remove(disconnectedSockets.at(index));

        if (pukMap.contains(disconnectedSockets.at(index)))
            pukMap.remove(disconnectedSockets.at(index));
        
        if (peersSessionKeys.contains(disconnectedSockets.at(index)))
            peersSessionKeys.remove(disconnectedSockets.at(index));
    }
}

void Connection::slotSpecifyPortForListening(quint16 port)
{
    serverPort = port;   
    std::cout << prefix << "Admes will listen to port " << port << "\n";
}

void Connection::slotSpecifyPortForSOCKS5(quint16 port)
{
    socks5Port = port;
    std::cout << prefix << "SOCKS5 service will listen to port " << port << "\n";
}

void Connection::slotReadTorOutput()
{
    QProcess *proc = reinterpret_cast<QProcess *>(QObject::sender());
    QByteArray output = proc->readAllStandardOutput();
    output.append(proc->readAllStandardError());
    QFile fout("./config/last_session.log");
    if (firstWriting && fout.open(QIODevice::WriteOnly))
    {
        fout.write(output);
        fout.close();
        firstWriting = false;
    }
    else if (!firstWriting && fout.open(QIODevice::WriteOnly | QIODevice::Append))
    {
        fout.write(output);
        fout.close();
    }
    else
    {
        std::cout << prefix << "Couldn't open Tor log\n";
    }
}

void Connection::slotShowTorLog()
{
    QFile fout("./config/last_session.log");
    if (fout.open(QIODevice::ReadOnly))
    {
        while (!fout.atEnd())
        {
            QString tmp = fout.readLine();
            std::cout << tmp.toStdString();
        }
        std::cout << "\n";
        fout.close();
    }
    else
    {
        std::cout << prefix << "Couldn't open Tor log\n";
    }
}

void Connection::slotAccept(qint64 id)
{
    if (!WaitingForConfirmation.contains(id) || !connectionStage.contains(id))
    {
        std::cout << prefix << "Couldn't find connection '" << id << "'\n";
        return;
    }

    if (connectionStage[id] == 2)
    {
        if (!NewPeer(IDPeer[id], pukMap[id]))
        {
            std::cout << prefix << "Couldn't add " + IDPeer[id].toStdString() + " to known peers\n";
        }
        else
        {
            std::cout << prefix << IDPeer[id].toStdString() << " was added to known peers\n";
        }
        // отправить зашифрованный и подписанный логин
        SendLogin(WaitingForConfirmation[id], id);
        connectionStage[id]++; // become 3
    }
    else if (connectionStage[id] == 102)
    {
        if (!NewPeer(IDPeer[id], pukMap[id]))
        {
            std::cout << prefix << "Couldn't add " + IDPeer[id].toStdString() + " to known peers\n";
        }
        else
        {
            std::cout << prefix << IDPeer[id].toStdString() << " was added to known peers\n";
        }

        // Отправить зашифрованное и подписанное случайное число А
        if (!SendNumber(id))
        {
            BadConnection(id);
            return;
        }
        connectionStage[id]++; // become 103
    }
    else
    {
        std::cout << prefix << "Wrong connection stage to 'accept'\n";
    }

    std::cout << prefix << "Connection was accepted\n";
}

void Connection::slotRefuse(qint64 id)
{
    if (!WaitingForConfirmation.contains(id) || !connectionStage.contains(id))
    {
        std::cout << prefix << "Couldn't find connection '" << id << "'\n";
        return;
    }

    if (connectionStage[id] == 2 || connectionStage[id] == 102)
    {
        BadConnection(id);
        return;
    }
    else
    {
        std::cout << prefix << "Wrong connection stage to 'refuse'\n";
    }
}

void Connection::slotShowConnected()
{
    int i = 1;
    QMap<qint64, QTcpSocket*>::iterator iter;
    for (iter = socketMap.begin(); iter != socketMap.end(); iter++)
    {
        std::cout << "[peer] " << i << ". " << IDPeer[iter.key()].toStdString() << "\n";
        i++;
    }
}

#if defined(_WIN32) || defined(_WIN64)

void Connection::slotSpecifyTorPath(QString path)
{
    tor_path = path;
}

#endif
