#pragma once
#include <hwpedit/model.hpp>
#include <deque>
namespace he {
struct Position {Id paragraph=0;int offset=0;bool operator==(const Position&)const=default;};
struct Selection {Position anchor,caret;bool empty()const{return anchor==caret;}};
struct BlockPatch {Id id;BlockPtr before,after;};
struct FlowPatch {Id id;Flow before,after;};
struct PagePatch {Id section;PageFormat before,after;};
struct Command {
    QString name;
    QVector<BlockPatch> blocks;
    QVector<FlowPatch> flows;
    QVector<PagePatch> pages;
    Selection before,after;
    quint64 stateBefore=0,stateAfter=0;
    qsizetype cost=0;
};
class Editor {
    std::deque<Command> undo_,redo_;
    quint64 state_=0,nextState_=1,savedState_=0;
    qsizetype historyBytes_=0;
    bool apply(const Command&cmd,bool forward);
public:
    Document document=Document::blank();
    Selection selection;
    std::function<void()> changed;
    QString lastError;
    qsizetype historyBudget=64*1024*1024;
    Editor();
    void load(Document doc);
    bool execute(Command cmd);
    bool insert(const QString& text);
    bool insertRuns(QVector<Run> runs);
    bool erase(bool backwards);
    bool splitParagraph(bool pageBreak=false);
    bool applyCharacter(const std::function<void(CharStyle&)>&fn);
    bool applyParagraph(const std::function<void(ParaStyle&)>&fn);
    bool insertObject(BlockPtr object);
    bool updateBlock(BlockPtr block,const QString&label);
    bool updatePage(int section,const PageFormat&page);
    bool undo(); bool redo();
    bool canUndo()const{return !undo_.empty();}bool canRedo()const{return !redo_.empty();}
    bool modified()const{return state_!=savedState_;}
    void markSaved(){savedState_=state_;}
    QString selectedText()const;
    bool setSelection(Position a,Position b);
    void collapse(Position p){setSelection(p,p);}
};
}
