#include <hwpedit/table.hpp>
#include <algorithm>
#include <numeric>

namespace he { namespace {
Error invalid(const QString&message){return {ErrorCode::InvalidEdit,"table",message};}
Result<Table> checked(const BlockPtr&block) {
    if(!block||!block->table())return std::unexpected(invalid("The selected object is not a table"));
    if(block->source.opaque)return std::unexpected(Error{ErrorCode::UnsafeSave,block->source.part,"Opaque table is protected"});
    auto grid=tableGrid(*block->table());if(!grid)return std::unexpected(grid.error());
    return *block->table();
}
Result<BlockPtr> finish(const BlockPtr&source,Table table) {
    auto grid=tableGrid(table);if(!grid)return std::unexpected(grid.error());
    auto out=std::make_shared<Block>(*source);out->value=std::move(table);out->source.dirty=true;
    return BlockPtr(out);
}
Cell emptyCell(int row,int col,const CharStyle&character={},const ParaStyle&paragraphStyle={}) {
    Cell c;c.row=row;c.col=col;
    auto block=std::make_shared<Block>(*paragraph({},character));
    auto p=*block->paragraph();p.style=paragraphStyle;block->value=p;
    c.content.blocks.append(block);return c;
}
CharStyle characterOf(const Table&t) {
    for(const auto&c:t.cells)for(const auto&b:c.content.blocks)if(b->paragraph())return b->paragraph()->styleAt(0);
    return {};
}
bool intersects(const Cell&c,CellRange r) {
    return c.row<=r.bottom&&c.row+c.rowSpan>r.top&&c.col<=r.right&&c.col+c.colSpan>r.left;
}
bool contains(CellRange r,const Cell&c) {
    return c.row>=r.top&&c.col>=r.left&&c.row+c.rowSpan-1<=r.bottom&&c.col+c.colSpan-1<=r.right;
}
bool validRange(const Table&t,CellRange r) {
    return r.top>=0&&r.left>=0&&r.bottom>=r.top&&r.right>=r.left&&r.bottom<t.rows&&r.right<t.columns;
}
}
Result<QVector<Id>> tableGrid(const Table&t) {
    if(t.rows<1||t.columns<1||t.rows>10000||t.columns>1000||qint64(t.rows)*t.columns>100000)
        return std::unexpected(invalid("Table grid exceeds the supported resource budget"));
    QVector<Id> grid(t.rows*t.columns,0);
    for(const auto&c:t.cells) {
        if(c.row<0||c.col<0||c.rowSpan<1||c.colSpan<1||c.row>t.rows-c.rowSpan||c.col>t.columns-c.colSpan)
            return std::unexpected(invalid("Cell span is outside its table"));
        if(!c.id)return std::unexpected(invalid("Cell has no object ID"));
        for(int r=c.row;r<c.row+c.rowSpan;++r)for(int col=c.col;col<c.col+c.colSpan;++col) {
            auto&slot=grid[r*t.columns+col];
            if(slot)return std::unexpected(invalid("Table cells overlap"));
            slot=c.id;
        }
    }
    if(std::find(grid.begin(),grid.end(),Id(0))!=grid.end())return std::unexpected(invalid("Table contains an uncovered grid slot"));
    return grid;
}
QVector<Unit> tableMinimumRowHeights(const Table&t) {
    QVector<Unit> rows(t.rows,0);
    for(const auto&c:t.cells)if(c.rowSpan==1&&c.row>=0&&c.row<t.rows)
        rows[c.row]=std::max(rows[c.row],c.minimumHeight);
    for(const auto&c:t.cells) {
        if(c.row<0||c.rowSpan<1||c.row+c.rowSpan>t.rows)continue;
        Unit total=0;for(int r=c.row;r<c.row+c.rowSpan;++r)total+=rows[r];
        Unit deficit=std::max<Unit>(0,c.minimumHeight-total);
        for(int r=c.row;r<c.row+c.rowSpan&&deficit>0;++r) {
            Unit add=(deficit+(c.row+c.rowSpan-r)-1)/(c.row+c.rowSpan-r);
            rows[r]+=add;deficit-=add;
        }
    }
    for(auto&row:rows)row=std::max<Unit>(1,row);
    return rows;
}
Result<BlockPtr> mergeCells(const BlockPtr&block,CellRange range) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(!validRange(t,range))return std::unexpected(invalid("Invalid merge range"));
    QVector<Cell> selected,remaining;
    for(const auto&c:t.cells) {
        if(intersects(c,range)) {
            if(!contains(range,c))return std::unexpected(invalid("The merge range cuts an existing merged cell"));
            selected.append(c);
        } else remaining.append(c);
    }
    if(selected.isEmpty())return std::unexpected(invalid("No cells selected"));
    std::sort(selected.begin(),selected.end(),[](const Cell&a,const Cell&b){return a.row==b.row?a.col<b.col:a.row<b.row;});
    Cell merged=selected.first();
    merged.row=range.top;merged.col=range.left;
    merged.rowSpan=range.bottom-range.top+1;merged.colSpan=range.right-range.left+1;
    auto rows=tableMinimumRowHeights(t);merged.minimumHeight=0;
    for(int r=range.top;r<=range.bottom;++r)merged.minimumHeight+=rows[r];
    merged.content.blocks.clear();
    for(const auto&c:selected)for(const auto&b:c.content.blocks)merged.content.blocks.append(b);
    if(merged.content.blocks.isEmpty())merged.content.blocks.append(paragraph());
    merged.source.dirty=true;remaining.append(merged);t.cells=std::move(remaining);
    return finish(block,std::move(t));
}
Result<BlockPtr> splitCell(const BlockPtr&block,Id id,int rows,int columns) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    int selected=-1;for(int i=0;i<t.cells.size();++i)if(t.cells[i].id==id){selected=i;break;}
    if(selected<0)return std::unexpected(invalid("Cell is not in this table"));
    const Cell original=t.cells[selected];
    if(rows==0)rows=original.rowSpan;
    if(columns==0)columns=original.colSpan;
    if(rows<1||columns<1||rows>100||columns>100)return std::unexpected(invalid("Invalid split dimensions"));
    const int rf=rows/std::gcd(rows,original.rowSpan),cf=columns/std::gcd(columns,original.colSpan);
    const int newRowCount=t.rows+original.rowSpan*(rf-1),newColumnCount=t.columns+original.colSpan*(cf-1);
    if(newRowCount>10000||newColumnCount>1000||qint64(newRowCount)*newColumnCount>100000)
        return std::unexpected(invalid("Cell split would exceed the table grid budget"));
    auto mapBoundary=[](int value,int start,int length,int factor) {
        if(value<=start)return value;
        if(value>=start+length)return value+length*(factor-1);
        return start+(value-start)*factor;
    };
    QVector<Unit> oldWidths=t.widths;
    if(oldWidths.size()!=t.columns)oldWidths=QVector<Unit>(t.columns,mm(150)/t.columns);
    QVector<Unit> widths;
    for(int c=0;c<t.columns;++c) {
        int factor=c>=original.col&&c<original.col+original.colSpan?cf:1;
        if(oldWidths[c]<factor)return std::unexpected(invalid("Column is too narrow to split"));
        Unit remaining=oldWidths[c];
        for(int k=0;k<factor;++k){Unit w=remaining/(factor-k);widths.append(w);remaining-=w;}
    }
    for(auto&cell:t.cells) {
        int r0=mapBoundary(cell.row,original.row,original.rowSpan,rf);
        int r1=mapBoundary(cell.row+cell.rowSpan,original.row,original.rowSpan,rf);
        int c0=mapBoundary(cell.col,original.col,original.colSpan,cf);
        int c1=mapBoundary(cell.col+cell.colSpan,original.col,original.colSpan,cf);
        cell.row=r0;cell.rowSpan=r1-r0;cell.col=c0;cell.colSpan=c1-c0;cell.source.dirty=true;
    }
    Cell expanded=t.cells.takeAt(selected);
    int rs=expanded.rowSpan/rows,cs=expanded.colSpan/columns;
    CharStyle character=characterOf(t);ParaStyle paragraphStyle;
    if(!expanded.content.blocks.isEmpty()&&expanded.content.blocks.first()->paragraph()) {
        character=expanded.content.blocks.first()->paragraph()->styleAt(0);
        paragraphStyle=expanded.content.blocks.first()->paragraph()->style;
    }
    Unit totalHeight=std::max<Unit>(rows,expanded.minimumHeight);
    for(int r=0;r<rows;++r)for(int c=0;c<columns;++c) {
        Cell cell;
        if(r==0&&c==0)cell=expanded;
        else {
            cell=emptyCell(expanded.row+r*rs,expanded.col+c*cs,character,paragraphStyle);
            cell.background=expanded.background;
            cell.left=expanded.left;cell.right=expanded.right;cell.top=expanded.top;cell.bottom=expanded.bottom;
            cell.verticalAlign=expanded.verticalAlign;
            for(int edge=0;edge<4;++edge)cell.borders[edge]=expanded.borders[edge];
        }
        cell.row=expanded.row+r*rs;cell.col=expanded.col+c*cs;cell.rowSpan=rs;cell.colSpan=cs;
        cell.minimumHeight=totalHeight/rows+(r==rows-1?totalHeight%rows:0);
        cell.source.dirty=true;t.cells.append(cell);
    }
    t.rows=newRowCount;t.columns=newColumnCount;t.widths=std::move(widths);
    return finish(block,std::move(t));
}
Result<BlockPtr> insertTableRow(const BlockPtr&block,int before) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(before<0||before>t.rows)return std::unexpected(invalid("Row insertion index is invalid"));
    auto character=characterOf(t);++t.rows;
    QVector<bool> occupied(t.columns,false);
    for(auto&c:t.cells) {
        if(c.row>=before)++c.row;
        else if(c.row+c.rowSpan>before){++c.rowSpan;c.source.dirty=true;}
        if(c.row<=before&&c.row+c.rowSpan>before)for(int col=c.col;col<c.col+c.colSpan;++col)occupied[col]=true;
    }
    for(int col=0;col<t.columns;++col)if(!occupied[col])t.cells.append(emptyCell(before,col,character));
    return finish(block,std::move(t));
}
Result<BlockPtr> deleteTableRow(const BlockPtr&block,int row) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(row<0||row>=t.rows||t.rows==1)return std::unexpected(invalid("The last row cannot be deleted; delete the table instead"));
    QVector<Cell> cells;
    for(auto c:t.cells) {
        if(c.row<=row&&c.row+c.rowSpan>row) {
            if(c.rowSpan==1)continue;
            --c.rowSpan;c.source.dirty=true;
        } else if(c.row>row)--c.row;
        cells.append(std::move(c));
    }
    --t.rows;t.cells=std::move(cells);return finish(block,std::move(t));
}
Result<BlockPtr> insertTableColumn(const BlockPtr&block,int before) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(before<0||before>t.columns)return std::unexpected(invalid("Column insertion index is invalid"));
    auto character=characterOf(t);
    if(t.widths.size()!=t.columns)t.widths=QVector<Unit>(t.columns,mm(150)/t.columns);
    Unit total=std::accumulate(t.widths.begin(),t.widths.end(),Unit(0));
    ++t.columns;Unit added=std::max<Unit>(100,total/t.columns);
    Unit remaining=total-added;
    QVector<Unit> widths;
    for(int c=0;c<t.columns-1;++c) {
        Unit width=c==t.columns-2?remaining:std::max<Unit>(1,t.widths[c]*(total-added)/std::max<Unit>(1,total));
        widths.append(width);remaining-=width;
    }
    widths.insert(before,added);t.widths=std::move(widths);
    QVector<bool> occupied(t.rows,false);
    for(auto&c:t.cells) {
        if(c.col>=before)++c.col;
        else if(c.col+c.colSpan>before){++c.colSpan;c.source.dirty=true;}
        if(c.col<=before&&c.col+c.colSpan>before)for(int r=c.row;r<c.row+c.rowSpan;++r)occupied[r]=true;
    }
    for(int r=0;r<t.rows;++r)if(!occupied[r])t.cells.append(emptyCell(r,before,character));
    return finish(block,std::move(t));
}
Result<BlockPtr> deleteTableColumn(const BlockPtr&block,int col) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(col<0||col>=t.columns||t.columns==1)return std::unexpected(invalid("The last column cannot be deleted; delete the table instead"));
    QVector<Cell> cells;
    for(auto c:t.cells) {
        if(c.col<=col&&c.col+c.colSpan>col) {
            if(c.colSpan==1)continue;
            --c.colSpan;c.source.dirty=true;
        } else if(c.col>col)--c.col;
        cells.append(std::move(c));
    }
    if(t.widths.size()==t.columns){Unit removed=t.widths.takeAt(col);t.widths[std::min(col,int(t.widths.size())-1)]+=removed;}
    --t.columns;t.cells=std::move(cells);return finish(block,std::move(t));
}
Result<BlockPtr> resizeTableColumn(const BlockPtr&block,int col,Unit width,bool preserve) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(col<0||col>=t.columns||width<100||width>mm(2000))return std::unexpected(invalid("Invalid column width"));
    if(t.widths.size()!=t.columns)t.widths=QVector<Unit>(t.columns,mm(150)/t.columns);
    if(preserve&&col+1<t.columns) {
        Unit other=t.widths[col+1]+t.widths[col]-width;
        if(other<100)return std::unexpected(invalid("Adjacent column would become too narrow"));
        t.widths[col+1]=other;
    }
    t.widths[col]=width;return finish(block,std::move(t));
}
Result<BlockPtr> resizeTableRow(const BlockPtr&block,int row,Unit height) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(row<0||row>=t.rows||height<100||height>mm(2000))return std::unexpected(invalid("Invalid row height"));
    auto previous=tableMinimumRowHeights(t);Unit delta=height-previous[row];
    for(auto&c:t.cells)if(c.row<=row&&c.row+c.rowSpan>row) {
        c.minimumHeight=std::max<Unit>(100,c.minimumHeight+delta);c.source.dirty=true;
    }
    return finish(block,std::move(t));
}
Result<BlockPtr> formatCells(const BlockPtr&block,CellRange range,const std::function<void(Cell&)>&fn) {
    auto result=checked(block);if(!result)return std::unexpected(result.error());auto t=*result;
    if(!validRange(t,range))return std::unexpected(invalid("Invalid cell formatting range"));
    for(auto&c:t.cells)if(intersects(c,range)){fn(c);c.source.dirty=true;}
    return finish(block,std::move(t));
}
QVector<Id> tableCellOrder(const Table&t) {
    QVector<const Cell*> cells;for(const auto&c:t.cells)cells.append(&c);
    std::sort(cells.begin(),cells.end(),[](const Cell*a,const Cell*b){return a->row==b->row?a->col<b->col:a->row<b->row;});
    QVector<Id> ids;for(const auto*c:cells)ids.append(c->id);return ids;
}
} // namespace he
