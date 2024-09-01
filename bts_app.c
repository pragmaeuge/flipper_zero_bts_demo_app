#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <core/log.h>
#include <targets/f7/ble_glue/furi_ble/gatt.h>
#include <targets/f7/ble_glue/ble_app.h>
#include <furi_hal_bt.h>
#include <bt/bt_service/bt.h>
#include <furi_ble/profile_interface.h>
#include <targets/f7/ble_glue/furi_ble/event_dispatcher.h>
#include <targets/f7/ble_glue/gap.h>

// AN5289: 4.7, in order to use flash controller interval must be at least 25ms + advertisement, which is 30 ms
// Since we don't use flash controller anymore interval can be lowered to 7.5ms
#define CONNECTION_INTERVAL_MIN (0x06)
// Up to 45 ms
#define CONNECTION_INTERVAL_MAX (0x24)

typedef enum {
    ViewIdMainMenu, // Add a view ID for text display
    ViewIdStartPage
} ViewId;

typedef struct {
    View* text_view; // Add a view for displaying text
} StartPage;

typedef struct {
    uint16_t svc_handle;
    BleGattCharacteristicInstance char_instance;
    //GapSvcEventHandler* event_handler;
} BleService;

typedef struct {
    FuriHalBleProfileBase base;
    BleService* svc;
} BleProfile;

typedef struct {
    BleGattCharacteristicInstance* char_instance;
    BleProfile* ble_profile;
    FuriHalBleProfileBase* base;
    Bt* bt;
} BleApp;

typedef struct {
    const char* ble_custom_svc_text;
} MainModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    StartPage* start_page;
    BleApp* ble;
} BtsApp;

typedef struct {
    const char* device_name_prefix;
    uint16_t mac_xor;
} BleAppProfileParams;

// Custom Service UUID
static const Service_UUID_t service_uuid = {
    .Service_UUID_128 = {
        0x00,
        0x00,
        0xfe,
        0x60,
        0xcc,
        0x7a,
        0x48,
        0x2a,
        0x98,
        0x4a,
        0x7f,
        0x2e,
        0xd5,
        0xb3,
        0xe5,
        0x8f}};
/*
static const Service_UUID_t service_uuid = {
    .Service_UUID_128 = {
        0x33,
        0xa9,
        0xb5,
        0x3e,
        0x87,
        0x5d,
        0x1a,
        0x8e,
        0xc8,
        0x47,
        0x5e,
        0xae,
        0x6d,
        0x66,
        0xf6,
        0x03}};
*/

// Service UUID
/*
const Service_UUID_t service_uuid = {
    .Service_UUID_128 = {
        0x12,
        0x34,
        0x56,
        0x78,
        0x9a,
        0xbc,
        0xde,
        0xf0,
        0x12,
        0x34,
        0x56,
        0x78,
        0x9a,
        0xbc,
        0xde,
        0xf0}};

*/

// Some constants
static const char dev_info_man_name[] = "Flipper Devices Inc.";
static const char* ble_custom_svc_success_text = "BleApp service started...";
static const char* ble_custom_svc_failure_text = "BleApp service failed to start!";

// CUSTOM
BleGattCharacteristicParams char_descriptor = {
    .name = "Test",
    .uuid =
        {.Char_UUID_128 =
             {0x12,
              0x56,
              0x56,
              0x78,
              0x9a,
              0xbc,
              0xde,
              0xf0,
              0x12,
              0x34,
              0x56,
              0x78,
              0x9a,
              0xbc,
              0xde,
              0xf0}},
    .data_prop_type = FlipperGattCharacteristicDataFixed,
    .uuid_type = UUID_TYPE_128,
    //USE THIS FOR CALLBACK
    .data.callback.fn = NULL,
    .data.callback.context = NULL,
    .data.fixed.length = sizeof(dev_info_man_name) - 1,
    .data.fixed.ptr = (const uint8_t*)&dev_info_man_name,
    .char_properties = CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_AUTHEN_READ,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_CONSTANT};

