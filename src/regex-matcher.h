//
// Created by dingjing on 2/12/25.
//

#ifndef hs_wrap_SCANNER_H
#define hs_wrap_SCANNER_H
#include <qmap.h>
#include <QObject>


class QFile;
class RegexMatcherPrivate;
class RegexMatcherResultIterator;
class RegexMatcher final : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(RegexMatcher);
    friend class ResultIterator;
public:
    class ResultIterator
    {
        // typedef QKeyValueIterator<const qint64&, const qint64&, QMap<qint64, qint64>::const_iterator> ResultConstIterator;
        typedef QMap<qint64, qint64>::const_iterator    ResultConstIterator;
    public:
        explicit ResultIterator(const RegexMatcher& rm);
        bool hasNext() const;
        // keyword, context
        QPair<QString, QString> next();
        void reset();

    private:
        const RegexMatcher&             mRI;
        ResultConstIterator             mEnd;
        ResultConstIterator             mCurrent;
    };

    explicit RegexMatcher(const QString& reg, bool stSensitive=true, bool caseSensitive=true, quint64 blockSize=2^20, QObject *parent = nullptr);
    ~RegexMatcher() override;

    qint64 getMatchedCount();

    bool match(QFile& file);
    bool match(const QString& str);

    QMap<qint64, qint64> getMatchResults();
    ResultIterator getResultIterator() const;

Q_SIGNALS:
    void matchedString(const QString& str, QPrivateSignal);
    bool matchedStringWithCtx(const QString& str, qint64 start, qint64 end, QPrivateSignal);

private:
    RegexMatcherPrivate*            d_ptr = nullptr;
};



#endif // hs_wrap_SCANNER_H
