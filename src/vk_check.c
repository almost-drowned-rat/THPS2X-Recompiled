/* vk_check.c -- see vk_check.h. The few Vulkan types are declared here with the layouts the spec fixes, so the
 * launcher needs no Vulkan headers or import library (model: the NinjaGaiden2-Recompiled launcher's VulkanAvailable,
 * taken over from Panzer Dragoon Orta Recompiled's src/vk_check.c). The requirement list mirrors the toolkit's
 * vk_device_ok (vulkan-backend nv2a_vk.c: VK_EXT_vertex_input_dynamic_state is optional there - a fallback bakes the
 * vertex formats); keep both in step. */
#include "vk_check.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VKC_VERSION(maj, min) (((uint32_t)(maj) << 22) | ((uint32_t)(min) << 12))
#define VKC_MAJOR(v) ((unsigned)((v) >> 22) & 0x7Fu)
#define VKC_MINOR(v) ((unsigned)((v) >> 12) & 0x3FFu)
#define VKC_PATCH(v) ((unsigned)(v) & 0xFFFu)

typedef void (*vkc_void_fn)(void);
typedef vkc_void_fn (*vkc_get_proc)(void *instance, const char *name);
typedef int (*vkc_enum_version)(uint32_t *v);
typedef struct { uint32_t sType; const void *next; const char *app; uint32_t app_v; const char *eng; uint32_t eng_v;
                 uint32_t api; } vkc_app_info;
typedef struct { uint32_t sType; const void *next; uint32_t flags; const vkc_app_info *app; uint32_t layers;
                 const char *const *layer_names; uint32_t exts; const char *const *ext_names; } vkc_inst_info;
typedef int (*vkc_create)(const vkc_inst_info *info, const void *alloc, void **instance);
typedef void (*vkc_destroy)(void *instance, const void *alloc);
typedef int (*vkc_enum_devices)(void *instance, uint32_t *n, void **devices);
typedef void (*vkc_props)(void *device, void *props);          /* VkPhysicalDeviceProperties: apiVersion at 0, name at 20 */
typedef int (*vkc_enum_ext)(void *device, const char *layer, uint32_t *n, void *props);   /* 256 name + uint32 */
typedef void (*vkc_queues)(void *device, uint32_t *n, void *props);                       /* 24 bytes, flags first */
typedef void (*vkc_features2)(void *device, void *f2);
typedef struct { uint32_t sType; void *next; uint32_t b[15]; } vkc_f13;   /* VkPhysicalDeviceVulkan13Features */
typedef struct { uint32_t sType; void *next; uint32_t f[55]; } vkc_f2;    /* VkPhysicalDeviceFeatures2 */
enum { VKC_ST_F2 = 1000059000, VKC_ST_F13 = 53, VKC_F13_DYNAMIC_RENDERING = 12, VKC_QUEUE_GRAPHICS = 1 };

static void add(char *out, size_t n, const char *fmt, const char *a, unsigned x, unsigned y, unsigned z)
{
    size_t l = strlen(out);
    if (l + 1 < n)
        snprintf(out + l, n - l, fmt, a, x, y, z);
}

