/*
   ServiceStatusChangedNotifier.cpp
   Copyright (c) 2024, Amit Gefen

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.
*/

#include "ServiceStatusChangedNotifier.h"

#include <bit>
#include <memory>
#include <ranges>

// NotifyCallbackFunc
// The callback has no access to instance-specific data directly because
// it is not associated with any instance of the class and does not have access
// to 'this' pointer.
// ** Important: The callback function must not block execution. **
VOID CALLBACK ServiceStatusChangedNotifier::NotifyCallbackFunc(
    _In_ DWORD dwNotify, _In_ PVOID pCallbackContext) {
  if (const auto notify_buffer{
          static_cast<PSERVICE_NOTIFY>(pCallbackContext)}) {
    if (const auto context{static_cast<Context *>(notify_buffer->pContext)}) {
      if (context->action_function &&
              (dwNotify | context->notify_mask) == context->notify_mask ||
          dwNotify == 0) {
        // Note: If the value of dwNotify is zero (0), it means that no specific
        // change flags were provided. In this case, the callback cannot rely on
        // dwNotify to determine what changed. Instead, the application is
        // responsible for verifying the current state of the service to
        // identify what has changed.
        context->action_function(notify_buffer->pszServiceNames,
                                 dwNotify); // <-- NOTIFY
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
//		a service state change occurs.
//
// Return:
//	- The function does not return any value.

namespace {

// SCHandleCloser
// A custom deleter to manage the lifetime of an SC_HANDLE and close it when it
// goes out of scope.
struct SCHandleCloser {
  void operator()(const SC_HANDLE handle) const noexcept {
    if (handle) {
      CloseServiceHandle(handle);
    }
  }
};

} // namespace

// Start
void ServiceStatusChangedNotifier::Start(
    const std::vector<std::wstring> &service_list, const DWORD notify_mask,
    const ActionFunction &action_function) noexcept {
  using ScopedSCHandle =
      const std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, SCHandleCloser>;

  // Instance-specific data (a context) for the static 'NotifyCallbackFunc':
  context_.notify_mask = notify_mask;
  context_.action_function = action_function;

  // Open the Service Control Manager (SCM) and manage its lifetime using
  // std::unique_ptr
  if (ScopedSCHandle scm{
          OpenSCManager(nullptr, SERVICES_ACTIVE_DATABASE,
                        SC_MANAGER_ALL_ACCESS),
          SCHandleCloser()}) { // If OpenSCManager fails, it returns nullptr.

    for (const auto &service_name : service_list) { // For each service name
      // Open the service handle and manage its lifetime using std::unique_ptr
      if (ScopedSCHandle service{
              OpenService(scm.get(), service_name.c_str(), SERVICE_ALL_ACCESS),
              SCHandleCloser()}) { // If OpenService fails, it returns nullptr.
        service_name.copy(service_data_map_[service_name].service_name,
                          service_name.length());
        const PSERVICE_NOTIFY notify_buffer =
            &service_data_map_[service_name].notify_buffer;

        std::memset(notify_buffer, 0, sizeof(SERVICE_NOTIFY)); // (Clear)
        notify_buffer->pContext = &context_; // Provide callback a context.
        notify_buffer->pszServiceNames =
            service_data_map_[service_name].service_name;

        // Subscribe to SC_EVENT_STATUS_CHANGE:
        service_data_map_[service_name]
            .system_error_code = SubscribeServiceChangeNotificationsWrapper(
            service.get(), SC_EVENT_STATUS_CHANGE, NotifyCallbackFunc,
            notify_buffer,
            &service_data_map_[service_name].registration); // Set the callback
      }
    }
  }
}

// ServiceStatusChangedNotifier
// Unsubscribe from all service notifications.
void ServiceStatusChangedNotifier::Stop() noexcept {
  for (auto &value : service_data_map_ | std::views::values) {
    if (value.registration) {
      UnsubscribeServiceChangeNotificationsWrapper(value.registration);
      value.registration = nullptr;
    }
  }
}

namespace {

// ScopedDllHandle
// A helper function to load a DLL and manage the lifetime of its handle
// (HINSTANCE) using a smart pointer.
std::unique_ptr<std::remove_pointer_t<HINSTANCE>, void (*)(HINSTANCE)>
ScopedDllHandle(const wchar_t *dll_name) {
  HINSTANCE dll_handle = LoadLibrary(dll_name);
  auto deleter = [](const HINSTANCE handle) {
    if (handle) {
      FreeLibrary(handle); // Automatically unload the DLL when the smart
                           // pointer goes out of scope.
    }
  };
  return {dll_handle, deleter};
}

} // namespace

// SubscribeServiceChangeNotificationsWrapper
//  This function acts as a wrapper around the
//  `SubscribeServiceChangeNotifications` function from the `SecHost.dll`
//  library. It takes the same arguments as the original function and returns
//  the same return value. However, it also performs some additional tasks:
//	- Loads the `SecHost.dll` library.
//	- Gets the address of the `SubscribeServiceChangeNotifications` function
//         using `GetProcAddress`.
//	- Calls the `SubscribeServiceChangeNotifications` function and passes
//         the provided arguments.
//	- Frees the `SecHost.dll` library if it was loaded successfully.
//
//  Returns:
//	- If the function succeeds, the return value is ERROR_SUCCESS. If the
//         function fails, the return value is one of the system error codes
DWORD WINAPI
ServiceStatusChangedNotifier::SubscribeServiceChangeNotificationsWrapper(
    _In_ SC_HANDLE hService, _In_ SC_EVENT_TYPE eEventType,
    _In_ PSC_NOTIFICATION_CALLBACK pCallback, _In_opt_ PVOID pCallbackContext,
    _Out_ PSC_NOTIFICATION_REGISTRATION *pSubscription) {
  DWORD return_value{0};

  // SubscribeServiceChangeNotifications function prototype.
  using FunctionPrototype = DWORD(CALLBACK *)(
      _In_ SC_HANDLE, _In_ SC_EVENT_TYPE, _In_ PSC_NOTIFICATION_CALLBACK,
      _In_opt_ PVOID, _Out_ PSC_NOTIFICATION_REGISTRATION *);

  *pSubscription = nullptr; // SubscribeServiceChangeNotifications(), *on
                            // success*, sets *pSubscription.

  // 1) Load SecHost DLL
  // (If the function succeeds, the return value is a handle to the module.
  // If the function fails, the return value is NULL. To get extended error
  // information, call GetLastError.)
  if (const auto dll{ScopedDllHandle(L"SecHost.dll")}) {
    // 2) Get SubscribeServiceChangeNotifications() address: The address
    // returned by GetProcAddress is a FARPROC, a pointer to a function with an
    // unknown signature. If the function fails, the return value is NULL. To
    // get extended error information, call GetLastError.
    if (const auto address = std::bit_cast<FunctionPrototype>(
            GetProcAddress(dll.get(), "SubscribeServiceChangeNotifications"))) {
      // 3) Call SubscribeServiceChangeNotifications():
      // (If the function succeeds, the return value is ERROR_SUCCESS. If
      //  the function fails, the return value is one of the system error
      //  codes.)
      return_value = address(hService, eEventType, pCallback, pCallbackContext,
                             pSubscription); //  <-- SUBSCRIBE
    }
  } else {
    return_value = GetLastError();
  }

  return return_value; // Either ERROR_SUCCESS or SystemError or LastError.
}

// UnsubscribeServiceChangeNotificationsWrapper
// This function acts as a wrapper around the
// `UnsubscribeServiceChangeNotifications` function from the `SecHost.dll`
// library. It takes the same argument as the original function. However, it
// also performs some additional tasks:
//	- Loads the `SecHost.dll` library.
//	- Gets the address of the `SubscribeServiceChangeNotifications` function
//        using `GetProcAddress`.
//	- Calls the `SubscribeServiceChangeNotifications` function and passes
//        the provided arguments.
//	- Frees the `SecHost.dll` library if it was loaded successfully.
//
// Return:
//	- The function does not return any value.
VOID WINAPI
ServiceStatusChangedNotifier::UnsubscribeServiceChangeNotificationsWrapper(
    _In_ PSC_NOTIFICATION_REGISTRATION pSubscription) {
  // UnsubscribeServiceChangeNotifications function prototype.
  using FunctionPrototype = VOID(WINAPI *)(_In_ PSC_NOTIFICATION_REGISTRATION);

  // 1) Load SecHost DLL
  if (const auto dll{ScopedDllHandle(L"SecHost.dll")}) {
    // 2) Get UnsubscribeServiceChangeNotifications() address: The address
    // returned by GetProcAddress is a FARPROC, a pointer to a function with an
    // unknown signature. If the function fails, the return value is NULL. To
    // get extended error information, call GetLastError.
    if (const auto address = std::bit_cast<FunctionPrototype>(
            // error information, call GetLastError.
            GetProcAddress(dll.get(),
                           "UnsubscribeServiceChangeNotifications"))) {
      // 3) Call UnsubscribeServiceChangeNotifications():
      address(pSubscription); //  <-- UNSUBSCRIBE
    }
  }
}