/*
BleGattCharacteristicParams char_descriptor = {
    .name = "RPC status",
    .data_prop_type = FlipperGattCharacteristicDataFixed,
    .data.fixed.length = sizeof(uint32_t),
    .uuid.Char_UUID_128 = BLE_SVC_SERIAL_RPC_STATUS_UUID,
    .uuid_type = UUID_TYPE_128,
    .char_properties = CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_AUTHEN_WRITE,
    .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
    .is_variable = CHAR_VALUE_LEN_CONSTANT};
*/

static GapConfig template_config = {
    .adv_service_uuid = 0x3080, //AN ADVERTISING SERVICE UUID FROM STANDARD BLE
    .appearance_char = 0x8600, //AN APPERANCE CHARACTERISTIC FROM STANDARD BLE
    .bonding_mode = true,
    .pairing_method = GapPairingNone, //CHOOSE A PAIRING METHOD
    .conn_param = {
        .conn_int_min = CONNECTION_INTERVAL_MIN,
        .conn_int_max = CONNECTION_INTERVAL_MAX,
        .slave_latency = 0,
        .supervisor_timeout = 0}};

// USER YOUR OWN PARAMETERS
static const BleAppProfileParams ble_profile_params = {
    .device_name_prefix = "TEST",
    .mac_xor = 0x0002,
};

// ANOTHER EXAMPLE OF GAP CONFIGURATION
/*
static GapConfig template_config = {
    .adv_service_uuid = 0x1812,
    .appearance_char = GAP_APPEARANCE_KEYBOARD,
    .bonding_mode = true,
    .pairing_method = GapPairingPinCodeVerifyYesNo,
    .conn_param =
        {
            .conn_int_min = CONNECTION_INTERVAL_MIN,
            .conn_int_max = CONNECTION_INTERVAL_MAX,
            .slave_latency = 0,
            .supervisor_timeout = 0,
        },
};*/

// Function to convert 128-bit UUID to ASCII UUID.
// The order of bytes in the UUID is reversed.
char* uuid128_to_ascii(const uint8_t input_uuid[16]) {
    uint8_t uuid[16];
    for(int i = 0; i < 16; i++) {
        uuid[i] = input_uuid[15 - i];
    }
    char* ascii_uuid = (char*)malloc(37 * sizeof(char));
    if(ascii_uuid == NULL) {
        return NULL; // Handle memory allocation failure
    }

    snprintf(
        ascii_uuid,
        37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0],
        uuid[1],
        uuid[2],
        uuid[3],
        uuid[4],
        uuid[5],
        uuid[6],
        uuid[7],
        uuid[8],
        uuid[9],
        uuid[10],
        uuid[11],
        uuid[12],
        uuid[13],
        uuid[14],
        uuid[15]);

    return ascii_uuid;
}

// Function to combine UUID ASCII string with arbitrary text
char* combine_uuid_with_text(const uint8_t uuid[16], const char* text) {
    char* ascii_uuid = uuid128_to_ascii(uuid);
    if(ascii_uuid == NULL) {
        return NULL; // Handle memory allocation failure
    }

    size_t text_len = strlen(text);
    size_t combined_len = text_len + 37 + 1; // 37 for UUID, 1 for null terminator

    char* combined_str = (char*)malloc(combined_len * sizeof(char));
    if(combined_str == NULL) {
        free(ascii_uuid);
        return NULL; // Handle memory allocation failure
    }

    snprintf(combined_str, combined_len, "%s %s", text, ascii_uuid);

    free(ascii_uuid);
    return combined_str;
}

// Example of a callback function for a characteristic
/*
static bool
    dev_info_char_data_callback(const void* context, const uint8_t** data, uint16_t* data_len) {
    *data_len = (uint16_t)strlen(context); //-V1029
    if(data) {
        *data = (const uint8_t*)context;
    }
    return false;
}
*/

