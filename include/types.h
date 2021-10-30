#ifndef __TYPES_H__
#define __TYPES_H__

#include "bluetooth_internal.h"
#include "gdbus.h"

enum STATE { LIST = 0, DEVICE, PAIR };

enum ENTRY {
    ENTRY_DEVICE = 1,
    ENTRY_CONTROLLER_PROP = 1 << 2,
    ENTRY_SCAN = 1 << 3,
    ENTRY_DEVICE_PROP = 1 << 4,
    ENTRY_DEVICE_PAIR = 1 << 5,
    ENTRY_DEVICE_CONNECT = 1 << 6,
    ENTRY_MENU_PAIR = 1 << 7,
    ENTRY_MENU_LIST = 1 << 8,
    ENTRY_ALLOCATED = 1 << 9
};

typedef struct {
    GDBusProxy *remote_proxy;
    b32 powered;
    b32 discoverable;
    b32 discovering;
} Controller;

typedef struct {
    GDBusProxy *remote_proxy;
    char *address;
    char *name;
    b32 connected;
    b32 paired;
    b32 trusted;

} Device;

typedef struct {
    char *text;
    u32 flags;
    union {
        u32 device;
        u32 controller_prop;
        u32 device_prop;
    };
} Entry;

typedef struct {
    u32 state;

    Entry *entries;
    u32 num_entries;
    u32 size_entries;

    Controller *controller;

    Device *devices;
    u32 num_devices;
    u32 num_paired_devices;
    u32 size_devices;
    u32 current_device;

    GDBusClient *client;
    DBusConnection *dbus_conn;

    char *command_status;

} BluetoothModePrivateData;

#endif
