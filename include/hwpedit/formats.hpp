#pragma once
#include <hwpedit/model.hpp>
#include <QDomDocument>
namespace he {
Result<QMap<QString,QByteArray>> readZip(const QByteArray& bytes,const Limits& limits={});
Result<QByteArray> writeZip(const QMap<QString,QByteArray>& parts);
Result<QDomDocument> readXml(const QByteArray& bytes,const QString&part,const Limits&limits={});
Result<Document> readHwpx(const QByteArray& bytes,const Limits&limits={});
Result<QByteArray> writeHwpx(const Document& document);
Result<Document> readHwp5(const QByteArray& bytes,const Limits&limits={});
Result<QByteArray> writeHwp5(const Document& document);
Result<QByteArray> readFile(const QString&path,const Limits&limits={});
Result<void> atomicWrite(const QString&path,const QByteArray&bytes);
QString errorName(ErrorCode code);
namespace xml {
inline const QString hp="http://www.hancom.co.kr/hwpml/2011/paragraph";
inline const QString hh="http://www.hancom.co.kr/hwpml/2011/head";
inline const QString hc="http://www.hancom.co.kr/hwpml/2011/core";
inline const QString hs="http://www.hancom.co.kr/hwpml/2011/section";
inline const QString ha="http://www.hancom.co.kr/hwpml/2011/app";
inline const QString opf="http://www.idpf.org/2007/opf/";
bool is(const QDomElement&e,const QString&ns,const QString&name);
QDomElement child(const QDomElement&e,const QString&ns,const QString&name);
QVector<QDomElement> children(const QDomElement&e,const QString&ns,const QString&name={});
QDomElement descendant(const QDomElement&e,const QString&ns,const QString&name);
QDomElement add(QDomNode parent,const QString&ns,const QString&qualifiedName);
QDomElement ensure(QDomNode parent,const QString&ns,const QString&qualifiedName);
void removeChildren(QDomNode parent,const QString&ns,const QString&name);
QByteArray fragment(const QDomElement&e);
QDomElement import(QDomDocument&dest,const QByteArray&bytes);
qint64 integer(const QDomElement&e,const QString&key,qint64 fallback=0);
bool boolean(const QDomElement&e,const QString&key,bool fallback=false);
QString color(const QColor&c);
QColor color(const QString&s,const QColor&fallback=Qt::black);
}
}
