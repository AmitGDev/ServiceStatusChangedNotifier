**Service Status Changed Notifier v1.0.0**

This project provides a C++ implementation for monitoring the status changes of Windows services.

**Author:** Amit Gefen

**License:** MIT License

<br>

**Overview**

A C++ implementation for monitoring the status changes of Windows services using the Service Control Manager (SCM) API. It allows users to subscribe to notifications for specific services and receive callbacks when their status changes.

<br>

**Features**

- Subscribe to SC_EVENT_STATUS_CHANGE notifications for specified services.
- Receive callbacks with the service name and current state upon status change.
- Unsubscribe from service notifications when no longer needed.

<br>

**Usage**

1. Include `ServiceStatusChangedNotifier.h` and `ServiceStatusChangedNotifier.cpp` in your project.
2. Use the `ServiceStatusChangedNotifier` class to subscribe to service notifications and define callback functions to handle status changes.
3. Rus as admin.

```cpp
// *RUN "AS ADMIN"!*

#include <Windows.h>
#include <iostream>
#include <thread>
#include "ServiceStatusChangedNotifier.h"


namespace  // (Anonymous namespace)
{

	// Implement what to do on service status-changed notification
	void OnNotificationActionFunction(const std::wstring& service_name, const DWORD& current_state)
	{
		// Do something.	
	}

}


// *RUN "AS ADMIN"!*
int main()
{
	ServiceStatusChangedNotifier service_status_change_notifier{};

	// Start (and subscribe to "W32Time" and "WebClient"):
	service_status_change_notifier.Start(
		std::vector<std::wstring>{ L"W32Time", L"WebClient" }, // A vector of service names.
		SERVICE_NOTIFY_STOPPED, // Notify about service STOPPED (Notify Mask).
		OnNotificationActionFunction); // <-- Notify to this function (see above).

	// Provide 5 minutes to manually Start / Stop "W32Time" and "WebClient" services and to check the functionality.
	std::this_thread::sleep_for(std::chrono::minutes(5));

	// Exit (Unsubscibe all):
	service_status_change_notifier.Stop();
}
```

<br>

**Example Usage**

See the **main.cpp** file for a comprehensive example.

<br>

**Dependencies**

- Windows 8 or later.
- Visual Studio or another C++ compiler for building the project.
- C++20.
