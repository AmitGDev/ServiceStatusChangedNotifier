#ifndef AMITG_FC_SERVICE_STATUS_CHANGED_NOTIFIER
#define AMITG_FC_SERVICE_STATUS_CHANGED_NOTIFIER

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

#include <Windows.h> // Windows headers first

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Windows 8 (or greater) implementation
class ServiceStatusChangedNotifier final {
public:
  using ActionFunction = std::function<void(const std::wstring &service_name,
                                            DWORD current_state)>;

  ServiceStatusChangedNotifier() = default;
  ~ServiceStatusChangedNotifier() { Stop(); } // (Non-default destructor)

  // Since non-default destructor__

  // Delete copy constructor and copy assignment operator
  ServiceStatusChangedNotifier(const ServiceStatusChangedNotifier &) = delete;
  ServiceStatusChangedNotifier &
  operator=(const ServiceStatusChangedNotifier &) = delete;

  // Delete move constructor and move assignment operator
  ServiceStatusChangedNotifier(ServiceStatusChangedNotifier &&) = delete;
  ServiceStatusChangedNotifier &
  operator=(ServiceStatusChangedNotifier &&) = delete;

  // __Since non-default destructor

  // Subscribe to SC_EVENT_STATUS_CHANGE notifications for the specified
  // services.
  void Start(const std::vector<std::wstring> &service_list, DWORD notify_mask,
             const ActionFunction &action_function) noexcept;

  // Unsubscribe from all service notifications.
  void Stop() noexcept;

protected:
  // Tailored context for NotifyCallbackFunc():
  using Context = struct {
    DWORD notify_mask;
    ActionFunction action_function;
  };

  // Keeps data (per monitored service) *that has to be persistent* as long as
  // monitoring intact.
  struct ServiceData {
    wchar_t service_name[MAX_PATH + 1]{
        L'\0'}; // ('non-const' wchar-string to be pointed by:
                // notify_buffer->pszServiceNames)
    SERVICE_NOTIFY
    notify_buffer{}; // That does NOT initialize all elements of the struct
                     // to zero! Rather, it initializes the struct to its
                     // default values.
    PSC_NOTIFICATION_REGISTRATION registration{nullptr};
    DWORD system_error_code{ERROR_SUCCESS};
  };

  std::unordered_map<std::wstring, ServiceData>
      service_data_map_{}; // Key: service_name, Value: SERVICE_DATA (see
                           // above).

  Context context_{};

  static VOID CALLBACK NotifyCallbackFunc(_In_ DWORD dwNotify,
                                          _In_ PVOID pCallbackContext);

  // SecHost.dll function wrappers:

  // SubscribeServiceChangeNotifications_wrapper()
  // Has the signature of SecHost.dll SubscribeServiceChangeNotifications()
  // Calls SubscribeServiceChangeNotifications()
  // Returns: SubscribeServiceChangeNotifications() return value
  // If failed before, returns -1.
  static [[nodiscard]] DWORD WINAPI SubscribeServiceChangeNotificationsWrapper(
      _In_ SC_HANDLE hService, _In_ SC_EVENT_TYPE eEventType,
      _In_ PSC_NOTIFICATION_CALLBACK pCallback, _In_opt_ PVOID pCallbackContext,
      _Out_ PSC_NOTIFICATION_REGISTRATION *pSubscription);

  // UnsubscribeServiceChangeNotifications_wrapper()
  // Has the signature of SecHost.dll UnsubscribeServiceChangeNotifications()
  // Calls UnsubscribeServiceChangeNotifications()
  static VOID WINAPI UnsubscribeServiceChangeNotificationsWrapper(
      _In_ PSC_NOTIFICATION_REGISTRATION pSubscription);
};

#endif
