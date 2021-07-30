/*
 * Copyright 2020-2021 AVSystem <avsystem@avsystem.com>
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

#include <anjay/anjay.h>

#include <drivers/sensor.h>
#include <zephyr.h>

#include "impl/three_axis_sensor_impl.h"
#include "objects.h"

#if MAGNETOMETER_AVAILABLE
const anjay_dm_object_def_t **magnetometer_object_create(void) {
    return three_axis_sensor_object_create(DEVICE_DT_GET(MAGNETOMETER_NODE),
                                           SENSOR_CHAN_MAGN_XYZ, "gauss", 3314);
}

void magnetometer_object_release(const anjay_dm_object_def_t ***out_def) {
    three_axis_sensor_object_release(out_def);
}

void magnetometer_object_update(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *def) {
    three_axis_sensor_object_update(anjay, def);
}
#else  // MAGNETOMETER_AVAILABLE
const anjay_dm_object_def_t **magnetometer_object_create(void) {
    return NULL;
}

void magnetometer_object_release(const anjay_dm_object_def_t ***out_def) {
    (void) out_def;
}

void magnetometer_object_update(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *def) {
    (void) anjay;
    (void) def;
}
#endif // MAGNETOMETER_AVAILABLE