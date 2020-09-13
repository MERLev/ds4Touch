#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/bt.h>
#include <psp2kern/ctrl.h>
#include <psp2/touch.h>
#include <psp2/motion.h>
#include <taihen.h>
#include <math.h>
#include "log.h"
#include "ds4touch.h"

#define DS4_VID   0x054C
#define DS4_PID   0x05C4
#define DS4_NEW_PID 0x09CC

#define DS4_TOUCHPAD_W 1920
#define DS4_TOUCHPAD_H 940

#define VITA_FRONT_TOUCHSCREEN_W 1920
#define VITA_FRONT_TOUCHSCREEN_H 1080

#define TTL_DS4_REPORT (100*1000)
#define TTL_DS4_CONNECTION (1000*1000)

#define EVF_EXIT	(1 << 0)

#define abs(x) (((x) < 0) ? -(x) : (x))

#define DECL_FUNC_HOOK(name, ...) \
	static tai_hook_ref_t name##_ref; \
	static SceUID name##_hook_uid = -1; \
	static int name##_hook_func(__VA_ARGS__)

#define BIND_FUNC_OFFSET_HOOK(name, pid, modid, segidx, offset, thumb) \
	name##_hook_uid = taiHookFunctionOffsetForKernel((pid), \
		&name##_ref, (modid), (segidx), (offset), thumb, name##_hook_func)

#define BIND_FUNC_EXPORT_HOOK(name, pid, module, lib_nid, func_nid) \
	name##_hook_uid = taiHookFunctionExportForKernel((pid), \
		&name##_ref, (module), (lib_nid), (func_nid), name##_hook_func)

#define UNBIND_FUNC_HOOK(name) \
	do { \
		if (name##_hook_uid > 0) \
			taiHookReleaseForKernel(name##_hook_uid, name##_ref); \
	} while(0)

struct ds4_input_report {
	unsigned char report_id;
	unsigned char left_x;
	unsigned char left_y;
	unsigned char right_x;
	unsigned char right_y;

	unsigned char dpad     : 4;
	unsigned char square   : 1;
	unsigned char cross    : 1;
	unsigned char circle   : 1;
	unsigned char triangle : 1;

	unsigned char l1      : 1;
	unsigned char r1      : 1;
	unsigned char l2      : 1;
	unsigned char r2      : 1;
	unsigned char share   : 1;
	unsigned char options : 1;
	unsigned char l3      : 1;
	unsigned char r3      : 1;

	unsigned char ps   : 1;
	unsigned char tpad : 1;
	unsigned char cnt1 : 6;

	unsigned char l_trigger;
	unsigned char r_trigger;

	unsigned char cnt2;
	unsigned char cnt3;

	unsigned char battery;

	signed short accel_x;
	signed short accel_y;
	signed short accel_z;

	union {
		signed short roll;
		signed short gyro_z;
	};
	union {
		signed short yaw;
		signed short gyro_y;
	};
	union {
		signed short pitch;
		signed short gyro_x;
	};

	unsigned char unk1[5];

	unsigned char battery_level : 4;
	unsigned char usb_plugged   : 1;
	unsigned char headphones    : 1;
	unsigned char microphone    : 1;
	unsigned char padding       : 1;

	unsigned char unk2[2];
	unsigned char trackpadpackets;
	unsigned char packetcnt;

	unsigned int finger1_id        : 7;
	unsigned int finger1_activelow : 1;
	unsigned int finger1_x         : 12;
	unsigned int finger1_y         : 12;

	unsigned int finger2_id        : 7;
	unsigned int finger2_activelow : 1;
	unsigned int finger2_x         : 12;
	unsigned int finger2_y         : 12;

} __attribute__((packed, aligned(32)));

static struct ds4_input_report ds4report;
volatile static SceUInt64 ds4reportTimestamp = 0;
volatile unsigned int ds4mac0 = 0,  ds4mac1 = 0;
volatile int isDs4Active = 0;

int32_t clamp(int32_t value, int32_t mini, int32_t maxi) {
	if (value < mini) return mini; 
	if (value > maxi) return maxi;
	return value;
}

static int is_ds4(const unsigned short vid_pid[2])
{
	return (vid_pid[0] == DS4_VID) &&
		((vid_pid[1] == DS4_PID) || (vid_pid[1] == DS4_NEW_PID));
}

static void patch_touch_data(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs,
			    struct ds4_input_report *ds4)
{
	//Return if no ds4 connected
	if (!isDs4Active)
		return;

	// Controller selected for touch lost connection
	if ((ksceKernelGetSystemTimeWide() - ds4reportTimestamp) > TTL_DS4_CONNECTION){
		isDs4Active = 0;
		ds4mac0 = 0;
		ds4mac1 = 0;
		return;
	}

	// Report from connected controller is too old to be used
	if ((ksceKernelGetSystemTimeWide() - ds4reportTimestamp) > TTL_DS4_REPORT)
		return;

	unsigned int i;

	if (port != SCE_TOUCH_PORT_FRONT)
		return;

	for (i = 0; i < nBufs; i++) {
		unsigned int num_reports = 0;

		// If finger1 present, add finger1 as report 0
		if (!ds4->finger1_activelow) {
			pData->report[0].id = ds4->finger1_id;
			pData->report[0].x = (ds4->finger1_x * VITA_FRONT_TOUCHSCREEN_W) / DS4_TOUCHPAD_W;
			pData->report[0].y = (ds4->finger1_y * VITA_FRONT_TOUCHSCREEN_H) / DS4_TOUCHPAD_H;
			num_reports++;
		}

		// If only finger2 is present, add finger2 as report 0
		if (!ds4->finger2_activelow && ds4->finger1_activelow) {
			pData->report[0].id = ds4->finger2_id;
			pData->report[0].x = (ds4->finger2_x * VITA_FRONT_TOUCHSCREEN_W) / DS4_TOUCHPAD_W;
			pData->report[0].y = (ds4->finger2_y * VITA_FRONT_TOUCHSCREEN_H) / DS4_TOUCHPAD_H;
			num_reports++;
		}

		// If both finger1 and finger2 present, add finger2 as report 1
		if (!ds4->finger2_activelow && !ds4->finger1_activelow) {
			pData->report[1].id = ds4->finger2_id;
			pData->report[1].x = (ds4->finger2_x * VITA_FRONT_TOUCHSCREEN_W) / DS4_TOUCHPAD_W;
			pData->report[1].y = (ds4->finger2_y * VITA_FRONT_TOUCHSCREEN_H) / DS4_TOUCHPAD_H;
			num_reports++;
		}

		if (num_reports > 0) {
			ksceKernelPowerTick(0);
			pData->reportNum = num_reports;
		}

		pData++;
	}
}

/*export*/ int ds4touch_onTouch(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs){
	if (nBufs >= 0 && nBufs < 64){
        SceTouchData std;
        ksceKernelMemcpyUserToKernel(&std, (uintptr_t)&pData[nBufs-1], sizeof(SceTouchData));
		patch_touch_data(port, &std, 1, &ds4report);
        ksceKernelMemcpyKernelToUser((uintptr_t)&pData[nBufs-1], &std, sizeof(SceTouchData)); 
	}

	return nBufs;
}

DECL_FUNC_HOOK(SceTouch_ksceTouchPeek, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs)
{
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchPeek_ref, port, pData, nBufs);
	
	if (ret >= 0 && ret < 64)
		patch_touch_data(port, pData, nBufs, &ds4report);

	return ret;
}

DECL_FUNC_HOOK(SceTouch_ksceTouchPeekRegion, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs, int region)
{
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchPeekRegion_ref, port, pData, nBufs, region);

	if (ret >= 0 && ret < 64)
		patch_touch_data(port, pData, nBufs, &ds4report);

	return ret;
}

