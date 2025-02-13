//
// Created by dingjing on 2/12/25.
//

#ifndef hs_wrap_SCANNER_H
#define hs_wrap_SCANNER_H
#include <QObject>

class RegexMatcherPrivate;
class RegexMatcher final : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(RegexMatcher);
public:
    explicit RegexMatcher(const QString& reg, bool caseSensitive=true, QObject *parent = nullptr);
    ~RegexMatcher() override;

    bool match(const QString& str);
    const QMap<quint32, quint32>& getMatchResult();


Q_SIGNALS:
    void matchedString(const QString& str, QPrivateSignal);

private:
    RegexMatcherPrivate*            d_ptr = nullptr;
};



#endif // hs_wrap_SCANNER_H
