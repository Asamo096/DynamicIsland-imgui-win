#include "scheduler.h"
#include <sddl.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "advapi32.lib")

TaskScheduler& TaskScheduler::Instance() {
    static TaskScheduler instance;
    return instance;
}

bool TaskScheduler::Initialize() {
    if (comInitialized) return true;
    
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }
    
    comInitialized = true;
    
    // 创建TaskService实例
    hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        CoUninitialize();
        comInitialized = false;
        return false;
    }
    
    // 连接到任务计划服务
    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        pService = nullptr;
        CoUninitialize();
        comInitialized = false;
        return false;
    }
    
    return true;
}

void TaskScheduler::Shutdown() {
    if (pService) {
        pService->Release();
        pService = nullptr;
    }
    if (comInitialized) {
        CoUninitialize();
        comInitialized = false;
    }
}

bool TaskScheduler::IsRegistered() {
    if (!pService && !Initialize()) return false;
    
    ITaskFolder* pRootFolder = nullptr;
    HRESULT hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) return false;
    
    IRegisteredTask* pTask = nullptr;
    hr = pRootFolder->GetTask(_bstr_t(GetTaskName()), &pTask);
    
    pRootFolder->Release();
    
    if (SUCCEEDED(hr) && pTask) {
        pTask->Release();
        return true;
    }
    
    return false;
}

bool TaskScheduler::Register(const TaskConfig& config) {
    if (!pService && !Initialize()) return false;
    
    // 获取或创建根文件夹
    ITaskFolder* pRootFolder = nullptr;
    HRESULT hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) return false;
    
    // 如果任务已存在，先删除
    if (IsRegistered()) {
        Unregister();
    }
    
    // 创建任务定义
    ITaskDefinition* pTask = nullptr;
    hr = pService->NewTask(0, &pTask);
    if (FAILED(hr)) {
        pRootFolder->Release();
        return false;
    }
    
    // 设置注册信息
    IRegistrationInfo* pRegInfo = nullptr;
    hr = pTask->get_RegistrationInfo(&pRegInfo);
    if (SUCCEEDED(hr)) {
        pRegInfo->put_Author(_bstr_t(L"DynamicIsland"));
        pRegInfo->put_Description(_bstr_t(L"DynamicIsland System Monitor - 开机自启动"));
        pRegInfo->Release();
    }
    
    // 设置主体 (运行身份)
    IPrincipal* pPrincipal = nullptr;
    hr = pTask->get_Principal(&pPrincipal);
    if (SUCCEEDED(hr)) {
        pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        if (config.runAsAdmin) {
            pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        } else {
            pPrincipal->put_RunLevel(TASK_RUNLEVEL_LUA);
        }
        pPrincipal->Release();
    }
    
    // 设置触发器 (登录时)
    ITriggerCollection* pTriggers = nullptr;
    hr = pTask->get_Triggers(&pTriggers);
    if (SUCCEEDED(hr)) {
        ITrigger* pTrigger = nullptr;
        hr = pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger);
        if (SUCCEEDED(hr)) {
            ILogonTrigger* pLogonTrigger = nullptr;
            hr = pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
            if (SUCCEEDED(hr)) {
                pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));
                
                // 设置延迟
                if (config.delayStart && config.delaySeconds > 0) {
                    std::wstring delayStr = L"PT" + std::to_wstring(config.delaySeconds) + L"S";
                    pLogonTrigger->put_Delay(_bstr_t(delayStr.c_str()));
                }
                
                pLogonTrigger->Release();
            }
            pTrigger->Release();
        }
        pTriggers->Release();
    }
    
    // 设置设置
    ITaskSettings* pSettings = nullptr;
    hr = pTask->get_Settings(&pSettings);
    if (SUCCEEDED(hr)) {
        pSettings->put_StartWhenAvailable(VARIANT_TRUE);
        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pSettings->put_Hidden(config.hidden ? VARIANT_TRUE : VARIANT_FALSE);
        pSettings->put_RunOnlyIfNetworkAvailable(VARIANT_FALSE);
        pSettings->put_AllowDemandStart(VARIANT_TRUE);
        pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); // 无时间限制
        pSettings->Release();
    }
    
    // 设置操作
    IActionCollection* pActions = nullptr;
    hr = pTask->get_Actions(&pActions);
    if (SUCCEEDED(hr)) {
        IAction* pAction = nullptr;
        hr = pActions->Create(TASK_ACTION_EXEC, &pAction);
        if (SUCCEEDED(hr)) {
            IExecAction* pExecAction = nullptr;
            hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
            if (SUCCEEDED(hr)) {
                std::wstring exePath = GetExecutablePath();
                pExecAction->put_Path(_bstr_t(exePath.c_str()));
                pExecAction->put_Arguments(_bstr_t(L"/background"));
                pExecAction->put_WorkingDirectory(_bstr_t(exePath.substr(0, exePath.find_last_of(L"\\")).c_str()));
                pExecAction->Release();
            }
            pAction->Release();
        }
        pActions->Release();
    }
    
    // 注册任务
    IRegisteredTask* pRegisteredTask = nullptr;
    hr = pRootFolder->RegisterTaskDefinition(
        _bstr_t(GetTaskName()),
        pTask,
        TASK_CREATE_OR_UPDATE,
        _variant_t(),
        _variant_t(),
        TASK_LOGON_INTERACTIVE_TOKEN,
        _variant_t(L""),
        &pRegisteredTask
    );
    
    pTask->Release();
    pRootFolder->Release();
    
    if (SUCCEEDED(hr) && pRegisteredTask) {
        pRegisteredTask->Release();
        return true;
    }
    
    return false;
}

