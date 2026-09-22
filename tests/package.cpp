#include <hwpedit/formats.hpp>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(int argc,char**argv){QCoreApplication a(argc,argv);try{
QMap<QString,QByteArray> p{{"mimetype","application/hwp+zip"},{"Contents/section0.xml","<a xmlns=\"urn:test\">한국어 &amp; data</a>"},{"BinData/raw.bin",QByteArray(12000,'x')}};auto z=he::writeZip(p);CHECK(z);auto r=he::readZip(*z);CHECK(r);CHECK(*r==p);CHECK(!he::readZip("not zip"));auto bad=p;bad.insert("../escape", "x");CHECK(!he::writeZip(bad));he::Limits l;l.partBytes=100;CHECK(!he::readZip(*z,l));
CHECK(he::readXml("<x xmlns=\"urn:x\">&lt;&amp;&gt;</x>","sample"));CHECK(!he::readXml("<!DOCTYPE x [<!ENTITY a SYSTEM 'file:///etc/passwd'>]><x>&a;</x>","sample"));CHECK(!he::readXml("<x><y></x>","sample"));l=he::Limits{};l.xmlDepth=3;CHECK(!he::readXml("<a><b><c><d/></c></b></a>","sample",l));
QTemporaryDir d;CHECK(d.isValid());QString f=d.filePath("atomic.hwpx");CHECK(he::atomicWrite(f,*z));CHECK(he::readFile(f));CHECK(*he::readFile(f)==*z);CHECK(!he::atomicWrite(d.filePath("missing/sub.hwpx"),*z));CHECK(*he::readFile(f)==*z);
std::cout<<"PASS package: bounded ZIP roundtrip, CRC, path validation, DTD denial, XML limits, atomic save\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
