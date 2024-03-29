#include "application.h"

const std::wstring SECTION_SERVER = L"Server";

//
// public
//
application::application()
	:appContext()
{
}
application::~application()
{
	::CoUninitialize();

	help->writeLog(logId::info, L"End of application.");
	help->release();
}
bool application::initialize(HINSTANCE instance)
{
	// 프로그램 이름
	DWORD size = MAX_PATH;
	std::wstring programName;
	programName.resize(size);
	::GetModuleFileNameW(nullptr, const_cast<wchar_t*>(programName.data()), size);
	programName = programName.substr(programName.rfind('\\') + 1);	// userAction_client.exe

	// 현재 실행중이라면 종료
	if (isAlreadyRunning(programName) == true)
	{
		help->writeLog(logId::error, L"[%s:%03d] Application is already running now.", __FUNCTIONW__, __LINE__);
		return false;
	}

	// 사용자 정보
	getPCInfos(&this->appContext);
	
	// window 생성
	if (createWindow(instance, programName, &this->appContext) == false)
	{
		help->writeLog(logId::error, L"[%s:%03d] createWindow is Failed.", __FUNCTIONW__, __LINE__);
		return false;
	}

	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/initializing-the-com-library
	//	: COINIT_DISABLE_OLE1DDE "OLE 1.0" 관련된 오버헤드를 줄일 수 있음 COINIT_APARTMENTTHREADED, COINIT_MULTITHREADED
	if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
	{
		help->writeLog(logId::error, L"[%s:%03d] err[%05d] CoInitializeEx is failed.", __FUNCTIONW__, __LINE__, ::GetLastError());
		return false;
	}

	// 환경 파일 (socket 설정)
	if (readEnvironmet(&this->appContext) == false)
	{
		help->writeLog(logId::error, L"[%s:%03d] readEnvironmet is Failed.", __FUNCTIONW__, __LINE__);
		return false;
	}

	// 컨텍스트 초기화
	if (this->appContext.initialize() == false)
	{
		help->writeLog(logId::error, L"[%s:%03d] Failed to create application context.", __FUNCTIONW__, __LINE__);
		return false;
	}

	return true;
}
int application::run()
{
	return this->appContext.tickTock();
}

//
// private
//
bool application::isAlreadyRunning(std::wstring programName)
{
	HANDLE mutex = ::CreateMutexW(nullptr, FALSE, programName.c_str());
	return ((::GetLastError() == ERROR_ALREADY_EXISTS) ? true : false);
}
void application::getPCInfos(context *appContext)
{
	DWORD size = MAX_PATH;

	// 이름
	std::wstring userName;
	userName.resize(size);
	::GetUserNameW(const_cast<wchar_t*>(userName.data()), &size);
	
	// 컴퓨터 이름
	size = MAX_PATH;
	std::wstring computerName;
	computerName.resize(size);
	::GetComputerNameW(const_cast<wchar_t*>(computerName.data()), &size);

	appContext->setPCInfo(userName, computerName);
}
bool application::createWindow(HINSTANCE instance, std::wstring programName, context *appContext)
{
	// 클래스 등록
	WNDCLASSW wndClass;
	::memset(&wndClass, 0x00, sizeof(WNDCLASSW));
	wndClass.hInstance = instance;
	wndClass.lpfnWndProc = appContext->getWndProc();
	wndClass.lpszClassName = programName.c_str();
	if (::RegisterClassW(&wndClass) == 0)
	{
		help->writeLog(logId::error, L"[%s:%03d] err[%05d] RegisterClassW is failed.", __FUNCTIONW__, __LINE__, ::GetLastError());
		return false;
	}

	// 윈도우 생성 (파일io watch 등록을 위해 필요함)
	HWND window = ::CreateWindowExW(0, wndClass.lpszClassName, wndClass.lpszClassName, 0, 0, 0, 0, 0, nullptr, nullptr, wndClass.hInstance, nullptr);
	if (window == nullptr)
	{
		help->writeLog(logId::error, L"[%s:%03d] err[%05d] CreateWindowExW is failed.", __FUNCTIONW__, __LINE__, ::GetLastError());
		return false;
	}

	appContext->setWindow(window);

	return true;
}
bool application::readEnvironmet(context *appContext)
{
	// ini 파일경로
	// https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
	wchar_t *profile = nullptr;
	if (FAILED(::SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)))
	{
		return false;
	}

	std::wstring iniFilePath;
	iniFilePath += profile;
	iniFilePath += L"\\.userAction\\settings.ini";

	// SHGetKnownFolderPath 로 확인한 wchar_t buffer 는 CoTaskMemFree 로 release
	safeCoTaskMemFree(profile);

	std::wstring ip;
	std::wstring port;
	ip.resize(16);		// xxx.xxx.xxx.xxx
	port.resize(6);		// xxxxx
	::GetPrivateProfileStringW(SECTION_SERVER.c_str(), L"ip", L"localhost", const_cast<wchar_t*>(ip.data()), ip.length(), iniFilePath.c_str());
	::GetPrivateProfileStringW(SECTION_SERVER.c_str(), L"port", L"30002", const_cast<wchar_t*>(port.data()), port.length(), iniFilePath.c_str());

	// context 에 설정
	appContext->setSocket(ip, port);

	return true;
}