BleService* ble_svc_start(void) {
    BleService* svc = malloc(sizeof(BleService));

    //EVENT HANDLER FOR CUSTOM INTERACTIONS DURING BLUETOOTH COMMUNICATION
    /*
    serial_svc->event_handler =
        ble_event_dispatcher_register_svc_handler(ble_svc_serial_event_handler, serial_svc);
    */

    FURI_LOG_I("BleApp", "Service started");
    if(!ble_gatt_service_add(UUID_TYPE_128, &service_uuid, PRIMARY_SERVICE, 12, &svc->svc_handle)) {
        free(svc);
        return NULL;
    }

    FURI_LOG_I("BleApp", "Service added");
    FURI_LOG_I("BleApp", "Service handle: %d", svc->svc_handle);

    ble_gatt_characteristic_init(svc->svc_handle, &char_descriptor, &svc->char_instance);

    FURI_LOG_I("BleApp", "Characteristic added");
    FURI_LOG_I(
        "BleApp",
        combine_uuid_with_text(char_descriptor.uuid.Char_UUID_128, "Characteristic UUID:"));

    //uint8_t protocol_mode = 1;
    //NULL FOR FIXED LENGTH
    //1 FOR CALLBACK
    ble_gatt_characteristic_update(svc->svc_handle, &svc->char_instance, NULL);

    return svc;
}

void ble_svc_stop(BleService* svc) {
    furi_check(svc);

    //ble_event_dispatcher_unregister_svc_handler(svc->event_handler);

    ble_gatt_characteristic_delete(svc->svc_handle, &svc->char_instance);
    FURI_LOG_I("BleApp", "Characteristic deleted");

    ble_gatt_service_delete(svc->svc_handle);
    FURI_LOG_I("BleApp", "Service deleted");

    free(svc);
}

/*Profile init*/
static FuriHalBleProfileBase* ble_profile_start(FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);

    FURI_LOG_I("BleApp", "Profile started");

    BleProfile* profile = malloc(sizeof(BleProfile));

    FURI_LOG_I("BleApp", combine_uuid_with_text(service_uuid.Service_UUID_128, "Service UUID:"));
    FURI_LOG_I("BleApp", "Service startedXXX");
    profile->svc = ble_svc_start();
    //You can start other services here

    return &profile->base;
}

static void ble_profile_stop(FuriHalBleProfileBase* profile) {
    UNUSED(profile);

    FURI_LOG_I("BleApp", "Profile stopped");

    BleProfile* ble_profile = (BleProfile*)profile;

    ble_svc_stop(ble_profile->svc);
    //You can stop other services here
}

// Get the configuration for the profile (SAME DEVICE)
/*
static void ble_profile_get_config(GapConfig* config, FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);

    FURI_LOG_I("BleApp", "Getting config");
    //furi_check(config);
    memcpy(config, &template_config, sizeof(GapConfig));
    // Set mac address
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
    // Set advertise name
    strlcpy(
        config->adv_name,
        furi_hal_version_get_ble_local_device_name_ptr(),
        FURI_HAL_VERSION_DEVICE_NAME_LENGTH);
    config->adv_service_uuid |= furi_hal_version_get_hw_color();
}
*/

/** Extracted from hid_profile.c **/
static void ble_profile_get_config(GapConfig* config, FuriHalBleProfileParams profile_params) {
    BleAppProfileParams* ble_app_profile_params = (BleAppProfileParams*)profile_params;

    furi_check(config);

    memcpy(config, &template_config, sizeof(GapConfig));
    // Set mac address
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));

    // Change MAC address for HID profile
    config->mac_address[2]++;
    if(ble_app_profile_params) {
        config->mac_address[0] ^= ble_app_profile_params->mac_xor;
        config->mac_address[1] ^= ble_app_profile_params->mac_xor >> 8;
    }

    // Set advertise name
    memset(config->adv_name, 0, sizeof(config->adv_name));
    FuriString* name = furi_string_alloc_set(furi_hal_version_get_ble_local_device_name_ptr());

    const char* clicker_str = "Control";
    if(ble_app_profile_params && ble_app_profile_params->device_name_prefix) {
        clicker_str = ble_app_profile_params->device_name_prefix;
    }
    furi_string_replace_str(name, "Flipper", clicker_str);
    if(furi_string_size(name) >= sizeof(config->adv_name)) {
        furi_string_left(name, sizeof(config->adv_name) - 1);
    }
    memcpy(config->adv_name, furi_string_get_cstr(name), furi_string_size(name));
    furi_string_free(name);
}

