#include <hwpedit/model.hpp>
#include <hwpedit/unicode.hpp>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
namespace he {
static std::atomic<quint64> serial{1};
Id newId(){return serial.fetch_add(1,std::memory_order_relaxed);}
Unit units(double pt){if(!std::isfinite(pt)||std::abs(pt)>1e10) return 0;return std::llround(pt*100.0);}
Unit mm(double v){return units(v*72.0/25.4);}
QString Paragraph::text()const{QString out;for(const auto&r:runs)out+=r.object||!r.field.isEmpty()?QString(QChar(0xfffc)):r.text;return out;}
int Paragraph::length()const{int n=0;for(const auto&r:runs)n+=r.length();return n;}
CharStyle Paragraph::styleAt(int p)const{int n=0;for(const auto&r:runs){n+=r.length();if(p<n)return r.style;}return runs.isEmpty()?CharStyle{}:runs.last().style;}
BlockPtr paragraph(QString text,const CharStyle&s){auto b=std::make_shared<Block>();Paragraph p;p.runs.append(Run{std::move(text),s,{},{},{},{}});b->value=p;return b;}
BlockPtr table(int rows,int cols,Unit width){if(rows<1||cols<1||rows>1000||cols>100||qint64(rows)*cols>100000)return {};auto b=std::make_shared<Block>();Table t;t.rows=rows;t.columns=cols;t.widths=QVector<Unit>(cols,std::max<Unit>(100,width/cols));for(int r=0;r<rows;++r)for(int c=0;c<cols;++c){Cell cell;cell.row=r;cell.col=c;cell.content.blocks.append(paragraph());t.cells.append(cell);}b->value=t;return b;}
Document Document::blank(){Document d;Section s;s.body.blocks.append(paragraph());d.sections.append(s);d.styles.insert("본문",Style{"본문",{},CharStyle{},ParaStyle{}});return d;}
static void visitFlow(const Flow& f,const std::function<void(const BlockPtr&,Id)>&fn);
static void visitBlock(const BlockPtr&b,Id f,const std::function<void(const BlockPtr&,Id)>&fn){if(!b)return;fn(b,f);if(auto p=b->paragraph()){for(const auto&r:p->runs)if(r.object)visitBlock(r.object,f,fn);}else if(auto t=b->table()){for(const auto&c:t->cells)visitFlow(c.content,fn);}else if(auto g=std::get_if<Graphic>(&b->value))visitFlow(g->content,fn);else if(auto n=std::get_if<Note>(&b->value))visitFlow(n->content,fn);}
static void visitFlow(const Flow&f,const std::function<void(const BlockPtr&,Id)>&fn){for(const auto&b:f.blocks)visitBlock(b,f.id,fn);}
BlockPtr Document::block(Id id)const{BlockPtr out;auto fn=[&](const BlockPtr&b,Id){if(b->id==id)out=b;};for(const auto&s:sections){visitFlow(s.body,fn);visitFlow(s.header,fn);visitFlow(s.footer,fn);}return out;}
QVector<ParagraphRef> Document::paragraphs(bool aux)const{QVector<ParagraphRef> out;auto fn=[&](const BlockPtr&b,Id f){if(b->paragraph())out.append({b,f});};for(const auto&s:sections){visitFlow(s.body,fn);if(aux){visitFlow(s.header,fn);visitFlow(s.footer,fn);}}return out;}
Id Document::paragraphFlow(Id id)const{for(const auto&p:paragraphs())if(p.block->id==id)return p.flow;return 0;}
static void findFlow(const Flow& f,Id id,std::optional<Flow>&out){if(f.id==id){out=f;return;}visitFlow(f,[&](const BlockPtr&b,Id){if(auto t=b->table()){for(const auto&c:t->cells)if(c.content.id==id)out=c.content;}if(auto g=std::get_if<Graphic>(&b->value);g&&g->content.id==id)out=g->content;if(auto n=std::get_if<Note>(&b->value);n&&n->content.id==id)out=n->content;});}
std::optional<Flow> Document::flow(Id id)const{std::optional<Flow> out;for(const auto&s:sections){findFlow(s.body,id,out);findFlow(s.header,id,out);findFlow(s.footer,id,out);}return out;}
static bool changeFlow(Flow&,Id,const BlockPtr&,const Flow*);
static bool changeBlock(BlockPtr&b,Id id,const BlockPtr&replacement,const Flow*fr){if(!b)return false;if(!fr&&b->id==id){b=replacement;return true;}auto copy=std::make_shared<Block>(*b);bool changed=false;if(auto p=std::get_if<Paragraph>(&copy->value)){for(auto&r:p->runs)if(r.object&&changeBlock(r.object,id,replacement,fr)){changed=true;break;}}else if(auto t=std::get_if<Table>(&copy->value)){for(auto&c:t->cells)if(changeFlow(c.content,id,replacement,fr)){changed=true;break;}}else if(auto g=std::get_if<Graphic>(&copy->value))changed=changeFlow(g->content,id,replacement,fr);else if(auto n=std::get_if<Note>(&copy->value))changed=changeFlow(n->content,id,replacement,fr);if(changed){copy->source.dirty=true;b=copy;}return changed;}
static bool changeFlow(Flow& f,Id id,const BlockPtr&replacement,const Flow*fr){if(fr&&f.id==id){f=*fr;return true;}for(auto&b:f.blocks)if(changeBlock(b,id,replacement,fr))return true;return false;}
bool Document::replaceBlock(Id id,BlockPtr b){for(auto&s:sections)if(changeFlow(s.body,id,b,nullptr)||changeFlow(s.header,id,b,nullptr)||changeFlow(s.footer,id,b,nullptr)){++revision;return true;}return false;}
bool Document::replaceFlow(Id id,const Flow&f){for(auto&s:sections)if(changeFlow(s.body,id,{},&f)||changeFlow(s.header,id,{},&f)||changeFlow(s.footer,id,{},&f)){++revision;return true;}return false;}
QString Document::plainText()const{QStringList out;for(auto p:paragraphs(false))out.append(p.block->paragraph()->text().replace(QChar(0xfffc),QString()));return out.join('\n');}
QVector<Run> sliceRuns(const Paragraph&p,int a,int z){QVector<Run>out;int pos=0;for(const auto&r:p.runs){int end=pos+r.length();int l=std::max(a,pos),h=std::min(z,end);if(h>l){Run part=r;if(!r.object&&r.field.isEmpty())part.text=r.text.mid(l-pos,h-l);out.append(std::move(part));}pos=end;}return out;}
QVector<Run> normalizeRuns(QVector<Run> runs){QVector<Run>out;for(auto&r:runs){if(!r.length())continue;if(!out.isEmpty()&&!r.object&&r.field.isEmpty()&&!out.last().object&&out.last().field.isEmpty()&&r.style==out.last().style&&r.hyperlink==out.last().hyperlink&&r.sourceXml==out.last().sourceXml)out.last().text+=r.text;else out.append(std::move(r));}return out;}
Result<Paragraph> replaceText(const Paragraph&p,int a,int z,QVector<Run>r){auto s=p.text();if(a<0||z<a||z>s.size()||!isGraphemeBoundary(s,a)||!isGraphemeBoundary(s,z))return std::unexpected(Error{ErrorCode::InvalidEdit,{},"Range splits a grapheme or is out of bounds"});for(const auto&run:r)if(!validUnicode(run.text))return std::unexpected(Error{ErrorCode::InvalidEdit,{},"Invalid Unicode"});auto out=p;out.runs=sliceRuns(p,0,a);out.runs+=r;out.runs+=sliceRuns(p,z,int(s.size()));out.runs=normalizeRuns(out.runs);if(out.runs.isEmpty())out.runs.append(Run{{},p.styleAt(a),{},{},{},{}});return out;}
Result<void> Document::validate(const Limits&lim)const{
    if(sections.isEmpty()||sections.size()>10000)return std::unexpected(Error{ErrorCode::InvalidHeader,{},"Invalid section count"});
    QSet<Id>ids;int paragraphsCount=0,cellsCount=0;QString problem;
    std::function<void(const Flow&,int)>vf;std::function<void(const BlockPtr&,int)>vb;
    auto addId=[&](Id id){if(!id||ids.contains(id)){problem="Duplicate or zero object ID";return false;}ids.insert(id);return true;};
    vb=[&](const BlockPtr&b,int depth){if(!problem.isEmpty())return;if(depth>lim.objectDepth){problem="Object nesting limit exceeded";return;}if(!b||!addId(b->id))return;
        if(auto p=b->paragraph()){if(++paragraphsCount>lim.paragraphs){problem="Paragraph limit exceeded";return;}if(!validUnicode(p->text())){problem="Invalid Unicode text";return;}for(const auto&r:p->runs){if(!std::isfinite(r.style.size)||r.style.size<0.1||r.style.size>1000||!std::isfinite(r.style.width)||r.style.width<1||r.style.width>1000){problem="Invalid font metrics";return;}if(r.object)vb(r.object,depth+1);}}
        else if(auto t=b->table()){if(t->rows<1||t->columns<1||t->rows>10000||t->columns>1000||qint64(t->rows)*t->columns>lim.tableCells){problem="Invalid table dimensions";return;}QVector<quint8> grid(t->rows*t->columns,0);for(const auto&c:t->cells){if(++cellsCount>lim.tableCells||!addId(c.id)){problem="Table cell limit or ID invalid";return;}if(c.row<0||c.col<0||c.rowSpan<1||c.colSpan<1||c.row+c.rowSpan>t->rows||c.col+c.colSpan>t->columns){problem="Invalid cell span";return;}for(int r=c.row;r<c.row+c.rowSpan;++r)for(int k=c.col;k<c.col+c.colSpan;++k){auto&v=grid[r*t->columns+k];if(v++){problem="Overlapping table cells";return;}}vf(c.content,depth+1);}if(std::find(grid.begin(),grid.end(),0)!=grid.end())problem="Uncovered table grid";}
        else if(auto g=std::get_if<Graphic>(&b->value)){if(g->width<1||g->height<1||g->width>100000000||g->height>100000000||!std::isfinite(g->rotation)||!std::isfinite(g->opacity)){problem="Invalid graphic geometry";return;}vf(g->content,depth+1);}
        else if(auto n=std::get_if<Note>(&b->value))vf(n->content,depth+1);
    };
    vf=[&](const Flow&f,int depth){if(!addId(f.id))return;for(const auto&b:f.blocks)vb(b,depth);};
    for(const auto&s:sections){addId(s.id);const auto&p=s.page;if(p.width<100||p.height<100||p.width>10000000||p.height>10000000||p.left<0||p.right<0||p.top<0||p.bottom<0||p.left+p.right+p.binding>=p.width||p.top+p.bottom>=p.height||p.columns<1||p.columns>16){problem="Invalid page geometry";break;}vf(s.body,0);vf(s.header,0);vf(s.footer,0);}
    if(!problem.isEmpty())return std::unexpected(Error{ErrorCode::InvalidEdit,{},problem});return {};
}
// The recovery codec is kept in a separate translation unit as the format grows.
QByteArray semanticDigest(const Document&d){QCryptographicHash hash(QCryptographicHash::Sha256);for(const auto&s:d.sections){hash.addData(QByteArray::number(s.page.width));hash.addData(":"+QByteArray::number(s.page.height));}for(const auto&p:d.paragraphs()){hash.addData(p.block->paragraph()->text().toUtf8());hash.addData("\0",1);for(const auto&r:p.block->paragraph()->runs){hash.addData(r.style.family.toUtf8());hash.addData(QByteArray::number(r.style.size));hash.addData(r.style.bold?"b":"n");}}return hash.result();}
}
