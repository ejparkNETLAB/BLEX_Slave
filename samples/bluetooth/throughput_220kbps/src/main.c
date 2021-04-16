/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 * In order to evaluate the performance of BLEX, it is a throughput measurement code that was modified and brought from Nordic SDK. 
 * In particular, among the 4 slaves, each slave generate different amount of traffic by changing the data interval.
 */

#include <kernel.h>
#include <console/console.h>
#include <sys/printk.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/types.h>

#include <stddef.h>
#include <errno.h>
#include <zephyr.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/crypto.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/hci.h>
#include <bluetooth/uuid.h>
#include "throughput.h"
#include "gatt_dm.h"

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define INTERVAL_MIN	0x140	/* 320 units, 400 ms */
#define INTERVAL_MAX	0x140	/* 320 units, 400 ms */

static volatile bool test_ready;
static struct bt_conn *default_conn;
bool start_flag= false;
static struct bt_conn *scan_conn;
static struct bt_gatt_throughput gatt_throughput;
static struct bt_gatt_exchange_params exchange_params;


#define sys_le16_to_cpu(val)(val)
static const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x0d, 0x18, 0x0f, 0x18, 0x0a, 0x18),
};

static const char img[][81] = {
#include "img.file"
};

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
			  struct bt_gatt_exchange_params *params)
{
	struct bt_conn_info info = {0};
	int err;

	printk("MTU exchange %s\n", att_err == 0 ? "successful" : "failed");

	err = bt_conn_get_info(conn, &info);
	if (err){
		printk("Failed to get connection info %d\n", err);
		return;
	}

	uint32_t stamp = k_uptime_get_32();
	while(k_uptime_get_32()-stamp < 60000);
	if (info.role == BT_CONN_ROLE_SLAVE) {
		test_ready = true;
	}
}

static void discovery_complete(struct bt_gatt_dm *dm,
			       void *context)
{
	int err;
	struct bt_gatt_throughput *throughput = context;

	printk("Service discovery completed\n");

	bt_gatt_dm_data_print(dm);
	bt_gatt_throughput_handles_assign(dm, throughput);
	bt_gatt_dm_data_release(dm);

	exchange_params.func = exchange_func;

	err = bt_gatt_exchange_mtu(default_conn, &exchange_params);
	if (err) {
		printk("MTU exchange failed (err %d)\n", err);
	} else {
		printk("MTU exchange pending\n");
	}
}

static void discovery_service_not_found(struct bt_conn *conn,
					void *context)
{
	printk("Service not found\n");
}

static void discovery_error(struct bt_conn *conn,
			    int err,
			    void *context)
{
	printk("Error while discovering GATT database: (%d)\n", err);
}

struct bt_gatt_dm_cb discovery_cb = {
	.completed         = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found       = discovery_error,
};

static void connected(struct bt_conn *conn, uint8_t hci_err)
{
	struct bt_conn_info info = {0};
	int err;

	if (hci_err) {
		if (hci_err == BT_HCI_ERR_UNKNOWN_CONN_ID) {
			/* Canceled creating connection */
			return;
		}

		printk("Connection failed (err 0x%02x)\n", hci_err);
		return;
	}

	if (default_conn) {
		printk("Connection exists, disconnect second connection\n");
		bt_conn_disconnect(conn, BT_HCI_ERR_LOCALHOST_TERM_CONN);
		return;
	}

	default_conn = bt_conn_ref(conn);

	if (scan_conn) {
		if (scan_conn != conn) {
			/* Cancel creating master connection. */
			printk("Stop scanning for master connection\n");
			bt_conn_disconnect(scan_conn,
					   BT_HCI_ERR_LOCALHOST_TERM_CONN);
		}

		bt_conn_unref(scan_conn);
		scan_conn = NULL;
	}

	err = bt_conn_get_info(default_conn, &info);
	if (err) {
		printk("Failed to get connection info %d\n", err);
		return;
	}

	printk("Connected as %s\n",
	       info.role == BT_CONN_ROLE_MASTER ? "master" : "slave");
	printk("Conn. interval is %u units\n", info.le.interval);

	if (info.role == BT_CONN_ROLE_SLAVE) {
		err = bt_gatt_dm_start(default_conn,
				       BT_UUID_THROUGHPUT,
				       &discovery_cb,
				       &gatt_throughput);

		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	}
}
static bool eir_found(struct bt_data *data, void *user_data)
{
        bt_addr_le_t *addr = user_data;
        int i;

        printk("[AD]: %u data_len %u\n", data->type, data->data_len);

        switch (data->type) {
        case BT_DATA_UUID16_SOME:
        case BT_DATA_UUID16_ALL:
                if (data->data_len % sizeof(uint16_t) != 0U) {
                        printk("AD malformed\n");
                        return true;
                }

                for (i = 0; i < data->data_len; i += sizeof(uint16_t)) {
                        struct bt_le_conn_param *param;
                        struct bt_uuid *uuid;
                        uint16_t u16;
                        int err;

                        memcpy(&u16, &data->data[i], sizeof(u16));
                        uuid = BT_UUID_DECLARE_16(sys_le16_to_cpu(u16));
                        if (bt_uuid_cmp(uuid, BT_UUID_HRS)) {
                                continue;
                        }

                        err = bt_le_scan_stop();
                        if (err) {
                                printk("Stop LE scan failed (err %d)\n", err);
                                continue;
                        }

                        param = BT_LE_CONN_PARAM_DEFAULT;
                        err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                                                param, &default_conn);
                        if (err) {
                                printk("Create conn failed (err %d)\n", err);
                        }

                        return false;
                }
        }

        return true;
}

