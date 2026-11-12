#include "common.h"

#define NUM_RENDER_TASK_LISTS 3
// Four times the original on Android: see DISPLAYCONTEXT_MAIN_GFX_COUNT.
#ifdef __ANDROID__
#define NUM_RENDER_TASKS_IN_LIST 0x400
#else
#define NUM_RENDER_TASKS_IN_LIST 0x100
#endif

extern RenderTask ClearRenderTaskLists[NUM_RENDER_TASK_LISTS][NUM_RENDER_TASKS_IN_LIST];
