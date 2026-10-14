/*
 * Battery voltage and charge estimate for boards that wire VBAT to an ADC input
 * through a divider (RAK19007: 1 M / 1.5 M to P0.05 = AIN3, see boards/rak4631.overlay).
 * Boards without the `zephyr,user` io-channels property report no battery.
 */
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>

#include "g7_battery.h"

LOG_MODULE_REGISTER(g7_battery, LOG_LEVEL_INF);

#define BATT_NODE DT_PATH(zephyr_user)

#if IS_ENABLED(CONFIG_ADC) && DT_NODE_HAS_PROP(BATT_NODE, io_channels)
#include <zephyr/drivers/adc.h>

/* Divider ratio (R1 + R2) / R2, times 1000. */
#define DIVIDER_X1000  DT_PROP_OR(BATT_NODE, battery_divider_x1000, 1667)
#define SAMPLE_PERIOD  K_SECONDS(30)
#define SAMPLES        8

static const struct adc_dt_spec adc = ADC_DT_SPEC_GET(BATT_NODE);
static int vbat_mv = -1;

/* Resting LiPo voltage -> charge, rough but monotonic. */
static const struct {
	uint16_t mv;
	uint8_t pct;
} curve[] = {
	{4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50},
	{3700, 30},  {3600, 15}, {3500, 7},  {3400, 3},  {3300, 0},
};

static int mv_to_pct(int mv)
{
	if (mv >= curve[0].mv) {
		return 100;
	}
	for (size_t i = 1; i < ARRAY_SIZE(curve); i++) {
		if (mv >= curve[i].mv) {
			int span = curve[i - 1].mv - curve[i].mv;

			return curve[i].pct +
			       (mv - curve[i].mv) * (curve[i - 1].pct - curve[i].pct) / span;
		}
	}
	return 0;
}

static void sample(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample);

static void sample(struct k_work *work)
{
	int16_t raw;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int32_t sum = 0;
	int n = 0;

	adc_sequence_init_dt(&adc, &seq);
	for (int i = 0; i < SAMPLES; i++) {
		int32_t mv;

		if (adc_read_dt(&adc, &seq) == 0) {
			mv = raw;
			if (adc_raw_to_millivolts_dt(&adc, &mv) == 0) {
				sum += mv;
				n++;
			}
		}
	}
	if (n) {
		int mv = sum / n * DIVIDER_X1000 / 1000;

		/* Light smoothing: radio bursts pull the cell down for a moment. */
		vbat_mv = vbat_mv < 0 ? mv : (vbat_mv * 3 + mv) / 4;
	}
	k_work_schedule(&sample_work, SAMPLE_PERIOD);
}

static int battery_init(void)
{
	if (!adc_is_ready_dt(&adc) || adc_channel_setup_dt(&adc)) {
		LOG_ERR("Battery ADC not available");
		return 0;
	}
	k_work_schedule(&sample_work, K_SECONDS(2));
	return 0;
}

SYS_INIT(battery_init, APPLICATION, 90);

bool g7_battery_get(struct g7_battery *b)
{
	if (vbat_mv < 0) {
		return false;
	}
	b->mv = vbat_mv;
	b->pct = mv_to_pct(vbat_mv);
	b->usb = nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
	return true;
}

#else

bool g7_battery_get(struct g7_battery *b)
{
	ARG_UNUSED(b);
	return false;
}

#endif
