#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <string>
#include <vector>
#include <algorithm>

// ABI and resource IDs documented by the Notepad++ official plugin interface.
struct NppData { HWND npp, mainView, secondView; };
struct ShortcutKey { bool ctrl, alt, shift; UCHAR key; };
struct FuncItem { wchar_t name[64]; void (__cdecl *fn)(); int id; bool checked; ShortcutKey* key; };
struct Entry { std::wstring keyword, description; };
static HINSTANCE module;
static NppData host{};
static HWND dialog, panel, choices, description;
static HBRUSH panelBrush;
static COLORREF panelColor, labelColor;
static UINT_PTR timerId;
static std::wstring config;
static std::vector<Entry> entries;
static std::vector<int> visibleRows;
static bool inside;
static bool shelfExpanded=true;
static int nativeMinimumHeight=0, expandedShelfPixels=0;
static int dpiValue=96;
static int scale(int value) { return MulDiv(value,dpiValue,96); }
static int shelfHeight() { return shelfExpanded?expandedShelfPixels:0; }
static int minimumShelfHeight() { return shelfExpanded?scale(160):0; }
static constexpr int FIND = 1601;
static std::wstring encode(const std::wstring& s) {
    std::wstring r=L"v:";
    for(wchar_t c:s) { switch(c) {case L'\\':r+=L"\\\\";break;case L' ':r+=L"\\s";break;case L'\t':r+=L"\\t";break;case L'\r':r+=L"\\r";break;case L'\n':r+=L"\\n";break;default:r+=c;} } return r;
}
static bool decode(const std::wstring& s,std::wstring& r) {
    if(s.substr(0,2)!=L"v:") return false;
    r.clear(); for(size_t i=2;i<s.size();i++) { wchar_t c=s[i]; if(c==L'\\') {if(++i==s.size()) return false; switch(s[i]) {case L'\\':c=L'\\';break;case L's':c=L' ';break;case L't':c=L'\t';break;case L'r':c=L'\r';break;case L'n':c=L'\n';break;default:return false;}} r+=c;} return true;
}
static std::wstring text(HWND w) { int n=GetWindowTextLengthW(w); std::wstring s(n+1,L'\0'); GetWindowTextW(w,s.data(),n+1); s.resize(n); return s; }
static RECT rect(HWND w, HWND parent) { RECT r{}; GetWindowRect(w,&r); MapWindowPoints(nullptr,parent,(POINT*)&r,2); return r; }
static void syncAppearance() {
    if(!IsWindow(panel)) return;
    // Use the native option label font, not the independently configurable
    // search-input font. List views and their headers need it explicitly.
    HFONT font=(HFONT)SendMessageW(GetDlgItem(dialog,1625),WM_GETFONT,0,0);
    if(!font) font=(HFONT)SendMessageW(dialog,WM_GETFONT,0,0);
    if(!font) font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for(HWND child=GetWindow(panel,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
        SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE);
    SendMessageW(ListView_GetHeader(choices),WM_SETFONT,(WPARAM)font,TRUE);

    panelColor=GetSysColor(COLOR_BTNFACE); labelColor=GetSysColor(COLOR_BTNTEXT);
    HDC dc=GetDC(dialog);
    bool dark=SendMessageW(host.npp,WM_USER+1000+107,0,0)!=FALSE;
    if(dark) {
        SetBkColor(dc,panelColor); SetTextColor(dc,labelColor);
        HBRUSH brush=(HBRUSH)SendMessageW(dialog,WM_CTLCOLORDLG,(WPARAM)dc,(LPARAM)dialog);
        LOGBRUSH info{};
        if(brush && GetObjectW(brush,sizeof(info),&info) && info.lbStyle==BS_SOLID) panelColor=info.lbColor;
        else panelColor=GetBkColor(dc);
        labelColor=GetTextColor(dc);
    } else if(HTHEME theme=OpenThemeData(dialog,L"TAB")) {
        // The search controls live on a themed tab body, whose background can
        // differ from COLOR_BTNFACE. Obtain that theme's actual rendered fill.
        HDC memory=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,16,16);
        if(memory && bitmap) {
            HGDIOBJ old=SelectObject(memory,bitmap); RECT sample{0,0,16,16};
            if(SUCCEEDED(DrawThemeBackground(theme,memory,TABP_BODY,0,&sample,nullptr))) {
                COLORREF color=GetPixel(memory,8,8); if(color!=CLR_INVALID) panelColor=color;
            }
            SelectObject(memory,old);
        }
        if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory);
        CloseThemeData(theme);
    }
    if(panelBrush) DeleteObject(panelBrush);
    panelBrush=CreateSolidBrush(panelColor);
    // Match the native input surface while keeping the surrounding tab fill.
    SetBkColor(dc,GetSysColor(COLOR_WINDOW)); SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
    if(dark) SendMessageW(dialog,WM_CTLCOLOREDIT,(WPARAM)dc,(LPARAM)GetDlgItem(dialog,FIND));
    ListView_SetBkColor(choices,GetBkColor(dc));
    ListView_SetTextBkColor(choices,GetBkColor(dc));
    ListView_SetTextColor(choices,GetTextColor(dc));
    ReleaseDC(dialog,dc);
    RedrawWindow(panel,nullptr,nullptr,RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN);
}
static void error(const wchar_t* s) { MessageBoxW(dialog?dialog:host.npp,s,L"搜索收藏",MB_OK|MB_ICONERROR); }
static std::wstring read(const std::wstring& path,const wchar_t* section,const wchar_t* key) { std::vector<wchar_t> b(32768); GetPrivateProfileStringW(section,key,L"",b.data(),(DWORD)b.size(),path.c_str()); return b.data(); }
static bool load(const std::wstring& path,std::vector<Entry>& out) {
    if(read(path,L"SearchShelf",L"version")!=L"1") return false;
    auto count=read(path,L"SearchShelf",L"count"); wchar_t* end=nullptr; long n=wcstol(count.c_str(),&end,10);
    if(count.empty() || *end || n<0 || n>1000) return false;
    std::vector<Entry> result;
    for(int i=0;i<n;i++) { auto section=L"Item"+std::to_wstring(i); Entry e; if(!decode(read(path,section.c_str(),L"keyword"),e.keyword) || !decode(read(path,section.c_str(),L"description"),e.description) || e.keyword.empty() || e.keyword.size()>4096 || e.description.size()>512) return false; result.push_back(e); }
    out=std::move(result); return true;
}
static bool save(const std::wstring& path,const std::vector<Entry>& data) {
    auto tmp=path+L".tmp";
    HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    WORD bom=0xfeff; DWORD wrote; bool ok=WriteFile(f,&bom,2,&wrote,nullptr)!=0; CloseHandle(f);
    auto put=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value) { if(!WritePrivateProfileStringW(section,key,value.c_str(),tmp.c_str())) ok=false; };
    put(L"SearchShelf",L"version",L"1"); put(L"SearchShelf",L"count",std::to_wstring(data.size()));
    for(size_t i=0;i<data.size();i++) { auto section=L"Item"+std::to_wstring(i); put(section.c_str(),L"keyword",encode(data[i].keyword)); put(section.c_str(),L"description",encode(data[i].description)); }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,tmp.c_str());
    if(ok) ok=MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok) DeleteFileW(tmp.c_str()); return ok;
}
static bool contains(const std::wstring& value,const std::wstring& query) {
    return query.empty() || FindStringOrdinal(FIND_FROMSTART,value.c_str(),(int)value.size(),query.c_str(),(int)query.size(),TRUE)>=0;
}
static int sourceRow(int row) {
    return row>=0 && row<(int)visibleRows.size()?visibleRows[row]:-1;
}
static void refresh(int selected=-1) {
    auto keywordFilter=text(GetDlgItem(panel,30)), descriptionFilter=text(GetDlgItem(panel,31));
    ListView_DeleteAllItems(choices);
    visibleRows.clear();
    int selectedVisible=-1;
    for(size_t i=0;i<entries.size();++i) {
        if(!contains(entries[i].keyword,keywordFilter) || !contains(entries[i].description,descriptionFilter)) continue;
        int row=(int)visibleRows.size(); visibleRows.push_back((int)i);
        if((int)i==selected) selectedVisible=row;
        LVITEMW item{}; item.mask=LVIF_TEXT; item.iItem=row; item.pszText=entries[i].keyword.data();
        ListView_InsertItem(choices,&item);
        ListView_SetItemText(choices,row,1,entries[i].description.data());
    }
    if(selectedVisible>=0) { ListView_SetItemState(choices,selectedVisible,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED); ListView_EnsureVisible(choices,selectedVisible,FALSE); }
    auto count=L"显示 "+std::to_wstring(visibleRows.size())+L" / "+std::to_wstring(entries.size());
    SetWindowTextW(GetDlgItem(panel,23),count.c_str());
}
static std::vector<int> selectedRows() {
    std::vector<int> selected;
    for(int row=ListView_GetNextItem(choices,-1,LVNI_SELECTED); row>=0;
        row=ListView_GetNextItem(choices,row,LVNI_SELECTED)) {
        int source=sourceRow(row); if(source>=0) selected.push_back(source);
    }
    std::sort(selected.begin(),selected.end());
    return selected;
}
static bool deleteRows(const std::vector<int>& selected) {
    if(selected.empty()) return false;
    std::vector<Entry> updated;
    for(size_t i=0;i<entries.size();++i)
        if(!std::binary_search(selected.begin(),selected.end(),(int)i)) updated.push_back(entries[i]);
    // Commit once for the entire batch; failed writes preserve data/selection.
    if(!save(config,updated)) return false;
    entries=std::move(updated);
    refresh();
    SetWindowTextW(description,L"");
    return true;
}
static void transfer(bool importing) {
    wchar_t path[MAX_PATH]=L"SearchShelf.ini";
    OPENFILENAMEW ofn{sizeof(ofn)}; ofn.hwndOwner=dialog?dialog:host.npp; ofn.lpstrFilter=L"搜索收藏 (*.ini)\0*.ini\0\0"; ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH; ofn.lpstrDefExt=L"ini"; ofn.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(importing?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT);
    if(!(importing?GetOpenFileNameW(&ofn):GetSaveFileNameW(&ofn))) return;
    if(!importing) { if(!save(path,entries)) error(L"导出失败，请检查文件写入权限。"); return; }
    std::vector<Entry> incoming;
    if(!load(path,incoming)) { error(L"配置格式不正确，未更改现有收藏。"); return; }
    auto merged=entries;
    for(auto& e:incoming) if(std::none_of(merged.begin(),merged.end(),[&](auto& x){return x.keyword==e.keyword;})) merged.push_back(e);
    if(merged.size()>1000) { error(L"最多支持 1000 条收藏。"); return; }
    if(!save(config,merged)) { error(L"保存失败，未更改现有收藏。"); return; }
    entries=std::move(merged); refresh();
}
static LRESULT CALLBACK panelProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_ERASEBKGND && panelBrush) {
        RECT client{}; GetClientRect(w,&client); FillRect((HDC)wp,&client,panelBrush); return TRUE;
    }
    if(m==WM_CTLCOLORSTATIC && panelBrush) {
        SetBkColor((HDC)wp,panelColor); SetTextColor((HDC)wp,labelColor); return (LRESULT)panelBrush;
    }
    if(m==WM_CTLCOLOREDIT || m==WM_CTLCOLORBTN || m==WM_CTLCOLORLISTBOX)
        return SendMessageW(dialog,m,wp,lp);
    if(m==WM_NOTIFY && lp) {
        auto* notification=reinterpret_cast<NMHDR*>(lp);
        if(notification->hwndFrom==choices && notification->code==NM_DBLCLK) {
            // Use the double-clicked row, not the first row in a multi-selection.
            int selected=sourceRow(reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem);
            if(selected>=0 && selected<(int)entries.size()) {
                SetWindowTextW(GetDlgItem(dialog,FIND),entries[selected].keyword.c_str());
                SetWindowTextW(description,entries[selected].description.c_str());
                SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(FIND,CBN_EDITCHANGE),(LPARAM)GetDlgItem(dialog,FIND));
            }
        }
        return 0;
    }
    if(m==WM_COMMAND) {
        int id=LOWORD(wp);
        if((id==30 || id==31) && HIWORD(wp)==EN_CHANGE) { refresh(); return 0; }
        if(id==32 && HIWORD(wp)==BN_CLICKED) {
            SetWindowTextW(GetDlgItem(w,30),L""); SetWindowTextW(GetDlgItem(w,31),L"");
            refresh(); return 0;
        }
        if(id==12) {
            Entry e{text(GetDlgItem(dialog,FIND)),text(description)};
            if(e.keyword.empty()) { error(L"请先在原搜索框输入关键词。"); return 0; }
            if(e.keyword.size()>4096) { error(L"关键词最多支持 4096 字。"); return 0; }
            auto updated=entries; auto it=std::find_if(updated.begin(),updated.end(),[&](auto& x){return x.keyword==e.keyword;});
            int index=(int)(it-updated.begin()); if(it!=updated.end()) *it=e; else { if(updated.size()>=1000) {error(L"最多支持 1000 条收藏。"); return 0;} updated.push_back(e); }
            if(save(config,updated)) { entries=std::move(updated); refresh(index); } else error(L"保存失败，请检查配置目录权限。");
        }
        if(id==13) {
            auto selected=selectedRows();
            if(!selected.empty()) {
                auto prompt=L"确定删除选中的 "+std::to_wstring(selected.size())+L" 条收藏？";
                if(MessageBoxW(dialog,prompt.c_str(),L"搜索收藏",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES && !deleteRows(selected)) error(L"删除失败，原收藏已保留。");
            }
        }
        if(id==14) transfer(true); if(id==15) transfer(false);
        return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
static void position(HWND child, int x, int y, int width=0, int height=0, bool resize=false) {
    if(!IsWindow(child)) return;
    auto current=rect(child,GetParent(child));
    if(current.left==x && current.top==y && (!resize ||
        (current.right-current.left==width && current.bottom-current.top==height))) return;
    SetWindowPos(child,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE|(resize?0:SWP_NOSIZE));
}
static void layout() {
    if(!shelfExpanded) return;
    RECT client{}; GetClientRect(dialog,&client);
    int statusHeight=0;
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        wchar_t cls[64]{}; GetClassNameW(child,cls,64);
        if(!wcscmp(cls,STATUSCLASSNAMEW)) { auto r=rect(child,dialog); statusHeight=r.bottom-r.top; }
    }
    // Reserve an added strip below the original dialog. Never reposition native
    // group boxes, selection controls, transparency slider or collapse button.
    RECT window{};GetWindowRect(dialog,&window);
    int nonClient=window.bottom-window.top-client.bottom;
    int top=nativeMinimumHeight-nonClient-statusHeight;
    int width=client.right-scale(16), height=client.bottom-statusHeight-top;
    expandedShelfPixels=height;
    position(panel,scale(8),top,width,height,true);
    int gap=scale(8), header=scale(24), line=scale(26), listTop=header+line+scale(4);
    int body=std::max(scale(40),height-listTop-line-gap-scale(14));
    position(GetDlgItem(panel,20),0,0,width-scale(200),header,true);
    position(GetDlgItem(panel,23),width-scale(196),0,scale(108),header,true);
    position(GetDlgItem(panel,32),width-scale(80),0,scale(80),scale(22),true);
    int split=width*65/100;
    position(GetDlgItem(panel,30),0,header,split-gap/2,line,true);
    position(GetDlgItem(panel,31),split+gap/2,header,width-split-gap/2,line,true);
    position(choices,0,listTop,width,body,true);
    int bottom=listTop+body+gap, button=scale(52), buttons=4*(button+gap);
    position(GetDlgItem(panel,22),0,bottom,scale(36),line,true);
    position(description,scale(40),bottom,std::max(scale(60),width-scale(40)-buttons),line,true);
    for(int i=0;i<4;i++) position(GetDlgItem(panel,12+i),width-buttons+gap+i*(button+gap),bottom,button,line,true);
    int usable=width-GetSystemMetrics(SM_CXVSCROLL)-scale(4);
    ListView_SetColumnWidth(choices,0,std::max(scale(180),usable*65/100));
    ListView_SetColumnWidth(choices,1,std::max(scale(140),usable-usable*65/100));
}
static bool needsLayout(UINT m, WPARAM wp, LPARAM lp) {
    if(m==WM_SIZE) return wp!=SIZE_MINIMIZED;
    if(m==WM_DPICHANGED) return true;
    if(m==WM_SHOWWINDOW) return wp!=FALSE;
    if(m==WM_COMMAND) return HIWORD(wp)==BN_CLICKED &&
        LOWORD(wp)==1724; // native two-button mode (shelf toggle is intercepted)
    if(m==WM_NOTIFY && lp) {
        const auto* notification=reinterpret_cast<const NMHDR*>(lp);
        return notification->code==TCN_SELCHANGE;
    }
    // In particular, NM_CUSTOMDRAW must never initiate another layout/paint.
    return false;
}
static LRESULT CALLBACK dialogProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(m==WM_NCDESTROY) {
        RemoveWindowSubclass(w,dialogProc,1); dialog=panel=choices=description=nullptr; visibleRows.clear();
        if(panelBrush) { DeleteObject(panelBrush); panelBrush=nullptr; }
        return DefSubclassProc(w,m,wp,lp);
    }
    if(m==WM_COMMAND && LOWORD(wp)==1640 && HIWORD(wp)==BN_CLICKED && IsWindow(panel)) {
        // Consume this command: the native compact mode clips/rearranges search
        // controls, whereas this button now toggles only the added shelf.
        RECT r{};GetWindowRect(w,&r);
        int oldHeight=shelfExpanded?(r.bottom-r.top-nativeMinimumHeight):0;
        if(shelfExpanded && IsChild(panel,GetFocus())) SetFocus(GetDlgItem(w,FIND));
        shelfExpanded=!shelfExpanded;
        ShowWindow(panel,shelfExpanded?SW_SHOWNA:SW_HIDE);
        SetWindowTextW(GetDlgItem(w,1640),shelfExpanded?L"˄":L"˅");
        SetWindowPos(w,nullptr,0,0,r.right-r.left,r.bottom-r.top+shelfHeight()-oldHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        if(shelfExpanded) layout();
        RedrawWindow(w,nullptr,nullptr,RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
        return TRUE;
    }
    if(m==WM_GETMINMAXINFO) {
        auto result=DefSubclassProc(w,m,wp,lp);
        auto* limits=reinterpret_cast<MINMAXINFO*>(lp);
        nativeMinimumHeight=limits->ptMinTrackSize.y;
        limits->ptMinTrackSize.y+=minimumShelfHeight();
        limits->ptMaxTrackSize.y=std::max(limits->ptMinTrackSize.y,(LONG)GetSystemMetrics(SM_CYMAXTRACK));
        return result;
    }
    if(m==WM_NCHITTEST) {
        // The native dialog deliberately converts top/bottom sizing hits into
        // horizontal borders. Restore standard Win32 edge/corner resizing.
        auto hit=DefWindowProcW(w,m,wp,lp);
        if(hit==HTTOP || hit==HTBOTTOM || hit==HTTOPLEFT || hit==HTTOPRIGHT ||
           hit==HTBOTTOMLEFT || hit==HTBOTTOMRIGHT) return hit;
    }
    if(m==WM_WINDOWPOSCHANGING && lp) {
        auto* pos=reinterpret_cast<WINDOWPOS*>(lp);
        if(!(pos->flags&SWP_NOSIZE)) {
            MINMAXINFO limits{}; SendMessageW(w,WM_GETMINMAXINFO,0,(LPARAM)&limits);
            // Enforce only a safe minimum, preserving the requested height.
            pos->cy=std::max(pos->cy,(int)limits.ptMinTrackSize.y);
        }
    }
    if(m==WM_DPICHANGED && HIWORD(wp)) dpiValue=HIWORD(wp);
    bool adapt=!inside && needsLayout(m,wp,lp);
    if(adapt) inside=true;
    auto result=DefSubclassProc(w,m,wp,lp);
    if((m==WM_DPICHANGED || m==WM_THEMECHANGED || m==WM_SYSCOLORCHANGE || m==WM_SETFONT ||
        (m==WM_SHOWWINDOW && wp)) && IsWindow(panel)) syncAppearance();
    if(adapt) {
        if(IsWindow(panel)) layout();
        inside=false;
        if(m==WM_SHOWWINDOW && wp && IsWindow(w)) RedrawWindow(w,nullptr,nullptr,RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
    }
    return result;
}
static BOOL CALLBACK discover(HWND w,LPARAM) {
    wchar_t cls[32]; GetClassNameW(w,cls,32);
    if(w==host.npp || wcscmp(cls,L"#32770") || !GetDlgItem(w,FIND) || !GetDlgItem(w,1625) || !GetDlgItem(w,1605)) return TRUE;
    // An older plugin/session may have persisted native compact mode. Restore
    // the native full layout once, before installing our command interceptor.
    HWND toggle=GetDlgItem(w,1640);
    auto arrow=text(toggle);
    if(arrow==L"˅" || arrow==L"∨" || arrow==L"v" || arrow==L"V")
        SendMessageW(w,WM_COMMAND,MAKEWPARAM(1640,BN_CLICKED),(LPARAM)toggle);
    shelfExpanded=true;
    dialog=w;
    using GetDpi=UINT(WINAPI*)(HWND); auto dpi=(GetDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    dpiValue=dpi?dpi(w):96;
    expandedShelfPixels=scale(208);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW wc{}; wc.hInstance=module; wc.lpfnWndProc=panelProc; wc.lpszClassName=L"SearchShelfPanel"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); RegisterClassW(&wc);
    panel=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,w,nullptr,module,nullptr);
    auto make=[&](const wchar_t* cls,const wchar_t* title,DWORD style,int id) { HWND c=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,1,1,panel,(HMENU)(INT_PTR)id,module,nullptr); SendMessageW(c,WM_SETFONT,SendMessageW(GetDlgItem(w,FIND),WM_GETFONT,0,0),TRUE); return c; };
    choices=make(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS|WS_BORDER|WS_VSCROLL|WS_HSCROLL,10);
    ListView_SetExtendedListViewStyle(choices,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW column{}; column.mask=LVCF_TEXT|LVCF_WIDTH; column.cx=scale(180); column.pszText=(LPWSTR)L"关键词"; ListView_InsertColumn(choices,0,&column);
    column.pszText=(LPWSTR)L"描述"; ListView_InsertColumn(choices,1,&column);
    make(L"STATIC",L"关键词收藏（双击填入；Ctrl / Shift 多选删除）",SS_LEFT,20);
    make(L"STATIC",L"",SS_LEFT,23);
    make(L"BUTTON",L"清除筛选",BS_PUSHBUTTON,32);
    HWND keywordFilter=make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,30);
    HWND descriptionFilter=make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,31);
    SendMessageW(keywordFilter,EM_SETCUEBANNER,TRUE,(LPARAM)L"筛选关键词：输入部分内容");
    SendMessageW(descriptionFilter,EM_SETCUEBANNER,TRUE,(LPARAM)L"筛选描述：输入部分内容");
    SendMessageW(keywordFilter,EM_SETLIMITTEXT,4096,0); SendMessageW(descriptionFilter,EM_SETLIMITTEXT,512,0);
    make(L"STATIC",L"描述",SS_LEFT,22);
    description=make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,11); SendMessageW(description,EM_SETCUEBANNER,0,(LPARAM)L"收藏描述"); SendMessageW(description,EM_SETLIMITTEXT,512,0);
    make(L"BUTTON",L"保存",BS_PUSHBUTTON,12); make(L"BUTTON",L"删除",BS_PUSHBUTTON,13); make(L"BUTTON",L"导入",BS_PUSHBUTTON,14); make(L"BUTTON",L"导出",BS_PUSHBUTTON,15);
    SendMessageW(host.npp,WM_USER+1000+112,0xB,(LPARAM)panel);
    syncAppearance();
    SetWindowSubclass(w,dialogProc,1,0);
    SetWindowTextW(toggle,L"˄");
    RECT r{}; GetWindowRect(w,&r); SetWindowPos(w,nullptr,0,0,r.right-r.left,r.bottom-r.top+shelfHeight(),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(panel,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    inside=true;layout();inside=false;refresh();
    return FALSE;
}
static void CALLBACK tick(HWND,UINT,UINT_PTR,DWORD) { if(!IsWindow(dialog)) EnumThreadWindows(GetWindowThreadProcessId(host.npp,nullptr),discover,0); }
static void importCommand() {transfer(true);} static void exportCommand() {transfer(false);}
static FuncItem commands[]={{L"导入搜索收藏…",importCommand,0,false,nullptr},{L"导出搜索收藏…",exportCommand,0,false,nullptr}};
extern "C" __declspec(dllexport) BOOL isUnicode() {return TRUE;}
extern "C" __declspec(dllexport) const wchar_t* getName() {return L"SearchShelf 搜索收藏";}
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* n) {*n=2;return commands;}
extern "C" __declspec(dllexport) void setInfo(NppData data) {host=data;}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT,WPARAM,LPARAM) {return TRUE;}
extern "C" __declspec(dllexport) void beNotified(void* notification) {
    auto hdr=(NMHDR*)notification;
    if(hdr->code==1027 && IsWindow(panel)) { SendMessageW(host.npp,WM_USER+1000+112,0xC,(LPARAM)panel); syncAppearance(); }
    if(hdr->code==1001) {
        wchar_t path[32768]{}; SendMessageW(host.npp,WM_USER+1000+46,32768,(LPARAM)path);
        if(!*path) {error(L"无法取得插件配置目录。");return;}
        CreateDirectoryW(path,nullptr); config=std::wstring(path)+L"\\SearchShelf.ini";
        if(GetFileAttributesW(config.c_str())!=INVALID_FILE_ATTRIBUTES && !load(config,entries)) {error(L"已有配置损坏，插件未启用。请备份并修复 SearchShelf.ini 后重启。"); return;}
        timerId=SetTimer(nullptr,0,150,tick);
    }
    if(hdr->code==1009 && timerId) {KillTimer(nullptr,timerId);timerId=0;}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {if(reason==DLL_PROCESS_ATTACH) {module=h;DisableThreadLibraryCalls(h);} return TRUE;}
