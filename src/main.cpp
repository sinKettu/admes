#include <QObject>
#include <QCoreApplication>
#include <QTextStream>
#include <QDir>
#include "control.h"
#include "common.h"
#include "account.h"

void help()
{
    std::cout << "-====== H E L P ======-\n\n";
    std::cout << "Welcome to 'admes', asynchronous P2P private messenger!\n";
    std::cout << "The program still is under development, please remember this\n\n";
    std::cout << "You can control (give commands) the program with placing '/' at the beginning of input line\n";
    std::cout << "Available commands (commands in '<...>' are optional):\n\n";
    /* /create */
    std::cout << "/create -- creating new admes account. Uniqueness of account is determined by private EC key.\n\n";
    /* /login */
    std::cout << "/login -- loggin in\n\n";
    /* /open ... */
    std::cout << "/open -- starting a server listening to specified port for incomming connections\n\n";
    /* /connect ... */
    std::cout << "/connect <tor> 'address' 'port' -- connecting to 'address':'port'\n";
    std::cout << "\t\tif you are specifying 'tor', you should specify tor hidden service address at 'address',\n";
    std::cout << "\t\tand the program will connect to this\n\n";
    /* /accept ... */
    std::cout << "/accept 'descriptor' -- accepting connection with unknown peer\n";
    std::cout << "\t\t(admes will say when you will have to enter this)\n\n";
    /* /refuse ... */
    std::cout << "/refuse 'descriptor' -- refusing connection with unknown peer\n";
    std::cout << "\t\t(admes will say when you will have to enter this)\n\n";
    /* /chat ... */
    std::cout << "/chat 'peer' -- starting a chat mode with peer 'peer'\n";
    std::cout << "\t\tIn the chat mode all your input without '/' at beginning of input line\n";
    std::cout << "\t\twill be considered as outcomming message to 'peer'\n\n";
    /* /close */
    std::cout << "/close -- stopping a chat mode\n\n";
    /* /disconnect ... */
    std::cout << "/disconnect 'peer' -- disconnecting socket 'peer', clearing all message history with it\n\n";
    /* /remove ... */
    std::cout << "/remove 'peer' -- removing 'peer' from list of known ones\n\n";
    /* /tor ... */
    std::cout << "/tor <check|kill|start|log> -- checking if tor is run |\n";
    std::cout << "\t\tchecking and killing tor |\n";
    std::cout << "\t\tstarting tor with user configurations |\n";
    std::cout << "\t\tOutput log of the Tor session\n\n";
    /* /configure ... */
    std::cout << "/configure <socks 'port'> <port 'port'> -- configuring ports which SOCKS5 server and\n";
    std::cout << "\t\tAdmes server will listen to. Defaults: SOCKS5 - 9050, Admes - 4242\n\n";
    /* /version */
    std::cout << "/version -- version of Admes\n\n";
    /* /exit */
    std::cout << "/exit -- close the program\n";

    std::cout << "\nThanks for using 'admes'\n";
    std::cout << "\n-=====================-\n";

}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QTextStream qin(stdin);
    QString input;
    Control *c = new Control();
    QObject::connect(c, SIGNAL(sigCloseProgram()), &a, SLOT(quit()), Qt::QueuedConnection);
    QString chatPeerName = 0;
    bool run = true;

    HighLight(QString("admes is ready to interpret your commands!"));
    std::cout << prefix << "Enter '/help' to get help\n";

#if defined (_WIN32) || defined(_WIN32)

    std::cout << prefix << "It seems like you use Windows\n";
    std::cout << prefix << "Please, specify a path to the 'tor.exe'. To exit type '/exit'\n";
    run = false;
    while (true)
    {
        std::cout << "Path: ";
        input = qin.readLine();
        if (QFile(input).exists())
        {
            std::cout << prefix << "Path is correct!\n";
            c->SpecifyTorPath(input);
            run = true;
            break;
        }
        else if (input.compare("/exit"))
        {
            std::cout << prefix << "Incorrect path!\n";
        }
        else
        {
            run = false;
            break;
        }
    }

