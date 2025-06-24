//
// Created by dingjing on 2/12/25.
//

#include "regex-matcher.h"

#include <QFile>
#include <QDebug>
#include <QRegExp>
#include <hs/hs.h>
#include <opencc.h>

#include "macros/macros.h"


static QString chineseSimpleToTradition(const QString& str);
static QString chineseTraditionToSimple(const QString& str);

static int hyper_scan_match_cb (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx);

class RegexMatcherPrivate
{
    Q_DECLARE_PUBLIC(RegexMatcher);
    friend int hyper_scan_match_cb (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx);
public:
    explicit RegexMatcherPrivate(RegexMatcher* q, const quint64 blockSize);
    ~RegexMatcherPrivate();

    bool compileHyperScan(int mode=HS_MODE_BLOCK);
    bool matchHyperScan(const QString& lineBuf);
    bool matchHyperScan(QFile& file);

    bool matchRegexp(QFile& file);
    bool matchRegexp(const QString& lineBuf);
    qint64 doMatchRegexp(const QString& lineBuf, const QRegExp& regexp, const qint64 offset=0);

    void addMatchPos(quint64 start, quint64 end);

private:
    RegexMatcher*               q_ptr = nullptr;
    bool                        mCaseSensitive = false;
    bool                        mTwMainlandSensitive = false;
    QSet<QString>               mRegxStrings;
    hs_database_t*              mHsDB = nullptr;
    hs_scratch_t*               mScratch = nullptr;

    qint64                      mBlockSize;

    QMap<quint64, quint64>      mMatchRes;
};

RegexMatcherPrivate::RegexMatcherPrivate(RegexMatcher* q, const quint64 blockSize)
    : q_ptr(q), mBlockSize(blockSize)
{
}

RegexMatcherPrivate::~RegexMatcherPrivate()
{
    C_FREE_FUNC(mScratch, hs_free_scratch);
    C_FREE_FUNC(mHsDB, hs_free_database);
}

bool RegexMatcherPrivate::compileHyperScan(int mode)
{
    C_RETURN_VAL_IF_OK(mRegxStrings.isEmpty(), false);
    C_RETURN_VAL_IF_OK(mHsDB && mScratch, true);

    int flags = HS_FLAG_SOM_LEFTMOST | HS_FLAG_ALLOWEMPTY | HS_FLAG_UTF8;
    if (!mCaseSensitive) {
        flags |= HS_FLAG_CASELESS;
    }

    mode |= HS_MODE_SOM_HORIZON_MEDIUM;

    hs_compile_error_t* hsCompileErr = nullptr;

    if (1 == mRegxStrings.count()) {
        const hs_error_t err = hs_compile(mRegxStrings.constBegin()->toUtf8().constData(), flags, mode, nullptr, &mHsDB, &hsCompileErr);
        if (HS_SUCCESS != err) {
            qWarning() << "Error compiling HS regex: " << *mRegxStrings.constBegin() << ", error: " << hsCompileErr->message;
            hs_free_compile_error(hsCompileErr);
            return false;
        }
    }
    else {
        int idx = 0;
        const int num = mRegxStrings.count();
        const auto regStr = new char*[num + 1];
        const auto regIds = new unsigned int[num + 1];
        const auto regFlags = new unsigned int[num + 1];
        for (auto it = mRegxStrings.constBegin(); it != mRegxStrings.constEnd(); ++it) {
            regStr[idx] = const_cast<char*>(it->toUtf8().constData());
            regIds[idx] = idx + 1;
            regFlags[idx] = flags;
            ++idx;
        }

        const hs_error_t err = hs_compile_multi(regStr, regFlags, regIds, num, mode, nullptr, &mHsDB, &hsCompileErr);
        if (HS_SUCCESS != err) {
            qWarning() << "Error compiling HS regex: " << mRegxStrings.toList().join("{]") << ", error: " << hsCompileErr->message;
            hs_free_compile_error(hsCompileErr);
            delete regStr;
            delete regIds;
            delete regFlags;
            return false;
        }

        delete regStr;
        delete regIds;
        delete regFlags;
    }

    if (HS_SUCCESS != hs_alloc_scratch(mHsDB, &mScratch)) {
        qWarning() << "Error allocating HS scratch";
        C_FREE_FUNC(mHsDB, hs_free_database);
        return false;
    }

    return true;
}

void RegexMatcherPrivate::addMatchPos(quint64 start, quint64 end)
{
    mMatchRes.insert(start, end);
}

bool RegexMatcherPrivate::matchHyperScan(const QString& lineBuf)
{
    mMatchRes.clear();

    if (HS_SUCCESS != hs_scan(mHsDB, lineBuf.toUtf8().constData(), lineBuf.toUtf8().count(), 0, mScratch, hyper_scan_match_cb, this)) {
        qWarning() << "Error matching HS regex.";
        return false;
    }

    return true;
}

