/**
 * rofi-bluetooth
 *
 * MIT/X11 License
 * Copyright (c) 2020 Rahul Aggarwal <rahulaggarwal965@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define G_LOG_DOMAIN "BluetoothMode"

#include <errno.h>
#include <gmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gdbus.h>

#include <rofi/mode.h>
#include <rofi/mode-private.h>
#include <rofi/helper.h>

#include <stdbool.h>
#include <stdint.h>

#include "bluetooth_internal.h"
#include "constants.h"
#include "types.h"

G_MODULE_EXPORT Mode mode;

#define ENTRY(i) &pd->entries[i]
#define TF(val) true_false_array[val]
#define C_TF(i) true_false_array[(&pd->controller->powered)[i]]
#define DS(i, val) device_strings[i][val]

global_variable struct GenericCallbackData {
    BluetoothModePrivateData *pd;
    void *data;
} generic_callback_data = {.pd = NULL, .data = NULL};

internal void free_generic_callback_data(void *user_data) {
    struct GenericCallbackData *data = user_data;
    g_free(data->data);
}

extern void rofi_view_reload(void);

inline internal void get_property(GDBusProxy *proxy, const char *name, void *data) {
    DBusMessageIter iter;
    if (g_dbus_proxy_get_property(proxy, name, &iter) == false) return;
    dbus_message_iter_get_basic(&iter, data);
}

internal void debug_print_device(Device *device) {
    g_debug("Device {\n\taddress: %s\n\tname: %s\n\tPaired: %d\n\tTrusted: %d\n\tConnected: %d\n}", device->address,
            device->name, device->paired, device->trusted, device->connected);
}

internal void debug_print_controller(Controller *controller) {
    g_debug("Controller {\n\tPowered: %d\n\tDiscoverable: %d\n\tDiscovering: %d\n}", controller->powered,
            controller->discoverable, controller->discovering);
}

inline internal void resize_entries_if_needed(BluetoothModePrivateData *pd, u32 new_num_entries) {

    pd->num_entries = new_num_entries;
    if (pd->size_entries < pd->num_entries) {
        pd->size_entries = pd->num_entries;
        pd->entries = g_realloc(pd->entries, sizeof(Entry) * pd->size_entries);
    }
}

inline internal void set_entry(Entry *entry, char *text, u32 flags, u32 data) {
    entry->text = text;
    entry->flags = flags;
    entry->device = data;
}

internal void update_entries(BluetoothModePrivateData *pd) {

    if (pd->state == LIST) {
        resize_entries_if_needed(pd, pd->num_paired_devices + (pd->controller != NULL) * 3 + 1);
        u32 i = 0;
        for (u32 j = 0; j < pd->num_devices; j++) {
            Device *device = &pd->devices[j];
            if (device->paired) {
                u64 nb = strlen(device->name);
                u64 nub = g_utf8_strlen(device->name, nb);
                g_debug("extra offset: %d", (u32) (20 + nb - nub));
                set_entry(ENTRY(i), g_strdup_printf("%-*s%-10s", (u32) (20 + nb - nub), device->name, TF(device->connected)),
                          ENTRY_DEVICE | ENTRY_ALLOCATED, j);
                i++;
            }
        }
        set_entry(ENTRY(i), " Pair Device", ENTRY_MENU_PAIR, 0);
        i++;
        for (; i < pd->num_entries; i++) {
            u32 a = i - pd->num_paired_devices - 1;
            set_entry(ENTRY(i), g_strdup_printf("%s: %s", controller_props[a], C_TF(a)),
                      ENTRY_CONTROLLER_PROP | ENTRY_ALLOCATED, a);
        }
        // TODO(rahul): maybe cleanup because this is ugly
        pd->entries[--i].flags = ENTRY_SCAN | ENTRY_ALLOCATED;
    } else if (pd->state == PAIR) {
        u32 num_unpaired_devices = pd->num_devices - pd->num_paired_devices;
        resize_entries_if_needed(pd, num_unpaired_devices + 1);
        u32 i = 0;
        for (u32 j = 0; j < pd->num_devices; j++) {
            Device *device = &pd->devices[j];
            if (!device->paired) {
                set_entry(ENTRY(i), g_strdup_printf("%-20s%-s", device->address, device->name),
                          ENTRY_DEVICE | ENTRY_ALLOCATED, j);
                i++;
            }
        }
        set_entry(ENTRY(i), " Back", ENTRY_MENU_LIST, 0);
    } else if (pd->state == DEVICE) {
        Device *dev = &pd->devices[pd->current_device];

        if (dev->paired) {
            resize_entries_if_needed(pd, 4);
            set_entry(ENTRY(0), DS(0, dev->connected), ENTRY_DEVICE_CONNECT, pd->current_device);
            set_entry(ENTRY(2), DS(2, dev->trusted), ENTRY_DEVICE_PROP, 2);
        } else
            resize_entries_if_needed(pd, 2);
        u32 pair_index = (pd->num_entries >> 1) - 1;
        set_entry(ENTRY(pair_index), DS(1, dev->paired), ENTRY_DEVICE_PAIR, pd->current_device);
        set_entry(ENTRY(pd->num_entries - 1), " Back", ENTRY_MENU_LIST, 0);
    }
}

internal void proxy_added(GDBusProxy *proxy, void *user_data) {
    const char *interface;
    Mode *sw = (Mode *)user_data;
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);

    interface = g_dbus_proxy_get_interface(proxy);
    if (!strcmp(interface, "org.bluez.Device1")) {
        if (pd->size_devices <= pd->num_devices) {
            pd->size_devices *= 2;
            pd->devices = g_realloc(pd->devices, sizeof(Device) * pd->size_devices);
        }
        Device *dev = &pd->devices[pd->num_devices];
        dev->remote_proxy = proxy;
        get_property(proxy, "Address", &dev->address);
        get_property(proxy, "Alias", &dev->name);
        get_property(proxy, "Connected", &dev->connected);
        get_property(proxy, "Paired", &dev->paired);
        get_property(proxy, "Trusted", &dev->trusted);

        debug_print_device(dev);
        pd->num_devices++;
        if (dev->paired)
            pd->num_paired_devices++;
        update_entries(pd);
        rofi_view_reload();

        if (pd->state == PAIR) {
            // triggGer redraw
        }
    } else if (!strcmp(interface, "org.bluez.Adapter1")) {
        if (!pd->controller) {
            b32 b = true;
            pd->controller = g_malloc0(sizeof(Controller));
            pd->controller->remote_proxy = proxy;
            g_dbus_proxy_set_property_basic(proxy, "Pairable", DBUS_TYPE_BOOLEAN, &b, NULL, NULL, NULL);
            get_property(proxy, "Powered", &pd->controller->powered);
            get_property(proxy, "Discoverable", &pd->controller->discoverable);
            get_property(proxy, "Discovering", &pd->controller->discovering);

            debug_print_controller(pd->controller);
            update_entries(pd);
            rofi_view_reload();
        }
    }
}

inline internal u32 find_device(GDBusProxy *proxy, Device *devices, u32 num_devices) {

    u32 i = 0;
    for (; i < num_devices; i++) {
        if (devices[i].remote_proxy == proxy)
            break;
    }

    return i;
}

internal void property_changed(GDBusProxy *proxy, const char *name, DBusMessageIter *iter, void *user_data) {
    Mode *sw = (Mode *)user_data;
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);

    const char *interface;

    interface = g_dbus_proxy_get_interface(proxy);

    if (!strcmp(interface, "org.bluez.Device1")) {

        g_debug("property_name_changed: %s", name);
        u32 dev_index = find_device(proxy, pd->devices, pd->num_devices);
        if (dev_index != pd->num_devices) {
            Device *dev = &pd->devices[dev_index];
            b32 update = false;
            // @Robustness @Slowness, when is "ServicesResolved" actually called, it could be
            // for more than connected. If so, we want to make sure that we only
            // really test for all this stuff when we need to
            if (!strcmp(name, "Connected") || !strcmp(name, "ServicesResolved")) {
                dbus_message_iter_get_basic(iter, &dev->connected);
                if (pd->state == DEVICE && pd->current_device == dev_index) {
                    g_debug("detect connect change and queue update");
                    g_debug("command_status: %s", pd->command_status);
                    Entry *entry = &pd->entries[0];
                    entry->text = device_strings[0][dev->connected];
                    update = true;
                } else if (pd->state == LIST) {
                    for (u32 i = 0; i < pd->num_entries; i++) {
                        Entry *entry = &pd->entries[i];
                        if (entry->device == dev_index) {
                            g_free(entry->text);
                            entry->text = g_strdup_printf("%-20s%-10s", dev->name, true_false_array[dev->connected]);
                            break;
                        }
                    }
                    update = true;
                }
            } else if (!strcmp(name, "Paired")) {
                dbus_message_iter_get_basic(iter, &dev->paired);
                pd->num_paired_devices += 2 * dev->paired - 1;
                update = true;
                update_entries(pd);
            } else if (!strcmp(name, "Trusted")) {
                dbus_message_iter_get_basic(iter, &dev->trusted);
                if (pd->state == DEVICE && pd->current_device == dev_index) {
                    Entry *entry = &pd->entries[2];
                    entry->text = device_strings[2][dev->trusted];
                    update = true;
                }
            }
            debug_print_device(dev);
            if (update)
                rofi_view_reload();
        }
    } else if (!strcmp(interface, "org.bluez.Adapter1")) {
        if (pd->controller->remote_proxy == proxy) {
            u32 i = 0;
            b32 update = false;
            b32 *controller_info = &pd->controller->powered;
            g_debug("property_name_changed: %s", name);
            for (; i < 3; i++) {
                if (!strcmp(name, controller_props[i])) {
                    dbus_message_iter_get_basic(iter, &(controller_info[i]));
                    update = true;
                    break;
                }
            }
            if (i == 2 && pd->controller->discovering) {
                pd->state = PAIR;
                sw->display_name = "Pair:";
                update_entries(pd);
            } else if (update && pd->state == LIST) {
                Entry *entry = &pd->entries[pd->num_paired_devices + i + 1];
                g_free(entry->text);
                entry->text = g_strdup_printf("%s: %s", name, true_false_array[controller_info[i]]);
            }

            rofi_view_reload();
            debug_print_controller(pd->controller);
        }
    }
}

internal void proxy_removed(GDBusProxy *proxy, void *user_data) {
    const char *interface;
    Mode *sw = (Mode *)user_data;
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);

    interface = g_dbus_proxy_get_interface(proxy);
    if (!strcmp(interface, "org.bluez.Device1")) {

        u32 dev_index = find_device(proxy, pd->devices, pd->num_devices);

        bool update = false;
        if (dev_index != pd->num_devices) {
            if (pd->devices[dev_index].paired) {
                pd->num_paired_devices--;
                update = (pd->state == LIST);
            } else {
                update = (pd->state == PAIR);
            }
            // TODO(rahul): we can just do a memcpy here / unordered remove
            // unordered remove
            pd->devices[dev_index] = pd->devices[--pd->num_devices];
        }
        if (pd->state == DEVICE && pd->current_device == dev_index) {
            pd->state = LIST;
            update = true;
        }
        if (update) {
            update_entries(pd);
            rofi_view_reload();
        }
    }
}

internal int bluetooth_mode_init(Mode *sw) {
    if (mode_get_private_data(sw) == NULL) {
        BluetoothModePrivateData *pd = g_malloc0(sizeof(*pd));
        mode_set_private_data(sw, (void *)pd);

        pd->dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);
        g_dbus_attach_object_manager(pd->dbus_conn);

        pd->client = g_dbus_client_new(pd->dbus_conn, "org.bluez", "/org/bluez");
        g_dbus_client_set_proxy_handlers(pd->client, proxy_added, proxy_removed, property_changed, sw);

        generic_callback_data.pd = pd;

        pd->num_devices = 0;
        pd->num_paired_devices = 0;
        pd->devices = g_malloc0(sizeof(Device));
        pd->size_devices = 1;
        pd->state = LIST;

        pd->current_device = 0;

        sw->display_name = "Device:";

        pd->num_entries = 0;
        pd->entries = g_malloc0(sizeof(Entry));
        pd->size_entries = 1;
    }
    return true;
}
internal unsigned int bluetooth_mode_get_num_entries(const Mode *sw) {
    const BluetoothModePrivateData *pd = (const BluetoothModePrivateData *)mode_get_private_data(sw);
    return pd->num_entries;
}

/** CALLBACKS **/

