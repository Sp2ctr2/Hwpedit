// 본 제품은 한컴의 HWP 문서 파일(.hwp) 공개 문서를 참고하여 개발하였습니다.
#include "internal.hpp"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QResource>
#include <algorithm>
#include <cmath>
#include <mutex>

static void initializeHwpEditResources() { Q_INIT_RESOURCE(hwpedit_assets); }

namespace he::hwpx {
using namespace xml;
QString number(qint64 n) { return QString::number(n); }
void attributes(QDomElement e, std::initializer_list<std::pair<QString,QString>> values) {
    for (const auto& [key,value] : values) e.setAttribute(key,value);
}
QString alignmentName(Alignment value) {
    switch(value) {
    case Alignment::Center:return "CENTER";
    case Alignment::Right:return "RIGHT";
    case Alignment::Justify:return "JUSTIFY";
    case Alignment::Distributed:return "DISTRIBUTE_SPACE";
    default:return "LEFT";
    }
}
Result<QMap<QString,QByteArray>> blankPackage() {
    static std::once_flag once;
    std::call_once(once,initializeHwpEditResources);
    QFile file(":/hwpedit/blank.hwpx");
    if(!file.open(QIODevice::ReadOnly))
        return std::unexpected(Error{ErrorCode::Io,file.fileName(),"Embedded blank package is missing"});
    auto bytes=file.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=
       "c91fb77a9933a3e5645a25778e2d2b99e98137ed99572a5d1dd29ef0c0fd4c98")
        return std::unexpected(Error{ErrorCode::InvalidContainer,file.fileName(),"Embedded template integrity failure"});
    return readZip(bytes);
}
Registry::Registry(QDomDocument document):header(std::move(document)) {
    refs_=child(header.documentElement(),hh,"refList");
    if(refs_.isNull()) throw Error{ErrorCode::InvalidHeader,"header.xml","Missing reference list"};
    for(auto e:children(group("styles"),hh,"style")) names_.insert(e.attribute("name"),e.attribute("id"));
}
QDomElement Registry::group(const QString& name) { return ensure(refs_,hh,"hh:"+name); }
QDomElement Registry::find(const QString& g,const QString& tag,const QString& id) {
    for(auto e:children(group(g),hh,tag)) if(e.attribute("id")==id) return e;
    return {};
}
QString Registry::append(const QString& g,const QString& tag,QDomElement e) {
    qint64 next=0;
    auto parent=group(g);
    for(auto old:children(parent,hh,tag)) next=std::max(next,integer(old,"id")+1);
    const auto id=number(next);
    e.setAttribute("id",id);
    parent.appendChild(e);
    parent.setAttribute("itemCnt",children(parent,hh,tag).size());
    return id;
}
QString Registry::font(const QString& language,const QString& family,const QString& currentId) {
    auto faces=group("fontfaces");
    QDomElement face;
    for(auto e:children(faces,hh,"fontface")) if(e.attribute("lang")==language.toUpper()) {face=e;break;}
    if(face.isNull()) {
        face=add(faces,hh,"hh:fontface");
        face.setAttribute("lang",language.toUpper());
        faces.setAttribute("itemCnt",children(faces,hh,"fontface").size());
    }
    auto existing=children(face,hh,"font");
    for(auto e:existing) if(e.attribute("id")==currentId&&e.attribute("face")==family) return currentId;
    qint64 next=0;
    for(auto e:existing) {
        if(e.attribute("face")==family) return e.attribute("id");
        next=std::max(next,integer(e,"id")+1);
    }
    auto f=add(face,hh,"hh:font");
    attributes(f,{{"id",number(next)},{"face",family},{"type","TTF"},{"isEmbedded","0"}});
    auto info=add(f,hh,"hh:typeInfo");
    attributes(info,{{"familyType","FCAT_UNKNOWN"},{"serifStyle","0"},{"weight","5"},
        {"proportion","0"},{"contrast","0"},{"strokeVariation","0"},{"armStyle","0"},
        {"letterform","0"},{"midline","0"},{"xHeight","0"}});
    face.setAttribute("fontCnt",existing.size()+1);
    return number(next);
}
static QByteArray characterKey(const CharStyle&s,const QString&base) {
    QByteArray bytes;QDataStream ds(&bytes,QIODevice::WriteOnly);ds.setVersion(QDataStream::Qt_6_0);
    ds<<base<<s.family<<s.languageFonts<<s.size<<s.width<<s.spacing<<s.relativeSize<<s.baseline
      <<s.bold<<s.italic<<s.underline<<s.strike<<s.outline<<s.shadow<<s.script
      <<s.color<<s.background<<s.underlineColor<<s.shadowColor;
    return bytes;
}
static void toggle(QDomElement parent,const QString& name,bool value) {
    if(value)ensure(parent,hh,"hh:"+name);else removeChildren(parent,hh,name);
}
QString Registry::character(const CharStyle&s,const QString&baseId) {
    // An unchanged format reference retains all source-only language metrics and effects.
    if(!s.sourceId.isEmpty()&&!find("charProperties","charPr",s.sourceId).isNull()) return s.sourceId;
    auto key=characterKey(s,baseId);
    if(characters_.contains(key))return characters_.value(key);
    auto original=find("charProperties","charPr",baseId.isEmpty()?"0":baseId);
    auto e=original.isNull()?header.createElementNS(hh,"hh:charPr"):original.cloneNode(true).toElement();
    attributes(e,{{"height",number(units(s.size))},{"textColor",xml::color(s.color)},
        {"shadeColor",xml::color(s.background)}});
    if(!e.hasAttribute("borderFillIDRef"))e.setAttribute("borderFillIDRef","1");
    if(!e.hasAttribute("useFontSpace"))e.setAttribute("useFontSpace","0");
    if(!e.hasAttribute("useKerning"))e.setAttribute("useKerning","0");
    if(!e.hasAttribute("symMark"))e.setAttribute("symMark","NONE");
    const QStringList languages={"hangul","latin","hanja","japanese","other","symbol","user"};
    auto f=ensure(e,hh,"hh:fontRef");
    for(int i=0;i<languages.size();++i) {
        const auto&lang=languages[i];
        QString family=i==0?s.family:(i<s.languageFonts.size()?s.languageFonts[i]:s.family);
        f.setAttribute(lang,font(lang,family,f.attribute(lang)));
    }
    auto metric=[&](const QString&name,double value,double fallback) {
        auto m=child(e,hh,name);
        if(!m.isNull()&&double(integer(m,"hangul",qint64(fallback)))==value)return;
        m=ensure(e,hh,"hh:"+name);
        for(const auto&lang:languages)m.setAttribute(lang,number(std::llround(value)));
    };
    metric("ratio",s.width,100);metric("spacing",s.spacing,0);
    metric("relSz",s.relativeSize,100);metric("offset",s.baseline,0);
    toggle(e,"bold",s.bold);toggle(e,"italic",s.italic);
    toggle(e,"supscript",s.script>0);toggle(e,"subscript",s.script<0);
    auto u=ensure(e,hh,"hh:underline");
    bool hadUnderline=u.attribute("type","NONE")!="NONE";
    if(hadUnderline!=s.underline||!u.hasAttribute("type"))u.setAttribute("type",s.underline?"BOTTOM":"NONE");
    if(!u.hasAttribute("shape"))u.setAttribute("shape","SOLID");
    u.setAttribute("color",xml::color(s.underlineColor));
    auto strike=ensure(e,hh,"hh:strikeout");
    if((strike.attribute("shape","NONE")!="NONE")!=s.strike||!strike.hasAttribute("shape"))
        strike.setAttribute("shape",s.strike?"SOLID":"NONE");
    if(!strike.hasAttribute("color"))strike.setAttribute("color",xml::color(s.color));
    auto outline=ensure(e,hh,"hh:outline");
    if((outline.attribute("type","NONE")!="NONE")!=s.outline||!outline.hasAttribute("type"))
        outline.setAttribute("type",s.outline?"SOLID":"NONE");
    auto shadow=ensure(e,hh,"hh:shadow");
    if((shadow.attribute("type","NONE")!="NONE")!=s.shadow||!shadow.hasAttribute("type"))
        shadow.setAttribute("type",s.shadow?"DROP":"NONE");
    attributes(shadow,{{"color",xml::color(s.shadowColor)}});
    if(!shadow.hasAttribute("offsetX"))shadow.setAttribute("offsetX","10");
    if(!shadow.hasAttribute("offsetY"))shadow.setAttribute("offsetY","10");
    auto id=append("charProperties","charPr",e);characters_.insert(key,id);return id;
}
QString Registry::tabs(const QVector<Unit>&positions) {
    auto parent=group("tabProperties");
    for(auto e:children(parent,hh,"tabPr")) {
        QVector<Unit> original;
        for(auto t:children(e,hh,"tabItem"))original.append(integer(t,"pos"));
        if(original==positions)return e.attribute("id");
    }
    auto e=header.createElementNS(hh,"hh:tabPr");
    attributes(e,{{"autoTabLeft","0"},{"autoTabRight","0"}});
    for(Unit p:positions) {
        auto t=add(e,hh,"hh:tabItem");
        attributes(t,{{"pos",number(p)},{"type","LEFT"},{"leader","NONE"}});
    }
    return append("tabProperties","tabPr",e);
}
QString Registry::numbering(const ParaStyle&s) {
    if(s.listFormat.isEmpty())return "0";
    bool bullet=!s.listFormat.contains('1')&&!s.listFormat.contains(QChar(0xac00));
    QString groupName=bullet?"bullets":"numberings",tag=bullet?"bullet":"numbering";
    auto parent=group(groupName);
    for(auto e:children(parent,hh,tag))
        if(e.attribute("hwpedit-format")==s.listFormat&&integer(e,"start",1)==s.listStart)return e.attribute("id");
    // Do not add private attributes to the standardized document. Compare definition text instead.
    auto e=header.createElementNS(hh,"hh:"+tag);
    if(bullet) {
        attributes(e,{{"char",s.listFormat.left(1)},{"checkedChar",s.listFormat.left(1)},{"useImage","0"}});
        auto h=add(e,hh,"hh:paraHead");
        attributes(h,{{"align","LEFT"},{"useInstWidth","1"},{"autoIndent","1"},
            {"widthAdjust","0"},{"textOffsetType","PERCENT"},{"textOffset","50"},
            {"numFormat","DIGIT"},{"charPrIDRef","0"},{"checkable","0"}});
    } else {
        e.setAttribute("start",s.listStart);
        for(int level=1;level<=7;++level) {
            auto h=add(e,hh,"hh:paraHead");
            attributes(h,{{"start",number(s.listStart)},{"level",number(level)},
                {"align","LEFT"},{"useInstWidth","1"},{"autoIndent","1"},
                {"widthAdjust","0"},{"textOffsetType","PERCENT"},{"textOffset","50"},
                {"numFormat",s.listFormat.contains(QChar(0xac00))?"HANGUL_SYLLABLE":"DIGIT"},
                {"charPrIDRef","0"},{"checkable","0"}});
            QString format;
            if(s.listFormat.contains("1.1"))for(int j=1;j<=level;++j){if(j>1)format+='.';format+='^'+number(j);}
            else format='^'+number(level)+'.';
            h.appendChild(header.createTextNode(format));
        }
    }
    return append(groupName,tag,e);
}
static QDomElement effectiveElement(const QDomElement&e,const QString&name) {
    auto direct=child(e,hh,name);if(!direct.isNull())return direct;
    return descendant(child(child(e,hp,"switch"),hp,"default"),hh,name);
}
QString Registry::paragraph(const ParaStyle&s,const QString&baseId) {
    if(!s.sourceId.isEmpty()&&!find("paraProperties","paraPr",s.sourceId).isNull())return s.sourceId;
    QByteArray key;QDataStream ds(&key,QIODevice::WriteOnly);ds.setVersion(QDataStream::Qt_6_0);
    ds<<baseId<<int(s.alignment)<<s.left<<s.right<<s.indent<<s.before<<s.after<<int(s.spacingKind)
      <<s.lineSpacing<<s.keepNext<<s.keepTogether<<s.widowOrphan<<s.breakBefore<<s.tabs
      <<s.listFormat<<s.listLevel<<s.listStart;
    if(paragraphs_.contains(key))return paragraphs_.value(key);
    auto original=find("paraProperties","paraPr",baseId.isEmpty()?"0":baseId);
    auto e=original.isNull()?header.createElementNS(hh,"hh:paraPr"):original.cloneNode(true).toElement();
    e.setAttribute("tabPrIDRef",tabs(s.tabs));
    for(const auto&name:QStringList{"condense","fontLineHeight","suppressLineNumbers","checked"})
        if(!e.hasAttribute(name))e.setAttribute(name,"0");
    if(!e.hasAttribute("snapToGrid"))e.setAttribute("snapToGrid","0");
    if(!e.hasAttribute("textDir"))e.setAttribute("textDir","LTR");
    auto al=ensure(e,hh,"hh:align");
    attributes(al,{{"horizontal",alignmentName(s.alignment)},{"vertical","BASELINE"}});
    auto br=ensure(e,hh,"hh:breakSetting");
    attributes(br,{{"keepWithNext",s.keepNext?"1":"0"},{"keepLines",s.keepTogether?"1":"0"},
        {"widowOrphan",s.widowOrphan?"1":"0"},{"pageBreakBefore",s.breakBefore?"1":"0"}});
    if(!br.hasAttribute("breakLatinWord"))br.setAttribute("breakLatinWord","KEEP_WORD");
    if(!br.hasAttribute("breakNonLatinWord"))br.setAttribute("breakNonLatinWord","BREAK_WORD");
    if(!br.hasAttribute("lineWrap"))br.setAttribute("lineWrap","BREAK");
    auto margins=e.elementsByTagNameNS(hh,"margin");
    if(margins.isEmpty()) {add(e,hh,"hh:margin");margins=e.elementsByTagNameNS(hh,"margin");}
    const std::pair<QString,Unit> values[]={{"intent",s.indent},{"left",s.left},{"right",s.right},{"prev",s.before},{"next",s.after}};
    auto effectiveMargin=effectiveElement(original,"margin");
    for(const auto&[name,value]:values) {
        if(!effectiveMargin.isNull()&&integer(child(effectiveMargin,hc,name),"value")==value)continue;
        for(int i=0;i<margins.size();++i) {
            auto m=ensure(margins.at(i),hc,"hc:"+name);
            attributes(m,{{"value",number(value)},{"unit","HWPUNIT"}});
        }
    }
    auto spacing=e.elementsByTagNameNS(hh,"lineSpacing");
    if(spacing.isEmpty()){add(e,hh,"hh:lineSpacing");spacing=e.elementsByTagNameNS(hh,"lineSpacing");}
    for(int i=0;i<spacing.size();++i)attributes(spacing.at(i).toElement(),{
        {"type",s.spacingKind==SpacingKind::Percent?"PERCENT":s.spacingKind==SpacingKind::Fixed?"FIXED":"AT_LEAST"},
        {"value",number(s.spacingKind==SpacingKind::Percent?std::llround(s.lineSpacing):units(s.lineSpacing))},
        {"unit","HWPUNIT"}});
    auto heading=ensure(e,hh,"hh:heading");
    attributes(heading,{{"type",s.listFormat.isEmpty()?"NONE":s.listFormat.contains('1')||s.listFormat.contains(QChar(0xac00))?"NUMBER":"BULLET"},
        {"idRef",numbering(s)},{"level",number(std::clamp(s.listLevel,0,6))}});
    auto id=append("paraProperties","paraPr",e);paragraphs_.insert(key,id);return id;
}
QString Registry::border(const Cell&c) {
    auto source=readXml(c.source.xml,"cell source");
    if(!c.source.dirty&&source) {
        QString id=source->documentElement().attribute("borderFillIDRef");
        if(!find("borderFills","borderFill",id).isNull())return id;
    }
    QByteArray key;QDataStream ds(&key,QIODevice::WriteOnly);ds<<c.background;
    for(const auto&b:c.borders)ds<<b.color<<b.width<<b.style;
    if(borders_.contains(key))return borders_.value(key);
    auto original=find("borderFills","borderFill",source?source->documentElement().attribute("borderFillIDRef"):"1");
    auto e=original.isNull()?header.createElementNS(hh,"hh:borderFill"):original.cloneNode(true).toElement();
    attributes(e,{{"threeD","0"},{"shadow","0"},{"centerLine","NONE"},{"breakCellSeparateLine","0"}});
    const QStringList edges={"leftBorder","topBorder","rightBorder","bottomBorder"};
    const QStringList types={"NONE","SOLID","DASH","DOT","DOUBLE_SLIM"};
    for(int i=0;i<4;++i) {
        auto b=ensure(e,hh,"hh:"+edges[i]);
        attributes(b,{{"type",types[std::clamp(c.borders[i].style,0,4)]},
            {"width",QString::number(c.borders[i].width*25.4/72.0,'f',2)+" mm"},
            {"color",xml::color(c.borders[i].color)}});
    }
    removeChildren(e,hc,"fillBrush");
    if(c.background.alpha()!=0) {
        auto brush=add(e,hc,"hc:fillBrush");auto win=add(brush,hc,"hc:winBrush");
        attributes(win,{{"faceColor",xml::color(c.background)},{"hatchColor","#000000"},{"alpha","0"}});
    }
    auto id=append("borderFills","borderFill",e);borders_.insert(key,id);return id;
}
void Registry::styles(const QMap<QString,Style>&styles) {
    for(auto it=styles.cbegin();it!=styles.cend();++it) {
        auto e=find("styles","style",names_.value(it.key()));
        if(e.isNull()) {
            e=header.createElementNS(hh,"hh:style");
            attributes(e,{{"type","PARA"},{"name",it.key()},{"engName",it.key()},
                {"nextStyleIDRef","0"},{"langID","1042"},{"lockForm","0"}});
            names_.insert(it.key(),append("styles","style",e));
        }
        e.setAttribute("paraPrIDRef",paragraph(it.value().paragraph,e.attribute("paraPrIDRef")));
        e.setAttribute("charPrIDRef",character(it.value().character,e.attribute("charPrIDRef")));
    }
}
QString Registry::styleId(const QString&name)const{return names_.value(name,"0");}
} // namespace he::hwpx