/** The actual custom profile definition **/
const FuriHalBleProfileTemplate profile_callbacks = {
    .start = ble_profile_start,
    .stop = ble_profile_stop,
    .get_gap_config = ble_profile_get_config};

// A gap event callback example
/*
bool gap_event(GapEvent event, void* context) {
    UNUSED(context);
    switch(event.type) {
    case GapEventTypeConnected:
        FURI_LOG_I("BleApp", "Connected");
        break;
    case GapEventTypeDisconnected:
        FURI_LOG_I("BleApp", "Disconnected");
        break;
    case GapEventTypeStartAdvertising:
        FURI_LOG_I("BleApp", "Start advertising");
        break;
    case GapEventTypeStopAdvertising:
        FURI_LOG_I("BleApp", "Stop advertising");
        break;
    case GapEventTypePinCodeShow:
        FURI_LOG_I("BleApp", "Pin code show");
        break;
    case GapEventTypePinCodeVerify:
        FURI_LOG_I("BleApp", "Pin code verify");
        break;
    case GapEventTypeUpdateMTU:
        FURI_LOG_I("BleApp", "Update MTU");
        break;
    case GapEventTypeBeaconStart:
        FURI_LOG_I("BleApp", "Beacon start");
        break;
    case GapEventTypeBeaconStop:
        FURI_LOG_I("BleApp", "Beacon stop");
        break;
    default:
        FURI_LOG_I("BleApp", "Unknown event");
        break;
    }

    return true;
}
*/

/** This is a custom ble gatt svc appended to a test gap ble device **/
bool start_custom_ble_gatt_svc(BtsApp* app) {
    UNUSED(profile_callbacks);
    if(furi_hal_bt_is_active()) {
        FURI_LOG_I("BleApp", "BT is working...");

        if(app->ble->bt) {
            FURI_LOG_I("BleApp", "BT not initialized.");
            app->ble->bt = furi_record_open(RECORD_BT);
        }

        FURI_LOG_I("BleApp", "BT disconnecting...");
        bt_disconnect(app->ble->bt);

        // Wait 2nd core to update nvm storage;
        // i have seen that in the original code, the delay is 200ms
        furi_delay_ms(200);

        // Starting a custom ble profile for service creation
        FURI_LOG_I("BleApp", "BT profile starting...");
        app->ble->base =
            bt_profile_start(app->ble->bt, &profile_callbacks, (void*)&ble_profile_params);
        FURI_LOG_I("BleApp", "BT profile started");

        if(!app->ble->base) {
            FURI_LOG_E("BleApp", "Failed to start the app");
            return false;
        }

        app->ble->base->config = &profile_callbacks;

        // Advertising new service (and device)
        FURI_LOG_I("BleApp", "BT profile start advertising...");
        furi_hal_bt_start_advertising();

    } else {
        FURI_LOG_I("BleApp", "Please, enable the Bluetooth and restart the app");
        return false;
    }

    return true;
}

/** Simple text callback to notify app status**/
static void text_view_draw_callback(Canvas* canvas, void* context) {
    furi_check(context);
    MainModel* model = context;
    const char* text = model->ble_custom_svc_text;

    if(!text) {
        return;
    }

    canvas_clear(canvas);
    canvas_draw_str(canvas, 10, 10, text);
}

/** Triggered on Start button click **/
static void submenu_callback(void* context, uint32_t index) {
    FURI_LOG_I("BtsApp", "Submenu item selected: %ld", index);
    furi_check(context);
    BtsApp* app = context;
    UNUSED(index);
    // Implement your submenu callback here

    //Service uuid (and updating view model)
    if(start_custom_ble_gatt_svc(app)) {
        with_view_model(
            app->start_page->text_view,
            MainModel * model,
            { model->ble_custom_svc_text = ble_custom_svc_success_text; },
            true);

    } else {
        with_view_model(
            app->start_page->text_view,
            MainModel * model,
            { model->ble_custom_svc_text = ble_custom_svc_failure_text; },
            true);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdStartPage);
}