internal void generic_callback(const DBusError *error, void *user_data) {
    struct GenericCallbackData *data = user_data;
    BluetoothModePrivateData *pd = data->pd;
    char *str = data->data;

    g_free(pd->command_status);
    if (dbus_error_is_set(error)) {
        pd->command_status =
            g_strdup_printf("<span foreground=\"red\" weight=\"bold\">Error:</span> Failed to set %s\n", str);
    } else {
        pd->command_status =
            g_strdup_printf("<span foreground =\"green\" weight=\"bold\">Success:</span> Changed %s\n", str);
    }
}

internal void connect_callback(DBusMessage *message, void *user_data) {
    struct GenericCallbackData *data = user_data;
    BluetoothModePrivateData *pd = data->pd;
    b32 connected = GPOINTER_TO_UINT(data->data);

    g_debug("connected: %s", TF(connected));

    DBusError err;

    g_free(pd->command_status);
    dbus_error_init(&err);
    if (dbus_set_error_from_message(&err, message)) {
        pd->command_status = g_strdup_printf("<span foreground=\"red\" weight=\"bold\">Error:</span> Failed to %s\n",
                                             (connected) ? "disconnect" : "connect");
        dbus_error_free(&err);
    }
    pd->command_status = g_strdup_printf("<span foreground =\"green\" weight=\"bold\">Success:</span> %s\n",
                                         (connected) ? "Disconnected" : "Connected");
    g_debug("command_status: %s", pd->command_status);
}

