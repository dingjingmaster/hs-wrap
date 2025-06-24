//
// Created by dingjing on 2/12/25.
//

#ifndef hs_wrap_SCANNER_H
#define hs_wrap_SCANNER_H
#include <QObject>

class QFile;
class RegexMatcherPrivate;
class RegexMatcher final : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(RegexMatcher);
public:
    explicit RegexMatcher(const QString& reg, bool stSensitive=true, bool caseSensitive=true, quint64 blockSize=2^20, QObject *parent = nullptr);
    ~RegexMatcher() override;

    bool match(QFile& file);
    bool match(const QString& str);
    bool match(QFile& file, QList<QString>& ctx, qint32 count);

Q_SIGNALS:
    void matchedString(const QString& str, QPrivateSignal);

private:
    RegexMatcherPrivate*            d_ptr = nullptr;
};



#endif // hs_wrap_SCANNER_H