bool TaskScheduler::Unregister() {
    if (!pService && !Initialize()) return false;
    
    ITaskFolder* pRootFolder = nullptr;
    HRESULT hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) return false;
    
    hr = pRootFolder->DeleteTask(_bstr_t(GetTaskName()), 0);
    pRootFolder->Release();
    
    return SUCCEEDED(hr);
}

bool TaskScheduler::UpdateConfig(const TaskConfig& config) {
    // 更新配置就是重新注册
    if (IsRegistered()) {
        return Register(config);
    }
    return false;
}

std::wstring TaskScheduler::GetExecutablePath() const {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path);
}

std::wstring TaskScheduler::GenerateTaskXml(const TaskConfig& config) const {
    std::wstringstream xml;
    
    xml << L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n";
    xml << L"<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n";
    xml << L"  <RegistrationInfo>\n";
    xml << L"    <Description>DynamicIsland System Monitor</Description>\n";
    xml << L"  </RegistrationInfo>\n";
    xml << L"  <Triggers>\n";
    xml << L"    <LogonTrigger>\n";
    if (config.delayStart) {
        xml << L"      <Delay>PT" << config.delaySeconds << L"S</Delay>\n";
    }
    xml << L"    </LogonTrigger>\n";
    xml << L"  </Triggers>\n";
    xml << L"  <Principals>\n";
    xml << L"    <Principal>\n";
    xml << L"      <LogonType>InteractiveToken</LogonType>\n";
    xml << L"      <RunLevel>" << (config.runAsAdmin ? L"HighestAvailable" : L"LeastPrivilege") << L"</RunLevel>\n";
    xml << L"    </Principal>\n";
    xml << L"  </Principals>\n";
    xml << L"  <Settings>\n";
    xml << L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n";
    xml << L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n";
    xml << L"    <Hidden>" << (config.hidden ? L"true" : L"false") << L"</Hidden>\n";
    xml << L"  </Settings>\n";
    xml << L"  <Actions>\n";
    xml << L"    <Exec>\n";
    xml << L"      <Command>" << GetExecutablePath() << L"</Command>\n";
    xml << L"      <Arguments>/background</Arguments>\n";
    xml << L"    </Exec>\n";
    xml << L"  </Actions>\n";
    xml << L"</Task>\n";
    
    return xml.str();
}
