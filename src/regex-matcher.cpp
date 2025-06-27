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
static QString validUtf8String(const char* data, int dataLen);
static QString validUtf8String(const QString& data);


static int hyper_scan_match_cb (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx);

class RegexMatcherPrivate
{
    Q_DECLARE_PUBLIC(RegexMatcher);
    friend int hyper_scan_match_cb (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void* ctx);
public:
    explicit RegexMatcherPrivate(RegexMatcher* q, qint64 blockSize);
    ~RegexMatcherPrivate();

    bool compileHyperScan(int mode=HS_MODE_BLOCK);
    bool matchHyperScan(const QString& lineBuf);
    bool matchHyperScan(QFile& file);

    bool matchRegexp(QFile& file);
    bool matchRegexp(const QString& lineBuf);
    qint64 doMatchRegexp(const QString& lineBuf, const QRegExp& regexp, const qint64 offset=0);

    void addMatchPos(quint64 start, quint64 end);
    bool alreadyMatched() const;

private:
    RegexMatcher*               q_ptr = nullptr;
    bool                        mCaseSensitive = false;
    bool                        mTwMainlandSensitive = false;
    QSet<QString>               mRegxStrings;
    hs_database_t*              mHsDB = nullptr;
    hs_scratch_t*               mScratch = nullptr;

    qint64                      mBlockSize;

    QMap<qint64, qint64>        mMatchRes;

    // 上下文
    QString                     mContext;           // 检查的字符串/或文件路径
};

RegexMatcherPrivate::RegexMatcherPrivate(RegexMatcher* q, qint64 blockSize)
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

    int flags = HS_FLAG_SOM_LEFTMOST | HS_FLAG_ALLOWEMPTY | HS_FLAG_UTF8 | HS_FLAG_UCP | HS_FLAG_MULTILINE;
    if (!mCaseSensitive) {
        flags |= HS_FLAG_CASELESS;
    }

    if (HS_MODE_STREAM == mode) {
        mode |= HS_MODE_SOM_HORIZON_LARGE;
    }

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
    mMatchRes.insert(static_cast<qint64>(start), static_cast<qint64>(end));
}

