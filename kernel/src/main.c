#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/registrymgr.h> 
#include <psp2kern/bt.h>
#include <psp2/touch.h>
#include <taihen.h>
#include "log.h"

#define DS4_VID   0x054C
#define DS4_PID   0x05C4
#define DS4_NEW_PID 0x09CC

#define DS4_TOUCHPAD_W 1920
#define DS4_TOUCHPAD_H 940
#define DS4_TOUCHPAD_W_DEAD 60
#define DS4_TOUCHPAD_H_DEAD 120

#define VITA_FRONT_TOUCHSCREEN_W 1920
#define VITA_FRONT_TOUCHSCREEN_H 1080

#define TTL_DS4_REPORT 		(100*1000)
#define TTL_DS4_CONNECTION 	(1000*1000)
#define RECHECK_REGISTRY 	(1000*1000)

#define EVF_EXIT	(1 << 0)

#define abs(x) (((x) < 0) ? -(x) : (x))
#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

#define DECL_FUNC_HOOK(name, ...) \
	static tai_hook_ref_t name##_ref; \
	static SceUID name##_hook_uid = -1; \
	static int name##_hook_func(__VA_ARGS__)

#define BIND_FUNC_EXPORT_HOOK(pid, module, lib_nid, func_nid, name) \
	name##_hook_uid = taiHookFunctionExportForKernel((pid), \
		&name##_ref, (module), (lib_nid), (func_nid), name##_hook_func); \
	LOG("HOOKING %s -> %s : %i\n", (module), (#name), name##_hook_uid);

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

static SceUID thread_uid = -1;
static int   thread_run = 1;

// Registry value of touch emulation on PS TV
int touchEmulation = 0;

static struct ds4_input_report ds4report;
volatile static SceUInt64 ds4reportTimestamp = 0;
volatile unsigned int ds4mac0 = 0,  ds4mac1 = 0;
volatile int isDs4Active = 0;

int32_t clamp(int32_t value, int32_t mini, int32_t maxi) {
	if (value < mini) return mini; 
	if (value > maxi) return maxi;
	return value;
}

static int is_ds4(const unsigned short vid_pid[2]){
	return (vid_pid[0] == DS4_VID) &&
		((vid_pid[1] == DS4_PID) || (vid_pid[1] == DS4_NEW_PID));
}

static int rescaleTouchCoordinate(int ds4coord, int ds4size, int ds4dead, int vitasize){
	return clamp(((ds4coord - ds4dead) * vitasize) / (ds4size - ds4dead * 2), 0, vitasize - 1);
}

static void patch_touch_data(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs, struct ds4_input_report *ds4){
	// Touch emulation on PS TV enabled
	if(touchEmulation)
		return;

	// No buffers from native call
	if (nBufs < 0 && nBufs > 64)
		return;

	// Return if no ds4 connected
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

	if (port != SCE_TOUCH_PORT_FRONT)
		return;

	for (unsigned int i = 0; i < nBufs; i++) {
		unsigned int num_reports = 0;

		// If finger1 present, add finger1 as report 0
		if (!ds4->finger1_activelow) {
			pData[i].report[0].id = ds4->finger1_id;
			pData[i].report[0].x = rescaleTouchCoordinate(ds4->finger1_x, DS4_TOUCHPAD_W, DS4_TOUCHPAD_W_DEAD, VITA_FRONT_TOUCHSCREEN_W);
			pData[i].report[0].y = rescaleTouchCoordinate(ds4->finger1_y, DS4_TOUCHPAD_H, DS4_TOUCHPAD_H_DEAD, VITA_FRONT_TOUCHSCREEN_H);
			num_reports++;
		}

		// If only finger2 is present, add finger2 as report 0
		if (!ds4->finger2_activelow && ds4->finger1_activelow) {
			pData[i].report[0].id = ds4->finger2_id;
			pData[i].report[0].x = rescaleTouchCoordinate(ds4->finger2_x, DS4_TOUCHPAD_W, DS4_TOUCHPAD_W_DEAD, VITA_FRONT_TOUCHSCREEN_W);
			pData[i].report[0].y = rescaleTouchCoordinate(ds4->finger2_y, DS4_TOUCHPAD_H, DS4_TOUCHPAD_H_DEAD, VITA_FRONT_TOUCHSCREEN_H);
			num_reports++;
		}

		// If both finger1 and finger2 present, add finger2 as report 1
		if (!ds4->finger2_activelow && !ds4->finger1_activelow) {
			pData[i].report[1].id = ds4->finger2_id;
			pData[i].report[1].x = rescaleTouchCoordinate(ds4->finger2_x, DS4_TOUCHPAD_W, DS4_TOUCHPAD_W_DEAD, VITA_FRONT_TOUCHSCREEN_W);
			pData[i].report[1].y = rescaleTouchCoordinate(ds4->finger2_y, DS4_TOUCHPAD_H, DS4_TOUCHPAD_H_DEAD, VITA_FRONT_TOUCHSCREEN_H);
			num_reports++;
		}

		if (num_reports > 0) {
			ksceKernelPowerTick(0);
			pData[i].reportNum = num_reports;
		}
	}
}

DECL_FUNC_HOOK(SceTouch_ksceTouchPeek, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs){
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchPeek_ref, port, pData, nBufs);
	patch_touch_data(port, pData, ret, &ds4report);
	return ret;
}

DECL_FUNC_HOOK(SceTouch_ksceTouchRead, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs){
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchRead_ref, port, pData, nBufs);
	patch_touch_data(port, pData, ret, &ds4report);
	return ret;
}

