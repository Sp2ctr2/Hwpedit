#pragma once
#include <hwpedit/model.hpp>
namespace he {
struct CellRange {int top=0,left=0,bottom=0,right=0;};
Result<QVector<Id>> tableGrid(const Table&table);
QVector<Unit> tableMinimumRowHeights(const Table&table);
Result<BlockPtr> mergeCells(const BlockPtr&table,CellRange range);
Result<BlockPtr> splitCell(const BlockPtr&table,Id cell,int rows=0,int columns=0);
Result<BlockPtr> insertTableRow(const BlockPtr&table,int before);
Result<BlockPtr> deleteTableRow(const BlockPtr&table,int row);
Result<BlockPtr> insertTableColumn(const BlockPtr&table,int before);
Result<BlockPtr> deleteTableColumn(const BlockPtr&table,int column);
Result<BlockPtr> resizeTableColumn(const BlockPtr&table,int column,Unit width,bool preserveTotal=false);
Result<BlockPtr> resizeTableRow(const BlockPtr&table,int row,Unit minimumHeight);
Result<BlockPtr> formatCells(const BlockPtr&table,CellRange range,const std::function<void(Cell&)>&format);
QVector<Id> tableCellOrder(const Table&table);
}
