#include <hwpedit/equation.hpp>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace he { namespace {
struct Style {QString family;double size=12;bool bold=false,italic=true;};
QImage& metricsDevice() {
    static thread_local QImage image=[] {QImage i(1,1,QImage::Format_ARGB32);i.setDotsPerMeterX(2835);i.setDotsPerMeterY(2835);return i;}();
    return image;
}
QFont font(const Style&s,bool italic) {QFont f(s.family);f.setPointSizeF(s.size);f.setBold(s.bold);f.setItalic(italic);return f;}
MathBox textBox(const QString&text,const Style&s,bool italic) {
    auto f=font(s,italic);QFontMetricsF fm(f,&metricsDevice());
    MathBox b;b.width=fm.horizontalAdvance(text);b.height=fm.height();b.baseline=fm.ascent();
    b.primitives.append(MathGlyph{text,f,{0,b.baseline}});return b;
}
void place(MathBox&out,const MathBox&child,double x,double y) {
    for(auto primitive:child.primitives) {
        if(auto g=std::get_if<MathGlyph>(&primitive))g->baseline+=QPointF(x,y);
        else {auto&p=std::get<MathPath>(primitive);p.path.translate(x,y);}
        out.primitives.append(std::move(primitive));
    }
}
void line(MathBox&b,double x1,double y1,double x2,double y2,double width) {
    QPainterPath p;p.moveTo(x1,y1);p.lineTo(x2,y2);b.primitives.append(MathPath{p,width,false});
}
void delimiter(MathBox&out,const QString&character,double x,double y,double width,double height,double stroke) {
    if(character.isEmpty()||character==".")return;
    QPainterPath p;
    if(character=="("){p.moveTo(x+width,y);p.cubicTo(x-width*.1,y+height*.2,x-width*.1,y+height*.8,x+width,y+height);}
    else if(character==")"){p.moveTo(x,y);p.cubicTo(x+width*1.1,y+height*.2,x+width*1.1,y+height*.8,x,y+height);}
    else if(character=="["){p.moveTo(x+width,y);p.lineTo(x,y);p.lineTo(x,y+height);p.lineTo(x+width,y+height);}
    else if(character=="]"){p.moveTo(x,y);p.lineTo(x+width,y);p.lineTo(x+width,y+height);p.lineTo(x,y+height);}
    else if(character=="{"||character=="}") {
        bool left=character=="{";auto px=[&](double v){return x+(left?v:1-v)*width;};
        p.moveTo(px(1),y);p.cubicTo(px(.2),y,px(.6),y+height*.42,px(0),y+height*.5);
        p.cubicTo(px(.6),y+height*.58,px(.2),y+height,px(1),y+height);
    } else if(character=="|"||character==QString::fromUtf8("‖")) {
        p.moveTo(x+width*.4,y);p.lineTo(x+width*.4,y+height);
        if(character!= "|"){p.moveTo(x+width*.85,y);p.lineTo(x+width*.85,y+height);}
    } else if(character==QString::fromUtf8("⟨")||character=="<") {p.moveTo(x+width,y);p.lineTo(x,y+height*.5);p.lineTo(x+width,y+height);}
    else if(character==QString::fromUtf8("⟩")||character==">") {p.moveTo(x,y);p.lineTo(x+width,y+height*.5);p.lineTo(x,y+height);}
    else {
        Style s{"DejaVu Serif",std::max(6.0,height*.8),false,false};auto t=textBox(character,s,false);
        place(out,t,x,y+(height-t.height)*.5);return;
    }
    out.primitives.append(MathPath{p,stroke,false});
}
MathBox layout(const MathNodePtr&n,const Style&s,int depth) {
    if(!n)return {};
    if(depth>80)throw Error{ErrorCode::ResourceLimit,"equation","Math layout depth exceeded"};
    const double em=s.size,stroke=std::max(.4,em*.045);
    if(n->kind==MathKind::Symbol||n->kind==MathKind::Text) {
        Style st=s;if(n->large)st.size*=1.45;
        bool variable=n->kind==MathKind::Symbol&&!n->large&&n->text.size()==1&&n->text[0].isLetter();
        if(n->text=="`"){MathBox space;space.width=em*.22;return space;}
        return textBox(n->text,st,s.italic&&variable);
    }
    if(n->kind==MathKind::Style) {
        auto style=s;
        if(n->text=="bold"||n->text=="mathbf")style.bold=true;
        else if(n->text=="it"||n->text=="mathit")style.italic=true;
        else style.italic=false;
        return layout(n->children.value(0),style,depth+1);
    }
    if(n->kind==MathKind::Row) {
        QVector<MathBox> children;QVector<double> gaps;
        double ascent=0,descent=0,width=0;
        const QString operators=QString::fromUtf8("+-=±∓×÷<>≤≥≈≠∈∉∪∩⇒⇔");
        for(const auto&child:n->children) {
            auto b=layout(child,s,depth+1);double gap=0;
            if(child&&child->kind==MathKind::Symbol&&child->text.size()==1&&operators.contains(child->text))gap=em*.18;
            children.append(b);gaps.append(gap);width+=b.width+2*gap;
            ascent=std::max(ascent,b.baseline);descent=std::max(descent,b.height-b.baseline);
        }
        MathBox out;out.width=width;out.height=ascent+descent;out.baseline=ascent;double x=0;
        for(int i=0;i<children.size();++i){x+=gaps[i];place(out,children[i],x,ascent-children[i].baseline);x+=children[i].width+gaps[i];}
        return out;
    }
    if(n->kind==MathKind::Fraction) {
        auto small=s;small.size*=.86;auto numerator=layout(n->children.value(0),small,depth+1);
        auto denominator=layout(n->children.value(1),small,depth+1);double gap=em*.18;
        MathBox out;out.width=std::max(numerator.width,denominator.width)+em*.5;
        double ruleY=numerator.height+gap,denY=ruleY+stroke+gap;
        out.height=denY+denominator.height;out.baseline=ruleY+em*.27;
        place(out,numerator,(out.width-numerator.width)/2,0);place(out,denominator,(out.width-denominator.width)/2,denY);
        if(n->rule)line(out,em*.08,ruleY,out.width-em*.08,ruleY,stroke);
        return out;
    }
    if(n->kind==MathKind::Root) {
        auto body=layout(n->children.value(0),s,depth+1);auto small=s;small.size*=.55;
        auto degree=layout(n->children.value(1),small,depth+1);
        double left=std::max(em*.7,degree.width+em*.3),top=em*.14;
        MathBox out;out.width=left+body.width+em*.16;out.height=body.height+top;out.baseline=body.baseline+top;
        place(out,body,left,top);if(n->children.value(1))place(out,degree,0,std::max(0.0,body.height*.28-degree.height));
        QPainterPath path;path.moveTo(left-em*.65,out.height*.56);path.lineTo(left-em*.48,out.height*.5);
        path.lineTo(left-em*.3,out.height*.88);path.lineTo(left-em*.06,stroke);path.lineTo(out.width,stroke);
        out.primitives.append(MathPath{path,stroke,false});return out;
    }
    if(n->kind==MathKind::Scripts) {
        auto base=layout(n->children.value(0),s,depth+1);auto small=s;small.size*=.65;
        auto sub=layout(n->children.value(1),small,depth+1),sup=layout(n->children.value(2),small,depth+1);
        bool limits=n->large&&n->children.value(0)&&!QString::fromUtf8("∫∬∭∮").contains(n->children[0]->text);
        MathBox out;
        if(limits) {
            double top=n->children.value(2)?sup.height+em*.1:0;
            out.width=std::max({base.width,sub.width,sup.width});out.baseline=top+base.baseline;
            out.height=top+base.height+(n->children.value(1)?sub.height+em*.1:0);
            if(n->children.value(2))place(out,sup,(out.width-sup.width)/2,0);
            place(out,base,(out.width-base.width)/2,top);
            if(n->children.value(1))place(out,sub,(out.width-sub.width)/2,top+base.height+em*.1);
        } else {
            double supY=base.baseline-em*.62-sup.baseline,subY=base.baseline+em*.34-sub.baseline;
            double minY=n->children.value(2)?std::min(0.0,supY):0;
            out.width=base.width+em*.08+std::max(sub.width,sup.width);
            out.height=std::max(base.height,n->children.value(1)?subY+sub.height:0)-minY;
            out.baseline=base.baseline-minY;place(out,base,0,-minY);
            if(n->children.value(2))place(out,sup,base.width+em*.08,supY-minY);
            if(n->children.value(1))place(out,sub,base.width+em*.08,subY-minY);
        }
        return out;
    }
    if(n->kind==MathKind::Delimited) {
        auto inner=layout(n->children.value(0),s,depth+1);
        double height=std::max(inner.height,em*1.05),pad=em*.12;
        double lw=n->text=="."||n->text.isEmpty()?0:em*.38;
        double rw=n->rightDelimiter=="."||n->rightDelimiter.isEmpty()?0:em*.38;
        MathBox out;out.height=height;out.width=lw+rw+inner.width+2*pad;
        out.baseline=inner.baseline+(height-inner.height)/2;
        place(out,inner,lw+pad,(height-inner.height)/2);
        delimiter(out,n->text,0,0,lw,height,stroke);delimiter(out,n->rightDelimiter,out.width-rw,0,rw,height,stroke);
        return out;
    }
    if(n->kind==MathKind::Matrix) {
        if(n->rows<1||n->columns<1||n->rows*n->columns!=n->children.size())throw Error{ErrorCode::InvalidEdit,"equation","Invalid matrix AST"};
        QVector<MathBox> cells;QVector<double> widths(n->columns,0),asc(n->rows,0),desc(n->rows,0);
        for(int i=0;i<n->children.size();++i) {
            auto b=layout(n->children[i],s,depth+1);int r=i/n->columns,c=i%n->columns;
            widths[c]=std::max(widths[c],b.width);asc[r]=std::max(asc[r],b.baseline);desc[r]=std::max(desc[r],b.height-b.baseline);cells.append(b);
        }
        MathBox out;double gapX=em*.65,gapY=em*.25;
        out.width=gapX*(n->columns-1);for(double w:widths)out.width+=w;
        out.height=gapY*(n->rows-1);for(int r=0;r<n->rows;++r)out.height+=asc[r]+desc[r];
        out.baseline=out.height/2+em*.25;double y=0;
        for(int r=0;r<n->rows;++r){double x=0;for(int c=0;c<n->columns;++c){auto&b=cells[r*n->columns+c];place(out,b,x+(widths[c]-b.width)/2,y+asc[r]-b.baseline);x+=widths[c]+gapX;}y+=asc[r]+desc[r]+gapY;}
        return out;
    }
    if(n->kind==MathKind::Accent) {
        auto body=layout(n->children.value(0),s,depth+1);double gap=em*.23;
        bool below=n->text=="underline"||n->text=="underbrace";
        MathBox out;out.width=std::max(body.width,em*.3);out.height=body.height+gap;out.baseline=body.baseline+(below?0:gap);
        place(out,body,(out.width-body.width)/2,below?0:gap);
        double y=below?body.height+gap*.55:gap*.4;
        if(n->text=="dot"||n->text=="ddot") {
            QPainterPath path;double radius=em*.055;
            if(n->text=="dot")path.addEllipse(QPointF(out.width/2,y),radius,radius);
            else{path.addEllipse(QPointF(out.width/2-em*.12,y),radius,radius);path.addEllipse(QPointF(out.width/2+em*.12,y),radius,radius);}
            out.primitives.append(MathPath{path,stroke,true});
        } else if(n->text=="hat") {QPainterPath p;p.moveTo(0,gap*.8);p.lineTo(out.width/2,0);p.lineTo(out.width,gap*.8);out.primitives.append(MathPath{p,stroke,false});}
        else if(n->text=="tilde") {QPainterPath p;p.moveTo(0,y);p.cubicTo(out.width*.3,-gap*.2,out.width*.7,gap,out.width,y);out.primitives.append(MathPath{p,stroke,false});}
        else if(n->text=="overbrace"||n->text=="underbrace") {
            QPainterPath p;double sign=below?1:-1;p.moveTo(0,y);p.cubicTo(out.width*.15,y-sign*gap*.6,out.width*.35,y,out.width*.5,y+sign*gap*.4);p.cubicTo(out.width*.65,y,out.width*.85,y-sign*gap*.6,out.width,y);out.primitives.append(MathPath{p,stroke,false});
        } else {line(out,0,y,out.width,y,stroke);if(n->text=="vec"){line(out,out.width-em*.2,y-em*.12,out.width,y,stroke);line(out,out.width-em*.2,y+em*.12,out.width,y,stroke);}}
        return out;
    }
    throw Error{ErrorCode::UnsupportedFeature,"equation","Unknown math node"};
}
}
Result<MathBox> layoutEquation(const MathNodePtr&node,double size,const QString&family) {
    if(!std::isfinite(size)||size<1||size>1000)return std::unexpected(Error{ErrorCode::InvalidEdit,"equation","Invalid equation size"});
    try {
        auto out=layout(node,{family,size,false,true},0);
        if(!std::isfinite(out.width)||!std::isfinite(out.height)||out.width>20000||out.height>20000)
            return std::unexpected(Error{ErrorCode::ResourceLimit,"equation","Equation geometry exceeds the budget"});
        return out;
    }catch(const Error&e){return std::unexpected(e);}
}
void paintEquation(QPainter&painter,const MathBox&box,const QPointF&pos,const QColor&color) {
    painter.save();painter.translate(pos);painter.setRenderHint(QPainter::Antialiasing,true);
    for(const auto&primitive:box.primitives) {
        if(auto glyph=std::get_if<MathGlyph>(&primitive)){painter.setPen(color);painter.setFont(glyph->font);painter.drawText(glyph->baseline,glyph->text);}
        else {const auto&path=std::get<MathPath>(primitive);painter.setPen(QPen(color,path.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.setBrush(path.filled?QBrush(color):QBrush(Qt::NoBrush));painter.drawPath(path.path);}
    }
    painter.restore();
}
} // namespace he
