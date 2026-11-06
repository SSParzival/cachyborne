// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CACHYBORNE_RUNTIME_PLUGIN_HLE_H
#define CACHYBORNE_RUNTIME_PLUGIN_HLE_H
#include <stdint.h>
/* Early registration only. Owners must keep code resident for in-flight calls.
 * Names are exported function names in the native import-name catalogue.
 * Existing data imports and unresolved native functions are never redirected. */
int runtime_plugin_hle_register(uint64_t owner,const char *name,void *function);
void runtime_plugin_hle_retire(uint64_t owner);
void runtime_plugin_hle_seal(void);
/* Called only during import binding. Returns a stable tail-jump thunk when a
 * replacement exists; cached imports fall back on retirement without rebinding. */
uintptr_t runtime_plugin_hle_resolve(const char *symbol,uintptr_t original);
int runtime_function_name_known(const char *symbol);
int runtime_plugin_hle_enabled(void);
#endif
