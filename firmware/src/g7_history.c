#include "g7_history.h"

#include <string.h>

#include <zephyr/kernel.h>

/* 24 h at 5 min is 288; leave room for a couple of hours of overlap/jitter. */
#define CAPACITY 320

/* Kept sorted by minute, oldest first; the oldest entry drops out when full. */
static struct g7_record records[CAPACITY];
static size_t used;
static K_MUTEX_DEFINE(lock);

/* Sensor clock anchor: G7 time `sync_s` was reported at uptime `sync_ms`. */
static bool synced;
static uint32_t sync_s;
static int64_t sync_ms;

void g7_history_sync(uint32_t sensor_s)
{
	k_mutex_lock(&lock, K_FOREVER);
	sync_s = sensor_s;
	sync_ms = k_uptime_get();
	synced = true;
	k_mutex_unlock(&lock);
}

bool g7_history_now_s(uint32_t *sensor_s)
{
	k_mutex_lock(&lock, K_FOREVER);

	bool ok = synced;

	if (ok) {
		*sensor_s = sync_s + (uint32_t)((k_uptime_get() - sync_ms) / 1000);
	}
	k_mutex_unlock(&lock);
	return ok;
}

bool g7_history_now(uint16_t *minute)
{
	uint32_t s;

	if (!g7_history_now_s(&s)) {
		return false;
	}
	*minute = (uint16_t)MIN(s / 60, UINT16_MAX);
	return true;
}

void g7_history_clear(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	used = 0;
	k_mutex_unlock(&lock);
}

bool g7_history_add(const struct g7_record *rec)
{
	bool is_new = true;

	k_mutex_lock(&lock, K_FOREVER);

	size_t i = used;

	/* Find the insertion point from the newest end (the usual case). */
	while (i > 0 && records[i - 1].minute > rec->minute) {
		i--;
	}
	if (i > 0 && rec->minute - records[i - 1].minute <= 2) {
		records[i - 1] = *rec;
		is_new = false;
	} else if (i < used && records[i].minute - rec->minute <= 2) {
		records[i] = *rec;
		is_new = false;
	} else {
		if (used == CAPACITY) {
			if (i == 0) {
				goto out; /* older than everything we keep */
			}
			memmove(&records[0], &records[1], (i - 1) * sizeof(records[0]));
			i--;
		} else {
			memmove(&records[i + 1], &records[i], (used - i) * sizeof(records[0]));
			used++;
		}
		records[i] = *rec;
	}
out:
	k_mutex_unlock(&lock);
	return is_new;
}

bool g7_history_latest(struct g7_record *out)
{
	k_mutex_lock(&lock, K_FOREVER);

	bool ok = used > 0;

	if (ok) {
		*out = records[used - 1];
	}
	k_mutex_unlock(&lock);
	return ok;
}

size_t g7_history_count(uint16_t min, uint16_t max)
{
	size_t n = 0;

	k_mutex_lock(&lock, K_FOREVER);
	for (size_t i = 0; i < used; i++) {
		n += records[i].minute >= min && records[i].minute <= max;
	}
	k_mutex_unlock(&lock);
	return n;
}

bool g7_history_get(uint16_t min, uint16_t max, size_t n, struct g7_record *out)
{
	bool ok = false;

	k_mutex_lock(&lock, K_FOREVER);
	for (size_t i = 0; i < used; i++) {
		if (records[i].minute < min || records[i].minute > max) {
			continue;
		}
		if (n-- == 0) {
			*out = records[i];
			ok = true;
			break;
		}
	}
	k_mutex_unlock(&lock);
	return ok;
}