internal void pair_callback(DBusMessage *message, void *user_data) {
    struct GenericCallbackData *data = user_data;
    BluetoothModePrivateData *pd = data->pd;

    DBusError err;

    g_free(pd->command_status);

    dbus_error_init(&err);
    if (dbus_set_error_from_message(&err, message)) {
        pd->command_status = g_strdup_printf("<span foreground=\"red\" weight=\"bold\">Error:</span> Failed to pair\n");
        dbus_error_free(&err);
    }
    pd->command_status = g_strdup_printf("<span foreground =\"green\" weight=\"bold\">Success:</span> Paired\n");
}

internal void remove_device_setup(DBusMessageIter *iter, void *user_data) {
    struct GenericCallbackData *data = user_data;

    const char *path = data->data;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

internal void remove_callback(DBusMessage *message, void *user_data) {
    struct GenericCallbackData *data = user_data;
    BluetoothModePrivateData *pd = data->pd;

    DBusError err;

    g_free(pd->command_status);

    dbus_error_init(&err);
    if (dbus_set_error_from_message(&err, message)) {
        pd->command_status =
            g_strdup_printf("<span foreground=\"red\" weight=\"bold\">Error:</span> Failed to remove\n");
        dbus_error_free(&err);
    }
    pd->command_status = g_strdup_printf("<span foreground =\"green\" weight=\"bold\">Success:</span> Removed\n");
}

internal void scan_callback(DBusMessage *message, void *user_data) {
    struct GenericCallbackData *data = user_data;
    BluetoothModePrivateData *pd = data->pd;
    b32 scan = GPOINTER_TO_UINT(data->data);

    DBusError err;

    dbus_error_init(&err);
    if (dbus_set_error_from_message(&err, message)) {
        pd->command_status = g_strdup_printf(
            "<span foreground=\"red\" weight=\"bold\">Error:</span> Failed to %s discovery\n", scan ? "Stop" : "Start");
        dbus_error_free(&err);
    }

    pd->command_status = g_strdup_printf("<span foreground=\"green\" weight=\"bold\">Success:</span> %s discovery\n",
                                         scan ? "stopped" : "started");
}

void switch_state(Mode *sw, const u32 next_state, const char *next_display_name) {
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);

    pd->state = next_state;
    g_free(pd->command_status);
    pd->command_status = NULL;
    sw->display_name = next_display_name;
    update_entries(pd);
}

