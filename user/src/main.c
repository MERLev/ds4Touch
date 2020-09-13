#include <vitasdk.h>
#include <taihen.h>
#include <stdbool.h>
#include <psp2/motion.h> 
#include <psp2/kernel/threadmgr.h> 
#include "log.h"
#include "../../kernel/src/ds4touch.h"

#define HOOKS_NUM 4
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];

#define DECL_FUNC_HOOK_PATCH_TOUCH(index, name) \
    static int name##_patched(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs) { \
		int ret = TAI_CONTINUE(int, refs[(index)], port, pData, nBufs); \
		if (ret > 0 && ret < 64) \
			return ds4touch_onTouch(port, pData, ret); \
		return ret; \
    }
DECL_FUNC_HOOK_PATCH_TOUCH(0, sceTouchRead)
DECL_FUNC_HOOK_PATCH_TOUCH(1, sceTouchPeek)
DECL_FUNC_HOOK_PATCH_TOUCH(2, sceTouchRead2)
DECL_FUNC_HOOK_PATCH_TOUCH(3, sceTouchPeek2)

// Simplified generic hooking function
void hookFunction(int id, uint32_t nid, const void *func) {
	hooks[id] = taiHookFunctionImport(&refs[id],TAI_MAIN_MODULE,TAI_ANY_LIBRARY,nid,func);
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
	LOGF("Plugin started\n");

	hookFunction(0, 0x169A1D58, sceTouchRead_patched);
	hookFunction(1, 0xFF082DF0, sceTouchPeek_patched);
	// Those will kill touch pointer on PS TV
	hookFunction(2, 0x39401BEA, sceTouchRead2_patched);
	hookFunction(3, 0x3AD3D0A1, sceTouchPeek2_patched);

	return SCE_KERNEL_START_SUCCESS;
}
 
int module_stop(SceSize argc, const void *args) {

    for (int i = 0; i < HOOKS_NUM; i++) {
        if (hooks[i] >= 0)
            taiHookRelease(hooks[i], refs[i]);
    }

	return SCE_KERNEL_STOP_SUCCESS;
}