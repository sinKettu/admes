#include <QCoreApplication>
#include <QString>
#include <QTextStream>
#include <iostream>
#include <string>
#include "control.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QTextStream qout(stdout);
    QTextStream qin(stdin);
    QString input;
    Control *c = new Control();
    qint64 chatID = 0;
    const char *separator = "--------------------------------\n";

    std::cout << "\n" << separator;
    std::cout << "admes is ready to interpret your commands!\n";
    std::cout << separator << "\n";

    while (true)
    {
        input = qin.readLine();
        if (input.length() == 0)
            continue;

        QStringList commands = input.split(' ');
        if (commands[0].compare("/open") == 0 && commands.length() >= 2)
        {
            if (commands[1].compare("tor") == 0 && commands.length() == 3)
            {
                c->StartTorServer(commands[2].toUShort());
                continue;
            }
            else if (commands[1].compare("tor") && commands.length() == 2)
            {
                c->StartServer(commands[1].toUShort());
                continue;
            }
        }
        if (commands[0].compare("/connect") == 0 && commands.length() >= 3)
        {
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
        if (commands[0].compare("/disconnect") == 0 && commands.length() == 2)
        {
            c->Disconnect(commands[1].toLongLong());
            continue;
        }
        if (commands[0].compare("/chat") == 0 && commands.length() == 2)
        {
            std::cout << "\n" << separator;
            std::cout << "admes switched to dialog mode\n";
            std::cout << separator << "\n";

            chatID = commands[1].toLongLong();
            c->OutputDialog(chatID);
            continue;
        }
        if (chatID && input[0] != '/')
        {
            c->Send(chatID, input);
            continue;
        }
        if (commands[0].compare("/close") == 0 && commands.length() == 1)
        {
            chatID = 0;
            c->CloseDialog();
            continue;
        }
        if (commands[0].compare("/exit") == 0)
        {
            exit(0);
        }

        printf("Wrong command!!!\n");
    }

    return a.exec();
}
