#include "client.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <cwctype>
#include <mutex>
#include <windows.h>
#include <wincrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "crypt32.lib")
#endif
#include <wincred.h>
#include <winhttp.h>

namespace yira {
namespace {
std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              (int)s.size(), nullptr, 0);
  std::wstring v(n, L'\0');
  if (n)
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(),
                        v.data(), n);
  return v;
}
std::string utf8(const wchar_t *s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
  std::string v(n ? n : 1, '\0');
  if (n)
    WideCharToMultiByte(CP_UTF8, 0, s, -1, v.data(), n, nullptr, nullptr);
  v.resize(n ? n - 1 : 0);
  return v;
}
struct Internet {
  std::atomic<HINTERNET> h{nullptr};
  explicit Internet(HINTERNET v) : h(v) {}
  void cancel() {
    auto v = h.exchange(nullptr);
    if (v)
      WinHttpCloseHandle(v);
  }
  ~Internet() { cancel(); }
  operator HINTERNET() const { return h.load(); }
};

// Stable, per-user fallback when Windows Credential Manager rejects writes.
std::wstring session_file(const std::wstring& target) {
    wchar_t root[32768]; DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",root,32768);
    if(!n || n>=32768)return {};
    std::wstring dir=std::wstring(root)+L"\\YoshisIslandRecomp";
    if(!CreateDirectoryW(dir.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)return {};
    unsigned long long hash=14695981039346656037ull;
    for(auto ch:target){hash^=(unsigned short)ch;hash*=1099511628211ull;}
    return dir+L"\\ra-session-"+std::to_wstring(hash)+L".dat";
}
bool protected_session(const std::wstring& target,Credential& value,bool write) {
    auto path=session_file(target);if(path.empty())return false;
    DATA_BLOB input{},output{};std::string bytes;
    if(write){
        if(value.username.empty()||value.token.empty()||value.username.find('\n')!=std::string::npos)return false;
        bytes=value.username+"\n"+value.token;
        input.pbData=(BYTE*)bytes.data();input.cbData=(DWORD)bytes.size();
        if(!CryptProtectData(&input,L"RetroAchievements session",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))return false;
        auto temp=path+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD count=0;bool ok=file!=INVALID_HANDLE_VALUE;
        if(ok){ok=WriteFile(file,output.pbData,output.cbData,&count,nullptr)&&count==output.cbData;ok=FlushFileBuffers(file)&&ok;CloseHandle(file);}
        if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok)DeleteFileW(temp.c_str());
        SecureZeroMemory(bytes.data(),bytes.size());LocalFree(output.pbData);return ok;
    }
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD size=GetFileSize(file,nullptr),count=0;
    if(!size||size>65536){CloseHandle(file);return false;}
    bytes.resize(size);bool ok=ReadFile(file,bytes.data(),size,&count,nullptr)&&count==size;CloseHandle(file);
    input.pbData=(BYTE*)bytes.data();input.cbData=size;
    if(!ok||!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))return false;
    std::string plain((char*)output.pbData,output.cbData);SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);
    auto split=plain.find('\n');ok=split!=std::string::npos&&split>0&&split+1<plain.size();
    if(ok){value.username=plain.substr(0,split);value.token=plain.substr(split+1);}
    SecureZeroMemory(plain.data(),plain.size());return ok;
}

struct Windows final : Services {
  std::wstring target;
  std::mutex mutex;
  Internet *active = nullptr;
  std::atomic<bool> cancelled{false};
  struct Active {
    Windows &owner;
    Active(Windows &w, Internet &request) : owner(w) {
      std::lock_guard<std::mutex> lock(w.mutex);
      w.active = &request;
      if (w.cancelled)
        request.cancel();
    }
    ~Active() {
      std::lock_guard<std::mutex> lock(owner.mutex);
      owner.active = nullptr;
    }
  };
  explicit Windows(std::wstring t) : target(std::move(t)) {}
  Response http(const std::string &address, const std::string &body) override {
    // Only official HTTPS endpoints. No HTTP, redirects or relaxed TLS flags.
    std::wstring url = wide(address);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS)
      return {};
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    for (auto &c : host)
      c = (wchar_t)towlower(c);
    if (host != L"retroachievements.org" &&
        host != L"static.retroachievements.org" &&
        host != L"media.retroachievements.org")
      return {};
    if (parts.nPort != 443)
      return {};
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
      path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet session(
        WinHttpOpen(L"YoshisIslandRecomp-RA-Test/0.1 Softcore rcheevos/12.5.0",
                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h)
      return {};
    WinHttpSetTimeouts(session, 3000, 4000, 8000, 8000);
    Internet connection(WinHttpConnect(session, host.c_str(), 443, 0));
    if (!connection.h)
      return {};
    Internet request(WinHttpOpenRequest(
        connection, body.empty() ? L"GET" : L"POST", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.h)
      return {};
    Active activity(*this, request);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                          sizeof(redirect)))
      return {};
    const wchar_t *headers =
        body.empty() ? L""
                     : L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (!WinHttpSendRequest(request, headers, (DWORD)-1L,
                            body.empty() ? WINHTTP_NO_REQUEST_DATA
                                         : (void *)body.data(),
                            (DWORD)body.size(), (DWORD)body.size(), 0) ||
        !WinHttpReceiveResponse(request, nullptr))
      return {};
    DWORD status = 0, bytes = sizeof(status);
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX);
    Response response;
    response.status = (int)status;
    for (;;) {
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(request, &available))
        return {};
      if (!available)
        break;
      if (available > 4u * 1024u * 1024u ||
          response.body.size() + available > 4u * 1024u * 1024u)
        return {};
      size_t start = response.body.size();
      response.body.resize(start + available);
      DWORD read = 0;
      if (!WinHttpReadData(request, response.body.data() + start, available,
                           &read))
        return {};
      response.body.resize(start + read);
      if (!read)
        break;
    }
    return response;
  }
  bool restore(Credential &out) override {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
      return protected_session(target,out,false);
    if (credential->UserName && credential->CredentialBlobSize &&
        credential->CredentialBlobSize <= CRED_MAX_CREDENTIAL_BLOB_SIZE) {
      out.username = utf8(credential->UserName);
      out.token.assign((const char *)credential->CredentialBlob,
                       credential->CredentialBlobSize);
    }
    CredFree(credential);
    return !out.username.empty() && !out.token.empty();
  }
  bool store(const Credential &value) override {
    if (value.username.empty() || value.token.empty() ||
        value.token.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
      return false;
    auto name = wide(value.username);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = (LPWSTR)target.c_str();
    credential.UserName = (LPWSTR)name.c_str();
    credential.CredentialBlobSize = (DWORD)value.token.size();
    credential.CredentialBlob = (LPBYTE)value.token.data();
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if(CredWriteW(&credential,0)){DeleteFileW(session_file(target).c_str());return true;}
    auto copy=value;bool ok=protected_session(target,copy,true);SecureZeroMemory(copy.token.data(),copy.token.size());return ok;
  }
  void forget() override { CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);DeleteFileW(session_file(target).c_str()); }
  void cancel() override {
    cancelled = true;
    std::lock_guard<std::mutex> lock(mutex);
    if (active)
      active->cancel();
  }
};
} // namespace
std::unique_ptr<Services> windows_services(const std::wstring &target) {
  return std::make_unique<Windows>(target);
}
} // namespace yira
#else
namespace yira {
std::unique_ptr<Services> windows_services(const std::wstring &) { return {}; }
} // namespace yira
#endif
