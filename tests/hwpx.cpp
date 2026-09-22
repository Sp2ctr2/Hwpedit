#include <hwpedit/editor.hpp>
#include <hwpedit/formats.hpp>
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
template<class T>T require(he::Result<T> result) {
    if(!result)throw std::runtime_error((he::errorName(result.error().code)+": "+result.error().part+": "+result.error().message).toStdString());
    return std::move(*result);
}
int main(int argc,char**argv) {
    QCoreApplication app(argc,argv);
    try {
        he::Editor editor;
        CHECK(editor.insert(QString::fromUtf8("한글 편집 & <호환성>\n두 번째 문단 é 👨‍👩‍👧‍👦")));
        CHECK(editor.applyCharacter([](he::CharStyle&s){s.size=14;s.bold=true;s.color=QColor("#204060");}));
        CHECK(editor.applyParagraph([](he::ParaStyle&s){s.alignment=he::Alignment::Center;s.before=300;s.indent=500;}));
        auto table=he::table(2,3,he::mm(150));CHECK(table);
        CHECK(editor.insertObject(table));
        auto firstCell=table->table()->cells.first().content.blocks.first();
        CHECK(editor.setSelection({firstCell->id,0},{firstCell->id,0}));
        CHECK(editor.insert("표 안의 실제 내용"));
        auto body=editor.document.paragraphs(false);
        CHECK(editor.setSelection({body.first().block->id,0},{body.first().block->id,0}));
        QImage image(16,8,QImage::Format_ARGB32);image.fill(QColor("#3676a3"));
        QByteArray png;QBuffer buffer(&png);CHECK(buffer.open(QIODevice::WriteOnly));CHECK(image.save(&buffer,"PNG"));
        editor.document.resources.insert("picture1",he::Resource{png,"image/png","BinData/picture1.png"});
        auto picture=std::make_shared<he::Block>();he::Graphic g;g.resource="picture1";g.width=he::mm(60);g.height=he::mm(30);g.alt="테스트 그림";picture->value=g;
        CHECK(editor.insertObject(picture));
        auto page=editor.document.sections.first().page;page.left=he::mm(25);page.numberPosition=5;
        CHECK(editor.updatePage(0,page));
        auto bytes=require(he::writeHwpx(editor.document));
        auto reopened=require(he::readHwpx(bytes));
        CHECK(reopened.plainText()==editor.document.plainText());
        CHECK(reopened.sections.first().page.left==page.left);
        CHECK(reopened.sections.first().page.numberPosition==5);
        CHECK(reopened.paragraphs(false).size()==8);
        CHECK(reopened.resources.value("picture1").bytes==png);
        bool sawTable=false,sawPicture=false,sawStyle=false;
        for(auto ref:reopened.paragraphs(false))for(const auto&run:ref.block->paragraph()->runs) {
            if(run.style.bold&&run.style.size==14)sawStyle=true;
            if(run.object&&run.object->table()){sawTable=true;CHECK(run.object->table()->cells.size()==6);}
            if(run.object&&std::holds_alternative<he::Graphic>(run.object->value))sawPicture=true;
        }
        CHECK(sawStyle&&sawTable&&sawPicture);
        CHECK(require(he::writeHwpx(reopened))==bytes);
        auto parts=require(he::readZip(bytes));
        parts.insert("Custom/vendor.bin",QByteArray::fromHex("00ff010203aabbcc"));
        auto section=require(he::readXml(parts["Contents/section0.xml"],"section"));
        auto opaque=section.createElementNS("urn:hwpedit:test-vendor","vendor:metadata");
        opaque.setAttribute("token","preserve-me");opaque.appendChild(section.createTextNode("opaque value"));
        section.documentElement().appendChild(opaque);
        parts["Contents/section0.xml"]=section.toByteArray(-1);
        auto external=require(he::writeZip(parts));
        he::Editor edited;edited.load(require(he::readHwpx(external)));
        CHECK(require(he::writeHwpx(edited.document))==external);
        CHECK(edited.insert("수정한 "));
        auto saved=require(he::writeHwpx(edited.document));
        auto savedParts=require(he::readZip(saved));
        CHECK(savedParts["Custom/vendor.bin"]==parts["Custom/vendor.bin"]);
        CHECK(savedParts["BinData/picture1.png"]==png);
        auto savedSection=require(he::readXml(savedParts["Contents/section0.xml"],"saved section"));
        CHECK(savedSection.elementsByTagNameNS("urn:hwpedit:test-vendor","metadata").size()==1);
        CHECK(require(he::readHwpx(saved)).plainText()==edited.document.plainText());
        CHECK(!he::readHwpx("invalid"));
        std::cout<<"PASS HWPX: text/style/table/image/page read-write-reopen, exact no-op, opaque package and XML preservation\n";
        return 0;
    }catch(const std::exception&exception){std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}