bool RegexMatcherPrivate::matchHyperScan(QFile& file)
{
#define FREE_STREAM(stream) if (stream) { hs_close_stream(stream, mScratch, hyper_scan_match_cb, NULL); stream = nullptr; }
    mMatchRes.clear();
    hs_stream* stream = nullptr;

    if (HS_SUCCESS != hs_open_stream(mHsDB, 0, &stream)) {
        qWarning() << "Error opening HS regex stream";
        return false;
    }

    while (!file.atEnd()) {
        QByteArray buffer = file.read(mBlockSize);
        if (HS_SUCCESS != hs_scan_stream(stream, buffer.data(), buffer.size(), 0, mScratch, hyper_scan_match_cb, this)) {
            qWarning() << "Error matching HS regex stream";
            FREE_STREAM(stream)
            return false;
        }
    }

    FREE_STREAM(stream);

    return true;
}

bool RegexMatcherPrivate::matchRegexp(QFile& file)
{
    C_RETURN_VAL_IF_OK(mRegxStrings.isEmpty(), false);

    mMatchRes.clear();

    const qint64 step = mBlockSize / 2;
    const qint64 step2 = mBlockSize / 6 * 4;

    auto matchBuffer = [&] (const QRegExp& exp) {
        qint64 readStart = 0;
        qint64 matchOffset = 0;
        while (!file.atEnd()) {
            const auto ret = file.read(mBlockSize);
            matchOffset = doMatchRegexp(ret, exp, readStart);
            if (matchOffset >= 0) {
                if (matchOffset > step) {
                    readStart += matchOffset;
                }
                else {
                    readStart += step;
                }
            }
            else {
                if (ret.size() >= mBlockSize) {
                    readStart += step2;
                }
                else {
                    readStart += ret.size();
                }
            }
            file.seek(readStart);
        }
    };

    if (1 == mRegxStrings.count()) {
        const QRegExp regExp(*mRegxStrings.constBegin(), mCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        matchBuffer(regExp);
    }
    else {
        for (auto& it : mRegxStrings) {
            const QRegExp regExp(it, mCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
            matchBuffer(regExp);
        }
    }

    return true;
}

bool RegexMatcherPrivate::matchRegexp(const QString& lineBuf)
{
    C_RETURN_VAL_IF_OK(mRegxStrings.isEmpty(), false);

    mMatchRes.clear();

    if (1 == mRegxStrings.count()) {
        const QRegExp regExp(*mRegxStrings.constBegin(), mCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        doMatchRegexp(lineBuf, regExp);
    }
    else {
        for (auto& it : mRegxStrings) {
            const QRegExp regExp(it, mCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
            doMatchRegexp(lineBuf, regExp);
        }
    }

    return true;
}

qint64 RegexMatcherPrivate::doMatchRegexp(const QString& lineBuf, const QRegExp& regexp, const qint64 offset)
{
    qint64 pos = 0;

    while (-1 != (pos = regexp.indexIn(lineBuf, pos))) {
        addMatchPos(pos + offset, pos + offset + regexp.matchedLength());
        pos += regexp.matchedLength();
    }

    return pos;
}

RegexMatcher::RegexMatcher(const QString& reg, bool stSensitive, bool caseSensitive, quint64 blockSize, QObject* parent)
    : QObject(parent), d_ptr(new RegexMatcherPrivate(this, blockSize))
{
    Q_D(RegexMatcher);

    d->mCaseSensitive = caseSensitive;
    d->mTwMainlandSensitive = stSensitive;
    if (!reg.isEmpty()) {
        d->mRegxStrings += reg;
    }
}

RegexMatcher::~RegexMatcher()
{
    delete d_ptr;
}

bool RegexMatcher::match(const QString& str)
{
    Q_D(RegexMatcher);

    if (d->compileHyperScan()) {
        return d->matchHyperScan(str);
    }

    return d->matchRegexp(str);
}

bool RegexMatcher::match(QFile& file)
{
    Q_D(RegexMatcher);

    const quint64 fileSize = file.size();
    if (fileSize <= d->mBlockSize) {
        const QByteArray all = file.readAll();
        return match(all);
    }

    if (d->compileHyperScan(HS_MODE_STREAM)) {
        if (d->matchHyperScan(file)) {
            return true;
        }
    }

    return d->matchRegexp(file);
}

const QMap<quint64, quint64>& RegexMatcher::getMatchResult()
{
    Q_D(RegexMatcher);

    return d->mMatchRes;
}

static int hyper_scan_match_cb (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx)
{
    if (!ctx) {
        return HS_SUCCESS;
    }

    static_cast<RegexMatcherPrivate*>(ctx)->addMatchPos(from, to);

    Q_UNUSED(flags);

    return HS_SUCCESS;
}

static QString chineseSimpleToTradition(const QString& str)
{
    opencc::SimpleConverter conv("s2t.json");

    const std::string res = conv.Convert(str.toStdString());

    return QString::fromStdString(res);
}

static QString chineseTraditionToSimple(const QString& str)
{
    opencc::SimpleConverter conv("t2s.json");

    const std::string res = conv.Convert(str.toStdString());

    return QString::fromStdString(res);
}
