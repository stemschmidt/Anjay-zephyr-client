/*
 * Copyright 2020-2022 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>

#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_time.h>
#include <avsystem/commons/avs_utils.h>

#include <device.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
#include <zephyr.h>

#include <console/console.h>
#include <drivers/flash.h>
#include <drivers/hwinfo.h>
#include <drivers/uart.h>
#include <settings/settings.h>

#include "config.h"
#include "default_config.h"
#include "utils.h"

#define SETTINGS_ROOT_NAME "anjay"
#define SETTINGS_NAME(Name) SETTINGS_ROOT_NAME "/" AVS_QUOTE_MACRO(Name)

#define EP_NAME_PREFIX "anjay-zephyr-demo"

struct anjay_client_option;

typedef int config_option_validate_t(const struct shell *shell, const char *value, size_t value_len,
				     const struct anjay_client_option *option);

struct anjay_client_app_config {
#ifdef CONFIG_WIFI
	char ssid[32];
	char password[32];
#endif // CONFIG_WIFI
	char uri[128];
	char lifetime[AVS_UINT_STR_BUF_SIZE(uint32_t)];
	char ep_name[64];
	char psk[32];
	char bootstrap[2];
#ifdef CONFIG_ANJAY_CLIENT_GPS_NRF
	char gps_nrf_prio_mode_timeout[AVS_UINT_STR_BUF_SIZE(uint32_t)];
	char gps_nrf_prio_mode_cooldown[AVS_UINT_STR_BUF_SIZE(uint32_t)];
#endif // CONFIG_ANJAY_CLIENT_GPS_NRF
#ifdef CONFIG_ANJAY_CLIENT_PERSISTENCE
	char use_persistence[2];
#endif // CONFIG_ANJAY_CLIENT_PERSISTENCE
};

static struct anjay_client_app_config app_config;

struct anjay_client_option {
	const char *const key;
	const char *const desc;
	char *const value;
	size_t value_capacity;
	config_option_validate_t *validator;
};

static config_option_validate_t string_validate;
static config_option_validate_t flag_validate;
static config_option_validate_t uint32_validate;

static struct anjay_client_option string_options[] = {
#ifdef CONFIG_WIFI
	{ AVS_QUOTE_MACRO(OPTION_KEY_SSID), "Wi-Fi SSID", app_config.ssid, sizeof(app_config.ssid),
	  string_validate },
	{ AVS_QUOTE_MACRO(OPTION_KEY_PASSWORD), "Wi-Fi password", app_config.password,
	  sizeof(app_config.password), string_validate },
#endif // CONFIG_WIFI
	{ AVS_QUOTE_MACRO(OPTION_KEY_URI), "LwM2M Server URI", app_config.uri,
	  sizeof(app_config.uri), string_validate },
	{ AVS_QUOTE_MACRO(OPTION_KEY_LIFETIME), "Device lifetime", app_config.lifetime,
	  sizeof(app_config.lifetime), uint32_validate },
	{ AVS_QUOTE_MACRO(OPTION_KEY_EP_NAME), "Endpoint name", app_config.ep_name,
	  sizeof(app_config.ep_name), string_validate },
	{ AVS_QUOTE_MACRO(OPTION_KEY_PSK), "PSK", app_config.psk, sizeof(app_config.psk),
	  string_validate },
	{ AVS_QUOTE_MACRO(OPTION_KEY_BOOTSTRAP), "Bootstrap", app_config.bootstrap,
	  sizeof(app_config.bootstrap), flag_validate },
#ifdef CONFIG_ANJAY_CLIENT_GPS_NRF
	{ AVS_QUOTE_MACRO(OPTION_KEY_GPS_NRF_PRIO_MODE_TIMEOUT), "GPS priority mode timeout",
	  app_config.gps_nrf_prio_mode_timeout, sizeof(app_config.gps_nrf_prio_mode_timeout),
	  uint32_validate },
	{ AVS_QUOTE_MACRO(OPTION_KEY_GPS_NRF_PRIO_MODE_COOLDOWN), "GPS priority mode cooldown",
	  app_config.gps_nrf_prio_mode_cooldown, sizeof(app_config.gps_nrf_prio_mode_cooldown),
	  uint32_validate },
#endif // CONFIG_ANJAY_CLIENT_GPS_NRF
#ifdef CONFIG_ANJAY_CLIENT_PERSISTENCE
	{ AVS_QUOTE_MACRO(OPTION_KEY_USE_PERSISTENCE), "Use persistence",
	  app_config.use_persistence, sizeof(app_config.use_persistence), flag_validate },
#endif // CONFIG_ANJAY_CLIENT_PERSISTENCE
};

static int settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (key) {
		for (int i = 0; i < AVS_ARRAY_SIZE(string_options); ++i) {
			if (strcmp(key, string_options[i].key) == 0) {
				if (len != string_options[i].value_capacity) {
					return -EINVAL;
				}

				int result = read_cb(cb_arg, string_options[i].value,
						     string_options[i].value_capacity);
				return result >= 0 ? 0 : result;
			}
		}
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(anjay, SETTINGS_ROOT_NAME, NULL, settings_set, NULL, NULL);

void config_print_summary(const struct shell *shell)
{
	shell_print(shell, "\nCurrent Anjay config:\n");
	for (int i = 0; i < AVS_ARRAY_SIZE(string_options); i++) {
		shell_print(shell, " %s: %s", string_options[i].desc, string_options[i].value);
	}
}

void config_save(const struct shell *shell)
{
	char key_buf[64];
	int result = 0;

	for (size_t i = 0; !result && i < AVS_ARRAY_SIZE(string_options); ++i) {
		result = avs_simple_snprintf(key_buf, sizeof(key_buf), SETTINGS_ROOT_NAME "/%s",
					     string_options[i].key);
		if (result >= 0) {
			result = settings_save_one(key_buf, string_options[i].value,
						   string_options[i].value_capacity);
		}
	}

	if (result) {
		shell_warn(shell, "Cannot save the config");
		for (size_t i = 0; i < AVS_ARRAY_SIZE(string_options); ++i) {
			result = avs_simple_snprintf(key_buf, sizeof(key_buf),
						     SETTINGS_ROOT_NAME "/%s",
						     string_options[i].key);
			if (result >= 0) {
				settings_delete(key_buf);
			}
		}
	}
}

void config_default_init(void)
{
	app_config = (struct anjay_client_app_config){
#ifdef CONFIG_WIFI
		.ssid = WIFI_SSID,
		.password = WIFI_PASSWORD,
#endif // CONFIG_WIFI
		.uri = SERVER_URI,
		.lifetime = LIFETIME,
		.psk = PSK_KEY,
		.bootstrap = BOOTSTRAP,
#ifdef CONFIG_ANJAY_CLIENT_GPS_NRF
		.gps_nrf_prio_mode_timeout = GPS_NRF_PRIO_MODE_TIMEOUT,
		.gps_nrf_prio_mode_cooldown = GPS_NRF_PRIO_MODE_COOLDOWN,
#endif // CONFIG_ANJAY_CLIENT_GPS_NRF
#ifdef CONFIG_ANJAY_CLIENT_PERSISTENCE
		.use_persistence = USE_PERSISTENCE,
#endif // CONFIG_ANJAY_CLIENT_PERSISTENCE
	};

	struct device_id id;

	AVS_STATIC_ASSERT(sizeof(app_config.ep_name) >= sizeof(id.value) + sizeof(EP_NAME_PREFIX) -
								sizeof('\0') + sizeof('-'),
			  ep_name_buffer_too_small);
	if (!get_device_id(&id)) {
		(void)avs_simple_snprintf(app_config.ep_name, sizeof(app_config.ep_name),
					  EP_NAME_PREFIX "-%s", id.value);
	} else {
		memcpy(app_config.ep_name, EP_NAME_PREFIX, sizeof(EP_NAME_PREFIX));
	}
}

void config_init(const struct shell *shell)
{
	config_default_init();

	if (settings_subsys_init()) {
		shell_warn(shell, "Failed to initialize settings subsystem");
		return;
	}

	if (settings_load_subtree(SETTINGS_ROOT_NAME)) {
		shell_warn(shell, "Restoring default configuration");
		config_default_init();
	} else {
		shell_print(shell, "Configuration successfully restored");
	}
}

int config_set_option(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(shell, "Wrong number of arguments.\n");
		return -1;
	}

	const char *key = argv[0];

	for (size_t i = 0; i < AVS_ARRAY_SIZE(string_options); ++i) {
		if (strcmp(key, string_options[i].key) == 0) {
			const char *value = argv[1];
			size_t value_len = strlen(value) + 1;

			assert(string_options[i].validator);
			if (string_options[i].validator(shell, value, value_len,
							&string_options[i])) {
				return -1;
			}

			memcpy(string_options[i].value, value, value_len);

			return 0;
		}
	}

	AVS_UNREACHABLE("Invalid option key");
	return -1;
}

static int parse_uint32(const char *value, uint32_t *out)
{
	return sscanf(value, "%" PRIu32, out) == 1 ? 0 : -1;
}

const char *config_get_endpoint_name(void)
{
	return app_config.ep_name;
}

#ifdef CONFIG_WIFI
const char *config_get_wifi_ssid(void)
{
	return app_config.ssid;
}

const char *config_get_wifi_password(void)
{
	return app_config.password;
}
#endif // CONFIG_WIFI

const char *config_get_server_uri(void)
{
	return app_config.uri;
}

uint32_t config_get_lifetime(void)
{
	uint32_t out;

	parse_uint32(app_config.lifetime, &out);
	return out;
}

const char *config_get_psk(void)
{
	return app_config.psk;
}

bool config_is_bootstrap(void)
{
	return app_config.bootstrap[0] == 'y';
}

#ifdef CONFIG_ANJAY_CLIENT_GPS_NRF
uint32_t config_get_gps_nrf_prio_mode_timeout(void)
{
	uint32_t out;

	parse_uint32(app_config.gps_nrf_prio_mode_timeout, &out);
	return out;
}

uint32_t config_get_gps_nrf_prio_mode_cooldown(void)
{
	uint32_t out;

	parse_uint32(app_config.gps_nrf_prio_mode_cooldown, &out);
	return out;
}
#endif // CONFIG_ANJAY_CLIENT_GPS_NRF

#ifdef CONFIG_ANJAY_CLIENT_PERSISTENCE
bool config_is_use_persistence(void)
{
	return app_config.use_persistence[0] == 'y';
}
#endif // CONFIG_ANJAY_CLIENT_PERSISTENCE

static int string_validate(const struct shell *shell, const char *value, size_t value_len,
			   const struct anjay_client_option *option)
{
	if (value_len > option->value_capacity) {
		shell_error(shell, "Value too long, maximum length is %d\n",
			    option->value_capacity - 1);
		return -1;
	}

	return 0;
}

static int flag_validate(const struct shell *shell, const char *value, size_t value_len,
			 const struct anjay_client_option *option)
{
	if (value_len != 2 || (value[0] != 'y' && value[0] != 'n')) {
		shell_error(shell, "Value invalid, 'y' or 'n' is allowed\n");
		return -1;
	}

	return 0;
}

static int uint32_validate(const struct shell *shell, const char *value, size_t value_len,
			   const struct anjay_client_option *option)
{
	if (string_validate(shell, value, value_len, option)) {
		return -1;
	}

	uint32_t out;

	if (parse_uint32(value, &out)) {
		shell_error(shell, "Argument is not a valid uint32_t value");
		return -1;
	}

	return 0;
}
