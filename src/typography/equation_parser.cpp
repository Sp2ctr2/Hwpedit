#include <hwpedit/equation.hpp>
#include <hwpedit/unicode.hpp>
#include <QHash>
#include <algorithm>

namespace he { namespace {
enum class TokenKind {End,Word,Symbol,Text,Open,Close,Sub,Super,Cell,Row};
struct Token {TokenKind kind=TokenKind::End;QString text;int offset=0;};
class Lexer {
    const QString&script_;int offset_=0;
public:
    explicit Lexer(const QString&s):script_(s){}
    Token next() {
        while(offset_<script_.size()&&script_[offset_].isSpace())++offset_;
        Token t;t.offset=offset_;
        if(offset_>=script_.size())return t;
        QChar c=script_[offset_++];
        if(c=='{')return {TokenKind::Open,"{",t.offset};
        if(c=='}')return {TokenKind::Close,"}",t.offset};
        if(c=='_')return {TokenKind::Sub,"_",t.offset};
        if(c=='^')return {TokenKind::Super,"^",t.offset};
        if(c=='&')return {TokenKind::Cell,"&",t.offset};
        if(c=='#')return {TokenKind::Row,"#",t.offset};
        if(c=='"') {
            QString value;bool closed=false;
            while(offset_<script_.size()) {
                QChar n=script_[offset_++];
                if(n=='"'){closed=true;break;}
                if(n=='\\'&&offset_<script_.size())n=script_[offset_++];
                value+=n;
            }
            if(!closed)throw Error{ErrorCode::InvalidEdit,"equation",QString("Unterminated quoted text at %1").arg(t.offset)};
            return {TokenKind::Text,value,t.offset};
        }
        if(c=='\\') {
            if(offset_>=script_.size())return {TokenKind::Symbol,"\\",t.offset};
            if(script_[offset_]=='\\'){++offset_;return {TokenKind::Row,"#",t.offset};}
            if(!script_[offset_].isLetter())return {TokenKind::Symbol,QString(script_[offset_++]),t.offset};
            c=script_[offset_++];
        }
        if(c.isLetterOrNumber()) {
            QString value(c);
            while(offset_<script_.size()) {
                QChar next=script_[offset_];
                if(!next.isLetterOrNumber()&&next.category()!=QChar::Mark_NonSpacing)break;
                value+=next;++offset_;
            }
            return {TokenKind::Word,value,t.offset};
        }
        if(c.isHighSurrogate()&&offset_<script_.size()&&script_[offset_].isLowSurrogate()) {
            QString scalar(c);scalar+=script_[offset_++];return {TokenKind::Symbol,scalar,t.offset};
        }
        return {TokenKind::Symbol,QString(c),t.offset};
    }
};
const QHash<QString,QString>& symbols() {
    static const QHash<QString,QString> values={
        {"alpha",QString::fromUtf8("α")},{"beta",QString::fromUtf8("β")},{"gamma",QString::fromUtf8("γ")},
        {"delta",QString::fromUtf8("δ")},{"epsilon",QString::fromUtf8("ε")},{"varepsilon",QString::fromUtf8("ϵ")},
        {"zeta",QString::fromUtf8("ζ")},{"eta",QString::fromUtf8("η")},{"theta",QString::fromUtf8("θ")},
        {"vartheta",QString::fromUtf8("ϑ")},{"iota",QString::fromUtf8("ι")},{"kappa",QString::fromUtf8("κ")},
        {"lambda",QString::fromUtf8("λ")},{"mu",QString::fromUtf8("μ")},{"nu",QString::fromUtf8("ν")},
        {"xi",QString::fromUtf8("ξ")},{"omicron",QString::fromUtf8("ο")},{"pi",QString::fromUtf8("π")},
        {"rho",QString::fromUtf8("ρ")},{"sigma",QString::fromUtf8("σ")},{"tau",QString::fromUtf8("τ")},
        {"upsilon",QString::fromUtf8("υ")},{"phi",QString::fromUtf8("φ")},{"varphi",QString::fromUtf8("ϕ")},
        {"chi",QString::fromUtf8("χ")},{"psi",QString::fromUtf8("ψ")},{"omega",QString::fromUtf8("ω")},
        {"int",QString::fromUtf8("∫")},{"iint",QString::fromUtf8("∬")},{"iiint",QString::fromUtf8("∭")},
        {"oint",QString::fromUtf8("∮")},{"sum",QString::fromUtf8("∑")},{"prod",QString::fromUtf8("∏")},
        {"coprod",QString::fromUtf8("∐")},{"bigcap",QString::fromUtf8("⋂")},{"bigcup",QString::fromUtf8("⋃")},
        {"infty",QString::fromUtf8("∞")},{"infinity",QString::fromUtf8("∞")},
        {"times",QString::fromUtf8("×")},{"cdot",QString::fromUtf8("⋅")},{"div",QString::fromUtf8("÷")},
        {"pm",QString::fromUtf8("±")},{"mp",QString::fromUtf8("∓")},{"le",QString::fromUtf8("≤")},
        {"leq",QString::fromUtf8("≤")},{"ge",QString::fromUtf8("≥")},{"geq",QString::fromUtf8("≥")},
        {"ne",QString::fromUtf8("≠")},{"neq",QString::fromUtf8("≠")},{"approx",QString::fromUtf8("≈")},
        {"equiv",QString::fromUtf8("≡")},{"propto",QString::fromUtf8("∝")},{"partial",QString::fromUtf8("∂")},
        {"nabla",QString::fromUtf8("∇")},{"forall",QString::fromUtf8("∀")},{"exists",QString::fromUtf8("∃")},
        {"in",QString::fromUtf8("∈")},{"notin",QString::fromUtf8("∉")},{"subset",QString::fromUtf8("⊂")},
        {"subseteq",QString::fromUtf8("⊆")},{"supset",QString::fromUtf8("⊃")},{"supseteq",QString::fromUtf8("⊇")},
        {"cup",QString::fromUtf8("∪")},{"cap",QString::fromUtf8("∩")},{"emptyset",QString::fromUtf8("∅")},
        {"to",QString::fromUtf8("→")},{"rightarrow",QString::fromUtf8("→")},{"leftarrow",QString::fromUtf8("←")},
        {"leftrightarrow",QString::fromUtf8("↔")},{"implies",QString::fromUtf8("⇒")},{"iff",QString::fromUtf8("⇔")},
        {"therefore",QString::fromUtf8("∴")},{"because",QString::fromUtf8("∵")},{"angle",QString::fromUtf8("∠")},
        {"perp",QString::fromUtf8("⊥")},{"parallel",QString::fromUtf8("∥")},{"degree",QString::fromUtf8("°")},
        {"ldots",QString::fromUtf8("…")},{"cdots",QString::fromUtf8("⋯")},{"vdots",QString::fromUtf8("⋮")},
        {"ddots",QString::fromUtf8("⋱")},{"aleph",QString::fromUtf8("ℵ")},{"hbar",QString::fromUtf8("ℏ")},
        {"ell",QString::fromUtf8("ℓ")},{"prime",QString::fromUtf8("′")},{"langle",QString::fromUtf8("⟨")},
        {"rangle",QString::fromUtf8("⟩")},{"lbrace","{"},{"rbrace","}"},{"vert","|"},{"Vert",QString::fromUtf8("‖")}
    };
    return values;
}
class Parser {
    Lexer lexer_;Token token_;MathLimits limits_;int nodes_=0;
    QStringList warnings_;
    void advance(){token_=lexer_.next();}
    [[noreturn]] void error(const QString&message)const {
        throw Error{ErrorCode::InvalidEdit,"equation",QString("%1 at UTF-16 offset %2").arg(message).arg(token_.offset)};
    }
    MathNodePtr node(MathKind kind,QVector<MathNodePtr> children={},QString text={},QString right={},bool large=false,int rows=0,int columns=0,bool rule=true) {
        if(++nodes_>limits_.nodes)throw Error{ErrorCode::ResourceLimit,"equation","Equation node budget exceeded"};
        auto n=std::make_shared<MathNode>();n->kind=kind;n->children=std::move(children);n->text=std::move(text);
        n->rightDelimiter=std::move(right);n->large=large;n->rows=rows;n->columns=columns;n->rule=rule;return n;
    }
    void depth(int d)const{if(d>limits_.depth)throw Error{ErrorCode::ResourceLimit,"equation","Equation nesting budget exceeded"};}
    bool word(const QString&s)const{return token_.kind==TokenKind::Word&&token_.text.compare(s,Qt::CaseInsensitive)==0;}
    QString delimiter() {
        if(token_.kind==TokenKind::End)error("Delimiter expected");
        QString value=token_.text;
        if(symbols().contains(value))value=symbols().value(value);
        advance();return value;
    }
    MathNodePtr group(int d) {
        depth(d);
        if(token_.kind!=TokenKind::Open)return primary(d+1);
        advance();auto out=sequence(d+1);
        if(token_.kind!=TokenKind::Close)error("Closing brace expected");advance();return out;
    }
    MathNodePtr matrix(const QString&type,int d) {
        if(token_.kind!=TokenKind::Open)error("Matrix requires a braced cell list");advance();
        QVector<QVector<MathNodePtr>> rows;QVector<MathNodePtr> row;
        while(token_.kind!=TokenKind::End&&token_.kind!=TokenKind::Close) {
            row.append(sequence(d+1));
            if(row.size()>limits_.matrixColumns)error("Matrix column budget exceeded");
            if(token_.kind==TokenKind::Cell){advance();continue;}
            rows.append(row);row.clear();
            if(rows.size()>limits_.matrixRows)error("Matrix row budget exceeded");
            if(token_.kind==TokenKind::Row){advance();continue;}
            if(token_.kind!=TokenKind::Close)error("Matrix row separator expected");
        }
        if(token_.kind!=TokenKind::Close)error("Unterminated matrix");advance();
        if(!row.isEmpty())rows.append(row);
        if(rows.isEmpty())rows.append({node(MathKind::Row)});
        int columns=0;for(const auto&r:rows)columns=std::max(columns,int(r.size()));
        QVector<MathNodePtr> flat;
        for(auto r:rows){while(r.size()<columns)r.append(node(MathKind::Row));flat+=r;}
        auto out=node(MathKind::Matrix,flat,{}, {},false,int(rows.size()),columns);
        if(type=="pmatrix")return node(MathKind::Delimited,{out},"(",")");
        if(type=="bmatrix")return node(MathKind::Delimited,{out},"[","]");
        if(type=="cases")return node(MathKind::Delimited,{out},"{",".");
        if(type=="vmatrix")return node(MathKind::Delimited,{out},"|","|");
        return out;
    }
    MathNodePtr primary(int d) {
        depth(d);auto t=token_;
        if(t.kind==TokenKind::Open)return group(d+1);
        if(t.kind==TokenKind::End||t.kind==TokenKind::Close||t.kind==TokenKind::Cell||t.kind==TokenKind::Row)error("Equation operand expected");
        advance();
        if(t.kind==TokenKind::Text)return node(MathKind::Text,{},t.text);
        if(t.kind==TokenKind::Symbol&&(t.text=="("||t.text=="[")) {
            QString close=t.text=="("?")":"]";
            auto inner=sequence(d+1,close);
            if(token_.text!=close)error("Closing delimiter expected");advance();
            return node(MathKind::Delimited,{inner},t.text,close);
        }
        if(t.kind==TokenKind::Word) {
            auto key=t.text.toLower();
            if(key=="frac"||key=="dfrac"||key=="tfrac") {auto a=group(d+1),b=group(d+1);return node(MathKind::Fraction,{a,b});}
            if(key=="binom") {auto a=group(d+1),b=group(d+1);return node(MathKind::Delimited,{node(MathKind::Fraction,{a,b},{},{},false,0,0,false)},"(",")");}
            if(key=="sqrt")return node(MathKind::Root,{group(d+1),{}});
            if(key=="root") {auto degree=group(d+1);if(word("of"))advance();auto body=group(d+1);return node(MathKind::Root,{body,degree});}
            if(QStringList{"matrix","pmatrix","bmatrix","vmatrix","cases","pile","eqalign"}.contains(key))return matrix(key,d+1);
            if(key=="left") {
                auto left=delimiter();auto inner=sequence(d+1,{},true);
                if(!word("right"))error("RIGHT delimiter expected");advance();auto right=delimiter();
                return node(MathKind::Delimited,{inner},left,right);
            }
            if(QStringList{"bar","overline","underline","vec","hat","tilde","dot","ddot","overbrace","underbrace"}.contains(key))
                return node(MathKind::Accent,{group(d+1)},key);
            if(QStringList{"bold","rm","roman","it","mathrm","mathbf","mathit"}.contains(key))
                return node(MathKind::Style,{group(d+1)},key);
            if(key=="text")return node(MathKind::Style,{group(d+1)},"rm");
            if(key=="lim"||key=="limsup"||key=="liminf")return node(MathKind::Symbol,{},key,{},true);
            if(symbols().contains(key)) {
                QString glyph=symbols().value(key);
                const QStringList operators={"int","iint","iiint","oint","sum","prod","coprod","bigcap","bigcup"};
                bool large=operators.contains(key);
                if(!large&&t.text==t.text.toUpper()&&t.text.size()>1)glyph=glyph.toUpper();
                return node(MathKind::Symbol,{},glyph,{},large);
            }
            if(QStringList{"sin","cos","tan","cot","sec","csc","sinh","cosh","tanh","log","ln","exp","det","max","min","gcd"}.contains(key))
                return node(MathKind::Text,{},key);
        }
        if(t.kind==TokenKind::Sub||t.kind==TokenKind::Super)error("A script has no base");
        return node(MathKind::Symbol,{},t.text);
    }
    MathNodePtr atom(int d) {
        auto base=primary(d+1);MathNodePtr sub,sup;
        while(token_.kind==TokenKind::Sub||token_.kind==TokenKind::Super||word("from")||(base->large&&word("to"))) {
            bool lower=token_.kind==TokenKind::Sub||word("from");advance();
            if(lower) {if(sub)error("Duplicate subscript");sub=group(d+1);}
            else {if(sup)error("Duplicate superscript");sup=group(d+1);}
        }
        return sub||sup?node(MathKind::Scripts,{base,sub,sup},{},{},base->large):base;
    }
    MathNodePtr sequence(int d,const QString&close={},bool stopAtRight=false) {
        depth(d);QVector<MathNodePtr> row;
        while(token_.kind!=TokenKind::End&&token_.kind!=TokenKind::Close&&token_.kind!=TokenKind::Cell&&token_.kind!=TokenKind::Row) {
            if((!close.isEmpty()&&token_.text==close)||(stopAtRight&&word("right")))break;
            if(word("over")||word("atop")) {
                if(row.isEmpty())error("A fraction has no numerator");bool rule=word("over");advance();
                auto numerator=row.size()==1?row.first():node(MathKind::Row,row);
                auto denominator=sequence(d+1,close,stopAtRight);
                if(denominator->kind==MathKind::Row&&denominator->children.isEmpty())error("A fraction has no denominator");
                return node(MathKind::Fraction,{numerator,denominator},{},{},false,0,0,rule);
            }
            row.append(atom(d+1));
        }
        return row.size()==1?row.first():node(MathKind::Row,row);
    }
public:
    Parser(const QString&s,const MathLimits&limits):lexer_(s),limits_(limits){advance();}
    MathParse run(){auto root=sequence(0);if(token_.kind!=TokenKind::End)error("Unexpected equation token");return {root,warnings_};}
};
}
Result<MathParse> parseEquation(const QString&s,const MathLimits&limits) {
    if(s.size()>limits.characters)return std::unexpected(Error{ErrorCode::ResourceLimit,"equation","Equation source exceeds the character budget"});
    if(!validUnicode(s))return std::unexpected(Error{ErrorCode::InvalidEdit,"equation","Equation contains invalid Unicode"});
    try{return Parser(s,limits).run();}
    catch(const Error&e){return std::unexpected(e);}
    catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::ResourceLimit,"equation","Equation allocation failed"});}
}
} // namespace he