#endif

    QDir dir("./config");
    if(!dir.exists() && !QDir().mkdir(dir.path()))
    {
        std::cout << prefix << "Couldn't find or create config directory!\n";
        run = false;
    }

    dir = QDir("./config/users");
    if(!dir.exists() && !QDir().mkdir(dir.path()))
    {
        std::cout << prefix << "Couldn't find or create user directory!\n";
        run = false;
    }

    while (run)
    {
        input = qin.readLine();
        if (input.length() == 0)
            continue;

        QStringList commands = input.split(' ');
        if (commands.length() == 1 && !commands[0].compare("/help"))
        {
            help();
            continue;
        }
        if (commands.length() == 1 && !commands[0].compare("/create"))
        {
            std::cout << "New user's name: ";
            QString login = qin.readLine();
            if (login.length() == 0)
            {
                std::cout << prefix << "Bad login\n";
                continue;
            }
            
            std::cout << prefix << "The password will not be hidden!\n";
            std::cout << prefix << "It will be fixed soon\n";
            std::cout << "Password: ";
            QString password = qin.readLine();
            if (password.length() == 0)
            {
                std:: cout << prefix << "Bad password\n";
                continue;
            }

            if (CreateAccount(login, password))
            {
                std::cout << prefix << "Account was successfully created\n";
                continue;
            }
            else
            {
                std::cout << prefix << "Couldn't create account\n";
                continue;
            }
        }
        if (commands.length() == 1 && commands[0].compare("/login") == 0)
        {
            std::cout << "Login: ";
            QString login = qin.readLine();
            if (login.length() == 0)
            {
                std::cout << prefix << "Bad login\n";
                continue;
            }
            
            std::cout << prefix << "The password will not be hidden!\n";
            std::cout << prefix << "It will be fixed soon\n";
            std::cout << "Password: ";
            QString password = qin.readLine();
            if (password.length() == 0)
            {
                std:: cout << prefix << "Bad password\n";
                continue;
            }

            if (LogIn(login, password))
            {
                std::cout << prefix << "Hi, " << login.toStdString() << "\n";
                continue;
            }
            else
            {
                std::cout << prefix << "Couldn't log in\n";
                continue;
            }
        }
        if (commands[0].compare("/open") == 0 && commands.length() == 1)
        {
            if (!IsLoggedIn())
            {
                std::cout << prefix << "You must log in first\n";
                continue;;
            }
            c->StartServer();
            continue;
        }
        if (!commands[0].compare("/tor") && commands.length() >= 2)
        {
            if (!commands[1].compare("check") && commands.length() == 2)
            {
                if (TorIsRunning())
                {
                    std::cout << prefix << "Tor is running\n";
                    continue;
                }
                else
                {
                    std::cout << prefix << "Tor is not running\n";
                    continue;
                }
            }
            else if (!commands[1].compare("kill") && commands.length() == 2)
            {
                if (KillTor())
                {
                    std::cout << prefix << "Tor process was killed\n";
                    continue;
                }
                else
                {
                    std::cout << prefix << "Couldn't find or kill tor process\n";
                    continue;
                }
            }
            else if (!commands[1].compare("start") && commands.length() == 2)
            {
                c->RunTor();
                continue;
            }
            else if (!commands[1].compare("log") && commands.length() == 2)
            {
                c->ShowTorLog();
                continue;
            }
        }
        else if (!commands[0].compare("/configure") && 
                (commands.length() == 3 || commands.length() == 5))
        {
            quint16 soc = 0;
            quint16 ser = 0;
            if (!commands[1].compare("socks"))
            {
                soc = commands[2].toUShort();
                if (!soc)
                {
                    std::cout << prefix << "Incorrect SOCKS5 port\n";
                    continue;
                }
            }
            else if (!commands[1].compare("port"))
            {
                ser = commands[2].toUShort();
                if (!ser)
                {
                    std::cout << prefix << "Incorrect port to listen\n";
                    continue;
                }
            }

            if (commands.length() == 5 && !commands[3].compare("socks"))
            {
                if (soc)
                {
                    std::cout << prefix << "Double definition for SOCKS5 port\n";
                    continue;
                }
                soc = commands[4].toUShort();
                if (!soc)
                {
                    std::cout << prefix << "Incorrect SOCKS5 port\n";
                    continue;
                }
            }
            else if (commands.length() == 5 && !commands[3].compare("port"))
            {
                if (ser)
                {
                    std::cout << prefix << "Double definition for port to listen\n";
                    continue;
                }
                ser = commands[4].toUShort();
                if (!ser)
                {
                    std::cout << prefix << "Incorrect port to listen\n";
                    continue;
                }
            }

            if (soc || ser)
            {
                if (ser)
                {
                    c->SpecifyPortForListening(ser);
                }
                if (soc)
                {
                    c->SpecifyPortForSOCKS5(soc);
                }
                continue;
            }
        }
        if (commands.length() == 2 && !commands[0].compare("/list"))
        {
            if (!commands[1].compare("known"))
            {
                if (!IsLoggedIn())
                {
                    std::cout << prefix << "You must log in first\n";
                    continue;;
                }

                ShowKnown();
                continue;
            }
            else if (!commands[1].compare("connected"))
            {
                if (!IsLoggedIn())
                {
                    std::cout << prefix << "You must log in first\n";
                    continue;;
                }

                c->ShowConnected();
                continue;
            }
            
        }
        if (commands.length() == 2 && !commands[0].compare("/remove"))
        {
            if (!IsLoggedIn())
            {
                std::cout << prefix << "You must log in first\n";
                continue;
            }

            RemovePeer(commands[1]);
            std::cout << prefix << "Done\n";
            c->Disconnect(commands[1]);
            continue;
        }
        if (commands[0].compare("/connect") == 0 && commands.length() >= 3)
        {
            if (!IsLoggedIn())
            {
                std::cout << prefix << "You must log in first\n";
                continue;;
            }
            if (commands[1].compare("tor") == 0 && commands.length() == 4)
            {
                c->ConnectThroughSOCKS5(commands[2], commands[3].toUShort());
                continue;
            }
            else if (commands[1].compare("tor") && commands.length() == 3)
            {
                c->ConnectTo(commands[1], commands[2].toUShort());
                continue;
            }
        }
        if (commands.length() == 2 && !commands[0].compare("/accept"))
        {
            c->Accept(commands[1].toLongLong());
            continue;
        }
        if (commands.length() == 2 && !commands[0].compare("/refuse"))
        {
            c->Refuse(commands[1].toLongLong());
            continue;
        }
        if (commands[0].compare("/disconnect") == 0 && commands.length() == 2)
        {
            c->Disconnect(commands[1]);
            continue;
        }
        if (commands[0].compare("/chat") == 0 && commands.length() == 2)
        {
            chatPeerName = commands[1];
            c->OutputDialog(commands[1]);
            continue;
        }
        if (chatPeerName.length() && input[0] != '/')
        {
            c->Send(chatPeerName, input);
            continue;
        }
        if (!commands[0].compare("/close") && commands.length() == 1)
        {
            chatPeerName = "";
            c->CloseDialog();
            continue;
        }
        if (!commands[0].compare("/version") && commands.length() == 1)
        {
            std::cout << prefix << "Version: 1.1\n";
            continue;
        }
        if (!commands[0].compare("/exit") && commands.length() == 1)
        {
            break;
        }

        std::cout <<  prefix << "Wrong command!\n";
    }

    c->StopAll();
    return a.exec();
}
