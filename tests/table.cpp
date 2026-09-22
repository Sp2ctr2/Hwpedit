#include <hwpedit/table.hpp>
#include <hwpedit/editor.hpp>
#include <hwpedit/formats.hpp>
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T require(he::Result<T>r){if(!r)throw std::runtime_error(r.error().message.toStdString());return std::move(*r);}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{
    auto original=he::table(4,4,he::mm(160));CHECK(original);
    auto merged=require(he::mergeCells(original,{0,0,1,1}));
    CHECK(merged->table()->cells.size()==13);CHECK(he::tableGrid(*merged->table()));
    CHECK(!he::mergeCells(merged,{0,1,0,2}));
    he::Id mergedId=0;for(const auto&c:merged->table()->cells)if(c.rowSpan==2&&c.colSpan==2)mergedId=c.id;
    CHECK(mergedId);
    auto split=require(he::splitCell(merged,mergedId));CHECK(split->table()->cells.size()==16);
    auto single=he::table(1,1,he::mm(80));he::Id id=single->table()->cells.first().id;
    auto six=require(he::splitCell(single,id,2,3));CHECK(six->table()->rows==2);CHECK(six->table()->columns==3);CHECK(six->table()->cells.size()==6);
    auto refined=require(he::splitCell(merged,mergedId,3,3));CHECK(refined->table()->rows==8);CHECK(refined->table()->columns==8);CHECK(he::tableGrid(*refined->table()));
    auto row=require(he::insertTableRow(merged,1));CHECK(row->table()->rows==5);CHECK(he::tableGrid(*row->table()));
    auto column=require(he::insertTableColumn(row,1));CHECK(column->table()->columns==5);CHECK(he::tableGrid(*column->table()));
    auto removed=require(he::deleteTableRow(column,0));removed=require(he::deleteTableColumn(removed,0));CHECK(he::tableGrid(*removed->table()));
    CHECK(!he::deleteTableRow(single,0));CHECK(!he::deleteTableColumn(single,0));
    auto resized=require(he::resizeTableColumn(original,0,he::mm(50),true));
    CHECK(resized->table()->widths[0]+resized->table()->widths[1]==original->table()->widths[0]+original->table()->widths[1]);
    he::Editor editor;CHECK(editor.insertObject(original));CHECK(editor.updateBlock(merged,"셀 합치기"));CHECK(editor.undo());CHECK(editor.document.block(original->id)->table()->cells.size()==16);CHECK(editor.redo());CHECK(editor.document.block(original->id)->table()->cells.size()==13);
    auto written=require(he::writeHwpx(editor.document));auto reopened=require(he::readHwpx(written));bool found=false;
    for(const auto&p:reopened.paragraphs())for(const auto&r:p.block->paragraph()->runs)if(r.object&&r.object->table()){
        found=true;CHECK(r.object->table()->cells.size()==13);CHECK(he::tableGrid(*r.object->table()));
    }
    CHECK(found);std::cout<<"PASS tables: merge guards, arbitrary split refinement, row/column insert/delete, resize, undo and roundtrip\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
