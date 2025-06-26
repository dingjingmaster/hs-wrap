//
// Created by dingjing on 2/12/25.
//

#include <QFile>
#include <QDebug>
#include <QString>

#include "regex-matcher.h"


int main (int argc, char* argv[])
{
#if 0
    QString str = "Hello Worldjkkkkkaaaazzzz";

    auto matchStr = [&] (const QString& str, const QString& reg) ->void {
        qInfo() << "str: " << str;
        qInfo() << "reg: " << reg;
        RegexMatcher rm(reg);
        rm.match(str);
        rm.match(str);
        rm.match(str);
        qInfo() << "match result: ";
        auto res = rm.getMatchResult();
        for (auto k : res.keys()) {
            // qInfo() << k << "=" << res[k];
            qInfo() << str.midRef(k, res[k] - k);
        }
        qInfo() << "<==================";
    };

    matchStr(str, "Hello");
    matchStr(str, "World");
    matchStr(str, "H\\D+W");
    matchStr(str, "K+");
    matchStr(str, "k+");
    matchStr(str, "k");
    matchStr(str, "H* W");
    return 0;
#endif

#if 0
    QFile file("/home/dingjing/aaa.txt");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        RegexMatcher rm("安得合众");
        rm.match(file);
        file.close();
        qInfo() << "=======>";
        RegexMatcher::ResultIterator res = rm.getResultIterator();
        while (res.hasNext()) {
            auto r = res.next();
            qInfo() << "k: " << r.first << " v: " << r.second;
        }
    }
#endif


    {
        // const QString file1 = "/home/dingjing/pf/运营商.md";
        const QString file1 = "/home/dingjing/pf/安得合众.md";
        QFile file(file1);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // RegexMatcher rm("(安\\.\\{0,15\\}得\\.\\{0,15\\}合\\.\\{0,15\\}众|安\\.\\{0,15\\}得\\.\\{0,15\\}合\\.\\{0,15\\}衆|运\\.\\{0,15\\}营\\.\\{0,15\\}商|運\\.\\{0,15\\}營\\.\\{0,15\\}商)");
            // RegexMatcher rm("(安.{0.15}得.{0,15}合.{0,15}众|运.{0,15}营.{0,15}商)");
            RegexMatcher rm("(安.{0,15}得.{0,15}合.{0,15}众|安.{0,15}得.{0,15}合.{0,15}衆|运.{0,15}营.{0,15}商|運.{0,15}營.{0,15}商)");
            rm.match(file);
            file.close();
            qInfo() << "=======>";
            RegexMatcher::ResultIterator res = rm.getResultIterator();
            while (res.hasNext()) {
                auto r = res.next();
                qInfo() << "k: " << r.first << " v: " << r.second;
            }
        }
    }

    return 0;
}
