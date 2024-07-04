/*
	ServiceStatusChangedNotifier.cpp
	Copyright (c) 2024, Amit Gefen

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/

#include "ServiceStatusChangedNotifier.h"


// The callback must be 'Static' and as such it has no access to instance-specific data directly because
// it is not associated with any instance of the class and does not have access to 'this' pointer.
VOID CALLBACK ServiceStatusChangedNotifier::NotifyCallbackFunc( // [STATIC]
	_In_    DWORD   dwNotify,
	_In_    PVOID   pCallbackContext)
{
	if (const auto notify_buffer = static_cast<PSERVICE_NOTIFY>(pCallbackContext); notify_buffer != nullptr) {
		if (const auto context = static_cast<Context*>(notify_buffer->pContext); context != nullptr) {
			if (context->action_function && dwNotify != 0 && (dwNotify | context->notify_mask) == context->notify_mask) {
				context->action_function(notify_buffer->pszServiceNames, dwNotify); // <-- NOTIFY
			}
		}
	}
}


// Subscribe to SC_EVENT_STATUS_CHANGE notifications for the specified services.
// Allows to monitor the status of Windows services and receive notifications
// when their status changes. You can specify a callback function to be invoked
// whenever a service status change occurs.
//
// Parameters:
//	- service_list: A list of service names to monitor.
//	- notify_mask: A bitmask indicating the types of service state changes
//		to receive notifications for. Valid values are defined in the
//		SC_EVENT_TYPE enumeration.
//	- action_function: A callback function that will be called whenever
//		a service state change occurs. The function takes two arguments:
//		- service_name: The name of the service that changed state.
//		- current_state: The new service state (e.g., SERVICE_RUNNING,
//			SERVICE_PAUSED, etc.).
//
// Return:
//	- The function does not return any value.
void ServiceStatusChangedNotifier::Start(
	const std::vector<std::wstring>& service_list,
	const DWORD notify_mask,
	const ActionFunction action_function) noexcept
{
	// Instance-specific data (a context) for the static 'NotifyCallbackFunc':
	context_.notify_mask = notify_mask;
	context_.action_function = action_function;

	// Open Service Control Manager (SCM)
	if (const SC_HANDLE scm = OpenSCManager(nullptr, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS); scm != nullptr) { // If the function fails, the return value is nullptr.
		for (const auto& service_name : service_list) { // (for each service name)
			if (const SC_HANDLE service = OpenService(scm, service_name.c_str(), SERVICE_ALL_ACCESS); service != nullptr) { // If the function fails, the return value is nullptr.
				service_name.copy(service_data_map_[service_name].service_name, service_name.length());
				const PSERVICE_NOTIFY notify_buffer = &service_data_map_[service_name].notify_buffer;
				std::memset(notify_buffer, 0, sizeof(SERVICE_NOTIFY)); // Lets start with a clean slate.

				notify_buffer->pContext = &context_; // ('NotifyCallbackFunc' must be static. Static functions do not have access to instance-specific data directly.)
				notify_buffer->pszServiceNames = service_data_map_[service_name].service_name;

				// Subscibe to SC_EVENT_STATUS_CHANGE:
				service_data_map_[service_name].system_error_code = SubscribeServiceChangeNotifications_wrapper( //  <-- SUBSCRIBE
					service, SC_EVENT_STATUS_CHANGE, NotifyCallbackFunc, notify_buffer, &service_data_map_[service_name].registration); // <-- Set the *specified callback function*

				CloseServiceHandle(service);
			}
		}

		CloseServiceHandle(scm);
	}
}


// Unsubscribe from all service notifications.
//
// Returns:
//	- The function does not return any value.
void ServiceStatusChangedNotifier::Stop() noexcept
{
	for (auto& service_data : service_data_map_) {
		if (service_data.second.registration != nullptr) {
			UnsubscribeServiceChangeNotifications_wrapper(service_data.second.registration); // <-- UNSUBSCRIBE
			service_data.second.registration = nullptr;
		}
	}
}


// This function acts as a wrapper around the `SubscribeServiceChangeNotifications` function from the `SecHost.dll` library.
// It takes the same arguments as the original function and returns the same return value.
// However, it also performs some additional tasks:
//	- Loads the `SecHost.dll` library.
//	- Gets the address of the `SubscribeServiceChangeNotifications` function using `GetProcAddress`.
//	- Calls the `SubscribeServiceChangeNotifications` function and passes the provided arguments.
//	- Frees the `SecHost.dll` library if it was loaded successfully.
//
// Returns:
//	- If the function succeeds, the return value is ERROR_SUCCESS. If the function fails, the return value is one of the system error codes
DWORD WINAPI ServiceStatusChangedNotifier::SubscribeServiceChangeNotifications_wrapper( // [STATIC]
	_In_     SC_HANDLE                     hService,
	_In_     SC_EVENT_TYPE                 eEventType,
	_In_     PSC_NOTIFICATION_CALLBACK     pCallback,
	_In_opt_ PVOID                         pCallbackContext,
	_Out_    PSC_NOTIFICATION_REGISTRATION* pSubscription)
{
	DWORD return_value{ 0 };

	// "SecHost.dll" SubscribeServiceChangeNotifications() function prototype.
	typedef DWORD(CALLBACK* LPFNDLLSubscribeServiceChangeNotifications)(
		_In_     SC_HANDLE,
		_In_     SC_EVENT_TYPE,
		_In_     PSC_NOTIFICATION_CALLBACK,
		_In_opt_ PVOID,
		_Out_    PSC_NOTIFICATION_REGISTRATION*);

	*pSubscription = nullptr; // SubscribeServiceChangeNotifications(), *on success*, sets *pSubscription.

	// 1) Load SecHost DLL
	//	(If the function succeeds, the return value is a handle to the module. If the function fails, the return value is NULL. To get extended error information, call GetLastError.)
	if (const HINSTANCE dll = LoadLibrary(L"SecHost.dll"); dll != nullptr) {
		// 2) Get SubscribeServiceChangeNotifications() address:  (the address returned by GetProcAddress is a FARPROC, a pointer to a function with an unknown signature)
		if (const auto fpSubscribeServiceChangeNotifications = reinterpret_cast<LPFNDLLSubscribeServiceChangeNotifications>(GetProcAddress(dll, "SubscribeServiceChangeNotifications")); fpSubscribeServiceChangeNotifications != nullptr) {
			// 3) Call SubscribeServiceChangeNotifications():
			//	(If the function succeeds, the return value is ERROR_SUCCESS. If the function fails, the return value is one of the system error codes.)
			return_value = fpSubscribeServiceChangeNotifications(hService, eEventType, pCallback, pCallbackContext, pSubscription); //  <-- SUBSCRIBE
		}

		FreeLibrary(dll);

	} else return_value = GetLastError();

	return return_value; // Either ERROR_SUCCESS or SystemError or LastError.
}


// This function acts as a wrapper around the `UnsubscribeServiceChangeNotifications` function from the `SecHost.dll` library.
// It takes the same argument as the original function.
// However, it also performs some additional tasks:
//	- Loads the `SecHost.dll` library.
//	- Gets the address of the `SubscribeServiceChangeNotifications` function using `GetProcAddress`.
//	- Calls the `SubscribeServiceChangeNotifications` function and passes the provided arguments.
//	- Frees the `SecHost.dll` library if it was loaded successfully.
//
// Return:
//	- The function does not return any value.
VOID WINAPI ServiceStatusChangedNotifier::UnsubscribeServiceChangeNotifications_wrapper( // [STATIC]
	_In_ PSC_NOTIFICATION_REGISTRATION pSubscription)
{
	// "SecHost.dll" SubscribeServiceChangeNotifications() function prototype.
	typedef VOID(WINAPI* LPFNDLLUnsubscribeServiceChangeNotifications)(
		_In_    PSC_NOTIFICATION_REGISTRATION);

	// 1) Load SecHost DLL
	if (const HINSTANCE dll = LoadLibrary(L"SecHost.dll"); dll != nullptr) {
		// 2) Get SubscribeServiceChangeNotifications() address: (the address returned by GetProcAddress is a FARPROC, a pointer to a function with an unknown signature)
		if (const auto fpUnsubscribeServiceChangeNotifications = reinterpret_cast<LPFNDLLUnsubscribeServiceChangeNotifications>(GetProcAddress(dll, "UnsubscribeServiceChangeNotifications")); fpUnsubscribeServiceChangeNotifications != nullptr) {
			// 3) Call UnsubscribeServiceChangeNotifications():
			fpUnsubscribeServiceChangeNotifications(pSubscription); //  <-- UNSUBSCRIBE
		}

		FreeLibrary(dll);
	}
}

