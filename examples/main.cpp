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

    return 0;
}
