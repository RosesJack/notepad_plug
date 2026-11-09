// Synthetic host regression: native geometry stays untouched, shelf owns scrolling.
#include "../src/SearchShelf.cpp"
#include <cassert>
#include <iostream>
static int moves=0, redrawSuspensions=0;
static int nativeHeight=420;
static int nativeCollapseCommands=0;
static LRESULT CALLBACK nativeHost(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(m==WM_COMMAND && LOWORD(wp)==1640) {
        ++nativeCollapseCommands;
        nativeHeight=nativeHeight==160?420:160;
        SetWindowTextW(GetDlgItem(w,1640),nativeHeight==420?L"˄":L"˅");
        SetWindowPos(w,nullptr,0,0,700,nativeHeight,SWP_NOMOVE|SWP_NOZORDER);
        return 0;
    }
    if(m==WM_GETMINMAXINFO) { auto* p=(MINMAXINFO*)lp; p->ptMinTrackSize={600,nativeHeight};p->ptMaxTrackSize={2000,nativeHeight};return 0; }
    if(m==WM_NCHITTEST) return HTBORDER; // reproduce native vertical-size lock
    if(m==WM_SETREDRAW && !wp) ++redrawSuspensions;
    return DefSubclassProc(w,m,wp,lp);
}
static LRESULT CALLBACK countMoves(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(m==WM_WINDOWPOSCHANGING) ++moves;
    return DefSubclassProc(w,m,wp,lp);
}
int main() {
    module=GetModuleHandleW(nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES}; InitCommonControlsEx(&controls);
    HWND w=CreateWindowExW(0,L"#32770",L"SearchShelf regression host",WS_OVERLAPPEDWINDOW,0,0,700,420,nullptr,nullptr,module,nullptr);
    assert(w); SetWindowSubclass(w,nativeHost,3,0);
    CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWN,110,30,350,200,w,(HMENU)1601,module,nullptr);
    HWND below=CreateWindowExW(0,L"BUTTON",L"Normal",WS_CHILD,110,120,100,24,w,(HMENU)1625,module,nullptr);
    HFONT nativeFont=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(below,WM_SETFONT,(WPARAM)nativeFont,FALSE);
    CreateWindowExW(0,L"BUTTON",L"Regex",WS_CHILD,110,150,100,24,w,(HMENU)1605,module,nullptr);
    HWND collapse=CreateWindowExW(0,L"BUTTON",L"^",WS_CHILD,630,330,24,24,w,(HMENU)1640,module,nullptr);
    auto original=rect(below,w), originalCollapse=rect(collapse,w);
    // Reproduce a session persisted in native compact mode by the old version.
    nativeHeight=160;SetWindowTextW(collapse,L"˅");
    SetWindowPos(w,nullptr,0,0,700,nativeHeight,SWP_NOMOVE|SWP_NOZORDER);
    assert(discover(w,0)==FALSE); assert(panel && choices);
    assert(nativeHeight==420 && nativeCollapseCommands==1); nativeCollapseCommands=0;
    assert(!GetDlgItem(panel,16) && !GetDlgItem(panel,21));
    assert((HFONT)SendMessageW(choices,WM_GETFONT,0,0)==nativeFont);
    assert((HFONT)SendMessageW(ListView_GetHeader(choices),WM_GETFONT,0,0)==nativeFont);
    RECT panelRect{};GetClientRect(panel,&panelRect); auto listRect=rect(choices,panel);
    assert(listRect.left==0 && listRect.right==panelRect.right);
    auto initial=rect(below,w); assert(EqualRect(&initial,&original));
    auto shelf=rect(panel,w); assert(shelf.top>=originalCollapse.bottom);
    for(HWND child=GetWindow(w,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) if(child!=panel) SetWindowSubclass(child,countMoves,2,0);
    NMHDR notification{below,1625,NM_CUSTOMDRAW}; moves=0;
    for(int i=0;i<1000;i++) {
        SendMessageW(w,WM_NOTIFY,1625,(LPARAM)&notification);
        SendMessageW(w,WM_COMMAND,MAKEWPARAM(1601,CBN_EDITCHANGE),0);
        SendMessageW(w,WM_COMMAND,MAKEWPARAM(1601,CBN_SETFOCUS),0);
        SendMessageW(w,WM_COMMAND,MAKEWPARAM(1601,CBN_DROPDOWN),0);
        SendMessageW(w,WM_HSCROLL,SB_THUMBTRACK,0);
        SendMessageW(w,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
        SendMessageW(w,WM_MOVE,0,MAKELPARAM(i,i));
    }
    assert(moves==0);
    for(int i=0;i<30;i++) {
        SendMessageW(w,WM_SIZE,SIZE_RESTORED,MAKELPARAM(700+i,420));
        ShowWindow(w,SW_HIDE); ShowWindow(w,SW_SHOWNOACTIVATE);
        auto current=rect(below,w); assert(EqualRect(&initial,&current));
        auto button=rect(collapse,w); assert(EqualRect(&originalCollapse,&button));
    }
    assert(moves==0 && redrawSuspensions==0 && !inside);
    RECT window{};GetWindowRect(w,&window); int expanded=window.bottom-window.top;
    assert(expanded==nativeHeight+shelfHeight());
    for(int i=0;i<10;i++) SetWindowPos(w,nullptr,0,0,700,nativeHeight,SWP_NOMOVE|SWP_NOZORDER);
    GetWindowRect(w,&window);assert(window.bottom-window.top==nativeHeight+minimumShelfHeight());
    auto smallList=rect(choices,panel);
    SetWindowPos(w,nullptr,100,100,700,expanded+200,SWP_NOZORDER);
    GetWindowRect(w,&window);assert(window.bottom-window.top==expanded+200);
    auto tallList=rect(choices,panel);
    assert(tallList.bottom-tallList.top>smallList.bottom-smallList.top);
    expanded=window.bottom-window.top;
    MINMAXINFO limits{};SendMessageW(w,WM_GETMINMAXINFO,0,(LPARAM)&limits);
    assert(limits.ptMaxTrackSize.y>limits.ptMinTrackSize.y);
    int middle=(window.left+window.right)/2;
    for(auto point:{POINT{middle,window.top+1},POINT{middle,window.bottom-1},
                    POINT{window.left+1,window.top+1},POINT{window.right-1,window.bottom-1}}) {
        LPARAM location=MAKELPARAM(point.x,point.y);
        auto expected=DefWindowProcW(w,WM_NCHITTEST,0,location);
        assert(expected==HTTOP || expected==HTBOTTOM || expected==HTTOPLEFT || expected==HTBOTTOMRIGHT);
        assert(SendMessageW(w,WM_NCHITTEST,0,location)==expected);
    }
    for(int i=0;i<20;i++) {
        SendMessageW(w,WM_COMMAND,MAKEWPARAM(1640,BN_CLICKED),(LPARAM)collapse);
        assert(!shelfExpanded && !(GetWindowLongPtrW(panel,GWL_STYLE)&WS_VISIBLE));
        GetWindowRect(w,&window);assert(window.bottom-window.top==nativeHeight);
        ShowWindow(w,SW_HIDE);ShowWindow(w,SW_SHOWNOACTIVATE);
        assert(!shelfExpanded && !(GetWindowLongPtrW(panel,GWL_STYLE)&WS_VISIBLE));
        auto option=rect(below,w), button=rect(collapse,w);
        assert(EqualRect(&option,&initial) && EqualRect(&button,&originalCollapse));
        SendMessageW(w,WM_COMMAND,MAKEWPARAM(1640,BN_CLICKED),(LPARAM)collapse);
        assert(shelfExpanded && (GetWindowLongPtrW(panel,GWL_STYLE)&WS_VISIBLE));
        GetWindowRect(w,&window);assert(window.bottom-window.top==expanded);
        auto shelf=rect(panel,w); assert(shelf.top>=originalCollapse.bottom);
    }
    assert(nativeCollapseCommands==0 && moves==0);
    entries.clear();for(int i=0;i<250;i++) entries.push_back({L"keyword-"+std::to_wstring(i)+std::wstring(500,L'x'),L"description"});
    refresh();assert(ListView_GetItemCount(choices)==250);
    SetWindowTextW(GetDlgItem(w,FIND),L"keep search"); SetWindowTextW(description,L"keep description");
    ListView_SetItemState(choices,230,LVIS_SELECTED,LVIS_SELECTED);ListView_EnsureVisible(choices,230,FALSE);
    assert(ListView_GetTopIndex(choices)>0);
    assert(text(GetDlgItem(w,FIND))==L"keep search" && text(description)==L"keep description");
    assert(!(GetWindowLongPtrW(choices,GWL_STYLE)&LVS_SINGLESEL));
    NMITEMACTIVATE activate{};activate.hdr={choices,10,NM_DBLCLK};activate.iItem=12;
    SendMessageW(panel,WM_NOTIFY,10,(LPARAM)&activate);
    assert(text(GetDlgItem(w,FIND))==entries[12].keyword && text(description)==entries[12].description);
    activate.iItem=-1; SendMessageW(panel,WM_NOTIFY,10,(LPARAM)&activate);
    assert(text(GetDlgItem(w,FIND))==entries[12].keyword);
    ListView_SetItemState(choices,-1,0,LVIS_SELECTED);
    for(int row:{1,3,4}) ListView_SetItemState(choices,row,LVIS_SELECTED,LVIS_SELECTED);
    auto selection=selectedRows();assert((selection==std::vector<int>{1,3,4}));
    auto before=entries;
    wchar_t file[MAX_PATH];GetFullPathNameW(L"layout-delete-test.ini",MAX_PATH,file,nullptr);config=file;
    assert(save(config,entries));
    auto validConfig=config;config+=L"\\missing.ini";
    assert(!deleteRows(selection));assert(entries.size()==before.size() && selectedRows()==selection);
    config=validConfig;assert(deleteRows(selection));assert(entries.size()==247);
    assert(entries[0].keyword==before[0].keyword && entries[1].keyword==before[2].keyword && entries[2].keyword==before[5].keyword);
    std::vector<Entry> persisted;assert(load(config,persisted) && persisted.size()==247);
    for(size_t i=0;i<entries.size();++i) assert(persisted[i].keyword==entries[i].keyword);
    assert(selectedRows().empty() && !deleteRows({}));DeleteFileW(config.c_str());
    // Visible rows are not storage indices after column filtering.
    entries={{L"hidden-0",L"隐藏"},{L"Dump.ERROR.+",L"网络错误"},
             {L"hidden-2",L"其他"},{L"dump.warn",L"网络警告"},{L"Dump.other",L"磁盘错误"}};
    assert(save(config,entries));refresh();
    SetWindowTextW(GetDlgItem(panel,30),L"DUMP");
    assert((visibleRows==std::vector<int>{1,3,4}));
    SetWindowTextW(GetDlgItem(panel,31),L"网络");
    assert((visibleRows==std::vector<int>{1,3}));
    SetWindowTextW(GetDlgItem(panel,30),L".+");
    assert((visibleRows==std::vector<int>{1})); // literal, not a regex
    SetWindowTextW(GetDlgItem(panel,30),L"no matches");
    assert(visibleRows.empty() && selectedRows().empty() && entries.size()==5);
    assert(load(config,persisted) && persisted.size()==5); // filtering never saves a subset
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(32,BN_CLICKED),0);
    assert(visibleRows.size()==5 && text(GetDlgItem(panel,30)).empty() && text(GetDlgItem(panel,31)).empty());
    SetWindowTextW(GetDlgItem(panel,30),L"dump");SetWindowTextW(GetDlgItem(panel,31),L"网络");
    activate.iItem=1;SendMessageW(panel,WM_NOTIFY,10,(LPARAM)&activate);
    assert(text(GetDlgItem(w,FIND))==L"dump.warn");
    for(int row:{0,1}) ListView_SetItemState(choices,row,LVIS_SELECTED,LVIS_SELECTED);
    assert((selectedRows()==std::vector<int>{1,3}));
    assert(deleteRows(selectedRows()));assert(visibleRows.empty() && entries.size()==3);
    assert(entries[0].keyword==L"hidden-0" && entries[1].keyword==L"hidden-2" && entries[2].keyword==L"Dump.other");
    assert(load(config,persisted) && persisted.size()==3);
    SetWindowTextW(GetDlgItem(panel,31),L"磁盘"); assert((visibleRows==std::vector<int>{2}));
    activate.iItem=0;SendMessageW(panel,WM_NOTIFY,10,(LPARAM)&activate);
    SetWindowTextW(description,L"新描述");SendMessageW(panel,WM_COMMAND,MAKEWPARAM(12,BN_CLICKED),0);
    assert(visibleRows.empty() && entries.size()==3 && entries[2].description==L"新描述");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(32,BN_CLICKED),0);
    assert(visibleRows.size()==3); DeleteFileW(config.c_str());
    auto current=rect(below,w); assert(EqualRect(&initial,&current));
    DestroyWindow(w); assert(!dialog && !panelBrush);
    std::cout<<"PASS: case-insensitive/literal/Chinese column filters and AND combination; empty/reset results; filtered double-click, save and batch deletion preserve hidden rows; resize, collapse, fonts and previous regressions\n";
}