int thps_vk_check(char *found, size_t n)
{
#if defined(_WIN32)
    const char *lib = "vulkan-1.dll";
#elif defined(__APPLE__)
    const char *lib = "libvulkan.1.dylib";
#else
    const char *lib = "libvulkan.so.1";
#endif
    const char *test = getenv("RECOMP_VK_TEST_NO_DEVICE");
    SDL_SharedObject *so;
    vkc_get_proc get;
    int ok = 0;

    found[0] = 0;
    so = SDL_LoadObject(lib);
    if (!so) {
        snprintf(found, n, "no Vulkan driver found");
        return 0;
    }
    get = (vkc_get_proc)SDL_LoadFunction(so, "vkGetInstanceProcAddr");
    if (get) {
        vkc_enum_version ev = (vkc_enum_version)get(NULL, "vkEnumerateInstanceVersion");
        vkc_create create = (vkc_create)get(NULL, "vkCreateInstance");
        uint32_t loader = VKC_VERSION(1, 0);
        void *inst = NULL;
        vkc_app_info app = { 0, NULL, "THPS 2X Recomp", 1, NULL, 0, VKC_VERSION(1, 3) };
        vkc_inst_info info = { 1, NULL, 0, &app, 0, NULL, 0, NULL };

        if (ev)
            ev(&loader);
        if (loader < VKC_VERSION(1, 1)) {
            /* a 1.0 loader rejects apiVersion 1.3 and has no vkGetPhysicalDeviceFeatures2 */
            add(found, n, "%sVulkan %u.%u loader", "", VKC_MAJOR(loader), VKC_MINOR(loader), 0);
        } else if (create && create(&info, NULL, &inst) == 0 && inst) {
            vkc_destroy destroy = (vkc_destroy)get(inst, "vkDestroyInstance");
            vkc_enum_devices ed = (vkc_enum_devices)get(inst, "vkEnumeratePhysicalDevices");
            vkc_props props = (vkc_props)get(inst, "vkGetPhysicalDeviceProperties");
            vkc_enum_ext ee = (vkc_enum_ext)get(inst, "vkEnumerateDeviceExtensionProperties");
            vkc_queues qs = (vkc_queues)get(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
            vkc_features2 f2fn = (vkc_features2)get(inst, "vkGetPhysicalDeviceFeatures2");
            void *dev[16];
            uint32_t nd = 16, i;

            if (ed && props && ee && qs && f2fn && ed(inst, &nd, dev) >= 0) {
                static unsigned char ext[512][260];
                for (i = 0; i < nd && i < 16; i++) {
                    unsigned char pp[2048];
                    unsigned char qp[16][24];
                    uint32_t api, ne = 512, nq = 16, k;
                    int push = 0, swap = 0, gfx = 0, dyn = 0;
                    char name[257];

                    memset(pp, 0, sizeof pp);
                    props(dev[i], pp);
                    memcpy(&api, pp, 4);
                    memcpy(name, pp + 20, 256);
                    name[256] = 0;
                    if (ee(dev[i], NULL, &ne, ext) < 0)
                        ne = 0;
                    for (k = 0; k < ne && k < 512; k++) {
                        if (!strcmp((const char *)ext[k], "VK_KHR_push_descriptor")) push = 1;
                        if (!strcmp((const char *)ext[k], "VK_KHR_swapchain")) swap = 1;
                    }
                    qs(dev[i], &nq, qp);
                    for (k = 0; k < nq && k < 16; k++) {
                        uint32_t fl;
                        memcpy(&fl, qp[k], 4);
                        if (fl & VKC_QUEUE_GRAPHICS) gfx = 1;
                    }
                    if (api >= VKC_VERSION(1, 3)) {
                        vkc_f13 f13;
                        vkc_f2 f2;
                        memset(&f13, 0, sizeof f13);
                        memset(&f2, 0, sizeof f2);
                        f13.sType = VKC_ST_F13;
                        f2.sType = VKC_ST_F2;
                        f2.next = &f13;
                        f2fn(dev[i], &f2);
                        dyn = f13.b[VKC_F13_DYNAMIC_RENDERING] != 0;
                    }
                    {
                        size_t l = strlen(found);
                        if (l + 1 < n)
                            snprintf(found + l, n - l, "%s%s (Vulkan %u.%u.%u)", l ? ", " : "", name,
                                     VKC_MAJOR(api), VKC_MINOR(api), VKC_PATCH(api));
                    }
                    if (api >= VKC_VERSION(1, 3) && dyn && push && swap && gfx)
                        ok = 1;
                }
                if (nd == 0)
                    snprintf(found, n, "a Vulkan driver, but no graphics device");
            }
            if (destroy)
                destroy(inst, NULL);
        } else {
            add(found, n, "%sVulkan %u.%u driver (no instance)", "", VKC_MAJOR(loader), VKC_MINOR(loader), 0);
        }
    }
    SDL_UnloadObject(so);
    if (test && *test == '1')
        ok = 0;
    return ok;
}

void thps_vk_message(char *out, size_t n, const char *found)
{
    snprintf(out, n,
             "I am sorry - THPS 2X Recomp unfortunately only supports graphics cards with at least "
             "Vulkan 1.3, and this PC does not have one.\n\n"
             "Found: %s\n\n"
             "Updating the graphics driver often adds Vulkan 1.3. If the graphics card itself is too old, the game "
             "cannot run on this PC.",
             found && *found ? found : "nothing");
}