static void device_found(const bt_addr_le_t *addr, uint8_t rssi, uint8_t type,
                         struct net_buf_simple *ad)
{
        char dev[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(addr, dev, sizeof(dev));
        printk("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n",
               dev, type, ad->len, rssi);

        /* We're only interested in connectable events */
        if (type == BT_GAP_ADV_TYPE_ADV_IND ||
            type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
                bt_data_parse(ad, eir_found, (void *)addr);
        }
}


static void adv_start(void)
{
	struct bt_le_adv_param *adv_param =
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE |
				BT_LE_ADV_OPT_ONE_TIME,
				BT_GAP_ADV_FAST_INT_MIN_2,
				BT_GAP_ADV_FAST_INT_MAX_2, NULL);
	int err;

	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL,
			      0);
	
	if (err) {
		printk("Failed to start advertiser (%d)\n", err);
		return;
	}

	printk("Start advertising\n");

}


static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct bt_conn_info info = {0};
	int err;

	printk("Disconnected (reason 0x%02x)\n", reason);

	test_ready = false;
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	err = bt_conn_get_info(conn, &info);
	if (err) {
		printk("Failed to get connection info (%d)\n", err);
		return;
	}

	/* Re-connect using same roles */
	if (info.role != BT_CONN_ROLE_MASTER) {
		adv_start();
	}
}

static uint8_t throughput_read(const struct bt_gatt_throughput_metrics *met)
{
	printk("[peer] received %u bytes (%u KB)"
	       " in %u GATT writes at %u bps\n",
	       met->write_len, met->write_len / 1024, met->write_count,
	       met->write_rate);

	test_ready = true;

	return BT_GATT_ITER_STOP;
}

static void throughput_received(const struct bt_gatt_throughput_metrics *met)
{
	static uint32_t kb;

	if (met->write_len == 0) {
		kb = 0;
		printk("\n");

		return;
	}

	if ((met->write_len / 1024) != kb) {
		kb = (met->write_len / 1024);
		printk("=");
	}
}

static void throughput_send(const struct bt_gatt_throughput_metrics *met)
{
	printk("\n[local] received %u bytes (%u KB)"
		" in %u GATT writes at %u bps\n",
		met->write_len, met->write_len / 1024,
		met->write_count, met->write_rate);
}

static const struct bt_gatt_throughput_cb throughput_cb = {
	.data_read = throughput_read,
	.data_received = throughput_received,
	.data_send = throughput_send
};

