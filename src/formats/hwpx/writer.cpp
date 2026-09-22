// 본 제품은 한컴의 HWP 문서 파일(.hwp) 공개 문서를 참고하여 개발하였습니다.
#include "internal.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QImageReader>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace he { namespace {
using namespace xml;
using hwpx::attributes;
using hwpx::number;
using hwpx::alignmentName;

class Writer {
    const Document& document_;
    QMap<QString,QByteArray> parts_;
    QMap<QString,QByteArray> defaults_;
    std::unique_ptr<hwpx::Registry> registry_;
    QString contentPath_="Contents/content.hpf",headerPath_="Contents/header.xml";
    QDomDocument content_;
    quint64 nextObjectId_=1000000000;
    QSet<QString> paragraphIds_;
    QSet<QString> seenSourceParagraphs_;

    [[noreturn]] void fail(ErrorCode code,const QString& part,const QString& message)const {
        throw Error{code,part,message};
    }
    QDomDocument parse(const QByteArray&bytes,const QString&part)const {
        auto parsed=readXml(bytes,part);
        if(!parsed)throw parsed.error();
        return *parsed;
    }
    QString newFileId(){return QString::number(nextObjectId_++);}
    QDomElement cloneSource(QDomDocument& target,const Source& source,const QString& name) {
        if(!source.xml.isEmpty()) {
            auto e=xml::import(target,source.xml);
            if(e.isNull())fail(ErrorCode::InvalidXml,source.part,"Cannot reconstruct a preserved XML element");
            return e;
        }
        return target.createElementNS(hp,"hp:"+name);
    }
    static void setText(QDomElement e,const QString&text) {
        while(!e.firstChild().isNull())e.removeChild(e.firstChild());
        e.appendChild(e.ownerDocument().createTextNode(text));
    }
    static void textContent(QDomElement parent,const QString&text) {
        auto doc=parent.ownerDocument();
        auto t=add(parent,hp,"hp:t");
        QString chunk;
        auto flush=[&]{if(!chunk.isEmpty()){t.appendChild(doc.createTextNode(chunk));chunk.clear();}};
        for(QChar c:text) {
            if(c=='\t'||c==QChar(0x2028)) {
                flush();
                auto special=add(t,hp,c=='\t'?"hp:tab":"hp:lineBreak");
                if(c=='\t')attributes(special,{{"width","8000"},{"leader","0"},{"type","1"}});
            } else chunk+=c;
        }
        flush();
    }
    static QVector<Unit> columnWidths(const Table&t) {
        QVector<Unit> widths=t.widths;
        if(widths.size()!=t.columns)widths=QVector<Unit>(t.columns,mm(150)/t.columns);
        for(auto&w:widths)w=std::max<Unit>(100,w);
        return widths;
    }
    static QVector<Unit> rowHeights(const Table&t) {
        QVector<Unit> heights(t.rows,1000);
        for(const auto&c:t.cells)if(c.rowSpan==1)heights[c.row]=std::max(heights[c.row],c.minimumHeight);
        for(const auto&c:t.cells) {
            Unit total=0;
            for(int r=c.row;r<c.row+c.rowSpan;++r)total+=heights[r];
            if(total<c.minimumHeight)heights[c.row+c.rowSpan-1]+=c.minimumHeight-total;
        }
        return heights;
    }
    void objectAttributes(QDomElement e,const QString&kind,int z,Wrap wrap) {
        if(!e.hasAttribute("id"))e.setAttribute("id",newFileId());
        e.setAttribute("zOrder",z);
        if(!e.hasAttribute("numberingType"))e.setAttribute("numberingType",kind);
        QString wrapping=wrap==Wrap::Square?"SQUARE":wrap==Wrap::Tight?"TIGHT":
            wrap==Wrap::Behind?"BEHIND_TEXT":wrap==Wrap::Front?"IN_FRONT_OF_TEXT":"TOP_AND_BOTTOM";
        attributes(e,{{"textWrap",wrapping},{"textFlow","BOTH_SIDES"}});
        if(!e.hasAttribute("lock"))e.setAttribute("lock","0");
        if(!e.hasAttribute("dropcapstyle"))e.setAttribute("dropcapstyle","None");
    }
    void sizePosition(QDomElement e,Unit width,Unit height,Anchor anchor=Anchor::Inline,
                      Unit x=0,Unit y=0,Alignment align=Alignment::Left) {
        auto size=ensure(e,hp,"hp:sz");
        attributes(size,{{"width",number(width)},{"widthRelTo","ABSOLUTE"},
            {"height",number(height)},{"heightRelTo","ABSOLUTE"},{"protect","0"}});
        auto pos=ensure(e,hp,"hp:pos");
        attributes(pos,{{"treatAsChar",anchor==Anchor::Inline?"1":"0"},
            {"vertRelTo",anchor==Anchor::Page?"PAGE":"PARA"},
            {"horzRelTo",anchor==Anchor::Page?"PAGE":"PARA"},
            {"vertAlign","TOP"},{"horzAlign",alignmentName(align)},
            {"vertOffset",number(y)},{"horzOffset",number(x)}});
        for(const auto&name:QStringList{"affectLSpacing","allowOverlap","holdAnchorAndSO"})
            if(!pos.hasAttribute(name))pos.setAttribute(name,"0");
        if(!pos.hasAttribute("flowWithText"))pos.setAttribute("flowWithText","1");
        auto margin=ensure(e,hp,"hp:outMargin");
        for(const auto&name:QStringList{"left","right","top","bottom"})
            if(!margin.hasAttribute(name))margin.setAttribute(name,"0");
    }
    void subList(QDomElement parent,const Flow&flow,Unit width=0,Unit height=0,int valign=0) {
        auto sub=ensure(parent,hp,"hp:subList");
        if(!sub.hasAttribute("id"))sub.setAttribute("id","");
        attributes(sub,{{"textDirection","HORIZONTAL"},{"lineWrap","BREAK"},
            {"vertAlign",valign==1?"CENTER":valign==2?"BOTTOM":"TOP"},
            {"textWidth",number(std::max<Unit>(0,width))},{"textHeight",number(std::max<Unit>(0,height))}});
        for(const auto&name:QStringList{"linkListIDRef","linkListNextIDRef","hasTextRef","hasNumRef"})
            if(!sub.hasAttribute(name))sub.setAttribute(name,"0");
        removeChildren(sub,hp,"p");
        writeFlow(sub,flow);
    }
    QDomElement tableObject(QDomDocument& target,const BlockPtr& block,const Table&t) {
        auto e=cloneSource(target,block->source,"tbl");
        auto widths=columnWidths(t),heights=rowHeights(t);
        Unit width=std::accumulate(widths.begin(),widths.end(),Unit(0));
        Unit height=std::accumulate(heights.begin(),heights.end(),Unit(0));
        objectAttributes(e,"TABLE",0,Wrap::TopBottom);
        attributes(e,{{"rowCnt",number(t.rows)},{"colCnt",number(t.columns)},
            {"pageBreak",t.allowRowSplit?"CELL":"TABLE"},{"repeatHeader",t.repeatHeading?"1":"0"},
            {"noAdjust","0"}});
        if(!e.hasAttribute("cellSpacing"))e.setAttribute("cellSpacing","0");
        if(!e.hasAttribute("borderFillIDRef"))e.setAttribute("borderFillIDRef","1");
        sizePosition(e,width,height,Anchor::Inline,0,0,t.alignment);
        auto im=ensure(e,hp,"hp:inMargin");
        for(const auto&n:QStringList{"left","right","top","bottom"})if(!im.hasAttribute(n))im.setAttribute(n,"0");
        removeChildren(e,hp,"tr");
        for(int row=0;row<t.rows;++row) {
            auto tr=add(e,hp,"hp:tr");
            QVector<const Cell*> cells;
            for(const auto&c:t.cells)if(c.row==row)cells.append(&c);
            std::sort(cells.begin(),cells.end(),[](const Cell*a,const Cell*b){return a->col<b->col;});
            for(const Cell*cell:cells) {
                const auto&c=*cell;
                auto ce=cloneSource(target,c.source,"tc");
                tr.appendChild(ce);
                attributes(ce,{{"header",t.repeatHeading&&c.row==0?"1":"0"},
                    {"hasMargin","1"},{"borderFillIDRef",registry_->border(c)}});
                if(!ce.hasAttribute("name"))ce.setAttribute("name","");
                for(const auto&n:QStringList{"protect","editable","dirty"})if(!ce.hasAttribute(n))ce.setAttribute(n,"0");
                Unit cw=0,ch=0;
                for(int col=c.col;col<c.col+c.colSpan;++col)cw+=widths[col];
                for(int r=c.row;r<c.row+c.rowSpan;++r)ch+=heights[r];
                subList(ce,c.content,std::max<Unit>(0,cw-c.left-c.right),std::max<Unit>(0,ch-c.top-c.bottom),c.verticalAlign);
                attributes(ensure(ce,hp,"hp:cellAddr"),{{"colAddr",number(c.col)},{"rowAddr",number(c.row)}});
                attributes(ensure(ce,hp,"hp:cellSpan"),{{"colSpan",number(c.colSpan)},{"rowSpan",number(c.rowSpan)}});
                attributes(ensure(ce,hp,"hp:cellSz"),{{"width",number(cw)},{"height",number(ch)}});
                attributes(ensure(ce,hp,"hp:cellMargin"),{{"left",number(c.left)},{"right",number(c.right)},
                    {"top",number(c.top)},{"bottom",number(c.bottom)}});
            }
        }
        if(!t.caption.isEmpty()) {
            auto caption=ensure(e,hp,"hp:caption");
            if(caption.text()!=t.caption) {
                attributes(caption,{{"side","BOTTOM"},{"fullSz","0"},{"width",number(width)},
                    {"gap","850"},{"lastWidth",number(width)}});
                Flow f;f.blocks.append(he::paragraph(t.caption));subList(caption,f,width);
            }
        } else removeChildren(e,hp,"caption");
        return e;
    }
    static void matrix(QDomElement parent,const QString&name,double a,double b,double c,double d,double e,double f) {
        auto m=ensure(parent,hc,"hc:"+name);
        double values[]={a,b,c,d,e,f};
        for(int i=0;i<6;++i)m.setAttribute("e"+QString::number(i+1),QString::number(values[i],'g',16));
    }
    void scaffold(QDomElement e,const Graphic&g) {
        if(!e.hasAttribute("instid"))e.setAttribute("instid",newFileId());
        if(!e.hasAttribute("groupLevel"))e.setAttribute("groupLevel","0");
        if(!e.hasAttribute("href"))e.setAttribute("href","");
        auto offset=ensure(e,hp,"hp:offset");
        if(!offset.hasAttribute("x"))offset.setAttribute("x","0");
        if(!offset.hasAttribute("y"))offset.setAttribute("y","0");
        auto org=ensure(e,hp,"hp:orgSz");
        Unit ow=std::max<Unit>(1,integer(org,"width",g.width));
        Unit oh=std::max<Unit>(1,integer(org,"height",g.height));
        attributes(org,{{"width",number(ow)},{"height",number(oh)}});
        attributes(ensure(e,hp,"hp:curSz"),{{"width",number(g.width)},{"height",number(g.height)}});
        auto flip=ensure(e,hp,"hp:flip");
        if(!flip.hasAttribute("horizontal"))flip.setAttribute("horizontal","0");
        if(!flip.hasAttribute("vertical"))flip.setAttribute("vertical","0");
        attributes(ensure(e,hp,"hp:rotationInfo"),{{"angle",number(std::llround(g.rotation))},
            {"centerX",number(g.width/2)},{"centerY",number(g.height/2)},{"rotateimage","1"}});
        auto rendering=ensure(e,hp,"hp:renderingInfo");
        matrix(rendering,"transMatrix",1,0,0,0,1,0);
        matrix(rendering,"scaMatrix",double(g.width)/ow,0,0,0,double(g.height)/oh,0);
        double radians=g.rotation*3.14159265358979323846/180;
        double c=std::cos(radians),s=std::sin(radians),cx=g.width/2.0,cy=g.height/2.0;
        matrix(rendering,"rotMatrix",c,-s,cx-c*cx+s*cy,s,c,cy-s*cx-c*cy);
    }
    QDomElement graphicObject(QDomDocument&target,const BlockPtr&block,const Graphic&g) {
        QString name=g.kind==GraphicKind::Image?"pic":g.kind==GraphicKind::Ellipse?"ellipse":
            g.kind==GraphicKind::Line||g.kind==GraphicKind::Arrow?"line":g.kind==GraphicKind::Polygon?"polygon":"rect";
        auto e=cloneSource(target,block->source,name);
        objectAttributes(e,g.kind==GraphicKind::Image?"PICTURE":"NONE",g.z,g.wrap);
        scaffold(e,g);
        auto line=ensure(e,hp,"hp:lineShape");
        const QStringList lineStyles={"NONE","SOLID","DASH","DOT","DOUBLE_SLIM"};
        attributes(line,{{"color",xml::color(g.stroke.color)},{"width",number(units(g.stroke.width))},
            {"style",lineStyles[std::clamp(g.stroke.style,0,4)]},{"endCap","FLAT"},
            {"headStyle","NORMAL"},{"tailStyle",g.kind==GraphicKind::Arrow?"ARROW":"NORMAL"},
            {"headfill","1"},{"tailfill","1"},{"headSz","MEDIUM_MEDIUM"},{"tailSz","MEDIUM_MEDIUM"},
            {"outlineStyle","NORMAL"},{"alpha",number(std::llround((1-std::clamp(g.opacity,0.0,1.0))*255))}});
        Unit ow=integer(child(e,hp,"orgSz"),"width",g.width),oh=integer(child(e,hp,"orgSz"),"height",g.height);
        if(g.kind==GraphicKind::Image) {
            if(!document_.resources.contains(g.resource))fail(ErrorCode::UnsafeSave,block->source.part,"Image resource is missing: "+g.resource);
            auto image=ensure(e,hc,"hc:img");
            attributes(image,{{"binaryItemIDRef",g.resource},{"alpha",number(std::llround((1-std::clamp(g.opacity,0.0,1.0))*255))}});
            for(const auto&n:QStringList{"bright","contrast"})if(!image.hasAttribute(n))image.setAttribute(n,"0");
            if(!image.hasAttribute("effect"))image.setAttribute("effect","REAL_PIC");
            auto rect=ensure(e,hp,"hp:imgRect");
            const QPair<Unit,Unit> ps[]={{0,0},{ow,0},{ow,oh},{0,oh}};
            for(int i=0;i<4;++i)attributes(ensure(rect,hc,"hc:pt"+number(i)),{{"x",number(ps[i].first)},{"y",number(ps[i].second)}});
            attributes(ensure(e,hp,"hp:imgClip"),{{"left",number(std::llround(g.cropLeft*ow))},
                {"top",number(std::llround(g.cropTop*oh))},{"right",number(std::llround((1-g.cropRight)*ow))},
                {"bottom",number(std::llround((1-g.cropBottom)*oh))}});
            auto margin=ensure(e,hp,"hp:inMargin");
            for(const auto&n:QStringList{"left","right","top","bottom"})if(!margin.hasAttribute(n))margin.setAttribute(n,"0");
            attributes(ensure(e,hp,"hp:imgDim"),{{"dimwidth",number(ow)},{"dimheight",number(oh)}});
            if(!e.hasAttribute("reverse"))e.setAttribute("reverse","0");
        } else {
            auto fill=ensure(e,hc,"hc:fillBrush");
            removeChildren(fill,hc,"winBrush");
            if(g.fill.alpha()!=0)attributes(add(fill,hc,"hc:winBrush"),{
                {"faceColor",xml::color(g.fill)},{"hatchColor","#000000"},
                {"alpha",number(255-g.fill.alpha())}});
            auto point=[&](const QString&n,Unit x,Unit y){attributes(ensure(e,hc,"hc:"+n),{{"x",number(x)},{"y",number(y)}});};
            if(name=="rect") {
                e.setAttribute("ratio",g.kind==GraphicKind::RoundedRectangle?20:0);
                point("pt0",0,0);point("pt1",ow,0);point("pt2",ow,oh);point("pt3",0,oh);
            } else if(name=="ellipse") {
                attributes(e,{{"intervalDirty","0"},{"hasArcPr","0"},{"arcType","NORMAL"}});
                point("center",ow/2,oh/2);point("ax1",ow,oh/2);point("ax2",ow/2,0);
                point("start1",0,0);point("end1",0,0);point("start2",0,0);point("end2",0,0);
            } else if(name=="line") {point("startPt",0,0);point("endPt",ow,oh);e.setAttribute("isReverseHV","0");}
            else {
                removeChildren(e,hc,"pt");
                auto points=g.polygon;
                if(points.isEmpty())points={{ow/2,0},{ow,oh},{0,oh}};
                for(const auto&p:points)attributes(add(e,hc,"hc:pt"),{{"x",number(p.first)},{"y",number(p.second)}});
            }
            if(g.kind==GraphicKind::TextBox||!g.content.blocks.isEmpty()) {
                auto draw=ensure(e,hp,"hp:drawText");
                attributes(draw,{{"lastWidth",number(g.width)},{"name",""},{"editable","0"}});
                auto margin=ensure(draw,hp,"hp:textMargin");
                for(const auto&n:QStringList{"left","right","top","bottom"})if(!margin.hasAttribute(n))margin.setAttribute(n,"283");
                subList(draw,g.content,std::max<Unit>(1,g.width-566),std::max<Unit>(1,g.height-566));
            }
        }
        sizePosition(e,g.width,g.height,g.anchor,g.x,g.y);
        setText(ensure(e,hp,"hp:shapeComment"),g.alt);
        return e;
    }
    QDomElement object(QDomDocument&target,const BlockPtr&block) {
        if(!block)fail(ErrorCode::InvalidEdit,{},"Null inline object");
        if(!block->source.dirty&&!block->source.xml.isEmpty())return cloneSource(target,block->source,"");
        if(block->source.opaque)fail(ErrorCode::UnsafeSave,block->source.part,"An opaque object was modified");
        if(auto t=std::get_if<Table>(&block->value))return tableObject(target,block,*t);
        if(auto g=std::get_if<Graphic>(&block->value))return graphicObject(target,block,*g);
        if(auto equation=std::get_if<Equation>(&block->value)) {
            auto e=cloneSource(target,block->source,"equation");
            objectAttributes(e,"EQUATION",0,Wrap::Inline);
            attributes(e,{{"version","Equation Version 60"},{"baseUnit",number(units(equation->size))}});
            if(!e.hasAttribute("baseLine"))e.setAttribute("baseLine","85");
            if(!e.hasAttribute("textColor"))e.setAttribute("textColor","#000000");
            if(!e.hasAttribute("lineMode"))e.setAttribute("lineMode","CHAR");
            if(!e.hasAttribute("font"))e.setAttribute("font","HancomEQN");
            setText(ensure(e,hp,"hp:script"),equation->script);
            sizePosition(e,units(std::max(24.0,equation->size*equation->script.size()*0.5)),units(equation->size*2));
            return e;
        }
        if(auto note=std::get_if<Note>(&block->value)) {
            auto e=cloneSource(target,block->source,note->endnote?"endNote":"footNote");
            if(!e.hasAttribute("id"))e.setAttribute("id",newFileId());
            subList(e,note->content);return e;
        }
        if(std::holds_alternative<Unknown>(block->value)) {
            if(block->source.xml.isEmpty())fail(ErrorCode::UnsafeSave,block->source.part,"An unknown binary object has no lossless HWPX representation");
            return cloneSource(target,block->source,"");
        }
        fail(ErrorCode::UnsupportedFeature,block->source.part,"Paragraph cannot be an inline control");
    }
    static bool global(const QDomElement&e) {
        return e.namespaceURI()==hp&&QStringList{"secPr","colPr","header","footer","pageNum"}.contains(e.localName());
    }
    void preserveGlobals(QDomElement destination,const QDomElement&original) {
        auto doc=destination.ownerDocument();
        for(auto r:children(original,hp,"run")) {
            QDomElement out;
            for(auto c=r.firstChildElement();!c.isNull();c=c.nextSiblingElement()) {
                if(global(c)) {
                    if(out.isNull()){out=add(destination,hp,"hp:run");out.setAttribute("charPrIDRef",r.attribute("charPrIDRef","0"));}
                    out.appendChild(doc.importNode(c,true));
                } else if(is(c,hp,"ctrl")) {
                    QDomElement ctrl;
                    for(auto x=c.firstChildElement();!x.isNull();x=x.nextSiblingElement())if(global(x)) {
                        if(out.isNull()){out=add(destination,hp,"hp:run");out.setAttribute("charPrIDRef",r.attribute("charPrIDRef","0"));}
                        if(ctrl.isNull())ctrl=add(out,hp,"hp:ctrl");
                        ctrl.appendChild(doc.importNode(x,true));
                    }
                }
            }
        }
    }
    QDomElement paragraphElement(QDomDocument&target,const BlockPtr&block) {
        auto p=block->paragraph();
        if(!p)fail(ErrorCode::InvalidEdit,{},"Flow contains a nonparagraph block");
        if(block->source.opaque&&block->source.dirty)
            fail(ErrorCode::UnsafeSave,block->source.part,"An unsupported paragraph control would be overwritten");
        auto original=cloneSource(target,block->source,"p");
        if(!block->source.dirty&&!block->source.xml.isEmpty())return original;
        auto e=original.cloneNode(true).toElement();
        QString id=e.attribute("id");
        if(id.isEmpty()||paragraphIds_.contains(id))id=newFileId();
        paragraphIds_.insert(id);e.setAttribute("id",id);
        e.setAttribute("paraPrIDRef",registry_->paragraph(p->style,original.attribute("paraPrIDRef")));
        e.setAttribute("styleIDRef",registry_->styleId(p->namedStyle));
        e.setAttribute("pageBreak",p->style.breakBefore?"1":"0");
        if(!e.hasAttribute("columnBreak"))e.setAttribute("columnBreak","0");
        if(!e.hasAttribute("merged"))e.setAttribute("merged","0");
        removeChildren(e,hp,"run");removeChildren(e,hp,"linesegarray");
        QString sourceKey=block->source.part+"/"+block->source.key;
        if(!block->source.key.isEmpty()&&!seenSourceParagraphs_.contains(sourceKey)) {
            preserveGlobals(e,original);seenSourceParagraphs_.insert(sourceKey);
        }
        for(const auto&run:p->runs) {
            if(!run.hyperlink.isEmpty())fail(ErrorCode::UnsafeSave,block->source.part,"Hyperlink serialization is not yet enabled");
            auto source=xml::import(target,run.sourceXml);
            auto r=source.isNull()?target.createElementNS(hp,"hp:run"):source.cloneNode(false).toElement();
            r.setAttribute("charPrIDRef",registry_->character(run.style,source.attribute("charPrIDRef",original.firstChildElement().attribute("charPrIDRef"))));
            e.appendChild(r);
            if(run.object) {
                auto o=object(target,run.object);
                if(std::holds_alternative<Note>(run.object->value))add(r,hp,"hp:ctrl").appendChild(o);
                else r.appendChild(o);
            } else if(!run.field.isEmpty()) {
                if(run.field!="page")fail(ErrorCode::UnsafeSave,block->source.part,"Unknown editable field: "+run.field);
                auto n=add(add(r,hp,"hp:ctrl"),hp,"hp:autoNum");
                attributes(n,{{"numType","PAGE"},{"num","1"}});
                attributes(add(n,hp,"hp:autoNumFormat"),{{"type","DIGIT"},{"userChar",""},
                    {"prefixChar",""},{"suffixChar",""},{"supscript","0"}});
            } else textContent(r,run.text);
        }
        if(children(e,hp,"run").isEmpty())textContent(add(e,hp,"hp:run"),{});
        return e;
    }
    void writeFlow(QDomElement parent,const Flow&flow) {
        auto target=parent.ownerDocument();
        if(flow.blocks.isEmpty())parent.appendChild(paragraphElement(target,he::paragraph()));
        else for(const auto&block:flow.blocks) {
            if(block->paragraph())parent.appendChild(paragraphElement(target,block));
            else {
                auto wrap=std::make_shared<Block>();Paragraph p;p.runs.append(Run{{},{},block,{},{},{}});wrap->value=p;
                parent.appendChild(paragraphElement(target,wrap));
            }
        }
    }
    QDomElement firstRun(QDomElement section) {
        auto p=child(section,hp,"p");if(p.isNull())p=add(section,hp,"hp:p");
        auto r=child(p,hp,"run");
        if(r.isNull()){r=add(p,hp,"hp:run");r.setAttribute("charPrIDRef","0");}
        return r;
    }
    QDomElement controlInSection(QDomElement section,const QString&name) {
        for(auto p:children(section,hp,"p"))for(auto r:children(p,hp,"run")) {
            auto direct=child(r,hp,name);if(!direct.isNull())return direct;
            for(auto c:children(r,hp,"ctrl")){auto e=child(c,hp,name);if(!e.isNull())return e;}
        }
        return {};
    }
    void sectionProperties(QDomElement root,const Section&s) {
        auto sec=controlInSection(root,"secPr");
        if(sec.isNull()) {
            auto templateSection=parse(defaults_.value("Contents/section0.xml"),"embedded section");
            auto templatePr=descendant(templateSection.documentElement(),hp,"secPr");
            sec=root.ownerDocument().importNode(templatePr,true).toElement();
            auto run=firstRun(root);run.insertBefore(sec,run.firstChild());
        }
        auto p=ensure(sec,hp,"hp:pagePr");
        attributes(p,{{"width",number(s.page.width)},{"height",number(s.page.height)}});
        auto m=ensure(p,hp,"hp:margin");
        attributes(m,{{"left",number(s.page.left)},{"right",number(s.page.right)},{"top",number(s.page.top)},
            {"bottom",number(s.page.bottom)},{"gutter",number(s.page.binding)},
            {"header",number(s.page.headerDistance)},{"footer",number(s.page.footerDistance)}});
        auto start=ensure(sec,hp,"hp:startNum");start.setAttribute("page",s.page.numberStart);
        auto columns=controlInSection(root,"colPr");
        if(columns.isNull())columns=add(add(firstRun(root),hp,"hp:ctrl"),hp,"hp:colPr");
        attributes(columns,{{"id",""},{"type","NEWSPAPER"},{"layout","LEFT"},
            {"colCount",number(s.page.columns)},{"sameSz","1"},{"sameGap",number(s.page.columnGap)}});
        for(const auto&kind:QStringList{"header","footer"}) {
            const auto&flow=kind=="header"?s.header:s.footer;
            auto e=controlInSection(root,kind);
            if(flow.blocks.isEmpty()){if(!e.isNull()&&e.attribute("applyPageType","BOTH")=="BOTH")e.parentNode().removeChild(e);continue;}
            if(!e.isNull()&&e.attribute("applyPageType","BOTH")!="BOTH")
                fail(ErrorCode::UnsafeSave,s.source.part,"Odd/even header variants cannot be collapsed into one region");
            if(e.isNull())e=add(add(firstRun(root),hp,"hp:ctrl"),hp,"hp:"+kind);
            if(!e.hasAttribute("id"))e.setAttribute("id",newFileId());
            e.setAttribute("applyPageType","BOTH");
            subList(e,flow,s.page.width-s.page.left-s.page.right);
        }
        auto pageNumber=controlInSection(root,"pageNum");
        if(s.page.numberPosition==0) {if(!pageNumber.isNull())pageNumber.parentNode().removeChild(pageNumber);}
        else {
            if(pageNumber.isNull())pageNumber=add(add(firstRun(root),hp,"hp:ctrl"),hp,"hp:pageNum");
            const QStringList positions={"TOP_LEFT","TOP_CENTER","TOP_RIGHT","BOTTOM_LEFT","BOTTOM_CENTER","BOTTOM_RIGHT"};
            attributes(pageNumber,{{"pos",positions[std::clamp(s.page.numberPosition-1,0,5)]},
                {"formatType","DIGIT"},{"sideChar",""}});
        }
    }
    QString resolve(const QString&href,const QString&base)const {
        if(parts_.contains(href))return href;
        return QDir::cleanPath(QFileInfo(base).path()+"/"+href);
    }
    void initialize() {
        auto blank=hwpx::blankPackage();if(!blank)throw blank.error();defaults_=std::move(*blank);
        bool imported=document_.origin&&document_.origin->format==Origin::Format::Hwpx;
        parts_=imported?document_.origin->parts:defaults_;
        for(auto i=parts_.cbegin();i!=parts_.cend();++i)if(i.key().endsWith(".hpf")){contentPath_=i.key();break;}
        if(!parts_.contains(contentPath_))fail(ErrorCode::InvalidHeader,contentPath_,"Content manifest is missing");
        content_=parse(parts_.value(contentPath_),contentPath_);
        auto manifest=child(content_.documentElement(),opf,"manifest");
        for(auto item:children(manifest,opf,"item"))if(item.attribute("id")=="header")headerPath_=resolve(item.attribute("href"),contentPath_);
        auto header=parse(parts_.value(headerPath_),headerPath_);
        registry_=std::make_unique<hwpx::Registry>(header);
        registry_->header.documentElement().setAttribute("secCnt",document_.sections.size());
        registry_->styles(document_.styles);
        if(!imported) {
            auto metadata=child(content_.documentElement(),opf,"metadata");
            while(!metadata.firstChild().isNull())metadata.removeChild(metadata.firstChild());
        }
        auto metadata=ensure(content_.documentElement(),opf,"opf:metadata");
        const QString dc="http://purl.org/dc/elements/1.1/";
        for(auto i=document_.metadata.cbegin();i!=document_.metadata.cend();++i) {
            if(QStringList{"title","creator","subject","description","language"}.contains(i.key()))
                setText(ensure(metadata,dc,"dc:"+i.key()),i.value());
        }
        auto generator=add(metadata,opf,"opf:meta");
        attributes(generator,{{"name","generator"},{"content","HwpEdit"}});
    }
public:
    explicit Writer(const Document&d):document_(d){}
    QByteArray run() {
        auto valid=document_.validate();if(!valid)throw valid.error();
        if(document_.origin&&document_.origin->format==Origin::Format::Hwpx&&document_.revision==0)
            return document_.origin->original;
        initialize();
        auto manifest=ensure(content_.documentElement(),opf,"opf:manifest");
        auto spine=ensure(content_.documentElement(),opf,"opf:spine");
        removeChildren(spine,opf,"itemref");
        QSet<QString> sectionPaths;
        for(int i=0;i<document_.sections.size();++i) {
            const auto&s=document_.sections[i];
            QString path=s.source.part.isEmpty()?QString("Contents/section%1.xml").arg(i):s.source.part;
            if(sectionPaths.contains(path))path=QString("Contents/section-generated-%1.xml").arg(i);
            sectionPaths.insert(path);
            QDomDocument target;
            if(!s.source.xml.isEmpty())target=parse(s.source.xml,s.source.part);
            else target=parse(defaults_.value("Contents/section0.xml"),"embedded section");
            auto root=target.documentElement();removeChildren(root,hp,"p");
            writeFlow(root,s.body);sectionProperties(root,s);
            parts_.insert(path,target.toByteArray(-1));
            QString manifestId;
            for(auto item:children(manifest,opf,"item"))if(resolve(item.attribute("href"),contentPath_)==path){manifestId=item.attribute("id");break;}
            if(manifestId.isEmpty()) {
                manifestId="section_generated_"+QString::number(i);
                auto item=add(manifest,opf,"opf:item");
                attributes(item,{{"id",manifestId},{"href",QDir(QFileInfo(contentPath_).path()).relativeFilePath(path)},
                    {"media-type","application/xml"}});
            }
            auto ref=add(spine,opf,"opf:itemref");ref.setAttribute("idref",manifestId);ref.setAttribute("linear","yes");
        }
        for(auto i=document_.resources.cbegin();i!=document_.resources.cend();++i) {
            const auto&r=i.value();QString path=r.originalPath;
            if(path.isEmpty())path="BinData/"+i.key()+".bin";
            if(path==contentPath_||path==headerPath_||sectionPaths.contains(path))
                fail(ErrorCode::UnsafeSave,path,"Image resource collides with a document part");
            parts_.insert(path,r.bytes);
            QDomElement item;
            for(auto old:children(manifest,opf,"item"))if(old.attribute("id")==i.key()){item=old;break;}
            if(item.isNull())item=add(manifest,opf,"opf:item");
            attributes(item,{{"id",i.key()},{"href",path},{"media-type",r.mime},{"isEmbeded","1"}});
        }
        parts_.insert(headerPath_,registry_->header.toByteArray(-1));
        parts_.insert(contentPath_,content_.toByteArray(-1));
        parts_.insert("mimetype","application/hwp+zip");
        QString preview=document_.plainText().left(2048);
        QByteArray text; text.reserve(preview.size()*2);
        for(QChar c:preview){ushort u=c.unicode();text.append(char(u&0xff));text.append(char(u>>8));}
        parts_.insert("Preview/PrvText.txt",text);
        auto bytes=writeZip(parts_);if(!bytes)throw bytes.error();
        // Never replace the user's file with a package the same core cannot reopen.
        auto reopened=readHwpx(*bytes);if(!reopened)throw reopened.error();
        if(reopened->plainText()!=document_.plainText())
            fail(ErrorCode::UnsafeSave,{},"Text preservation check failed after serialization");
        return *bytes;
    }
};
}
Result<QByteArray> writeHwpx(const Document&document) {
    try{return Writer(document).run();}
    catch(const Error&e){return std::unexpected(e);}
    catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::ResourceLimit,{},"Writer allocation budget exhausted"});}
    catch(const std::exception&e){return std::unexpected(Error{ErrorCode::Io,{},QString::fromUtf8(e.what())});}
}
} // namespace he