internal ModeMode bluetooth_mode_result(Mode *sw, int mretv, char **input, unsigned int selected_line) {
    ModeMode retv = RELOAD_DIALOG;
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);

    Entry *entry = &pd->entries[selected_line];

    if (mretv & MENU_OK) {
        switch (entry->flags & ~ENTRY_ALLOCATED) {
        case ENTRY_MENU_LIST:
            switch_state(sw, LIST, "Device:");
            break;
        case ENTRY_MENU_PAIR:
            switch_state(sw, PAIR, "Pair Device:");
            break;
        case ENTRY_DEVICE:
            pd->current_device = entry->device;
            switch_state(sw, DEVICE, pd->devices[pd->current_device].name);
            break;
        case ENTRY_DEVICE_PROP: {
            b32 prop;
            Device *dev = &pd->devices[pd->current_device];
            const char *prop_name = device_props[entry->controller_prop >> 1];
            prop = !(&dev->connected)[entry->device_prop];

            generic_callback_data.data = g_strdup_printf("[%s] to %s", prop_name, true_false_array[prop]);
            if (g_dbus_proxy_set_property_basic(dev->remote_proxy, prop_name, DBUS_TYPE_BOOLEAN, &prop,
                                                generic_callback, &generic_callback_data,
                                                free_generic_callback_data) == false)
                g_free(generic_callback_data.data);
        } break;
        case ENTRY_DEVICE_CONNECT: {
            Device *dev = &pd->devices[entry->device];
            const char *method;

            method = (dev->connected) ? "Disconnect" : "Connect";
            generic_callback_data.data = GUINT_TO_POINTER(dev->connected);

            if (g_dbus_proxy_method_call(dev->remote_proxy, method, NULL, connect_callback, &generic_callback_data,
                                         NULL) == false) {
                g_free(pd->command_status);
                pd->command_status =
                    g_strdup_printf("<span foreground=\"red\" weight=\"bold\">Error:</span> Failed to %s\n",
                                    (dev->connected) ? "disconnect" : "connect");
            };
        } break;
        case ENTRY_DEVICE_PAIR: {
            Device *dev = &pd->devices[entry->device];
            const char *path = g_dbus_proxy_get_path(dev->remote_proxy);
            generic_callback_data.data = g_strdup(path);

            if (dev->paired) {
                if (g_dbus_proxy_method_call(pd->controller->remote_proxy, "RemoveDevice", remove_device_setup,
                                             remove_callback, &generic_callback_data,
                                             free_generic_callback_data) == false) {
                    g_free(generic_callback_data.data);
                    retv = MODE_EXIT;
                }
            } else {
                g_dbus_proxy_method_call(dev->remote_proxy, "Pair", NULL, pair_callback, &generic_callback_data, NULL);
            }
        } break;
        case ENTRY_CONTROLLER_PROP: {
            b32 prop;
            char *str;
            const char *prop_name = controller_props[entry->controller_prop];
            prop = !(&pd->controller->powered)[entry->controller_prop];

            generic_callback_data.data = g_strdup_printf("[%s] to %s", prop_name, true_false_array[prop]);
            if (g_dbus_proxy_set_property_basic(pd->controller->remote_proxy, prop_name, DBUS_TYPE_BOOLEAN, &prop,
                                                generic_callback, &generic_callback_data,
                                                free_generic_callback_data) == false)
                g_free(generic_callback_data.data);
            break;
        }
        case ENTRY_SCAN: {
            const char *method;
            if (!pd->controller->discovering) {
                method = "StartDiscovery";
            } else
                method = "StopDiscovery";

            generic_callback_data.data = GUINT_TO_POINTER(pd->controller->discovering);

            g_dbus_proxy_method_call(pd->controller->remote_proxy, method, NULL, scan_callback, &generic_callback_data,
                                     NULL);
        } break;
        }
    } else if (mretv & MENU_NEXT) {
        retv = NEXT_DIALOG;
    } else if (mretv & MENU_PREVIOUS) {
        retv = PREVIOUS_DIALOG;
    } else if (mretv & MENU_CANCEL) {
        retv = MODE_EXIT;
    } else if (mretv & MENU_QUICK_SWITCH) {
        retv = (mretv & MENU_LOWER_MASK);
    } else if ((mretv & MENU_ENTRY_DELETE) == MENU_ENTRY_DELETE) {
        retv = RELOAD_DIALOG;
    }
    return retv;
}

