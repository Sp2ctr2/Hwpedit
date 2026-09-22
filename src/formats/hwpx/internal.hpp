#pragma once
#include <hwpedit/formats.hpp>
#include <QSet>
namespace he::hwpx {
Result<QMap<QString,QByteArray>> blankPackage();
void attributes(QDomElement e, std::initializer_list<std::pair<QString,QString>> values);
QString number(qint64 n);
QString alignmentName(Alignment alignment);
class Registry {
    QDomElement refs_;
    QHash<QByteArray,QString> characters_, paragraphs_, borders_;
    QMap<QString,QString> names_;
    QDomElement group(const QString& name);
    QDomElement find(const QString& groupName,const QString& tag,const QString& id);
    QString append(const QString& groupName,const QString& tag,QDomElement element);
    QString font(const QString& language,const QString& family,const QString& currentId);
    QString tabs(const QVector<Unit>& positions);
    QString numbering(const ParaStyle& style);
public:
    QDomDocument header;
    Registry(QDomDocument document);
    QString character(const CharStyle& style,const QString& baseId={});
    QString paragraph(const ParaStyle& style,const QString& baseId={});
    QString border(const Cell& cell);
    void styles(const QMap<QString,Style>& styles);
    QString styleId(const QString& name)const;
};
}