DECL_FUNC_HOOK(SceTouch_ksceTouchRead, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs)
{
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchRead_ref, port, pData, nBufs);

	if (ret >= 0 && ret < 64)
		patch_touch_data(port, pData, nBufs, &ds4report);

	return ret;
}

DECL_FUNC_HOOK(SceTouch_ksceTouchReadRegion, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs, int region)
{
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchReadRegion_ref, port, pData, nBufs, region);

	if (ret >= 0 && ret < 64)
		patch_touch_data(port, pData, nBufs, &ds4report);

	return ret;
}

DECL_FUNC_HOOK(SceTouch_ksceBtHidTransfer, unsigned int mac0, unsigned int mac1, SceBtHidRequest *request)
{
	int ret = TAI_CONTINUE(int, SceTouch_ksceBtHidTransfer_ref, mac0, mac1, request);

	// LOG("ksceBtHidTransfer(%u, %u, [%u, %hhu])\n", mac0, mac1, request->length, request->type);
	if (request->length == 80 && request->type == 0){
		if (!isDs4Active){
			unsigned short vid_pid[2];
			ksceBtGetVidPid(mac0, mac1, vid_pid);
			if (is_ds4(vid_pid)){
				isDs4Active = 1;
				ds4mac0 = mac0;
				ds4mac1 = mac1;
			}
		}
		if (isDs4Active && ds4mac0 == mac0 && ds4mac1 == mac1){
			ds4reportTimestamp = (SceUInt64)ksceKernelGetSystemTimeWide();
			memcpy(&ds4report, request->buffer, sizeof(ds4report));
		}
	}
	return ret;
}

void _start() __attribute__ ((weak, alias ("module_start")));

int module_start(SceSize argc, const void *args)
{
	int ret;
	tai_module_info_t SceBt_modinfo;

	log_reset();
	memset(&ds4report, 0, sizeof(ds4report));

	LOG("ds34vita started\n");

	SceBt_modinfo.size = sizeof(SceBt_modinfo);
	ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &SceBt_modinfo);
	if (ret < 0) {
		LOG("Error finding SceBt module\n");
		goto error_find_scebt;
	}

	/* SceTouch hooks */
	BIND_FUNC_EXPORT_HOOK(SceTouch_ksceTouchPeek, KERNEL_PID, "SceTouch", TAI_ANY_LIBRARY, 0xBAD1960B);
	BIND_FUNC_EXPORT_HOOK(SceTouch_ksceTouchPeekRegion, KERNEL_PID,	"SceTouch", TAI_ANY_LIBRARY, 0x9B3F7207);
	BIND_FUNC_EXPORT_HOOK(SceTouch_ksceTouchRead, KERNEL_PID, "SceTouch", TAI_ANY_LIBRARY, 0x70C8AACE);
	BIND_FUNC_EXPORT_HOOK(SceTouch_ksceTouchReadRegion, KERNEL_PID,	"SceTouch", TAI_ANY_LIBRARY, 0x9A91F624);

	BIND_FUNC_EXPORT_HOOK(SceTouch_ksceBtHidTransfer, KERNEL_PID,	"SceBt", TAI_ANY_LIBRARY, 0xF9DCEC77);

	LOG("module_start finished successfully!\n");

	return SCE_KERNEL_START_SUCCESS;

error_find_scebt:
	return SCE_KERNEL_START_FAILED;
}

int module_stop(SceSize argc, const void *args)
{
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchPeek);
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchPeekRegion);
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchRead);
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchReadRegion);

	UNBIND_FUNC_HOOK(SceTouch_ksceBtHidTransfer);

	log_flush();

	return SCE_KERNEL_STOP_SUCCESS;
}
