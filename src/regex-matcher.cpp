//
// Created by dingjing on 2/12/25.
//

#include "regex-matcher.h"

#include <QDebug>
#include <QRegExp>
#include <hs/hs.h>

#include "macros/macros.h"


static int hyper_scan_match_cb (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx);

class RegexMatcherPrivate
{
    Q_DECLARE_PUBLIC(RegexMatcher);
    friend int hyper_scan_match_cb (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx);
public:
    explicit RegexMatcherPrivate(RegexMatcher* q);
    ~RegexMatcherPrivate();

    bool compileHyperScan();
    bool matchHyperScan(const QString& lineBuf);

    bool matchRegexp(const QString& lineBuf);

    void addMatchPos(quint32 start, quint32 end);

private:
    RegexMatcher*               q_ptr = nullptr;
    bool                        mCaseSensitive = false;
    QSet<QString>               mRegxStrings;
    hs_database_t*              mHsDB = nullptr;
    hs_scratch_t*               mScratch = nullptr;

    QMap<quint32, quint32>      mMatchRes;
};

RegexMatcherPrivate::RegexMatcherPrivate(RegexMatcher* q)
    : q_ptr(q)
{
}

RegexMatcherPrivate::~RegexMatcherPrivate()
{
    C_FREE_FUNC(mScratch, hs_free_scratch);
    C_FREE_FUNC(mHsDB, hs_free_database);
}

bool RegexMatcherPrivate::compileHyperScan()
{
    C_RETURN_VAL_IF_OK(mRegxStrings.isEmpty(), false);
    C_RETURN_VAL_IF_OK(mHsDB && mScratch, true);

    int flags = HS_FLAG_SOM_LEFTMOST | HS_FLAG_ALLOWEMPTY;
    if (!mCaseSensitive) {
        flags |= HS_FLAG_CASELESS;
    }

    hs_compile_error_t* hsCompileErr = nullptr;

    if (1 == mRegxStrings.count()) {
        const hs_error_t err = hs_compile(mRegxStrings.constBegin()->toUtf8().constData(), flags, HS_MODE_BLOCK, nullptr, &mHsDB, &hsCompileErr);
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

        const hs_error_t err = hs_compile_multi(regStr, regFlags, regIds, num, HS_MODE_BLOCK, NULL, &mHsDB, &hsCompileErr);
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

void RegexMatcherPrivate::addMatchPos(quint32 start, quint32 end)
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

bool RegexMatcherPrivate::matchRegexp(const QString& lineBuf)
{
    C_RETURN_VAL_IF_OK(mRegxStrings.isEmpty(), false);

    mMatchRes.clear();

    auto matchRegexp = [this] (const QString& str, const QString& reg) ->void {
        int pos = 0;
        const QRegExp regExp(reg, mCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        while (-1 != (pos = regExp.indexIn(str, pos))) {
            addMatchPos(pos, pos + regExp.matchedLength());
            pos += regExp.matchedLength();
        }
    };

    if (1 == mRegxStrings.count()) {
        matchRegexp(lineBuf, *mRegxStrings.constBegin());
    }
    else {
        for (auto it : mRegxStrings) {
            matchRegexp(lineBuf, it);
        }
    }

    return true;
}

RegexMatcher::RegexMatcher(const QString& reg, bool caseSensitive, QObject* parent)
    : QObject(parent), d_ptr(new RegexMatcherPrivate(this))
{
    Q_D(RegexMatcher);

    d->mCaseSensitive = caseSensitive;
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

const QMap<quint32, quint32>& RegexMatcher::getMatchResult()
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