DECL_FUNC_HOOK(SceTouch_ksceTouchPeekRegion, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs, int region){
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchPeekRegion_ref, port, pData, nBufs, region);
	patch_touch_data(port, pData, ret, &ds4report);
	return ret;
}

DECL_FUNC_HOOK(SceTouch_ksceTouchReadRegion, SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs, int region){
	int ret = TAI_CONTINUE(int, SceTouch_ksceTouchReadRegion_ref, port, pData, nBufs, region);
	patch_touch_data(port, pData, ret, &ds4report);
	return ret;
}

DECL_FUNC_HOOK(SceBT_ksceBtHidTransfer, unsigned int mac0, unsigned int mac1, SceBtHidRequest *request){
	int ret = TAI_CONTINUE(int, SceBT_ksceBtHidTransfer_ref, mac0, mac1, request);

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

DECL_FUNC_HOOK(SceRegistryMgr_ksceRegMgrSetKeyInt, const char *category, const char *name, int buf){
	int ret = TAI_CONTINUE(int, SceRegistryMgr_ksceRegMgrSetKeyInt_ref, category, name, buf);
	if (strcmp(category, "/CONFIG/SHELL") == 0 && strcmp(name, "touch_emulation") == 0)
		touchEmulation = buf;
	return ret;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args){
	LOG("module_start\n");

	int ret;
	memset(&ds4report, 0, sizeof(ds4report));
	ksceRegMgrGetKeyInt("/CONFIG/SHELL", "touch_emulation", &touchEmulation);

	tai_module_info_t modInfo;
	modInfo.size = sizeof(modInfo);
	ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &modInfo);
	if (ret < 0) {
		LOG("Error finding SceBt module\n");
		goto error_find_scebt;
	}

	modInfo.size = sizeof(modInfo);
	ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceTouch", &modInfo);

	// PS TV uses SceTouchDummy instead of SceTouch
	char* sceTouchModuleName = (ret >= 0) ? "SceTouch" : "SceTouchDummy";
	BIND_FUNC_EXPORT_HOOK(KERNEL_PID, sceTouchModuleName, TAI_ANY_LIBRARY, 0xBAD1960B, SceTouch_ksceTouchPeek);
	BIND_FUNC_EXPORT_HOOK(KERNEL_PID, sceTouchModuleName, TAI_ANY_LIBRARY, 0x70C8AACE, SceTouch_ksceTouchRead);
	BIND_FUNC_EXPORT_HOOK(KERNEL_PID, sceTouchModuleName, TAI_ANY_LIBRARY, 0x9B3F7207, SceTouch_ksceTouchPeekRegion);
	BIND_FUNC_EXPORT_HOOK(KERNEL_PID, sceTouchModuleName, TAI_ANY_LIBRARY, 0x9A91F624, SceTouch_ksceTouchReadRegion);
	BIND_FUNC_EXPORT_HOOK(KERNEL_PID, "SceRegistryMgr", 0xB2223AEB, 0xD72EA399, SceRegistryMgr_ksceRegMgrSetKeyInt);
	BIND_FUNC_EXPORT_HOOK(KERNEL_PID, "SceBt", TAI_ANY_LIBRARY, 0xF9DCEC77, SceBT_ksceBtHidTransfer);

	LOGF("module_start finished successfully!\n");

	return SCE_KERNEL_START_SUCCESS;

error_find_scebt:
	LOGF("module_start failed!\n");
	return SCE_KERNEL_START_FAILED;
}

int module_stop(SceSize argc, const void *args){
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchPeek);
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchRead);
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchPeekRegion);
	UNBIND_FUNC_HOOK(SceTouch_ksceTouchReadRegion);
	UNBIND_FUNC_HOOK(SceRegistryMgr_ksceRegMgrSetKeyInt);
	UNBIND_FUNC_HOOK(SceBT_ksceBtHidTransfer);
	
    if (thread_uid >= 0) {
        thread_run = 0;
        ksceKernelWaitThreadEnd(thread_uid, NULL, NULL);
        ksceKernelDeleteThread(thread_uid);
    }

	return SCE_KERNEL_STOP_SUCCESS;
}
