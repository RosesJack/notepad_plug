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
    std::vector<Entry> input={{L" 中文关键词 ",L"中文描述"},{L"\\d+\\s+\"quoted\"",L"首尾空格 \t\r\n"},{L"'quote;=中文",L""}};
    assert(save(path,input)); std::vector<Entry> output;
    assert(load(path,output)); assert(output.size()==input.size());
    for(size_t i=0;i<input.size();i++) { assert(input[i].keyword==output[i].keyword); assert(input[i].description==output[i].description); }
    assert(!decode(L"v:broken\\",decoded));
    assert(!save(L"missing-directory\\test.ini",input));
    WritePrivateProfileStringW(L"SearchShelf",L"count",L"1001",path.c_str());
    assert(!load(path,output)); assert(output.size()==input.size());
    DeleteFileW(path.c_str()); std::cout<<"PASS: DLL loading and exports, Unicode, escaping, round trip, write failure, invalid import preserves data\n";
}
