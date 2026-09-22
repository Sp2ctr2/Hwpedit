#include <hwpedit/equation.hpp>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(int argc,char**argv){QGuiApplication app(argc,argv);try{
    const QStringList scripts={"{a+b} over {c+d}","sqrt {a^2+b^2}","root {3} of {x}",
        "int _0 ^1 {x^2} dx","sum _{i=1} ^n {i^2}","prod _{k=1} ^n k", "x_1^2 + alpha times beta",
        "pmatrix {a&b#c&d}","left [ {1 over 2} + x right ]","vec {AB} + bar x + hat y + dot z",
        "binom {n} {k}","lim _{x to 0} {sin x over x}","cases {x&x ge 0#-x&x < 0}"};
    for(const auto&script:scripts){auto parsed=he::parseEquation(script);if(!parsed)throw std::runtime_error((script+": "+parsed.error().message).toStdString());auto box=he::layoutEquation(parsed->root,14);CHECK(box);CHECK(box->width>0&&box->height>0);CHECK(!box->primitives.isEmpty());QImage image(1200,400,QImage::Format_ARGB32);image.fill(Qt::white);QPainter painter(&image);he::paintEquation(painter,*box,{10,10});painter.end();bool changed=false;for(int y=0;y<image.height()&&!changed;++y)for(int x=0;x<image.width();++x)if(image.pixelColor(x,y)!=QColor(Qt::white)){changed=true;break;}CHECK(changed);}
    CHECK(!he::parseEquation("{x"));CHECK(!he::parseEquation("x^^2"));CHECK(!he::parseEquation("matrix {a&b#c"));
    he::MathLimits limits;limits.depth=5;CHECK(!he::parseEquation("{{{{{{x}}}}}}",limits));limits={};limits.characters=10;CHECK(!he::parseEquation(QString(20,'x'),limits));
    std::cout<<"PASS equation: bounded HWP-syntax AST, fractions, roots, scripts, operators, Greek, matrices, native vector painting\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