internal void bluetooth_mode_destroy(Mode *sw) {

    b32 pairable = false;

    g_debug("destroying");
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);
    if (pd == NULL)
        return;
    g_dbus_proxy_set_property_basic(pd->controller->remote_proxy, "Pairable", DBUS_TYPE_BOOLEAN, &pairable, NULL, NULL,
                                    NULL);

    generic_callback_data.pd = NULL;

    g_dbus_client_unref(pd->client);
    g_debug("freed client");
    g_debug("unref dbus connection");
    dbus_connection_unref(pd->dbus_conn);

    mode_set_private_data(sw, NULL);

    g_debug("freeing controller");
    g_free(pd->controller);
    g_debug("freeing devices");
    g_free(pd->devices);

    g_debug("freeing entries");
    for (u32 i = 0; i < pd->num_entries; i++) {
        if (pd->entries[i].flags & ENTRY_ALLOCATED)
            g_free(pd->entries[i].text);
    }
    g_free(pd->entries);

    g_debug("freeing private data");
    g_free(pd);
}

internal char *_get_display_value(const Mode *sw, unsigned int selected_line, G_GNUC_UNUSED int *state,
                                  G_GNUC_UNUSED GList **attr_list, int get_entry) {
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);

    Entry *entry = &pd->entries[selected_line];

    return get_entry ? g_strdup(entry->text) : NULL;
}