static void test_run(void)
{
	int err;
	uint64_t stamp;
	uint32_t delta;
	uint32_t data = 0;
	uint32_t prog = 0;

	/* a dummy data buffer */
	static char dummy[256];


	/* wait for user input to continue */

	if (!test_ready) {
		/* disconnected while blocking inside _getchar() */
		return;
	}

	test_ready = false;

	if(start_flag == true){
		data = 0;
		prog = 0;
		/* reset peer metrics */
		err = bt_gatt_throughput_write(&gatt_throughput, dummy, 1);
		if (err) {
			printk("Reset peer metrics failed.\n");
			return;
		}

		/* get cycle stamp */
		stamp = k_uptime_get_32();
		uint64_t stamp2;
		int app_th = 220;
		int data2 = 0;
		/*BLEX: By changing packet interval, we can change the traffic size*/
		int break_time = 110*8/app_th;
		int packet_size = 110;
		while (k_uptime_get_32()-stamp < 60000) {
			err = bt_gatt_throughput_write(&gatt_throughput, dummy, packet_size);
			if (err) {
				printk("GATT write failed (err %d)\n", err);
				break;
			}
			while(k_uptime_get_32()-stamp2 <break_time) ;
			data += packet_size;
			prog++;
			data2 += packet_size;
			if(prog %1000 == 0) {
				uint32_t delta2 = k_uptime_get_32()-stamp2;
				printk("[local] sent %u bytes (%u KB) in %u ms at %llu kbps\n",
						data2, data2 / 1024, delta2, ((uint64_t)data2 * 8 / delta2));
				stamp2 = k_uptime_get_32();
				data2 = 0;
			}
		}
		delta = k_uptime_delta_32(&stamp);

		printk("\nDone\n");
		printk("[local] sent %u bytes (%u KB) in %u ms at %llu kbps\n",
				data, data / 1024, delta, ((uint64_t)data * 8 / delta));

		/* read back char from peer */
		err = bt_gatt_throughput_read(&gatt_throughput);
		if (err) {
			printk("GATT read failed (err %d)\n", err);
		}
	}
	else{

		/* reset peer metrics */
		err = bt_gatt_throughput_write(&gatt_throughput, dummy, 1);
		if (err) {
			printk("Reset peer metrics failed.\n");
			return;
		}

		/* get cycle stamp */
		stamp = k_uptime_get_32();

		data = 0;
		prog = 0;
		int temp_delta = 20;
		uint64_t stamp2;
		int app_th =220;
		int packet_size = 110;
		/*BLEX: By changing packet interval, we can change the traffic size*/
		int break_time = packet_size*8/app_th;
		uint64_t stamp3;
		int data2 =0;
		stamp2 = k_uptime_get_32();
		while (k_uptime_get_32()-stamp < 6000000000) {
			stamp3 = k_uptime_get_32();
			err = bt_gatt_throughput_write(&gatt_throughput, dummy, packet_size);
			if (err) {
				printk("GATT write failed (err %d)\n", err);
				break;
			}
			while(k_uptime_get_32()-stamp3 <break_time) ;
			data += packet_size;
			prog++;
			data2+=packet_size;
			if(prog %1000 == 0) {

				uint32_t delta2 = k_uptime_get_32()-stamp2;
				printk("[local] sent %u bytes (%u KB) in %u ms at %llu kbps\n",
						data2, data2 / 1024, delta2, ((uint64_t)data2 * 8 / delta2));
				stamp2 = k_uptime_get_32();
				data2= 0;
			}

		}

		delta = k_uptime_delta_32(&stamp);

		printk("\nDone\n");
		printk("[local] sent %u bytes (%u KB) in %u ms at %llu kbps\n",
				data, data / 1024, delta, ((uint64_t)data * 8 / delta));

		/* read back char from peer */
		err = bt_gatt_throughput_read(&gatt_throughput);
		if (err) {
			printk("GATT read failed (err %d)\n", err);
		}
	}
	if(start_flag == true) start_flag = false;
	else 
		start_flag = true;

}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	/* reject peer conn param request */
	return true;
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

void main(void)
{
	int err;

	uint16_t offset;

	static struct bt_conn_cb conn_callbacks = {
		.connected = connected,
		.disconnected = disconnected,
		.le_param_req = le_param_req,
	};

	printk("Starting Bluetooth Throughput example\n");

	console_init();

	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&auth_cb_display);

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = bt_gatt_throughput_init(&gatt_throughput, &throughput_cb);
	if (err) {
		printk("Throughput service initialization failed.\n");
		return;
	}

	adv_start();

	for (;;) {
		if (test_ready) {
			test_run();
		}
	}
}

