#include <hwpedit/editor.hpp>
#include <hwpedit/unicode.hpp>
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{
    he::Editor e;CHECK(e.document.validate());CHECK(e.insert(QStringLiteral("한글 English 漢字")));CHECK(e.document.plainText()==QStringLiteral("한글 English 漢字"));CHECK(e.undo());CHECK(e.document.plainText().isEmpty());CHECK(e.redo());CHECK(e.modified());e.markSaved();CHECK(!e.modified());
    auto s=QString::fromUtf8("a👨‍👩‍👧‍👦é한");auto b=he::graphemeBoundaries(s);CHECK(b.size()==5);CHECK(he::nextGrapheme(s,1)>2);CHECK(!he::isGraphemeBoundary(s,2));
    CHECK(e.insert("\n두 번째\n세 번째"));CHECK(e.document.paragraphs().size()==3);CHECK(e.undo());CHECK(e.document.paragraphs().size()==1);CHECK(e.redo());CHECK(e.erase(true));CHECK(e.undo());
    CHECK(e.applyCharacter([](he::CharStyle&s){s.bold=true;s.size=12;}));CHECK(e.document.validate());CHECK(e.undo());
    auto t=he::table(3,3,he::mm(120));CHECK(t);CHECK(e.insertObject(t));CHECK(e.document.validate());CHECK(e.document.paragraphs().size()==12);CHECK(e.undo());CHECK(e.document.paragraphs().size()==3);CHECK(e.redo());CHECK(e.document.validate());
    he::Editor stress;for(int i=0;i<1000;++i)CHECK(stress.insert("가"));for(int i=0;i<1000;++i)CHECK(stress.undo());CHECK(stress.document.plainText().isEmpty());for(int i=0;i<1000;++i)CHECK(stress.redo());CHECK(stress.document.plainText().size()==1000);
    std::cout<<"PASS core: Unicode, text transactions, undo/redo, flow paste, table model, 1000 operations\n";return 0;
}catch(const std::exception&ex){std::cerr<<"FAIL "<<ex.what()<<'\n';return 1;}}
