// *RUN "AS ADMIN"!*

#include <Windows.h> // Windows headers first

#include "ServiceStatusChangedNotifier.h"
#include <iostream>
#include <syncstream>
#include <thread>

namespace // (Anonymous namespace)
{

// Implement what to do on service status-changed notification
void OnNotificationActionFunction(const std::wstring &service_name,
                                  const DWORD &current_state) {
  std::wosyncstream sync_stream(std::wcout); // (Since C++20)

  sync_stream << L"notification: " << service_name << L" current state: "
              << current_state << '\n';

  if (current_state == SERVICE_NOTIFY_STOPPED) { // Only if service is stopped:
    sync_stream << L"action" << '\n';
  }
}

} // namespace

// *RUN "AS ADMIN"!*
int main() {
  ServiceStatusChangedNotifier service_status_change_notifier;

  // Start (and subscribe to "W32Time" and "WebClient"):
  service_status_change_notifier.Start(
      std::vector<std::wstring>{L"W32Time",
                                L"WebClient"}, // A vector of service names.
      SERVICE_NOTIFY_STOPPED, // Notify about service STOPPED (Notify Mask).
      OnNotificationActionFunction); // <-- Notify to this function (see above).

  // Provide 5 minutes to manually Start / Stop "W32Time" and "WebClient"
  // services and to check the functionality.
  std::this_thread::sleep_for(std::chrono::minutes(5));

  // Exit (unsubscribe all):
  service_status_change_notifier
      .Stop(); // Test: Set BP on Sleep(). On break, Set-Next-Statement here +
               // single-step (to see that the WT exited).
}