/**
 * @param sw The mode object.
 * @param tokens The tokens to match against.
 * @param index  The index in this plugin to match against.
 *
 * Match the entry.
 *
 * @param returns try when a match.
 */
internal int bluetooth_token_match(const Mode *sw, rofi_int_matcher **tokens, unsigned int index) {
    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);
    return helper_token_match(tokens, pd->entries[index].text);
}

internal char *_get_message(const Mode *sw) {

    BluetoothModePrivateData *pd = (BluetoothModePrivateData *)mode_get_private_data(sw);

    const char *command_status = "";
    if (pd->command_status)
        command_status = pd->command_status;
    char *message = NULL;

    switch (pd->state) {
    case LIST:
        message =
            g_strdup_printf("%s%s\n%-20s%-10s", command_status, "<b>Connect:</b> <i>Ctrl-C</i>", "Name", "Connected");
        break;
    case DEVICE: {
        Device *dev = &pd->devices[pd->current_device];
        message = g_strdup_printf("%s%-20s%-10s%-10s%-10s\n%-20s%-10s%-10s%-10s", command_status, "ID", "Connected",
                                  "Paired", "Trusted", dev->address, true_false_array[dev->connected],
                                  true_false_array[dev->paired], true_false_array[dev->trusted]);
        break;
    }
    case PAIR:
        message = g_strdup_printf("%s<b>Pair: </b> <i>Ctrl-P</i>\n%-20s%-20s", command_status, "ID", "Name");
        break;
    }
    return message;
}

Mode mode = {
    .abi_version = ABI_VERSION,
    .name = "bluetooth",
    .cfg_name_key = "display-bluetooth",
    ._init = bluetooth_mode_init,
    ._get_num_entries = bluetooth_mode_get_num_entries,
    ._result = bluetooth_mode_result,
    ._destroy = bluetooth_mode_destroy,
    ._token_match = bluetooth_token_match,
    ._get_display_value = _get_display_value,
    ._get_message = _get_message,
    ._get_completion = NULL,
    ._preprocess_input = NULL,
    .private_data = NULL,
    .free = NULL,
};
