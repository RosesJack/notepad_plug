#include "../src/SearchShelf.cpp"
#include <cassert>
#include <iostream>
int main() {
    HMODULE dll=LoadLibraryW(L"dist\\SearchShelf.dll"); assert(dll);
    for(auto name:{"isUnicode","getName","getFuncsArray","setInfo","beNotified","messageProc"}) assert(GetProcAddress(dll,name));
    auto unicode=(BOOL(*)())GetProcAddress(dll,"isUnicode"); assert(unicode());
    auto funcs=(FuncItem*(*)(int*))GetProcAddress(dll,"getFuncsArray"); int count=0; assert(funcs(&count));assert(count==2);
    FreeLibrary(dll);
    wchar_t full[MAX_PATH]; GetFullPathNameW(L"config-test.ini",MAX_PATH,full,nullptr);
    std::wstring path=full, decoded;
    std::vector<Entry> input={{L" 中文关键词 ",L"中文描述",L"日志",L"网络, error"},{L"\\d+\\s+\"quoted\"",L"首尾空格 \t\r\n"},{L"'quote;=中文",L""}};
    assert(save(path,input)); std::vector<Entry> output;
    assert(load(path,output)); assert(output.size()==input.size());
    for(size_t i=0;i<input.size();i++) { assert(input[i].keyword==output[i].keyword); assert(input[i].description==output[i].description);assert(input[i].category==output[i].category && input[i].tags==output[i].tags); }
    assert(!decode(L"v:broken\\",decoded));
    assert(!save(L"missing-directory\\test.ini",input));
    WritePrivateProfileStringW(L"SearchShelf",L"count",L"1001",path.c_str());
    assert(!load(path,output)); assert(output.size()==input.size());
    config=path;
    entries={{L"same",L"本地描述"},{L"local-only",L"保留"}};
    assert(save(config,entries));
    std::vector<Entry> imported={{L"same",L"外部描述"},{L"new",L"第一条"},{L"new",L"重复条目"}};
    assert(importEntries(imported,false)==ImportResult::Success);
    assert(entries.size()==3 && entries[0].description==L"本地描述" && entries[2].description==L"第一条");
    assert(load(config,output) && output.size()==3 && output[0].description==L"本地描述");
    assert(importEntries(imported,true)==ImportResult::Success);
    assert(entries.size()==2 && entries[0].keyword==L"same" && entries[0].description==L"外部描述");
    assert(load(config,output) && output.size()==2 && output[1].description==L"第一条");
    config=path+L"\\missing.ini";
    assert(importEntries({{L"replacement",L""}},true)==ImportResult::SaveFailed);
    assert(entries.size()==2 && entries[0].description==L"外部描述");
    assert(load(path,output) && output.size()==2);
    config=path; entries.clear();
    for(int i=0;i<1000;i++) entries.push_back({L"k"+std::to_wstring(i),L""});
    assert(save(config,entries));
    assert(importEntries({{L"brand-new",L""}},false)==ImportResult::TooManyEntries);
    assert(entries.size()==1000 && load(config,output) && output.size()==1000);
    assert(importEntries(imported,true)==ImportResult::Success && entries.size()==2);
    assert(importEntries({},false)==ImportResult::Success && entries.size()==2);
    assert(importEntries({},true)==ImportResult::Success && entries.empty());
    assert(load(config,output) && output.empty());
    assert(save(path,input));WritePrivateProfileStringW(L"SearchShelf",L"version",L"1",path.c_str());
    assert(load(path,output) && output[0].category.empty() && output[0].tags.empty());
    assert(save(path,input)); assert(read(path+L".v1.bak",L"SearchShelf",L"version")==L"1");
    assert(load(path,output) && output[0].category==L"日志" && output[0].tags==L"网络, error");
    DeleteFileW((path+L".v1.bak").c_str());
    DeleteFileW(path.c_str()); std::cout<<"PASS: metadata round trip and v1 upgrade backup; DLL/config/import and failure regressions\n";
}
