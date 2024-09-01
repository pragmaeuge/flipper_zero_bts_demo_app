# Flipper Zero Bluetooth GATT Service Application

## Overview

This application sets up a Bluetooth service with a specific UUID and a characteristic that returns a constant string value using Flipper Zero (0.105.0). The application also includes GUI elements and manages resources.

## Features

- **Bluetooth Service**: Adds a service with UUID `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000`.
- **Characteristic**: Contains a characteristic with UUID `f0debc9a-7856-3412-f0de-bc9a78565612` that returns a constant string value.
- **GUI Management**: Includes functions to manage and free GUI resources.
- **Resource Management**: Ensures proper allocation and deallocation of resources to prevent memory leaks.

## Code Excerpt

### GUI and Resource Management

The following code snippet demonstrates how the application manages GUI resources and frees allocated memory:

```c
// GUI
view_free_model(app.start_page->text_view);
view_dispatcher_remove_view(app.view_dispatcher, ViewIdMainMenu);
view_dispatcher_remove_view(app.view_dispatcher, ViewIdStartPage);
FURI_LOG_I("BtsApp", "Text view removed from dispatcher");
view_dispatcher_free(app.view_dispatcher);
FURI_LOG_I("BtsApp", "View dispatcher freed");

furi_record_close(RECORD_GUI);
FURI_LOG_I("BtsApp", "GUI record closed");

// Free resources of the app (may not be exhaustive)
free(app.ble->base);
free(app.ble->ble_profile);
free(app.ble->ble_profile);
free(app.ble->char_instance);
free(app.ble);

view_free_model(app.start_page->text_view);
view_free(app.start_page->text_view);
free(app.start_page);
```

## UUID Details

- **Service UUID**: `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000`
- **Characteristic UUID**: `f0debc9a-7856-3412-f0de-bc9a78565612`
- **Characteristic Value**: Returns a constant string value.

## How to Run

1. Initialize the BLE stack.
2. Set up the BLE profile with the specified service and characteristic UUIDs.
3. Start advertising the BLE service.
4. Ensure proper management of GUI resources and memory allocation.

## Conclusion

This application demonstrates the setup of a Bluetooth service with specific UUIDs. It serves as a template for developing BLE applications with GUI components.