bool RegexMatcherPrivate::alreadyMatched() const
{
    return mMatchRes.count() > 0;
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

    const qint64 step2 = mBlockSize / 6 * 4;

    auto matchBuffer = [&] (const QRegExp& exp) {
        qint64 readStart = 0;
        qint64 matchOffset = 0;
        while (!file.atEnd()) {
            const auto ret = file.read(mBlockSize);
            matchOffset = doMatchRegexp(ret, exp, readStart);
            if (matchOffset >= 0) {
                if (matchOffset > step2) {
                    readStart += matchOffset;
                }
                else {
                    readStart += step2;
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
        const qint64 pc = regexp.matchedLength();
        const QString key = lineBuf.mid(pos, pc);
        const qint64 left = lineBuf.left(pos).toUtf8().size();
        // qInfo() << "==> " << key << " p: " << pos << " off: " << offset;
        const qint64 keyLen = key.toUtf8().size();
        addMatchPos(offset + left, offset + left + keyLen);
        pos += pc;
    }

    return pos;
}

RegexMatcher::ResultIterator::ResultIterator(const RegexMatcher& map)
    : mRI(map)
{
    reset();
}

bool RegexMatcher::ResultIterator::hasNext() const
{
    return mCurrent != mEnd;
}

QPair<QString, QString> RegexMatcher::ResultIterator::next()
{
    QPair<QString, QString> pair("", "");

    if (mCurrent != mEnd) {
        const qint64 s = mCurrent.key();
        const qint64 e = mCurrent.value();
        if (!QFile::exists(mRI.d_ptr->mContext)) {
            qint64 s1 = s - 24;
            qint64 e1 = e + 24;

            const QByteArray bt = mRI.d_ptr->mContext.toUtf8();

            if (s1 < 0) { s1 = 0; }
            if (e1 > bt.size()) { e1 = bt.size(); }

            const QString key = bt.mid(static_cast<int>(s), static_cast<int>(e - s));
            const QByteArray ctxT = bt.mid(static_cast<int>(s1), static_cast<int>(e1 - s1));
            const QString ctx = validUtf8String(ctxT);
            pair = QPair<QString, QString>(key, ctx);
        }
        else {
            QFile file(mRI.d_ptr->mContext);

            qint64 s1 = s - 24;
            qint64 e1 = e + 24;

            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const qint64 fileSize = file.size();
                if (s1 < 0) { s1 = 0; }
                if (e1 > fileSize) { e1 = fileSize; }

                file.seek(s1);
                const QString ctxT = file.read(e1 - s1);
                file.seek(s);
                const QString key = file.read(e - s);
                file.close();
                const QString ctx = validUtf8String(ctxT.toUtf8().constData(), ctxT.toUtf8().size());
                pair = QPair<QString, QString>(key, ctx);
            }
        }
        ++mCurrent;
    }

    return pair;
}

void RegexMatcher::ResultIterator::reset()
{
    mCurrent = mRI.d_ptr->mMatchRes.constBegin();
    mEnd = mRI.d_ptr->mMatchRes.constEnd();
}

RegexMatcher::RegexMatcher(const QString& reg, bool caseSensitive, qint64 blockSize, QObject* parent)
    : QObject(parent), d_ptr(new RegexMatcherPrivate(this, blockSize))
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

bool RegexMatcher::match(QFile& file)
{
    Q_D(RegexMatcher);

    d->mContext = file.fileName();

    bool ret = false;

    const quint64 fileSize = file.size();
    if (fileSize <= d->mBlockSize) {
        const QByteArray all = file.readAll();
        ret = match(all);
    }

    if (!ret) {
        if (d->compileHyperScan(HS_MODE_STREAM)) {
            if (d->matchHyperScan(file)) {
                ret = true;
            }
        }
    }
    if (!ret) {
        ret = d->matchRegexp(file);
    }

    return ret;
}

QMap<qint64, qint64> RegexMatcher::getMatchResults()
{
    Q_D(RegexMatcher);

    return d->mMatchRes;
}

RegexMatcher::ResultIterator RegexMatcher::getResultIterator() const
{
    return ResultIterator(*this);
}

qint64 RegexMatcher::getMatchedCount()
{
    Q_D(RegexMatcher);

    return d->mMatchRes.size();
}

bool RegexMatcher::match(const QString& str)
{
    Q_D(RegexMatcher);

    d->mContext = str;

    if (d->compileHyperScan()) {
        return d->matchHyperScan(str);
    }

    return d->matchRegexp(str);
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


inline uint32_t utf8_to_codepoint(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return 0;

    uint32_t codepoint = 0;
    switch (len) {
        default:
        case 1: {
            codepoint = data[0];
            break;
        }
        case 2: {
            codepoint = ((data[0] & 0x1F) << 6) | (data[1] & 0x3F);
            break;
        }
        case 3: {
            codepoint = ((data[0] & 0x0F) << 12) | ((data[1] & 0x3F) << 6) | (data[2] & 0x3F);
            break;
        }
        case 4: {
            codepoint = ((data[0] & 0x07) << 18) | ((data[1] & 0x3F) << 12) | ((data[2] & 0x3F) << 6) | (data[3] & 0x3F);
            break;
        }
    }

    return codepoint;
}

inline bool is_visible_2byte_char(uint32_t codepoint)
{
    // 2字节范围: U+0080 - U+07FF
    if (codepoint < 0x80 || codepoint > 0x7FF) {
        return false;
    }

    // 拉丁扩展补充 (Latin-1 Supplement): U+00A0 - U+00FF
    if (codepoint >= 0x00A0 && codepoint <= 0x00FF) {
        return true;
    }

    // 拉丁扩展-A (Latin Extended-A): U+0100 - U+017F
    if (codepoint >= 0x0100 && codepoint <= 0x017F) {
        return true;
    }

    // 拉丁扩展-B (Latin Extended-B): U+0180 - U+024F
    if (codepoint >= 0x0180 && codepoint <= 0x024F) {
        return true;
    }

    // IPA 扩展 (IPA Extensions): U+0250 - U+02AF
    if (codepoint >= 0x0250 && codepoint <= 0x02AF) {
        return true;
    }

    // 希腊文和科普特文 (Greek and Coptic): U+0370 - U+03FF
    if (codepoint >= 0x0370 && codepoint <= 0x03FF) {
        return true;
    }

    // 西里尔文 (Cyrillic): U+0400 - U+04FF
    if (codepoint >= 0x0400 && codepoint <= 0x04FF) {
        return true;
    }

    // 西里尔文补充 (Cyrillic Supplement): U+0500 - U+052F
    if (codepoint >= 0x0500 && codepoint <= 0x052F) {
        return true;
    }

    // 亚美尼亚文 (Armenian): U+0530 - U+058F
    if (codepoint >= 0x0530 && codepoint <= 0x058F) {
        return true;
    }

    // 希伯来文 (Hebrew): U+0590 - U+05FF
    if (codepoint >= 0x0590 && codepoint <= 0x05FF) {
        return true;
    }

    // 阿拉伯文 (Arabic): U+0600 - U+06FF
    if (codepoint >= 0x0600 && codepoint <= 0x06FF) {
        return true;
    }

    return false;
}

inline bool is_visible_3byte_char(uint32_t codepoint)
{
    // 3字节范围: U+0800 - U+FFFF
    if (codepoint < 0x0800 || codepoint > 0xFFFF) {
        return false;
    }

    // 排除代理对范围
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
        return false;
    }

    // 一般标点符号 (General Punctuation): U+2000 - U+206F
    if (codepoint >= 0x2000 && codepoint <= 0x206F) {
        return true;
    }

    // 上标和下标 (Superscripts and Subscripts): U+2070 - U+209F
    if (codepoint >= 0x2070 && codepoint <= 0x209F) {
        return true;
    }

    // 货币符号 (Currency Symbols): U+20A0 - U+20CF
    if (codepoint >= 0x20A0 && codepoint <= 0x20CF) {
        return true;
    }

    // 数学符号 (Mathematical Operators): U+2200 - U+22FF
    if (codepoint >= 0x2200 && codepoint <= 0x22FF) {
        return true;
    }

    // 杂项技术符号 (Miscellaneous Technical): U+2300 - U+23FF
    if (codepoint >= 0x2300 && codepoint <= 0x23FF) {
        return true;
    }

    // 控制图片 (Control Pictures): U+2400 - U+243F
    if (codepoint >= 0x2400 && codepoint <= 0x243F) {
        return true;
    }

    // 几何形状 (Geometric Shapes): U+25A0 - U+25FF
    if (codepoint >= 0x25A0 && codepoint <= 0x25FF) {
        return true;
    }

    // 杂项符号 (Miscellaneous Symbols): U+2600 - U+26FF
    if (codepoint >= 0x2600 && codepoint <= 0x26FF) {
        return true;
    }

    // 中日韩符号和标点 (CJK Symbols and Punctuation): U+3000 - U+303F
    if (codepoint >= 0x3000 && codepoint <= 0x303F) {
        return true;
    }

    // 日文平假名 (Hiragana): U+3040 - U+309F
    if (codepoint >= 0x3040 && codepoint <= 0x309F) {
        return true;
    }

    // 日文片假名 (Katakana): U+30A0 - U+30FF
    if (codepoint >= 0x30A0 && codepoint <= 0x30FF) {
        return true;
    }

    // 韩文注音字母 (Bopomofo): U+3100 - U+312F
    if (codepoint >= 0x3100 && codepoint <= 0x312F) {
        return true;
    }

    // 韩文兼容字母 (Hangul Compatibility Jamo): U+3130 - U+318F
    if (codepoint >= 0x3130 && codepoint <= 0x318F) {
        return true;
    }

    // 中日韩统一表意文字 (CJK Unified Ideographs): U+4E00 - U+9FFF
    if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
        return true;
    }

    // 韩文音节 (Hangul Syllables): U+AC00 - U+D7AF
    if (codepoint >= 0xAC00 && codepoint <= 0xD7AF) {
        return true;
    }

    // 中日韩兼容表意文字 (CJK Compatibility Ideographs): U+F900 - U+FAFF
    if (codepoint >= 0xF900 && codepoint <= 0xFAFF) {
        return true;
    }

    // 字母表示形式 (Alphabetic Presentation Forms): U+FB00 - U+FB4F
    if (codepoint >= 0xFB00 && codepoint <= 0xFB4F) {
        return true;
    }

    // 阿拉伯文表示形式-A (Arabic Presentation Forms-A): U+FB50 - U+FDFF
    if (codepoint >= 0xFB50 && codepoint <= 0xFDFF) {
        return true;
    }

    // 组合半标记 (Combining Half Marks): U+FE20 - U+FE2F
    if (codepoint >= 0xFE20 && codepoint <= 0xFE2F) {
        return true;
    }

    // 中日韩兼容形式 (CJK Compatibility Forms): U+FE30 - U+FE4F
    if (codepoint >= 0xFE30 && codepoint <= 0xFE4F) {
        return true;
    }

    // 小形式变体 (Small Form Variants): U+FE50 - U+FE6F
    if (codepoint >= 0xFE50 && codepoint <= 0xFE6F) {
        return true;
    }

    // 阿拉伯文表示形式-B (Arabic Presentation Forms-B): U+FE70 - U+FEFF
    if (codepoint >= 0xFE70 && codepoint <= 0xFEFF) {
        return true;
    }

    // 半角和全角形式 (Halfwidth and Fullwidth Forms): U+FF00 - U+FFEF
    if (codepoint >= 0xFF00 && codepoint <= 0xFFEF) {
        return true;
    }

    return false;
}

inline bool is_visible_4byte_char(uint32_t codepoint) {
    // 4字节范围: U+10000 - U+10FFFF
    if (codepoint < 0x10000 || codepoint > 0x10FFFF) {
        return false;
    }

    // 表情符号 (Emoticons): U+1F600 - U+1F64F
    if (codepoint >= 0x1F600 && codepoint <= 0x1F64F) {
        return true;
    }

    // 杂项符号和象形文字 (Miscellaneous Symbols and Pictographs): U+1F300 - U+1F5FF
    if (codepoint >= 0x1F300 && codepoint <= 0x1F5FF) {
        return true;
    }

    // 传输和地图符号 (Transport and Map Symbols): U+1F680 - U+1F6FF
    if (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) {
        return true;
    }

    // 补充符号和象形文字 (Supplemental Symbols and Pictographs): U+1F900 - U+1F9FF
    if (codepoint >= 0x1F900 && codepoint <= 0x1F9FF) {
        return true;
    }

    // 中日韩统一表意文字扩展B (CJK Unified Ideographs Extension B): U+20000 - U+2A6DF
    if (codepoint >= 0x20000 && codepoint <= 0x2A6DF) {
        return true;
    }

    // 中日韩统一表意文字扩展C (CJK Unified Ideographs Extension C): U+2A700 - U+2B73F
    if (codepoint >= 0x2A700 && codepoint <= 0x2B73F) {
        return true;
    }

    // 中日韩统一表意文字扩展D (CJK Unified Ideographs Extension D): U+2B740 - U+2B81F
    if (codepoint >= 0x2B740 && codepoint <= 0x2B81F) {
        return true;
    }

    // 中日韩兼容表意文字补充 (CJK Compatibility Ideographs Supplement): U+2F800 - U+2FA1F
    if (codepoint >= 0x2F800 && codepoint <= 0x2FA1F) {
        return true;
    }

    return false;
}


static QString validUtf8String(const QString& data)
{
    const QByteArray bt = data.toUtf8();
    return validUtf8String(bt.data(), bt.size());
}

static QString validUtf8String(const char* data, int dataLen)
{
    C_RETURN_VAL_IF_FAIL(data && dataLen > 0, "");

    QByteArray buffer;
    for (int i = 0; i < dataLen; ++i) {
        const uchar byte = ((uint8_t*)data)[i];
        if ((0x80 & byte) == 0x00) {
            buffer.append(data[i]);
        }
        else if ((0xE0 & byte) == 0xC0) {
            if ((i + 2) < dataLen){
                const uint32_t code = utf8_to_codepoint((uint8_t*)(data + i), 2);
                if (is_visible_2byte_char(code)) {
                    buffer.append(data[i]);
                    buffer.append(data[i + 1]);
                    ++i;
                }
            }
        }
        else if ((0xF0 & byte) == 0xE0) {
            if ((i + 3) < dataLen) {
                const uint32_t code = utf8_to_codepoint((uint8_t*)(data + i), 3);
                if (is_visible_3byte_char(code)) {
                    buffer.append(data[i]);
                    buffer.append(data[i + 1]);
                    buffer.append(data[i + 2]);
                    i += 2;
                }
            }
        }
        else if ((0xF8 & byte) == 0xF0) {
            if ((i + 4) < dataLen) {
                const uint32_t code = utf8_to_codepoint((uint8_t*)(data + i), 4);
                if (is_visible_4byte_char(code)) {
                    buffer.append(data[i]);
                    buffer.append(data[i + 1]);
                    buffer.append(data[i + 2]);
                    buffer.append(data[i + 3]);
                    i += 3;
                }
            }
        }
    }

    return buffer;
}
