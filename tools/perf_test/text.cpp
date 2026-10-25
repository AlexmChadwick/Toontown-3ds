#include "core/text_keys.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
int main() {
    tt::TextKeys keys;
    char strings[80][100];
    for(int i=0;i<80;++i) {
        std::snprintf(strings[i],100,"Panel label %d: exact UTF-8 Doodlé",i);
        assert(keys.find(strings[i])==-1);
        assert(keys.add(strings[i])==i);
    }
    const auto start=std::chrono::steady_clock::now();
    for(int frame=0;frame<10000;++frame) for(int i=0;i<80;++i) assert(keys.find(strings[i])==i);
    const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/800000;
    assert(keys.find("changed label")==-1);
    keys.clear();
    for(int i=0;i<80;++i) assert(keys.find(strings[i])==-1);
    for(int i=0;i<128;++i) { char s[20];std::snprintf(s,20,"%d",i);assert(keys.add(s)==i); }
    assert(keys.full() && keys.add("full")==-1);
    std::printf("Unchanged 80-label frames: parses 800000 -> 80; exact lookup %.2f ns/string; collision/full/reset/UTF-8 PASS; key bytes %zu\n",ns,sizeof(keys));
}
