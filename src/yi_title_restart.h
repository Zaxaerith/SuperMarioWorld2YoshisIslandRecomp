#ifndef YI_TITLE_RESTART_H
#define YI_TITLE_RESTART_H
/* Independent host restart. Prepare suspended; release only after old services close.
 * No emulated memory or save file is written by this helper. */
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
typedef struct YiTitleRestart {PROCESS_INFORMATION process;} YiTitleRestart;
static bool YiTitleRestartPrepare(YiTitleRestart *s,const wchar_t *exe,wchar_t *command) {
 STARTUPINFOW startup={0};startup.cb=sizeof(startup);
 return CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&s->process)!=0;
}
static bool YiTitleRestartReady(YiTitleRestart *s,const char *rom) {
 wchar_t exe[32768],path[32768],command[65536];
 DWORD count=GetModuleFileNameW(NULL,exe,32768);
 if(!count||count>=32768||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,rom,-1,path,32768))return false;
 /* Real Windows filenames cannot contain quotes; the verified .sfc path cannot
  * end in a directory separator. No shell or user command is involved. */
 if(wcschr(path,L'"'))return false;
 int n=swprintf(command,65536,L"\"%ls\" --rom \"%ls\" --return-to-title",exe,path);
 return n>0&&n<65536&&YiTitleRestartPrepare(s,exe,command);
}
static bool YiTitleRestartFinish(YiTitleRestart *s) {
 if(!s->process.hThread)return true;
 bool ok=ResumeThread(s->process.hThread)!=(DWORD)-1;
 if(!ok)TerminateProcess(s->process.hProcess,1);
 CloseHandle(s->process.hThread);CloseHandle(s->process.hProcess);
 s->process=(PROCESS_INFORMATION){0};return ok;
}
#else
typedef struct YiTitleRestart {bool unused;} YiTitleRestart;
static bool YiTitleRestartReady(YiTitleRestart *s,const char *rom){(void)s;(void)rom;return false;}
static bool YiTitleRestartFinish(YiTitleRestart *s){(void)s;return false;}
#endif
#endif
