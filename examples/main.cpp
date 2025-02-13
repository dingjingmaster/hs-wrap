//
// Created by dingjing on 2/12/25.
//

#include <QDebug>
#include <QString>

#include "regex-matcher.h"


int main (int argc, char* argv[])
{
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
}
