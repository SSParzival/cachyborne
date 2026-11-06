// SPDX-License-Identifier: GPL-2.0-or-later
// Real dlopen fixture: no C++ unique symbols that could cause RTLD_NODELETE.
struct CallbackFixture { void (*host)(void*); void* context; };
void cachyborne_test_plugin_callback(void* user) {
    struct CallbackFixture* fixture = user;
    fixture->host(fixture->context);
}
