#include "context.h"

feature *context::fileIo = nullptr;

LRESULT CALLBACK context::wndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_FILE_IO:
		{
			paramsFileIo *params = new paramsFileIo{ hWnd, wParam, lParam };
			fileIo->watch(static_cast<void*>(params));
			safeDelete(params);
		}
		break;
		case WM_ENDSESSION:
		{
			// https://docs.microsoft.com/en-us/windows/win32/shutdown/wm-endsession
			help->writeUserAction(featureId::logoff, L"pc is logoff");
		}
		break;
	}
	
	return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

//
// public
//
context::context()
	:userName(), rule(), window(nullptr), socket(nullptr), callback(wndProc), features()
{
}
context::~context()
{
	safeDelete(this->socket);
}
WNDPROC context::getWndProc()
{
	return this->callback;
}
void context::setPCInfo(std::wstring userName, std::wstring computerName)
{
	this->userName = userName;
	this->computerName = computerName;
}
void context::setWindow(HWND window)
{
	this->window = window;
}
void context::setSocket(std::wstring ip, std::wstring port)
{
	if (this->socket == nullptr)
	{
		this->socket = new winSock(ip, port);
	}
}
bool context::initialize()
{
	// 소켓 확인
	if (this->socket->initialize() == false)
	{
		help->writeLog(logId::warning, L"[%s:%03d] Failed to initialize winSock.", __FUNCTIONW__, __LINE__);
	}

	// 정책 확인
	this->rule.initialize(this->socket, this->window);
	
	// TODO: 프로그램 종료로 업로드되지 못한 데이터 재전송 (threading 처리해서 따로 병렬처리할 것)

	// 감시기능 - 자리비움 (** 반드시 처음 리스트에 포함 **)
	feature *afk = new featureAFK();
	if (afk != nullptr)
	{
		if (afk->initialize(this->rule.getAFKRule(), sizeof(ruleAFK)) == true)
		{
			this->features.push_back(afk);
		}
		else
		{
			safeDelete(afk);
		}
	}

	// 감시기능 - 프로세스
	feature *proc = new featureProcess();
	if (proc != nullptr)
	{
		if (proc->initialize(this->rule.getProcessRule(), sizeof(ruleProcess)) == true)
		{
			this->features.push_back(proc);
		}
		else
		{
			safeDelete(proc);
		}
	}

	// 감시기능 - 프린트 출력
	feature *prn = new featurePrint();
	if (prn != nullptr)
	{
		if (prn->initialize(this->rule.getPrintRule(), sizeof(rulePrint)) == true)
		{
			this->features.push_back(prn);
		}
		else
		{
			safeDelete(prn);
		}
	}

	// 감시기능 - 파일io
	fileIo = new featureFileIo();
	if (fileIo != nullptr)
	{
		// wndProc 에서 message callback 처리를 하기때문에 loop 에서 처리하지 않음
		//if (fileIo->initialize(this->rule) == true)
		//{
		//	this->features.push_back(fileIo);
		//}
		//else
		//{
		//	safeDelete(fileIo);
		//}
		if (fileIo->initialize(this->rule.getFileIoRule(), sizeof(ruleFileIo)) == false)
		{
			safeDelete(fileIo);
		}
	}

	return true;
}
int context::tickTock()
{
	LARGE_INTEGER dueTime;
	dueTime.QuadPart = 0;

	// 메인 watch 타이머 설정
	HANDLE watchTimer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
	if (::SetWaitableTimer(watchTimer, &dueTime, this->rule.getTimerInterval(), nullptr, nullptr, FALSE) == FALSE)
	{
		help->writeLog(logId::error, L"[%s:%03d] code[%d] SetWaitableTimer is failed.", __FUNCTIONW__, __LINE__, ::GetLastError());
		return -1;
	}

	// 소켓 재연결은 정책에 있는경우에만
	HANDLE retryTimer = INVALID_HANDLE_VALUE;
	int serverRetryInterval = this->rule.getServerRetryInterval();
	if (serverRetryInterval > 0)
	{
		retryTimer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
		if (::SetWaitableTimer(retryTimer, &dueTime, serverRetryInterval, nullptr, nullptr, FALSE) == FALSE)
		{
			help->writeLog(logId::error, L"[%s:%03d] code[%d] SetWaitableTimer is failed.", __FUNCTIONW__, __LINE__, ::GetLastError());
			return -1;
		}
	}

	// 
	help->writeUserAction(featureId::logon, L"%s (%s)", this->userName.c_str(), this->computerName.c_str());

	// 메시지 루프
	MSG message;
	while (::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE) == TRUE)
	// while (true) fileIo 를 감시하기 위해 wndProc 를 사용하려면 윈도우 메시지 루프를 추가해야함
	{
		::TranslateMessage(&message);
		::DispatchMessageW(&message);

		watch(watchTimer);

		if (retryTimer != INVALID_HANDLE_VALUE)
		{
			retryConnect(retryTimer);
		}
	}

	// release
	for (int i = 0; i < this->features.size(); i++)
	{
		safeDelete(this->features[i]);
	}
	this->features.clear();

	this->rule.release();

	::CancelWaitableTimer(retryTimer);
	::CancelWaitableTimer(watchTimer);
	safeCloseHandle(retryTimer);
	safeCloseHandle(watchTimer);
}

//
// private
//
void context::watch(HANDLE timer)
{
	if (::WaitForSingleObject(timer, 1) == WAIT_OBJECT_0)
	{
		bool inAFK = false;
		for (int i = 0; i < this->features.size(); i++)
		{
			if (i == 0)
			{
				inAFK = this->features[i]->watch();
			}
			else
			{
				if (inAFK == false)
				{
					this->features[i]->watch();
				}
			}
		}
	}
}
void context::retryConnect(HANDLE timer)
{
	if (::WaitForSingleObject(timer, 1) == WAIT_OBJECT_0)
	{
		if (this->socket->isOnline() == false)
		{
			if (this->socket->initialize() == true) 
			{
				help->writeLog(logId::info, L"[%s:%03d] server is on line again.", __FUNCTIONW__, __LINE__);
			}
#if _DEBUG
			else
			{

				help->writeLog(logId::warning, L"[%s:%03d] server is not on line.", __FUNCTIONW__, __LINE__);
			}
#endif
		}
	}
}