/** Used to exit from the app (interrupting app loop) **/
bool view_dispatcher_navigation_callback_event(void* context) {
    UNUSED(context);
    FURI_LOG_I("BtsApp", "Navigation callback");

    // Closing app
    return false;
}

/** The entrypoint: name quite unfortunate but this is my first Flipper zero application ;) **/
int32_t bts_app_app(void* p) {
    UNUSED(p);
    FURI_LOG_I("BtsApp", "Start");

    BtsApp app;
    app.gui = furi_record_open(RECORD_GUI);
    app.view_dispatcher = view_dispatcher_alloc();
    app.ble = malloc(sizeof(BleApp));
    app.ble->bt = furi_record_open(RECORD_BT);

    // Create a submenu
    app.submenu = submenu_alloc();
    submenu_add_item(app.submenu, "Start", 0, submenu_callback, &app);

    // Alternative to submenu callback (native events)
    // view_set_input_callback(submenu_get_view(app.submenu), input_event_callback);

    // Add submenu to ViewDispatcher
    view_dispatcher_add_view(app.view_dispatcher, ViewIdMainMenu, submenu_get_view(app.submenu));

    // Initializing
    app.start_page = malloc(sizeof(StartPage));

    FURI_LOG_I("BtsApp", "Text View Creation");

    // Create a view for displaying text
    app.start_page->text_view = view_alloc();
    FURI_LOG_I("BtsApp", "Text View Allocated");

    FURI_LOG_I("BtsApp", "Text View Adding");
    // Attach views to ViewDispatcher
    view_dispatcher_add_view(app.view_dispatcher, ViewIdStartPage, app.start_page->text_view);
    view_dispatcher_set_event_callback_context(app.view_dispatcher, &app);
    view_dispatcher_set_navigation_event_callback(
        app.view_dispatcher, view_dispatcher_navigation_callback_event);
    // Set up text view
    view_set_context(app.start_page->text_view, &app);
    view_allocate_model(app.start_page->text_view, ViewModelTypeLockFree, sizeof(MainModel));
    view_set_draw_callback(app.start_page->text_view, text_view_draw_callback);
    FURI_LOG_I("BtsApp", "Text View Done");

    view_dispatcher_switch_to_view(app.view_dispatcher, ViewIdMainMenu);

    // Add debug logging before running the dispatcher
    FURI_LOG_I("BtsApp", "Running view dispatcher");

    view_dispatcher_attach_to_gui(app.view_dispatcher, app.gui, ViewDispatcherTypeFullscreen);

    // OBSOLETE
    //view_dispatcher_enable_queue(app.view_dispatcher);

    // Run the ViewDispatcher => application loop (on exit the loop ends, and
    // the below cleanup code is executed)
    view_dispatcher_run(app.view_dispatcher);

    // Add debug logging after running the dispatcher
    FURI_LOG_I("BtsApp", "View dispatcher run completed");

    // Cleanup

    // BLE

    if(app.ble->bt) {
        FURI_LOG_I("BtsApp", "Disconnecting from BT");
        bt_disconnect(app.ble->bt);

        // Wait 2nd core to update nvm storage
        furi_delay_ms(200);

        // Maybe not needed because not changing the original
        // storage path for bluetooth keys (which should be better)
        FURI_LOG_I("BtsApp", "Restoring default profile");
        bt_keys_storage_set_default_path(app.ble->bt);

        // Maybe not needed?
        furi_hal_bt_reinit();
        bt_profile_restore_default(app.ble->bt);

        FURI_LOG_I("BtsApp", "Closing BT record");
        furi_record_close(RECORD_BT);
    }

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

    submenu_free(app.submenu);

    return 0;
}
