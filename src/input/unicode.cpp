#include <hwpedit/unicode.hpp>
#include <unicode/ubrk.h>
#include <algorithm>
#include <memory>
namespace he {
bool validUnicode(const QString& s) {
    for (qsizetype i=0;i<s.size();++i) {
        const QChar c=s[i];
        if(c.isHighSurrogate()) { if(++i>=s.size() || !s[i].isLowSurrogate()) return false; }
        else if(c.isLowSurrogate()) return false;
        else if(c.unicode()==0) return false;
    }
    return true;
}
static QVector<int> boundaries(const QString& s, UBreakIteratorType type) {
    UErrorCode error=U_ZERO_ERROR;
    std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> it(
        ubrk_open(type,"ko_KR",reinterpret_cast<const UChar*>(s.utf16()),int(s.size()),&error),ubrk_close);
    QVector<int> out;
    if(U_FAILURE(error) || !it) return {0,int(s.size())};
    for(int n=ubrk_first(it.get());n!=UBRK_DONE;n=ubrk_next(it.get())) out.append(n);
    return out;
}
QVector<int> graphemeBoundaries(const QString& s) { return boundaries(s,UBRK_CHARACTER); }
bool isGraphemeBoundary(const QString& s,int p) { auto b=graphemeBoundaries(s);return std::binary_search(b.begin(),b.end(),p); }
static int move(const QString& s,int p,bool forward,UBreakIteratorType t) {
    auto b=boundaries(s,t);p=std::clamp(p,0,int(s.size()));
    if(forward) {auto i=std::upper_bound(b.begin(),b.end(),p);return i==b.end()?int(s.size()):*i;}
    auto i=std::lower_bound(b.begin(),b.end(),p);return i==b.begin()?0:*std::prev(i);
}
int previousGrapheme(const QString&s,int p){return move(s,p,false,UBRK_CHARACTER);}
int nextGrapheme(const QString&s,int p){return move(s,p,true,UBRK_CHARACTER);}
int previousWord(const QString&s,int p){return move(s,p,false,UBRK_WORD);}
int nextWord(const QString&s,int p){return move(s,p,true,UBRK_WORD);}
QString normalizeNewlines(QString s){s.replace("\r\n","\n");s.replace('\r','\n');return s;}
}
