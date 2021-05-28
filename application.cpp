#include "application.h"

const std::wstring SECTION_SERVER = L"Server";

//
// public
//
application::application(HINSTANCE instance)
	:instance(instance), programName(), appContext(nullptr)
{
}
application::~application()
{
	// release() ���� ������
}
bool application::initialize()
{
	bool result = false;

	// ���α׷� �̸�
	size_t size = MAX_PATH;
	this->programName.resize(size);
	::GetModuleFileNameW(nullptr, const_cast<wchar_t*>(this->programName.data()), size);
	this->programName = this->programName.substr(this->programName.rfind('\\') + 1);	// userAction_client.exe

	// ���� �������̶�� ����
	if (isAlreadyRunning(this->programName) == true)
	{
		log->write(errId::error, L"[%s:%03d] Application is already running now.", __FUNCTIONW__, __LINE__);
		return false;
	}

	// ���ؽ�Ʈ ����
	this->appContext = new context();
	if (this->appContext == nullptr)
	{
		log->write(errId::error, L"[%s:%03d] Failed to create application context.", __FUNCTIONW__, __LINE__);
		return false;
	}

	// window ����
	if (createWindow(this->instance, this->programName, this->appContext) == false)
	{
		log->write(errId::error, L"[%s:%03d] createWindow is Failed.", __FUNCTIONW__, __LINE__);
		return false;
	}

	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/initializing-the-com-library
	//	: COINIT_DISABLE_OLE1DDE "OLE 1.0" ���õ� ������带 ���� �� ���� COINIT_APARTMENTTHREADED, COINIT_MULTITHREADED
	if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
	{
		log->write(errId::error, L"[%s:%03d] err[%05d] CoInitializeEx is failed.", __FUNCTIONW__, __LINE__, ::GetLastError());
		return false;
	}

	// ȯ�� ����
	std::wstring ip;
	std::wstring port;
	if (readEnvironmet(this->appContext) == false)
	{
		log->write(errId::error, L"[%s:%03d] readEnvironmet is Failed.", __FUNCTIONW__, __LINE__);
		return false;
	}

	// ���ؽ�Ʈ �ʱ�ȭ
	if (this->appContext->initialize() == false)
	{
		log->write(errId::error, L"[%s:%03d] Failed to create application context.", __FUNCTIONW__, __LINE__);
		return false;
	}

	return true;
}
void application::run()
{
	this->appContext->tickTock();
}
int application::release()
{
	safeDelete(this->appContext);

	::CoUninitialize();

	log->write(errId::info, L"End of application.");
	log->release();

	return 0;
}

//
// private
//
bool application::isAlreadyRunning(std::wstring programName)
{
	HANDLE mutex = ::CreateMutexW(nullptr, FALSE, programName.c_str());
	return ((::GetLastError() == ERROR_ALREADY_EXISTS) ? true : false);
}
bool application::createWindow(HINSTANCE instance, std::wstring programName, context *appContext)
{
	// Ŭ���� ���
	WNDCLASSW wndClass;
	::memset(&wndClass, 0x00, sizeof(WNDCLASSW));
	wndClass.hInstance = instance;
	wndClass.lpfnWndProc = appContext->getWndProc();
	wndClass.lpszClassName = programName.c_str();
	if (::RegisterClassW(&wndClass) == 0)
	{
		log->write(errId::error, L"[%s:%03d] err[%05d] RegisterClassW is failed.", __FUNCTIONW__, __LINE__, ::GetLastError());
		return false;
	}

	// ������ ����
	appContext->setWindow(::CreateWindowExW(0, wndClass.lpszClassName, wndClass.lpszClassName, 0, 0, 0, 0, 0, nullptr, nullptr, wndClass.hInstance, nullptr));

	return true;
}
bool application::readEnvironmet(context *appContext)
{
	// ini ���ϰ��
	// https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
	wchar_t *profile = nullptr;
	if (FAILED(::SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)))
	{
		return false;
	}

	std::wstring iniFilePath;
	iniFilePath += profile;
	iniFilePath += L"\\.userAction\\settings.ini";

	// SHGetKnownFolderPath �� Ȯ���� wchar_t buffer �� CoTaskMemFree �� release
	safeCoTaskMemFree(profile);

	std::wstring ip;
	std::wstring port;
	ip.resize(16);
	port.resize(5);
	::GetPrivateProfileStringW(SECTION_SERVER.c_str(), L"ip", nullptr, const_cast<wchar_t*>(ip.data()), ip.length(), iniFilePath.c_str());
	::GetPrivateProfileStringW(SECTION_SERVER.c_str(), L"port", nullptr, const_cast<wchar_t*>(port.data()), port.length(), iniFilePath.c_str());
	int retryInterval = ::GetPrivateProfileIntW(SECTION_SERVER.c_str(), L"retryInterval", 0, iniFilePath.c_str());

	// context �� ����
	appContext->setSocket(ip, port, retryInterval);

	return true;
}